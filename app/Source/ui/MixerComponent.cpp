#include "MixerComponent.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <cmath>

using vsm::audio::engine::MasterBus;

namespace {
float gainToDb(float g) { return g > 1.0e-5f ? 20.0f * std::log10(g) : -60.0f; }
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
} // namespace

// ============================================================ ChannelStrip

void ChannelStrip::setMembers(const juce::StringArray& membres) {
    if (track_.kind != vsm::sequencer::Track::Kind::Group) return;
    juce::String texte = juce::String::fromUTF8(track_.name.c_str())
                       + juce::String::fromUTF8(" — bus de groupe");
    texte << (membres.isEmpty() ? juce::String::fromUTF8(" : aucune piste n'y est routée")
                                : juce::String::fromUTF8(" : ") + membres.joinIntoString(", "));
    nameLabel_.setTooltip(texte);
}

ChannelStrip::ChannelStrip(vsm::sequencer::Track& track, size_t index,
                            const std::vector<std::string>& sendNames)
    : track_(track), index_(index) {
    nameLabel_.setText(track_.name.empty() ? "Track" : track_.name, juce::dontSendNotification);
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
    nameLabel_.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    // UN BUS DE GROUPE SE RECONNAÎT : son nom en ambre, comme le master est à
    // part. Sans cela, « Batterie » (le bus) et « Batterie · hihat » (une
    // pièce) se ressemblaient trait pour trait, et un projet reconstruit en
    // parité en aligne onze.
    // LE NOM ENTIER EN INFOBULLE : une tranche de console est étroite, et
    // « Batterie · kick+kick2 » s'y tronque en « Batterie · ki… ».
    nameLabel_.setTooltip(juce::String::fromUTF8(track_.name.c_str()));
    if (track_.kind == vsm::sequencer::Track::Kind::Group) {
        nameLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentAmber);
        nameLabel_.setTooltip(juce::String::fromUTF8(track_.name.c_str())
                              + juce::String::fromUTF8(" — bus de groupe : les pistes routées vers lui "
                                                       "passent par ce fader"));
    }
    addAndMakeVisible(nameLabel_);

    volume_.setSliderStyle(juce::Slider::LinearVertical);
    volume_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    volume_.setRange(-60.0, 6.0, 0.1);
    volume_.setSkewFactorFromMidPoint(-12.0);
    volume_.setValue(gainToDb(track_.volume), juce::dontSendNotification);
    volume_.setTextValueSuffix(" dB");
    volume_.onDragStart = [this] {
        if (onMixEditStarted) onMixEditStarted();
        ouvrirPasse("mix.volume");
    };
    volume_.onDragEnd = [this] { fermerPasse("mix.volume", false); };
    volume_.onValueChange = [this] {
        track_.volume = dbToGain(static_cast<float>(volume_.getValue()));
        // LA COURBE REÇOIT LE GAIN LINÉAIRE, pas les décibels du curseur :
        // `mix.volume` est en gain (voir `AutomationCurve::parameter`), et
        // écrire des dB ici ferait dessiner une courbe qui ne correspond pas
        // à ce que le moteur applique.
        noterDansLaPasse("mix.volume", track_.volume);
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(volume_);

    pan_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pan_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setValue(track_.pan, juce::dontSendNotification);
    pan_.onDragStart = [this] {
        if (onMixEditStarted) onMixEditStarted();
        ouvrirPasse("mix.pan");
    };
    pan_.onDragEnd = [this] { fermerPasse("mix.pan", false); };
    pan_.onValueChange = [this] {
        track_.pan = static_cast<float>(pan_.getValue());
        noterDansLaPasse("mix.pan", track_.pan);
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(pan_);

    // LE DÉCALAGE DE PISTE (D16.7). Bornes à +/- 200 ms : au-delà on ne
    // corrige plus un temps de réaction, on déplace la partie -- et cela se
    // fait au clip, où l'on VOIT ce qu'on déplace.
    delay_.setSliderStyle(juce::Slider::LinearBar);
    delay_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 16);
    delay_.setRange(-200.0, 200.0, 0.1);
    delay_.setTextValueSuffix(" ms");
    delay_.setValue(track_.delayMs, juce::dontSendNotification);
    delay_.setTooltip(juce::String::fromUTF8(
        u8"Décalage de la piste, en millisecondes. Négatif : elle sonne plus tôt. "
        u8"Ne change pas la compensation de latence."));
    delay_.onDragStart = [this] { if (onMixEditStarted) onMixEditStarted(); };
    delay_.onValueChange = [this] {
        track_.delayMs = delay_.getValue();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(delay_);

    // UN BOUTON PAR BUS DÉCLARÉ, et son infobulle dit lequel : « send A » et
    // « send B » n'apprenaient rien, et le projet ne disait même pas ce qu'ils
    // alimentaient.
    for (size_t bus = 0; bus < sendNames.size(); ++bus) {
        auto* s = new juce::Slider();
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setRange(0.0, 1.0, 0.01);
        s->setValue(track_.sendLevel(bus), juce::dontSendNotification);
        s->setTooltip(juce::String("Depart vers ") + juce::String(sendNames[bus]));
        const std::string parametre = "mix.send." + std::to_string(bus + 1);
        s->onDragStart = [this, parametre] {
            if (onMixEditStarted) onMixEditStarted();
            ouvrirPasse(parametre);
        };
        s->onDragEnd = [this, parametre] { fermerPasse(parametre, false); };
        s->onValueChange = [this, s, bus, parametre] {
            track_.setSendLevel(bus, static_cast<float>(s->getValue()));
            noterDansLaPasse(parametre, static_cast<float>(s->getValue()));
            if (onMixChanged) onMixChanged();
        };
        addAndMakeVisible(s);
        sends_.add(s);
    }

    // LE BOUTON W (D16.8), et le mot plutôt qu'un pictogramme, comme chez
    // Cubase : trois états qui se lisent à la couleur, off → touch → latch.
    armer_.setTooltip(juce::String::fromUTF8(
        u8"Écrire l'automation en jouant. Un clic : Touch (la main sur un réglage écrit "
        u8"tant qu'on la tient). Deux : Latch (elle écrit jusqu'à l'arrêt du transport). "
        u8"Trois : éteint."));
    armer_.onClick = [this] { basculerArmement(); };
    addAndMakeVisible(armer_);
    rafraichirArmement();

    mute_.setClickingTogglesState(true);
    mute_.setToggleState(track_.muted, juce::dontSendNotification);
    mute_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentRed);
    mute_.onClick = [this] {
        track_.muted = mute_.getToggleState();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(mute_);

    solo_.setClickingTogglesState(true);
    solo_.setToggleState(track_.solo, juce::dontSendNotification);
    solo_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentAmber);
    solo_.onClick = [this] {
        track_.solo = solo_.getToggleState();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(solo_);

    addAndMakeVisible(meter_);
}

void ChannelStrip::paint(juce::Graphics& g) {
    g.setColour(vsm::ui::Palette::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    // Bandeau couleur de la piste en haut.
    g.setColour(juce::Colour(track_.colorRgba));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f).removeFromTop(4.0f), 2.0f);
}

void ChannelStrip::resized() {
    auto r = getLocalBounds().reduced(4);
    r.removeFromTop(4); // bandeau couleur
    nameLabel_.setBounds(r.removeFromTop(18));
    pan_.setBounds(r.removeFromTop(34).reduced(6, 2));
    delay_.setBounds(r.removeFromTop(18).reduced(4, 1));

    // Deux petits knobs de send (A/B).
    auto sendRow = r.removeFromTop(28);
    // Les boutons se partagent la rangée à parts égales, quel qu'en soit le
    // nombre. Aucun bus déclaré : aucune rangée, plutôt que deux boutons qui
    // n'enverraient nulle part.
    if (!sends_.isEmpty()) {
        const int largeur = std::max(1, sendRow.getWidth() / sends_.size());
        for (auto* s : sends_) s->setBounds(sendRow.removeFromLeft(largeur).reduced(2, 1));
    }

    // D16.8 : LE W A SA PROPRE RANGÉE. Mis en tiers avec M et S, les trois
    // libellés étaient tronqués en « ... » sur une tranche de 76 pixels à
    // l'échelle 150 % -- et entre « ça tient dans la case » et « ça se lit »,
    // c'est la lisibilité qui gagne : on agrandit la case.
    auto bottom = r.removeFromBottom(22);
    mute_.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).reduced(1));
    solo_.setBounds(bottom.reduced(1));
    armer_.setBounds(r.removeFromBottom(22).reduced(1, 1));

    // Fader + mètre côte à côte.
    auto meterArea = r.removeFromRight(10);
    meter_.setBounds(meterArea.reduced(0, 2));
    volume_.setBounds(r);
}

// ---------------------------------------------------------------------------
// D16.8 — ÉCRIRE L'AUTOMATION EN JOUANT.
//
// Touch et Latch ne sont pas deux mécanismes : le même enregistrement tourne,
// et seul l'instant où il s'arrête change (le lâcher, ou l'arrêt du
// transport). C'est pourquoi il n'y a qu'une `Passe` et qu'un `fermerPasse`.
// ---------------------------------------------------------------------------

void ChannelStrip::basculerArmement() {
    using vsm::sequencer::AutomationMode;
    track_.automationMode = track_.automationMode == AutomationMode::Off   ? AutomationMode::Touch
                          : track_.automationMode == AutomationMode::Touch ? AutomationMode::Latch
                                                                           : AutomationMode::Off;
    // Désarmer clôt ce qui courait : sans cela, une passe en `latch` resterait
    // ouverte pour toujours et se déposerait au prochain arrêt, longtemps
    // après que l'utilisateur a cru avoir tout éteint.
    if (track_.automationMode == vsm::sequencer::AutomationMode::Off) closeLatchedPasses();
    rafraichirArmement();
    if (onMixChanged) onMixChanged();
}

void ChannelStrip::rafraichirArmement() {
    using vsm::sequencer::AutomationMode;
    const bool arme = track_.automationMode != AutomationMode::Off;
    armer_.setButtonText(track_.automationMode == AutomationMode::Latch  ? "W latch"
                          : track_.automationMode == AutomationMode::Touch ? "W touch"
                                                                           : "W");
    armer_.setColour(juce::TextButton::buttonColourId,
                      arme ? (track_.automationMode == AutomationMode::Latch
                                  ? vsm::ui::Palette::accentRed
                                  : vsm::ui::Palette::accentAmber)
                           : vsm::ui::Palette::panelRaised);
}

vsm::sequencer::AutomationCurve& ChannelStrip::courbeDe(const std::string& parametre) {
    for (auto& courbe : track_.automation)
        if (courbe.parameter == parametre) return courbe;
    vsm::sequencer::AutomationCurve neuve;
    neuve.parameter = parametre;
    track_.automation.push_back(std::move(neuve));
    return track_.automation.back();
}

void ChannelStrip::ouvrirPasse(const std::string& parametre) {
    if (track_.automationMode == vsm::sequencer::AutomationMode::Off) return;
    // LE TRANSPORT DOIT ROULER. Écrire à l'arrêt déposerait toute la passe sur
    // un seul tick -- c'est-à-dire rien de lisible --, et surtout cela
    // transformerait un simple réglage de mixage en édition de courbe.
    if (!transportPlayingProvider || !transportPlayingProvider()) return;
    Passe passe;
    passe.debut = playheadTickProvider ? playheadTickProvider() : 0;
    passes_[parametre] = std::move(passe);
}

void ChannelStrip::noterDansLaPasse(const std::string& parametre, float valeur) {
    auto it = passes_.find(parametre);
    if (it == passes_.end()) return;
    const vsm::midi::Tick ou = playheadTickProvider ? playheadTickProvider() : it->second.debut;
    // Un point par tick : deux valeurs au même instant rendraient le segment
    // entre elles indéfini, et la souris en produit plusieurs par milliseconde.
    if (!it->second.points.empty() && it->second.points.back().tick == ou)
        it->second.points.back().value = valeur;
    else
        it->second.points.push_back({ou, valeur, false});
}

void ChannelStrip::fermerPasse(const std::string& parametre, bool arretDuTransport) {
    auto it = passes_.find(parametre);
    if (it == passes_.end()) return;

    // EN LATCH, LE LÂCHER NE CLÔT RIEN : on continue d'écrire la dernière
    // valeur jusqu'à l'arrêt. C'est toute la différence avec Touch, et elle
    // tient dans cette ligne.
    if (track_.automationMode == vsm::sequencer::AutomationMode::Latch && !arretDuTransport) {
        it->second.relachee = true;
        return;
    }

    Passe passe = std::move(it->second);
    passes_.erase(it);
    if (passe.points.empty()) return;

    vsm::midi::Tick fin = playheadTickProvider ? playheadTickProvider() : passe.points.back().tick;
    if (fin < passe.points.back().tick) fin = passe.points.back().tick;
    // En latch, la valeur tenue court du lâcher jusqu'à l'arrêt : un point de
    // plus à la fin suffit à l'écrire, sans minuterie qui échantillonnerait
    // une valeur qui ne bouge plus.
    if (passe.relachee && fin > passe.points.back().tick)
        passe.points.push_back({fin, passe.points.back().value, false});

    vsm::sequencer::writeAutomationRange(courbeDe(parametre), passe.debut, fin, passe.points);
    if (onAutomationWritten) onAutomationWritten();
}

void ChannelStrip::closeLatchedPasses() {
    std::vector<std::string> ouvertes;
    for (const auto& [parametre, passe] : passes_) ouvertes.push_back(parametre);
    for (const auto& parametre : ouvertes) fermerPasse(parametre, true);
}

void MixerComponent::closeLatchedPasses() {
    for (auto* strip : strips_) strip->closeLatchedPasses();
}

// ============================================================= MasterStrip

MasterStrip::MasterStrip() {
    titleLabel_.setText("MASTER", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
    titleLabel_.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    addAndMakeVisible(titleLabel_);

    enableButton_.setClickingTogglesState(true);
    enableButton_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentTeal);
    enableButton_.onClick = [this] {
        if (onMasterEnable) onMasterEnable(enableButton_.getToggleState());
    };
    addAndMakeVisible(enableButton_);

    addKnob(MasterBus::kLowShelfGainDb, "LOW", -18.0f, 18.0f, 0.0f, " dB");
    addKnob(MasterBus::kMidGainDb, "MID", -18.0f, 18.0f, 0.0f, " dB");
    addKnob(MasterBus::kHighShelfGainDb, "HIGH", -18.0f, 18.0f, 0.0f, " dB");
    addKnob(MasterBus::kCompThresholdDb, "COMP", -48.0f, 0.0f, 0.0f, " dB");
    addKnob(MasterBus::kCompRatio, "RATIO", 1.0f, 20.0f, 2.0f, ":1");
    addKnob(MasterBus::kSaturationDrive, "SAT", 0.0f, 1.0f, 0.0f, "");
    addKnob(MasterBus::kLimiterCeilingDb, "CEIL", -12.0f, 0.0f, -0.3f, " dB");

    lufsLabel_.setText("-inf LUFS", juce::dontSendNotification);
    lufsLabel_.setJustificationType(juce::Justification::centred);
    lufsLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    lufsLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(lufsLabel_);

    // LA CORRÉLATION DE PHASE EN CLAIR (D4.7). Une bande colorée dit qu'il y a
    // un problème, elle ne dit pas s'il est de -0,1 ou de -0,9 -- et c'est ce
    // qui décide si on va chercher.
    phaseLabel_.setText("1.00", juce::dontSendNotification);
    phaseLabel_.setJustificationType(juce::Justification::centred);
    phaseLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    phaseLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    phaseLabel_.setTooltip("Correlation de phase : +1 en phase, 0 sans rapport, "
                            "negatif = la piste disparait en mono.");
    addAndMakeVisible(phaseLabel_);

    addAndMakeVisible(meter_);
}

juce::Slider& MasterStrip::addKnob(vsm::audio::plugin::ParamId id, const juce::String& label,
                                   float min, float max, float def, const juce::String& suffix) {
    Knob k;
    k.id = id;
    k.slider = std::make_unique<juce::Slider>();
    k.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k.slider->setRange(min, max, (max - min) / 1000.0);
    k.slider->setValue(def, juce::dontSendNotification);
    k.slider->setTextValueSuffix(suffix);
    const auto pid = id;
    juce::Slider* raw = k.slider.get();
    raw->onValueChange = [this, raw, pid] {
        if (onMasterParam) onMasterParam(pid, static_cast<float>(raw->getValue()));
    };
    addAndMakeVisible(*k.slider);

    k.label = std::make_unique<juce::Label>();
    k.label->setText(label, juce::dontSendNotification);
    k.label->setJustificationType(juce::Justification::centred);
    k.label->setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    k.label->setFont(juce::Font(juce::FontOptions(9.5f)));
    addAndMakeVisible(*k.label);

    knobs_.push_back(std::move(k));
    return *knobs_.back().slider;
}

void MasterStrip::syncFromEngine() {
    if (masterParamProvider) {
        for (auto& k : knobs_)
            k.slider->setValue(masterParamProvider(k.id), juce::dontSendNotification);
        enableButton_.setToggleState(masterParamProvider(MasterBus::kEnabled) >= 0.5f,
                                     juce::dontSendNotification);
    }
}

void MasterStrip::paint(juce::Graphics& g) {
    g.setColour(vsm::ui::Palette::panelRaised);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    g.setColour(vsm::ui::Palette::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f, 1.0f);
}

void MasterStrip::resized() {
    auto r = getLocalBounds().reduced(6);
    titleLabel_.setBounds(r.removeFromTop(18));
    enableButton_.setBounds(r.removeFromTop(22).reduced(8, 2));
    r.removeFromTop(4);

    // Grille de knobs 2 colonnes.
    auto meterArea = r.removeFromRight(12);
    meter_.setBounds(meterArea.reduced(0, 2));
    {
        auto bas = getLocalBounds().reduced(6);
        lufsLabel_.setBounds(bas.removeFromBottom(16));
        phaseLabel_.setBounds(bas.removeFromBottom(14));
    }
    r.removeFromBottom(18);

    const int cols = 2;
    const int knobH = 46;
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const int cw = r.getWidth() / cols;
        juce::Rectangle<int> cell(r.getX() + col * cw, r.getY() + row * knobH, cw, knobH);
        knobs_[i].label->setBounds(cell.removeFromBottom(12));
        knobs_[i].slider->setBounds(cell.reduced(2));
    }
}

// =========================================================== MixerComponent

MixerComponent::MixerComponent() {
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&stripContainer_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(master_);
}

void MixerComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    strips_.clear();
    std::vector<std::string> sendNames;
    if (project_ != nullptr)
        for (const auto& bus : project_->sends)
            sendNames.push_back(bus.name.empty() ? "Bus " + std::to_string(sendNames.size() + 1)
                                                  : bus.name);
    if (project_ != nullptr) {
        for (size_t i = 0; i < project_->tracks.size(); ++i) {
            auto* strip = new ChannelStrip(project_->tracks[i], i, sendNames);
            if (project_->tracks[i].kind == vsm::sequencer::Track::Kind::Group) {
                juce::StringArray membres;
                for (const auto& autre : project_->tracks)
                    if (autre.outputGroup == static_cast<int>(i))
                        membres.add(juce::String::fromUTF8(autre.name.c_str()));
                strip->setMembers(membres);
            }
            strip->onMixChanged = [this] { if (onMixChanged) onMixChanged(); };
            strip->onMixEditStarted = [this] { if (onMixEditStarted) onMixEditStarted(); };
            // D16.8 : la tranche a besoin de savoir OÙ en est le transport et
            // s'il roule ; ces deux réponses appartiennent à l'application.
            strip->playheadTickProvider = playheadTickProvider;
            strip->transportPlayingProvider = transportPlayingProvider;
            strip->onAutomationWritten = [this] { if (onAutomationWritten) onAutomationWritten(); };
            stripContainer_.addAndMakeVisible(strip);
            strips_.add(strip);
        }
    }
    master_.onMasterParam = [this](vsm::audio::plugin::ParamId id, float v) {
        if (onMasterParam) onMasterParam(id, v);
    };
    master_.onMasterEnable = [this](bool on) { if (onMasterEnable) onMasterEnable(on); };
    master_.masterParamProvider = masterParamProvider;
    master_.syncFromEngine();
    resized();
}

void MixerComponent::updateMeters(
    const std::function<vsm::audio::engine::TrackMeasurement(size_t)>& trackMeasure,
    double masterLufs, float masterPeak, float masterRms, float masterCorrelation) {
    for (int i = 0; i < strips_.size(); ++i)
        strips_[i]->setMeasurement(trackMeasure(static_cast<size_t>(i)));
    master_.setMeters(masterLufs, masterPeak, masterRms, masterCorrelation);
}

void MixerComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::background);
}

void MixerComponent::resized() {
    auto r = getLocalBounds();
    master_.setBounds(r.removeFromRight(kMasterWidth));
    viewport_.setBounds(r);

    const int n = strips_.size();
    stripContainer_.setSize(juce::jmax(r.getWidth(), n * kStripWidth), r.getHeight() - 12);
    for (int i = 0; i < n; ++i)
        strips_[i]->setBounds(i * kStripWidth, 0, kStripWidth, stripContainer_.getHeight());
}
