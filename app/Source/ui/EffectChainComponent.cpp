#include "EffectChainComponent.h"
#include "Langue.h"
#include "vsm/sequencer/MidiEffects.h"
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
    // LE VOLET DÉFILE (D31.4) : tout le contenu vit dans `contenu_`, que le
    // viewport promène. La barre horizontale est refusée -- la largeur suit
    // toujours celle du volet, et une barre qui ne sert jamais mange 10 px.
    contenu_.onResized = [this](juce::Rectangle<int> b) { placerContenu(b); };
    viewport_.setViewedComponent(&contenu_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    titleLabel_.setText(vsm::app::ui::tr("Effets - aucune piste"), juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    titleLabel_.setColour(juce::Label::textColourId, Palette::textPrimary);
    contenu_.addAndMakeVisible(titleLabel_);

    addLabel_.setText(vsm::app::ui::tr("Ajouter :"), juce::dontSendNotification);
    addLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    contenu_.addAndMakeVisible(addLabel_);

    int id = 1;
    for (const auto& info : EffectFactory::available())
        addBox_.addItem(juce::String(info.displayName), id++);
    prochainIdMenu_ = id;
    addBox_.setTextWhenNothingSelected(vsm::app::ui::tr("choisir un effet"));
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
    contenu_.addAndMakeVisible(addBox_);
    contenu_.addAndMakeVisible(allButton_);
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
    contenu_.addAndMakeVisible(paramHeader_);

    // D31.4 : l'en-tête de la chaîne MIDI. Visible seulement quand elle
    // existe : un titre au-dessus de rien fait chercher ce qui manque.
    midiHeader_.setColour(juce::Label::textColourId, Palette::accentAmber);
    midiHeader_.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    midiHeader_.setText(vsm::app::ui::tr(u8"Effets MIDI (sur les notes, avant la machine)"),
                         juce::dontSendNotification);
    // `addChildComponent` ET NON `addAndMakeVisible` : ce dernier REND VISIBLE
    // et annulait le `setVisible(false)` qui le précédait. Le défaut était
    // LATENT -- `rebuildMidiList` repose la visibilité au premier
    // rafraîchissement --, mais il contredisait le commentaire trois lignes
    // plus haut, et le même motif a bel et bien caché quelque chose ailleurs
    // (D48, la ligne de phase du master).
    contenu_.addChildComponent(midiHeader_);
}

std::vector<vsm::sequencer::MidiEffect>* EffectChainComponent::activeMidiChain() {
    if (project_ == nullptr || activeTrack_ < 0
        || static_cast<size_t>(activeTrack_) >= project_->tracks.size())
        return nullptr;
    return &project_->tracks[static_cast<size_t>(activeTrack_)].midiEffects;
}

void EffectChainComponent::notifyMidiChanged() {
    rebuildMidiList();
    rebuildParamControls();
    if (onMidiChainChanged) onMidiChainChanged();
}

void EffectChainComponent::scrollBy(int pixels) {
    // DIFFÉRÉ, et il a fallu une capture pour le comprendre : les commandes de
    // vue s'exécutent avant que la disposition ne soit posée, si bien que le
    // contenu n'était pas encore plus haut que le volet et qu'il n'y avait
    // rien à faire défiler. La demande arrivait, ne bougeait rien, et la
    // capture montrait le haut de la liste comme si le défilement n'existait
    // pas.
    juce::Component::SafePointer<EffectChainComponent> moi(this);
    juce::MessageManager::callAsync([moi, pixels] {
        if (moi == nullptr) return;
        moi->viewport_.setViewPosition(
            0, juce::jmax(0, moi->viewport_.getViewPositionY() + pixels));
    });
}

void EffectChainComponent::selectLastMidiEffect() {
    auto* c = activeMidiChain();
    if (c == nullptr || c->empty()) return;
    selectedIsMidi_ = true;
    selectedEffect_ = static_cast<int>(c->size()) - 1;
    rebuildEffectList();
    rebuildMidiList();
    rebuildParamControls();
}

void EffectChainComponent::rebuildMidiList() {
    midiRows_.clear();
    auto* chaine = activeMidiChain();
    midiHeader_.setVisible(chaine != nullptr && !chaine->empty());
    if (chaine == nullptr) { resized(); return; }

    for (size_t i = 0; i < chaine->size(); ++i) {
        MidiRow row;
        const auto index = static_cast<int>(i);
        const bool actif = (*chaine)[i].enabled;

        row.select = std::make_unique<juce::TextButton>(juce::String::fromUTF8(
            vsm::sequencer::midiEffectDisplayName((*chaine)[i].type).c_str()));
        row.select->setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
        row.select->setAlpha(actif ? 1.0f : 0.45f);
        row.select->setClickingTogglesState(true);
        row.select->setToggleState(selectedIsMidi_ && index == selectedEffect_,
                                    juce::dontSendNotification);
        row.select->onClick = [this, index] {
            selectedIsMidi_ = true; selectedEffect_ = index;
            rebuildEffectList(); rebuildMidiList(); rebuildParamControls();
        };
        contenu_.addAndMakeVisible(*row.select);

        row.bypass = std::make_unique<juce::TextButton>(actif ? "On" : "Off");
        row.bypass->setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
        row.bypass->setClickingTogglesState(true);
        row.bypass->setToggleState(actif, juce::dontSendNotification);
        row.bypass->setTooltip(vsm::app::ui::tr(
            u8"Actif / contourné : contourné, l'effet ne transforme plus rien, et les "
            u8"notes passent telles qu'elles sont écrites."));
        row.bypass->onClick = [this, index] {
            auto* c = activeMidiChain();
            if (!c || index >= static_cast<int>(c->size())) return;
            if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Contourner un effet MIDI"));
            (*c)[static_cast<size_t>(index)].enabled = !(*c)[static_cast<size_t>(index)].enabled;
            notifyMidiChanged();
        };
        contenu_.addAndMakeVisible(*row.bypass);

        // L'ORDRE DE LA CHAÎNE MIDI COMPTE (D31.2) : transposer puis arpéger
        // n'est pas arpéger puis transposer.
        row.up = std::make_unique<juce::TextButton>("^");
        row.up->onClick = [this, index] {
            auto* c = activeMidiChain();
            if (!c || index <= 0 || index >= static_cast<int>(c->size())) return;
            if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Déplacer un effet MIDI"));
            std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index - 1)]);
            selectedEffect_ = index - 1; selectedIsMidi_ = true;
            notifyMidiChanged();
        };
        contenu_.addAndMakeVisible(*row.up);

        row.down = std::make_unique<juce::TextButton>("v");
        row.down->onClick = [this, index] {
            auto* c = activeMidiChain();
            if (!c || index + 1 >= static_cast<int>(c->size())) return;
            if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Déplacer un effet MIDI"));
            std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index + 1)]);
            selectedEffect_ = index + 1; selectedIsMidi_ = true;
            notifyMidiChanged();
        };
        contenu_.addAndMakeVisible(*row.down);

        row.remove = std::make_unique<juce::TextButton>("X");
        row.remove->setColour(juce::TextButton::buttonColourId, Palette::accentRed.darker(0.3f));
        row.remove->onClick = [this, index] {
            auto* c = activeMidiChain();
            if (!c || index >= static_cast<int>(c->size())) return;
            if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Retirer un effet MIDI"));
            c->erase(c->begin() + index);
            selectedEffect_ = -1; selectedIsMidi_ = false;
            notifyMidiChanged();
        };
        contenu_.addAndMakeVisible(*row.remove);

        midiRows_.push_back(std::move(row));
    }
    resized();
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
    addBox_.addItem(vsm::app::ui::tr(u8"Un plugin (.clap / .vst3)..."), idMenuPlugin_);
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
EffectChainComponent::buildChain(const std::vector<TrackEffect>& described,
                                  std::vector<juce::String>* rapport) const {
    Chain chain;
    for (const auto& entry : described) {
        auto fx = EffectFactory::create(entry.type);
        // Un type inconnu n'est PAS remplacé par autre chose : on saute, comme
        // le chargement d'un projet saute une machine absente au lieu d'y
        // substituer une voisine. Le décalage d'index qui en résulterait est
        // évité en n'ajoutant rien à la chaîne vivante -- la description, elle,
        // reste intacte et sera réécrite telle quelle.
        //
        // D71 : ET ON LE DIT. Le commentaire ci-dessus expliquait avec soin
        // pourquoi on ne le REMPLACE pas, sans se demander s'il fallait le
        // DIRE. Un projet dont un insert saute s'ouvrait, jouait, se réécrivait
        // intact -- et sonnait sans son effet, sans un mot ; le rendu hors
        // ligne du MÊME dossier, lui, le nommait.
        // D94 : LA PHRASE EST ÉCRITE PAR SON MODÈLE (`kModeles`, Langue.cpp), et
        // reste française : c'est une donnée du rapport, que le terminal et les
        // bancs relisent ; `trPhrase` la traduit à l'affichage.
        if (!fx) {
            if (rapport != nullptr)
                rapport->push_back(juce::String(u8"effet « %1 » inconnu, non appliqué")
                                       .replace("%1", juce::String::fromUTF8(entry.type.c_str())));
            continue;
        }
        // D15.1 : chaque insert vivant est enrobé pour pouvoir être contourné
        // sans reconstruire la chaîne ; le drapeau suit la description.
        auto enrobe = std::make_unique<vsm::audio::effect::BypassableEffect>(std::move(fx));
        enrobe->setBypassed(!entry.enabled);
        enrobe->prepare(sampleRate_, blockSize_);
        const auto applique = vsm::interchange::applyEffectDescription(entry, *enrobe);
        if (rapport != nullptr)
            for (const auto& inconnu : applique.unknownParameters)
                rapport->push_back(juce::String(u8"effet « %1 » : réglage inconnu « %2 »")
                                       .replace("%1", juce::String::fromUTF8(entry.type.c_str()))
                                       .replace("%2", juce::String::fromUTF8(inconnu.c_str())));
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
    for (size_t piste = 0; piste < project_->tracks.size(); ++piste) {
        const auto& track = project_->tracks[piste];
        // D30.2 : UNE PISTE DÉSACTIVÉE NE FABRIQUE AUCUN INSERT. Sa
        // description reste dans la piste et revient telle quelle à la
        // réactivation -- ce qui cesse, c'est de construire des effets qui
        // tourneraient à vide. Elle n'a donc rien à signaler non plus : ce
        // n'est pas une panne, c'est une décision de l'utilisateur.
        if (track.disabled) { chains_.push_back(Chain{}); continue; }
        std::vector<juce::String> reserves;
        chains_.push_back(buildChain(track.effects, onEffectReserve ? &reserves : nullptr));
        for (const auto& reserve : reserves) onEffectReserve(piste, reserve);
    }
    for (size_t i = 0; i < chains_.size(); ++i) publishChain(i);
    rebuildEffectList();
    rebuildMidiList();
    rebuildParamControls();
}

void EffectChainComponent::retraduire() {
    // D77 : CE QUI EST ÉCRIT UNE FOIS, À LA CONSTRUCTION, SE REPOSE ICI. Sans
    // cette fonction, le panneau restait dans la langue du démarrage : la
    // règle de D73 -- le changement de langue est immédiat -- s'arrêtait à sa
    // porte. Le reste (lignes d'effets, en-têtes de paramètres) se refabrique
    // à chaque geste, et une fois de plus maintenant.
    addLabel_.setText(vsm::app::ui::tr("Ajouter :"), juce::dontSendNotification);
    addBox_.setTextWhenNothingSelected(vsm::app::ui::tr("choisir un effet"));
    if (idMenuPlugin_ > 0)
        addBox_.changeItemText(idMenuPlugin_, vsm::app::ui::tr(u8"Un plugin (.clap / .vst3)..."));
    midiHeader_.setText(vsm::app::ui::tr(u8"Effets MIDI (sur les notes, avant la machine)"),
                        juce::dontSendNotification);
    refreshTrackName();
    rebuildEffectList();
    rebuildMidiList();
    rebuildParamControls();
}

void EffectChainComponent::refreshTrackName() {
    // LE NOM DE LA PISTE, PAS SON NUMÉRO : « Effets — Batterie » se lit,
    // « piste 11 » se compte sur la liste.
    //
    // D37.1 : SORTI DE `setActiveTrack`, où il était enfermé. Renommer une
    // piste n'est pas en changer : repasser par `setActiveTrack` pour rafraîchir
    // ce titre aurait effacé l'effet choisi (`selectedEffect_ = -1`) au milieu
    // d'un réglage -- une correction d'affichage qui casse ce qu'on faisait est
    // pire que l'affichage faux qu'elle corrige.
    juce::String titre = vsm::app::ui::tr("Effets - aucune piste");
    if (activeTrack_ >= 0) {
        titre = vsm::app::ui::tr("Effets - piste ") + juce::String(activeTrack_ + 1);
        if (project_ != nullptr && static_cast<size_t>(activeTrack_) < project_->tracks.size()
            && !project_->tracks[static_cast<size_t>(activeTrack_)].name.empty())
            titre = vsm::app::ui::tr(u8"Effets \u2014 ")
                  + juce::String::fromUTF8(project_->tracks[static_cast<size_t>(activeTrack_)].name.c_str());
    }
    titleLabel_.setText(titre, juce::dontSendNotification);
}

void EffectChainComponent::setActiveTrack(int trackIndex) {
    activeTrack_ = trackIndex;
    selectedEffect_ = -1;
    refreshTrackName();
    selectedIsMidi_ = false;   // D31.4 : changer de piste ne garde pas une sélection MIDI
    rebuildEffectList();
    rebuildMidiList();
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
        allButton_.setButtonText(vsm::app::ui::tr(unActif ? "Contourner tout" : "Tout remettre"));
    }

    for (size_t i = 0; i < chain->size(); ++i) {
        EffectRow row;
        const auto index = static_cast<int>(i);

        const bool actif = i < description->size() ? (*description)[i].enabled : true;
        row.select = std::make_unique<juce::TextButton>((*chain)[i]->effectName());
        row.select->setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
        row.select->setAlpha(actif ? 1.0f : 0.45f);
        row.select->setTooltip(actif ? juce::String() : vsm::app::ui::tr(u8"Contourné : le signal passe sec, retardé de la latence de l'effet"));
        row.select->setClickingTogglesState(true);
        row.select->setToggleState(index == selectedEffect_, juce::dontSendNotification);
        row.select->onClick = [this, index] { selectedEffect_ = index; rebuildEffectList(); rebuildParamControls(); };
        contenu_.addAndMakeVisible(*row.select);

        row.bypass = std::make_unique<juce::TextButton>(actif ? "On" : "Off");
        row.bypass->setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
        row.bypass->setClickingTogglesState(true);
        row.bypass->setToggleState(actif, juce::dontSendNotification);
        row.bypass->setTooltip(vsm::app::ui::tr(u8"Actif / contourné (Bypass) : l'effet tourne encore et garde sa latence"));
        row.bypass->onClick = [this, index] { setEffectEnabled(static_cast<size_t>(index), !effectEnabled(static_cast<size_t>(index))); };
        contenu_.addAndMakeVisible(*row.bypass);

        row.preset = std::make_unique<juce::TextButton>("Preset");
        row.preset->setTooltip(vsm::app::ui::tr(u8"Enregistrer ce réglage comme preset, ou en charger un du même type"));
        row.preset->onClick = [this, index] { showPresetMenu(static_cast<size_t>(index)); };
        contenu_.addAndMakeVisible(*row.preset);

        row.up = std::make_unique<juce::TextButton>("^");
        row.up->onClick = [this, index] {
            Chain* c = activeChain();
            auto* d = activeDescription();
            if (c && d && index > 0 && d->size() == c->size()) {
                if (onEditStarted) onEditStarted(u8"Déplacer un effet");
                std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index - 1)]);
                std::swap((*d)[static_cast<size_t>(index)], (*d)[static_cast<size_t>(index - 1)]);
                selectedEffect_ = index - 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        contenu_.addAndMakeVisible(*row.up);

        row.down = std::make_unique<juce::TextButton>("v");
        row.down->onClick = [this, index] {
            Chain* c = activeChain();
            auto* d = activeDescription();
            if (c && d && index + 1 < static_cast<int>(c->size()) && d->size() == c->size()) {
                if (onEditStarted) onEditStarted(u8"Déplacer un effet");
                std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index + 1)]);
                std::swap((*d)[static_cast<size_t>(index)], (*d)[static_cast<size_t>(index + 1)]);
                selectedEffect_ = index + 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        contenu_.addAndMakeVisible(*row.down);

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
        contenu_.addAndMakeVisible(*row.remove);

        rows_.push_back(std::move(row));
    }
    resized();
}

void EffectChainComponent::rebuildParamControls() {
    params_.clear();
    paramHeader_.setText("", juce::dontSendNotification);

    // D31.4 : LE MÊME BANDEAU SERT AUX DEUX CHAÎNES. Un second jeu de knobs
    // pour trois paramètres aurait doublé la disposition ; `selectedIsMidi_`
    // dit seulement laquelle des deux `selectedEffect_` désigne.
    if (selectedIsMidi_) {
        auto* midi = activeMidiChain();
        if (midi == nullptr || selectedEffect_ < 0
            || selectedEffect_ >= static_cast<int>(midi->size())) { resized(); return; }
        auto& effet = (*midi)[static_cast<size_t>(selectedEffect_)];
        paramHeader_.setText(juce::String::fromUTF8(
                                  vsm::sequencer::midiEffectDisplayName(effet.type).c_str())
                                  + vsm::app::ui::tr(u8" — paramètres"),
                              juce::dontSendNotification);
        // LES BORNES VIENNENT DE `core/`, la même source que les défauts posés
        // à l'ajout : deux tables finiraient par en donner deux.
        for (const auto& info : vsm::sequencer::midiEffectParameters(effet.type)) {
            ParamControl pc;
            pc.slider = std::make_unique<juce::Slider>();
            pc.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            pc.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
            // UN PAS ENTIER POUR CE QUI EST ENTIER : un arpégiateur au mode
            // « 1,4 » n'a pas de sens, et un demi-ton se compte.
            const bool entier = std::string(info.name) == "Semitones"
                                 || std::string(info.name) == "Mode"
                                 || std::string(info.name) == "Division"
                                 || std::string(info.name) == "Offset";
            pc.slider->setRange(info.minValue, info.maxValue,
                                 entier ? 1.0 : (info.maxValue - info.minValue) / 1000.0);
            const auto it = effet.parameters.find(info.name);
            pc.slider->setValue(it == effet.parameters.end() ? info.defaultValue : it->second,
                                 juce::dontSendNotification);
            juce::Slider* raw = pc.slider.get();
            const std::string nom = info.name;
            const int slot = selectedEffect_;
            raw->onDragStart = [this] {
                if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Réglage d'effet MIDI"));
            };
            raw->onValueChange = [this, raw, nom, slot] {
                auto* c = activeMidiChain();
                if (!c || slot < 0 || static_cast<size_t>(slot) >= c->size()) return;
                (*c)[static_cast<size_t>(slot)].parameters[nom] = static_cast<float>(raw->getValue());
                // ET LE PLANNING EST REFAIT : un effet MIDI change ce qui est
                // JOUÉ, pas ce qu'on calcule sur le son -- sans cela, le
                // réglage ne s'entendrait qu'à la prochaine relecture.
                if (onMidiChainChanged) onMidiChainChanged();
            };
            contenu_.addAndMakeVisible(*pc.slider);

            pc.label = std::make_unique<juce::Label>();
            pc.label->setText(juce::String(info.name), juce::dontSendNotification);
            pc.label->setJustificationType(juce::Justification::centred);
            pc.label->setColour(juce::Label::textColourId, Palette::textSecondary);
            pc.label->setFont(juce::Font(juce::FontOptions(10.0f)));
            contenu_.addAndMakeVisible(*pc.label);
            params_.push_back(std::move(pc));
        }
        resized();
        return;
    }

    Chain* chain = activeChain();
    if (chain == nullptr || selectedEffect_ < 0 || selectedEffect_ >= static_cast<int>(chain->size())) {
        resized();
        return;
    }
    auto* fx = (*chain)[static_cast<size_t>(selectedEffect_)].get();
    // D77 : le tiret long et l'accent de l'en-tête des effets MIDI, deux lignes
    // plus haut. « - parametres » les évitait pour ne pas lire ses octets en
    // Latin-1 ; `tr()` passe par `fromUTF8`, et le détour n'a plus de raison.
    paramHeader_.setText(juce::String(fx->effectName()) + vsm::app::ui::tr(u8" — paramètres"), juce::dontSendNotification);

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
        raw->onDragStart = [this] { if (onEditStarted) onEditStarted(u8"Réglage d'effet"); };
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
        contenu_.addAndMakeVisible(*pc.slider);

        pc.label = std::make_unique<juce::Label>();
        pc.label->setText(juce::String(info.name), juce::dontSendNotification);
        pc.label->setJustificationType(juce::Justification::centred);
        pc.label->setColour(juce::Label::textColourId, Palette::textSecondary);
        pc.label->setFont(juce::Font(juce::FontOptions(10.0f)));
        contenu_.addAndMakeVisible(*pc.label);

        params_.push_back(std::move(pc));
    }
    resized();
}

void EffectChainComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
}

void EffectChainComponent::resized() {
    // LE VOLET DÉFILE (D31.4). `resized()` ne place plus rien lui-même : il
    // donne au conteneur la hauteur dont le contenu a besoin, et le viewport
    // fait le reste. Sans défilement, la chaîne MIDI poussait les libellés des
    // knobs sous le bas de la fenêtre.
    viewport_.setBounds(getLocalBounds());
    const int largeur = juce::jmax(80, getWidth() - (hauteurVoulue_ > getHeight() ? 10 : 0));
    contenu_.setSize(largeur, juce::jmax(getHeight(), hauteurVoulue_));
    // ET ON REPLACE, TOUJOURS. `setSize` d'une taille INCHANGÉE ne déclenche
    // pas `resized()` : après un `rebuildEffectList` qui n'a pas changé la
    // hauteur, les rangées neuves seraient restées sans bornes, donc
    // invisibles. C'est le même piège que le `setValue` de D30.4, à un autre
    // étage. Le second passage recalcule la même hauteur et ne relance rien.
    placerContenu(contenu_.getLocalBounds());
}

void EffectChainComponent::placerContenu(juce::Rectangle<int> bounds) {
    auto area = bounds.reduced(8);
    titleLabel_.setBounds(area.removeFromTop(22));

    auto addRow = area.removeFromTop(26);
    addLabel_.setBounds(addRow.removeFromLeft(56));
    addBox_.setBounds(addRow.removeFromLeft(200));
    addRow.removeFromLeft(8);
    allButton_.setBounds(addRow.removeFromLeft(150));
    area.removeFromTop(6);

    // D31.4 : LA CHAÎNE MIDI EN PREMIER, parce qu'elle agit en premier -- sur
    // les notes, avant que la machine ne les joue.
    if (midiHeader_.isVisible()) {
        midiHeader_.setBounds(area.removeFromTop(18));
        for (auto& row : midiRows_) {
            auto r = area.removeFromTop(26).reduced(0, 1);
            row.remove->setBounds(r.removeFromRight(28).reduced(1));
            row.down->setBounds(r.removeFromRight(26).reduced(1));
            row.up->setBounds(r.removeFromRight(26).reduced(1));
            row.bypass->setBounds(r.removeFromLeft(44).reduced(1));
            row.select->setBounds(r.reduced(1));
        }
        area.removeFromTop(8);
    }

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
    int rangeesKnobs = 0;
    for (size_t i = 0; i < params_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int rowIdx = static_cast<int>(i) / cols;
        rangeesKnobs = rowIdx + 1;
        juce::Rectangle<int> cell(area.getX() + col * knobW, area.getY() + rowIdx * knobH, knobW, knobH);
        params_[i].label->setBounds(cell.removeFromBottom(14));
        params_[i].slider->setBounds(cell.reduced(4));
    }

    // CE QUE LE CONTENU RÉCLAME, en pixels : ce qu'on a consommé au-dessus de
    // la grille, plus la grille elle-même, plus la marge du bas. C'est ce
    // nombre qui décide s'il y a une barre de défilement -- et il est calculé
    // ICI parce que c'est ici que la disposition sait ce qu'elle a posé.
    const int voulu = (area.getY() - bounds.getY()) + rangeesKnobs * knobH + 8;
    if (voulu != hauteurVoulue_) {
        hauteurVoulue_ = voulu;
        // Un appel différé : changer la taille du conteneur depuis son propre
        // `resized()` rappellerait celui-ci en cascade.
        juce::MessageManager::callAsync([this] { resized(); });
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

/// D91 : LE MENU DE PRESET, CONSTRUIT PAR UNE FONCTION -- le clic l'affiche, le
/// banc y cherche une entrée ; un seul menu, donc un seul chemin.
juce::PopupMenu menuDePreset(const std::vector<PresetTrouve>& trouves) {
    juce::PopupMenu menu;
    menu.addItem(1, vsm::app::ui::tr(u8"Enregistrer comme preset..."));
    menu.addSeparator();
    if (trouves.empty())
        menu.addItem(2, vsm::app::ui::tr(u8"(aucun preset de ce type dans la bibliothèque ni le projet)"), false);
    for (size_t i = 0; i < trouves.size(); ++i)
        menu.addItem(100 + static_cast<int>(i), juce::String::fromUTF8(trouves[i].nom.c_str()));
    return menu;
}

std::vector<juce::File> fichiersDe(const std::vector<PresetTrouve>& trouves) {
    std::vector<juce::File> fichiers;
    for (const auto& t : trouves) fichiers.push_back(t.fichier);
    return fichiers;
}
}

void EffectChainComponent::showPresetMenu(size_t index) {
    auto* d = activeDescription();
    if (!d || index >= d->size()) return;
    const std::string type = (*d)[index].type;
    auto trouves = std::make_shared<std::vector<PresetTrouve>>(
        presetsDuType(presetFoldersProvider ? presetFoldersProvider() : std::vector<juce::File>{}, type));

    juce::PopupMenu menu = menuDePreset(*trouves);   // D91
    juce::Component* ancre = index < rows_.size() ? rows_[index].preset.get() : nullptr;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(ancre),
                       [this, index, trouves](int choix) { presetMenuAction(index, choix, fichiersDe(*trouves)); });
}

void EffectChainComponent::presetMenuAction(size_t index, int choix, const std::vector<juce::File>& fichiers) {
    if (choix == 1) savePresetOf(index);
    else if (choix >= 100 && static_cast<size_t>(choix - 100) < fichiers.size())
        loadPresetInto(index, fichiers[static_cast<size_t>(choix - 100)]);
}

bool EffectChainComponent::presetMenuPourCapture(size_t index, const juce::String& libelle) {
    auto* d = activeDescription();
    if (!d || index >= d->size()) return false;
    const auto trouves =
        presetsDuType(presetFoldersProvider ? presetFoldersProvider() : std::vector<juce::File>{}, (*d)[index].type);
    const juce::PopupMenu menu = menuDePreset(trouves);
    int choix = 0, parDebut = 0;
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
        const auto& item = it.getItem();
        if (item.itemID == 0 || !item.isEnabled) continue;
        if (item.text == libelle) { choix = item.itemID; break; }
        if (parDebut == 0 && item.text.startsWith(libelle)) parDebut = item.itemID;
    }
    if (choix == 0) choix = parDebut;
    if (choix == 0) return false;
    presetMenuAction(index, choix, fichiersDe(trouves));
    return true;
}

void EffectChainComponent::savePresetOf(size_t index) {
    auto* d = activeDescription();
    if (!d || index >= d->size() || !presetSaveFolderProvider) return;
    const auto description = (*d)[index];
    auto fenetre = std::make_shared<juce::AlertWindow>(
        vsm::app::ui::tr(u8"Enregistrer un preset d'effet"),
        vsm::app::ui::tr(u8"Nom du preset (%1) :").replace("%1", juce::String(description.type)),
        juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", "", "");
    fenetre->addButton(vsm::app::ui::tr(u8"Enregistrer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
                    juce::AlertWindow::WarningIcon, vsm::app::ui::tr(u8"Preset non enregistré"),
                    vsm::app::ui::tr(u8"Impossible d'écrire %1").replace("%1", fichier.getFullPathName()));
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
    // D71 : UN PRESET D'EFFET VENU D'UNE AUTRE VERSION PEUT PORTER DES RÉGLAGES
    // QUE CELLE-CI NE CONNAÎT PAS -- exactement le cas de D52 pour les presets
    // de machine, à ceci près que le rapport était jeté ici aussi.
    const auto applique = vsm::interchange::applyEffectDescription(description, *(*c)[index]);
    if (onEffectReserve && activeTrack_ >= 0)
        for (const auto& inconnu : applique.unknownParameters)
            onEffectReserve(static_cast<size_t>(activeTrack_),
                            // Le nom de FICHIER en dernier : c'est lui qui peut
                            // porter un « %2 », pas l'identifiant du réglage.
                            juce::String(u8"preset d'effet « %1 » : réglage inconnu « %2 »")
                                .replace("%2", juce::String::fromUTF8(inconnu.c_str()))
                                .replace("%1", fichier.getFileNameWithoutExtension()));
    selectedEffect_ = static_cast<int>(index);
    rebuildEffectList();
    rebuildParamControls();
}
