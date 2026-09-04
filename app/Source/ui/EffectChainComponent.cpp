#include "EffectChainComponent.h"
#include "vsm/audio/effect/BypassableEffect.h"
#include "vsm/interchange/EffectPreset.h"
#include <algorithm>
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/EffectDescription.h"
#include <cmath>

using namespace vsm::ui;
using vsm::audio::effect::EffectFactory;
using vsm::interchange::describeEffect;
using vsm::sequencer::TrackEffect;

EffectChainComponent::EffectChainComponent() {
    titleLabel_.setText("Effets - aucune piste", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    titleLabel_.setColour(juce::Label::textColourId, Palette::textPrimary);
    addAndMakeVisible(titleLabel_);

    addLabel_.setText("Ajouter :", juce::dontSendNotification);
    addLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(addLabel_);

    int id = 1;
    for (const auto& info : EffectFactory::available())
        addBox_.addItem(juce::String(info.displayName), id++);
    prochainIdMenu_ = id;
    addBox_.setTextWhenNothingSelected("choisir un effet");
    addBox_.onChange = [this] {
        const int selection = addBox_.getSelectedId();
        addBox_.setSelectedId(0, juce::dontSendNotification);
        if (selection <= 0) return;

        if (selection == idMenuPlugin_ && pluginEffectChooser_) {
            pluginEffectChooser_([this](std::string effectId) {
                if (effectId.empty()) return;
                if (onEditStarted) onEditStarted("Ajouter un effet");
                addEffectById(effectId);
            });
            return;
        }

        const size_t idx = static_cast<size_t>(selection - 1);
        if (idx >= EffectFactory::available().size()) return;
        if (onEditStarted) onEditStarted("Ajouter un effet");
        addEffectById(EffectFactory::available()[idx].id);
    };
    addAndMakeVisible(addBox_);
    addAndMakeVisible(allButton_);
    allButton_.setVisible(false);
    allButton_.onClick = [this] {
        auto* d = activeDescription();
        if (!d) return;
        bool unActif = false;
        for (const auto& e : *d) unActif = unActif || e.enabled;
        setAllEffectsEnabled(!unActif);
    };

    paramHeader_.setColour(juce::Label::textColourId, Palette::textSecondary);
    paramHeader_.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    addAndMakeVisible(paramHeader_);
}

void EffectChainComponent::setPluginEffectChooser(
    std::function<void(std::function<void(std::string)>)> chooser) {
    pluginEffectChooser_ = std::move(chooser);
    if (!pluginEffectChooser_ || idMenuPlugin_ != 0) return;
    // D7.3 : LES EFFETS DES AUTRES, DANS LE MÊME MENU ET AU MÊME RANG. Un
    // second bouton « ajouter un plugin » à côté de « ajouter un effet »
    // suggérerait deux mécanismes ; il n'y en a qu'un, et « insérables au même
    // titre que les natifs » veut dire exactement cela.
    idMenuPlugin_ = prochainIdMenu_;
    addBox_.addSeparator();
    addBox_.addItem(juce::String(u8"Un plugin (.clap / .vst3)..."), idMenuPlugin_);
}

bool EffectChainComponent::addEffectById(const std::string& effectId) {
    Chain* chain = activeChain();
    auto* described = activeDescription();
    if (chain == nullptr || described == nullptr) return false;

    // LA MÊME FABRIQUE POUR TOUT LE MONDE. Un identifiant interne (« reverb »)
    // et un identifiant de plugin (« vst3:... ») entrent par la même porte :
    // c'est ce qui fait qu'un effet tiers est insérable « au même titre » qu'un
    // natif, plutôt que par un chemin parallèle qu'il faudrait tenir d'accord
    // avec le premier.
    auto fx = EffectFactory::create(effectId);
    if (!fx) return false;

    fx->prepare(sampleRate_, blockSize_);     // prepare AVANT publication (thread UI)
    // La description et l'instance sont poussées ENSEMBLE : leurs deux vecteurs
    // restent index pour index alignés, ce qui est la seule chose qui permette
    // de retrouver le type d'un effet vivant.
    described->push_back(describeEffect(effectId, *fx));
    chain->push_back(std::move(fx));
    selectedEffect_ = static_cast<int>(chain->size()) - 1;
    publishActiveChain();
    rebuildEffectList();
    rebuildParamControls();
    return true;
}

EffectChainComponent::Chain* EffectChainComponent::activeChain() {
    if (activeTrack_ < 0 || static_cast<size_t>(activeTrack_) >= chains_.size()) return nullptr;
    return &chains_[static_cast<size_t>(activeTrack_)];
}

std::vector<TrackEffect>* EffectChainComponent::activeDescription() {
    if (project_ == nullptr || activeTrack_ < 0) return nullptr;
    if (static_cast<size_t>(activeTrack_) >= project_->tracks.size()) return nullptr;
    return &project_->tracks[static_cast<size_t>(activeTrack_)].effects;
}

EffectChainComponent::Chain
EffectChainComponent::buildChain(const std::vector<TrackEffect>& described) const {
    Chain chain;
    for (const auto& entry : described) {
        auto fx = EffectFactory::create(entry.type);
        // Un type inconnu n'est PAS remplacé par autre chose : on saute, comme
        // le chargement d'un projet saute une machine absente au lieu d'y
        // substituer une voisine. Le décalage d'index qui en résulterait est
        // évité en n'ajoutant rien à la chaîne vivante -- la description, elle,
        // reste intacte et sera réécrite telle quelle.
        if (!fx) continue;
        // D15.1 : chaque insert vivant est enrobé pour pouvoir être contourné
        // sans reconstruire la chaîne ; le drapeau suit la description.
        auto enrobe = std::make_unique<vsm::audio::effect::BypassableEffect>(std::move(fx));
        enrobe->setBypassed(!entry.enabled);
        enrobe->prepare(sampleRate_, blockSize_);
        vsm::interchange::applyEffectDescription(entry, *enrobe);
        chain.push_back(std::move(enrobe));
    }
    return chain;
}

void EffectChainComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    activeTrack_ = -1;
    selectedEffect_ = -1;
    rebuildFromProject();
}

void EffectChainComponent::setAudioConfig(double sampleRate, int blockSize) {
    // Comparaison à une tolérance : une fréquence d'échantillonnage est un
    // double qui vient d'un pilote, et l'égalité exacte sur des flottants n'a
    // pas de sens (elle vaut un avertissement du compilateur, à juste titre).
    if (std::abs(sampleRate - sampleRate_) < 1.0 && blockSize == blockSize_) return;
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    // Refabriquer plutôt que re-prepare() : `prepare()` remet les lignes à
    // retard à la bonne taille, mais rien ne garantit qu'un effet reprenne ses
    // réglages -- les repasser par la description est la voie qu'un test
    // couvre déjà.
    rebuildFromProject();
}

void EffectChainComponent::rebuildFromProject() {
    chains_.clear();
    if (project_ == nullptr) { rebuildEffectList(); rebuildParamControls(); return; }
    chains_.reserve(project_->tracks.size());
    for (const auto& track : project_->tracks)
        chains_.push_back(buildChain(track.effects));
    for (size_t i = 0; i < chains_.size(); ++i) publishChain(i);
    rebuildEffectList();
    rebuildParamControls();
}

void EffectChainComponent::setActiveTrack(int trackIndex) {
    activeTrack_ = trackIndex;
    selectedEffect_ = -1;
    // LE NOM DE LA PISTE, PAS SON NUMÉRO : « Effets — Batterie » se lit,
    // « piste 11 » se compte sur la liste.
    juce::String titre = "Effets - aucune piste";
    if (trackIndex >= 0) {
        titre = "Effets - piste " + juce::String(trackIndex + 1);
        if (project_ != nullptr && static_cast<size_t>(trackIndex) < project_->tracks.size()
            && !project_->tracks[static_cast<size_t>(trackIndex)].name.empty())
            titre = juce::String::fromUTF8("Effets \u2014 ")
                  + juce::String::fromUTF8(project_->tracks[static_cast<size_t>(trackIndex)].name.c_str());
    }
    titleLabel_.setText(titre,
                        juce::dontSendNotification);
    rebuildEffectList();
    rebuildParamControls();
}

void EffectChainComponent::publishChain(size_t trackIndex) {
    if (!onChainChanged || trackIndex >= chains_.size()) return;
    // Copie immuable publiée au moteur (RT-safe).
    onChainChanged(trackIndex, std::make_shared<const Chain>(chains_[trackIndex]));
}

void EffectChainComponent::publishActiveChain() {
    if (activeTrack_ < 0) return;
    publishChain(static_cast<size_t>(activeTrack_));
}

void EffectChainComponent::rebuildEffectList() {
    rows_.clear();
    Chain* chain = activeChain();
    auto* description = activeDescription();
    if (chain == nullptr || description == nullptr) { allButton_.setVisible(false); resized(); return; }
    allButton_.setVisible(!chain->empty());
    {
        // Le libellé dit ce que le clic FERA : contourner tous les inserts
        // tant qu'un seul est actif, sinon les remettre tous.
        bool unActif = false;
        for (const auto& e : *description) unActif = unActif || e.enabled;
        allButton_.setButtonText(unActif ? "Contourner tout" : "Tout remettre");
    }

    for (size_t i = 0; i < chain->size(); ++i) {
        EffectRow row;
        const auto index = static_cast<int>(i);

        const bool actif = i < description->size() ? (*description)[i].enabled : true;
        row.select = std::make_unique<juce::TextButton>((*chain)[i]->effectName());
        row.select->setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
        row.select->setAlpha(actif ? 1.0f : 0.45f);
        row.select->setTooltip(actif ? juce::String() : juce::String(u8"Contourné : le signal passe sec, retardé de la latence de l'effet"));
        row.select->setClickingTogglesState(true);
        row.select->setToggleState(index == selectedEffect_, juce::dontSendNotification);
        row.select->onClick = [this, index] { selectedEffect_ = index; rebuildEffectList(); rebuildParamControls(); };
        addAndMakeVisible(*row.select);

        row.bypass = std::make_unique<juce::TextButton>(actif ? "On" : "Off");
        row.bypass->setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
        row.bypass->setClickingTogglesState(true);
        row.bypass->setToggleState(actif, juce::dontSendNotification);
        row.bypass->setTooltip(u8"Actif / contourné (Bypass) : l'effet tourne encore et garde sa latence");
        row.bypass->onClick = [this, index] { setEffectEnabled(static_cast<size_t>(index), !effectEnabled(static_cast<size_t>(index))); };
        addAndMakeVisible(*row.bypass);

        row.preset = std::make_unique<juce::TextButton>("Preset");
        row.preset->setTooltip(u8"Enregistrer ce réglage comme preset, ou en charger un du même type");
        row.preset->onClick = [this, index] { showPresetMenu(static_cast<size_t>(index)); };
        addAndMakeVisible(*row.preset);

        row.up = std::make_unique<juce::TextButton>("^");
        row.up->onClick = [this, index] {
            Chain* c = activeChain();
            auto* d = activeDescription();
            if (c && d && index > 0 && d->size() == c->size()) {
                if (onEditStarted) onEditStarted("Deplacer un effet");
                std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index - 1)]);
                std::swap((*d)[static_cast<size_t>(index)], (*d)[static_cast<size_t>(index - 1)]);
                selectedEffect_ = index - 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.up);

        row.down = std::make_unique<juce::TextButton>("v");
        row.down->onClick = [this, index] {
            Chain* c = activeChain();
            auto* d = activeDescription();
            if (c && d && index + 1 < static_cast<int>(c->size()) && d->size() == c->size()) {
                if (onEditStarted) onEditStarted("Deplacer un effet");
                std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index + 1)]);
                std::swap((*d)[static_cast<size_t>(index)], (*d)[static_cast<size_t>(index + 1)]);
                selectedEffect_ = index + 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.down);

        row.remove = std::make_unique<juce::TextButton>("X");
        row.remove->setColour(juce::TextButton::buttonColourId, Palette::accentRed.darker(0.3f));
        row.remove->onClick = [this, index] {
            Chain* c = activeChain();
            auto* d = activeDescription();
            if (c && d && index < static_cast<int>(c->size()) && d->size() == c->size()) {
                if (onEditStarted) onEditStarted("Retirer un effet");
                c->erase(c->begin() + index);
                d->erase(d->begin() + index);
                selectedEffect_ = -1;
                publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.remove);

        rows_.push_back(std::move(row));
    }
    resized();
}

void EffectChainComponent::rebuildParamControls() {
    params_.clear();
    paramHeader_.setText("", juce::dontSendNotification);

    Chain* chain = activeChain();
    if (chain == nullptr || selectedEffect_ < 0 || selectedEffect_ >= static_cast<int>(chain->size())) {
        resized();
        return;
    }
    auto* fx = (*chain)[static_cast<size_t>(selectedEffect_)].get();
    paramHeader_.setText(juce::String(fx->effectName()) + " - parametres", juce::dontSendNotification);

    for (const auto& info : fx->parameterList()) {
        ParamControl pc;
        pc.slider = std::make_unique<juce::Slider>();
        pc.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pc.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        pc.slider->setRange(info.minValue, info.maxValue,
                            (info.maxValue - info.minValue) / 1000.0);
        pc.slider->setValue(fx->getParameter(info.id), juce::dontSendNotification);
        if (!info.unit.empty()) pc.slider->setTextValueSuffix(" " + juce::String(info.unit));
        const auto pid = info.id;
        juce::Slider* raw = pc.slider.get();
        const int slot = selectedEffect_;
        raw->onDragStart = [this] { if (onEditStarted) onEditStarted("Reglage d'effet"); };
        raw->onValueChange = [this, fx, raw, pid, slot] {
            fx->setParameter(pid, static_cast<float>(raw->getValue()));
            // ET dans la piste, tout de suite : un réglage qui ne vit que dans
            // l'objet vivant est un réglage perdu à la fermeture. On re-décrit
            // l'effet entier plutôt que le seul paramètre touché -- c'est le
            // prix d'une poignée de flottants, et cela rend impossible qu'une
            // description dérive de l'objet qu'elle décrit.
            auto* d = activeDescription();
            if (d && slot >= 0 && static_cast<size_t>(slot) < d->size())
                (*d)[static_cast<size_t>(slot)] = describeEffect((*d)[static_cast<size_t>(slot)].type, *fx);
        };
        addAndMakeVisible(*pc.slider);

        pc.label = std::make_unique<juce::Label>();
        pc.label->setText(juce::String(info.name), juce::dontSendNotification);
        pc.label->setJustificationType(juce::Justification::centred);
        pc.label->setColour(juce::Label::textColourId, Palette::textSecondary);
        pc.label->setFont(juce::Font(juce::FontOptions(10.0f)));
        addAndMakeVisible(*pc.label);

        params_.push_back(std::move(pc));
    }
    resized();
}

void EffectChainComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
}

void EffectChainComponent::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel_.setBounds(area.removeFromTop(22));

    auto addRow = area.removeFromTop(26);
    addLabel_.setBounds(addRow.removeFromLeft(56));
    addBox_.setBounds(addRow.removeFromLeft(200));
    addRow.removeFromLeft(8);
    allButton_.setBounds(addRow.removeFromLeft(150));
    area.removeFromTop(6);

    // Liste des effets (rangées de 24 px).
    for (auto& row : rows_) {
        auto r = area.removeFromTop(26).reduced(0, 1);
        row.remove->setBounds(r.removeFromRight(28).reduced(1));
        row.down->setBounds(r.removeFromRight(26).reduced(1));
        row.up->setBounds(r.removeFromRight(26).reduced(1));
        row.preset->setBounds(r.removeFromRight(70).reduced(1));
        row.bypass->setBounds(r.removeFromLeft(44).reduced(1));
        row.select->setBounds(r.reduced(1));
    }

    area.removeFromTop(8);
    paramHeader_.setBounds(area.removeFromTop(18));

    // Grille de knobs de paramètres (colonnes de 84 px).
    const int knobW = 84, knobH = 74;
    const int cols = juce::jmax(1, area.getWidth() / knobW);
    for (size_t i = 0; i < params_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int rowIdx = static_cast<int>(i) / cols;
        juce::Rectangle<int> cell(area.getX() + col * knobW, area.getY() + rowIdx * knobH, knobW, knobH);
        params_[i].label->setBounds(cell.removeFromBottom(14));
        params_[i].slider->setBounds(cell.reduced(4));
    }
}

// --- D15.1 : contourner un insert, ou tous ceux de la piste ----------------

bool EffectChainComponent::effectEnabled(size_t index) const {
    if (project_ == nullptr || activeTrack_ < 0) return true;
    const auto& d = project_->tracks[static_cast<size_t>(activeTrack_)].effects;
    return index < d.size() ? d[index].enabled : true;
}

void EffectChainComponent::setEffectEnabled(size_t index, bool enabled) {
    Chain* c = activeChain();
    auto* d = activeDescription();
    if (!c || !d || index >= d->size() || d->size() != c->size()) return;
    if ((*d)[index].enabled == enabled) return;
    if (onEditStarted) onEditStarted(enabled ? "Remettre un effet" : "Contourner un effet");
    (*d)[index].enabled = enabled;
    // Le drapeau est atomique sur l'instance vivante : rien à republier, donc
    // aucun clic de reconstruction de chaîne.
    if (auto* enrobe = dynamic_cast<vsm::audio::effect::BypassableEffect*>((*c)[index].get()))
        enrobe->setBypassed(!enabled);
    rebuildEffectList();
}

void EffectChainComponent::setAllEffectsEnabled(bool enabled) {
    Chain* c = activeChain();
    auto* d = activeDescription();
    if (!c || !d || d->size() != c->size()) return;
    bool change = false;
    for (const auto& e : *d) change = change || (e.enabled != enabled);
    if (!change) return;
    if (onEditStarted) onEditStarted(enabled ? "Remettre tous les effets" : "Contourner tous les effets");
    for (size_t i = 0; i < d->size(); ++i) {
        (*d)[i].enabled = enabled;
        if (auto* enrobe = dynamic_cast<vsm::audio::effect::BypassableEffect*>((*c)[i].get()))
            enrobe->setBypassed(!enabled);
    }
    rebuildEffectList();
}

// --- D15.4 : les presets d'effet ----------------------------------------------

namespace {
struct PresetTrouve { juce::File fichier; std::string nom; };

std::vector<PresetTrouve> presetsDuType(const std::vector<juce::File>& dossiers, const std::string& type) {
    std::vector<PresetTrouve> trouves;
    for (const auto& dossier : dossiers) {
        if (!dossier.isDirectory()) continue;
        for (const auto& f : dossier.findChildFiles(juce::File::findFiles, true, "*.effect.json")) {
            const auto lu = vsm::interchange::parseEffectPreset(f.loadFileAsString().toStdString());
            if (!lu.success || lu.preset.type != type) continue;   // un autre type, ou illisible : pas proposé
            trouves.push_back({f, lu.preset.name});
        }
    }
    std::sort(trouves.begin(), trouves.end(), [](const PresetTrouve& a, const PresetTrouve& b) { return a.nom < b.nom; });
    return trouves;
}
}

void EffectChainComponent::showPresetMenu(size_t index) {
    auto* d = activeDescription();
    if (!d || index >= d->size()) return;
    const std::string type = (*d)[index].type;
    auto trouves = std::make_shared<std::vector<PresetTrouve>>(
        presetsDuType(presetFoldersProvider ? presetFoldersProvider() : std::vector<juce::File>{}, type));

    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8(u8"Enregistrer comme preset..."));
    menu.addSeparator();
    if (trouves->empty())
        menu.addItem(2, juce::String::fromUTF8(u8"(aucun preset de ce type dans la bibliothèque ni le projet)"), false);
    for (size_t i = 0; i < trouves->size(); ++i)
        menu.addItem(100 + static_cast<int>(i), juce::String::fromUTF8((*trouves)[i].nom.c_str()));

    juce::Component* ancre = index < rows_.size() ? rows_[index].preset.get() : nullptr;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(ancre),
                       [this, index, trouves](int choix) {
                           if (choix == 1) savePresetOf(index);
                           else if (choix >= 100 && static_cast<size_t>(choix - 100) < trouves->size())
                               loadPresetInto(index, (*trouves)[static_cast<size_t>(choix - 100)].fichier);
                       });
}

void EffectChainComponent::savePresetOf(size_t index) {
    auto* d = activeDescription();
    if (!d || index >= d->size() || !presetSaveFolderProvider) return;
    const auto description = (*d)[index];
    auto fenetre = std::make_shared<juce::AlertWindow>(
        juce::String::fromUTF8(u8"Enregistrer un preset d'effet"),
        juce::String::fromUTF8(u8"Nom du preset (") + juce::String(description.type) + ") :",
        juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", "", "");
    fenetre->addButton("Enregistrer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, description, fenetre](int resultat) {
            const juce::String nom = fenetre->getTextEditorContents("nom").trim();
            fenetre->exitModalState(resultat);
            fenetre->setVisible(false);
            if (resultat != 1 || nom.isEmpty()) return;
            const juce::File dossier = presetSaveFolderProvider();
            dossier.createDirectory();
            const juce::File fichier = dossier.getChildFile(
                juce::File::createLegalFileName(nom) + juce::String(vsm::interchange::kEffectPresetExtension));
            const auto preset = vsm::interchange::effectPresetFromDescription(description, nom.toStdString());
            if (!fichier.replaceWithText(juce::String::fromUTF8(
                    vsm::interchange::effectPresetToJson(preset).toString().c_str()))) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, juce::String::fromUTF8(u8"Preset non enregistré"),
                    juce::String::fromUTF8(u8"Impossible d'écrire ") + fichier.getFullPathName());
                return;
            }
            if (onPresetsChanged) onPresetsChanged();
        }), false);
}

void EffectChainComponent::loadPresetInto(size_t index, const juce::File& fichier) {
    Chain* c = activeChain();
    auto* d = activeDescription();
    if (!c || !d || index >= d->size() || d->size() != c->size()) return;
    const auto lu = vsm::interchange::parseEffectPreset(fichier.loadFileAsString().toStdString());
    if (!lu.success || lu.preset.type != (*d)[index].type) return;
    if (onEditStarted) onEditStarted("Charger un preset d'effet");
    auto description = vsm::interchange::descriptionFromEffectPreset(lu.preset);
    description.enabled = (*d)[index].enabled;   // le contournement est une décision de mixage, il reste
    (*d)[index] = description;
    vsm::interchange::applyEffectDescription(description, *(*c)[index]);
    selectedEffect_ = static_cast<int>(index);
    rebuildEffectList();
    rebuildParamControls();
}
