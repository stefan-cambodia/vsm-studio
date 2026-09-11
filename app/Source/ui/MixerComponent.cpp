#include "MixerComponent.h"
#include "Langue.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <cmath>

using vsm::audio::engine::MasterBus;
using vsm::app::ui::tr;

// ============================================================ ChannelStrip

void ChannelStrip::setMembers(const juce::StringArray& membres) {
    if (track_.kind != vsm::sequencer::Track::Kind::Group) return;
    membres_ = membres;
    membresConnus_ = true;
    poserInfobulleDuNom();
}

void ChannelStrip::poserInfobulleDuNom() {
    // LE NOM ENTIER EN INFOBULLE : une tranche de console est étroite, et
    // « Batterie · kick+kick2 » s'y tronque en « Batterie · ki… ». Un bus de
    // groupe y ajoute ce qu'il est, et ses membres dès qu'on les connaît.
    const juce::String nom = juce::String::fromUTF8(track_.name.c_str());
    if (track_.kind != vsm::sequencer::Track::Kind::Group)
        nameLabel_.setTooltip(nom);
    else if (!membresConnus_)
        nameLabel_.setTooltip(tr(u8"%1 — bus de groupe : les pistes routées vers lui passent par ce fader")
                                  .replace("%1", nom));
    else if (membres_.isEmpty())
        nameLabel_.setTooltip(tr(u8"%1 — bus de groupe : aucune piste n'y est routée").replace("%1", nom));
    else
        nameLabel_.setTooltip(tr(u8"%1 — bus de groupe : %2")
                                  .replace("%1", nom).replace("%2", membres_.joinIntoString(", ")));
}

ChannelStrip::ChannelStrip(vsm::sequencer::Track& track, size_t index,
                            const std::vector<std::string>& sendNames)
    : track_(track), index_(index), sendNames_(sendNames) {
    nameLabel_.setText(track_.name.empty() ? "Track" : track_.name, juce::dontSendNotification);
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
    nameLabel_.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    // UN BUS DE GROUPE SE RECONNAÎT : son nom en ambre, comme le master est à
    // part. Sans cela, « Batterie » (le bus) et « Batterie · hihat » (une
    // pièce) se ressemblaient trait pour trait, et un projet reconstruit en
    // parité en aligne onze. Son infobulle : `poserInfobulleDuNom()`.
    if (track_.kind == vsm::sequencer::Track::Kind::Group)
        nameLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentAmber);
    addAndMakeVisible(nameLabel_);

    volume_.setSliderStyle(juce::Slider::LinearVertical);
    volume_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    volume_.setRange(-60.0, 6.0, 0.1);
    volume_.setDoubleClickReturnValue(true, 0.0);   // D25.3 : 0 dB
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
    pan_.setName("mixeur.pan");   // D135 : le nom par lequel le banc le désigne (appuyer:)
    pan_.textFromValueFunction = [](double v) { return vsm::app::ui::textePanoramique(v); };   // D135
    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setDoubleClickReturnValue(true, 0.0);      // D25.3 : centre
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

    // D30.4 : LE TRIM D'ENTRÉE. Bornes à +/- 24 dB : au-delà on ne corrige
    // plus un niveau d'entrée, on refait le mixage -- et c'est le fader qui
    // fait cela. Automatisable comme le volume et le panoramique, d'où la
    // passe ouverte au glissé.
    trim_.setSliderStyle(juce::Slider::LinearBar);
    trim_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 16);
    trim_.setRange(-24.0, 24.0, 0.1);
    trim_.setDoubleClickReturnValue(true, 0.0);     // D25.3 : 0 dB
    // « TRIM » DANS LA CASE, ET NON LE SEUL NOMBRE. La première capture
    // donnait « -6,0 dB » en tête de tranche, au-dessus d'un fader qui n'écrit
    // pas sa valeur : rien ne distinguait le trim du volume, et un réglage
    // qu'on prend pour un autre est pire qu'un réglage caché. La tranche a été
    // ÉLARGIE pour que le mot tienne (76 -> 88 px) plutôt que le texte
    // rétréci -- entre « ça tient dans la case » et « ça se lit », c'est la
    // lisibilité qui gagne.
    trim_.textFromValueFunction = [](double v) {
        return juce::String("Trim ") + juce::String(v, 1) + " dB";
    };
    trim_.valueFromTextFunction = [](const juce::String& t) {
        return t.retainCharacters("-0123456789.").getDoubleValue();
    };
    trim_.setValue(track_.inputTrimDb, juce::dontSendNotification);
    // ET IL FAUT REDEMANDER LE TEXTE. `setValue` d'une valeur DÉJÀ EN PLACE ne
    // notifie rien, donc ne rappelle pas `textFromValueFunction` : la première
    // capture montrait « Trim -6.0 dB » sur la piste réglée et un « 0.0 » nu
    // sur celle qui ne l'était pas -- le libellé manquait précisément là où
    // rien n'avait bougé, c'est-à-dire sur presque toutes les tranches.
    trim_.updateText();
    trim_.onDragStart = [this] {
        if (onMixEditStarted) onMixEditStarted();
        ouvrirPasse("mix.trim");
    };
    trim_.onDragEnd = [this] { fermerPasse("mix.trim", false); };
    trim_.onValueChange = [this] {
        track_.inputTrimDb = static_cast<float>(trim_.getValue());
        // EN DÉCIBELS DANS LA COURBE, comme dans le curseur : `mix.volume` est
        // en gain parce que le fader l'est ; le trim est gradué en dB, et une
        // courbe qui interpolerait son gain linéaire dessinerait une rampe et
        // en ferait entendre une autre.
        noterDansLaPasse("mix.trim", track_.inputTrimDb);
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(trim_);

    // LE DÉCALAGE DE PISTE (D16.7). Bornes à +/- 200 ms : au-delà on ne
    // corrige plus un temps de réaction, on déplace la partie -- et cela se
    // fait au clip, où l'on VOIT ce qu'on déplace.
    delay_.setSliderStyle(juce::Slider::LinearBar);
    delay_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 16);
    delay_.setRange(-200.0, 200.0, 0.1);
    delay_.setDoubleClickReturnValue(true, 0.0);    // D25.3 : 0 ms
    delay_.setTextValueSuffix(" ms");
    delay_.setValue(track_.delayMs, juce::dontSendNotification);
    delay_.onDragStart = [this] { if (onMixEditStarted) onMixEditStarted(); };
    delay_.onValueChange = [this] {
        track_.delayMs = delay_.getValue();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(delay_);

    // D17.5 : la transposition, en demi-tons. Bornes à +/- 48 : quatre octaves
    // de part et d'autre couvrent tout ce qu'un clavier peut demander, et
    // au-delà toute note sortirait de la plage MIDI de toute façon.
    transposition_.setSliderStyle(juce::Slider::LinearBar);
    transposition_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 16);
    transposition_.setRange(-48.0, 48.0, 1.0);
    transposition_.setDoubleClickReturnValue(true, 0.0);   // D25.3 : 0 demi-ton
    transposition_.setValue(track_.transposeSemitones, juce::dontSendNotification);   // l'unité et l'infobulle : `retraduire()`
    transposition_.onDragStart = [this] { if (onMixEditStarted) onMixEditStarted(); };
    transposition_.onValueChange = [this] {
        track_.transposeSemitones = static_cast<int>(transposition_.getValue());
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(transposition_);

    // UN BOUTON PAR BUS DÉCLARÉ, et son infobulle dit lequel : « send A » et
    // « send B » n'apprenaient rien, et le projet ne disait même pas ce qu'ils
    // alimentaient.
    for (size_t bus = 0; bus < sendNames.size(); ++bus) {
        auto* s = new juce::Slider();
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setName("mixeur.depart");   // D135 : le nom par lequel le banc le désigne (appuyer:)
        s->textFromValueFunction = [](double v) { return vsm::app::ui::texteDecibels(v); };   // D135
        bullesDesDeparts_.push_back(std::make_unique<vsm::app::ui::BulleDeValeur>(*s));
        s->setRange(0.0, 1.0, 0.01);
        s->setValue(track_.sendLevel(bus), juce::dontSendNotification);
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
    armer_.onClick = [this] { basculerArmement(); };
    addAndMakeVisible(armer_);
    rafraichirArmement();

    // D23.1 : LA POLARITÉ. Annulable comme un geste de fader : la piste
    // porte l'état, le moteur le lit à la republication du projet.
    phase_.setClickingTogglesState(true);
    phase_.setToggleState(track_.invertPhase, juce::dontSendNotification);
    phase_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentTeal);
    phase_.onClick = [this] {
        if (onMixEditStarted) onMixEditStarted();
        track_.invertPhase = phase_.getToggleState();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(phase_);

    mute_.setClickingTogglesState(true);
    mute_.setToggleState(track_.muted, juce::dontSendNotification);
    mute_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentRed);
    mute_.onClick = [this] {
        track_.muted = mute_.getToggleState();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(mute_);

    solo_.setClickingTogglesState(true);
    solo_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentAmber);
    rafraichirSolo();
    solo_.onClick = [this] {
        // D21.2 : CTRL+CLIC = SOLO EXCLUSIF. Le bouton a déjà basculé son
        // état ; on le laisse à l'application, qui éteint les autres et
        // resynchronise toutes les tranches.
        if (juce::ModifierKeys::getCurrentModifiers().isCtrlDown()
            || juce::ModifierKeys::getCurrentModifiers().isCommandDown()) {
            solo_.setToggleState(track_.solo, juce::dontSendNotification);
            if (onExclusiveSoloRequested) onExclusiveSoloRequested(index_);
            return;
        }
        // D30.1 : ALT+CLIC = SOLO PROTÉGÉ. Sur le bouton Solo parce que c'est
        // du solo qu'il parle -- « celui des autres ne me concerne pas » --,
        // et non un huitième bouton dans une tranche de 76 pixels.
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) {
            if (onMixEditStarted) onMixEditStarted();
            track_.soloSafe = !track_.soloSafe;
            rafraichirSolo();
            if (onMixChanged) onMixChanged();
            return;
        }
        track_.solo = solo_.getToggleState();
        if (onMixChanged) onMixChanged();
    };
    addAndMakeVisible(solo_);

    addAndMakeVisible(meter_);
    retraduire();   // D94 : infobulles et unité, dans la langue courante
}

void ChannelStrip::retraduire() {
    poserInfobulleDuNom();
    trim_.setTooltip(tr(u8"Trim d'entrée : le gain AVANT les inserts. Pousse la piste dans son "
                        u8"compresseur ou sa saturation sans toucher à leur réglage. Sans insert, "
                        u8"il fait ce que ferait le fader."));
    delay_.setTooltip(tr(u8"Décalage de la piste, en millisecondes. Négatif : elle sonne plus tôt. "
                         u8"Ne change pas la compensation de latence."));
    // « dt » est une abréviation FRANÇAISE (demi-ton) : l'anglais écrit « st ».
    transposition_.setTextValueSuffix(tr(u8" dt"));
    transposition_.setTooltip(tr(u8"Transposition de la piste, en demi-tons, appliquée À LA LECTURE : le "
                                 u8"matériau ne bouge pas, et le piano roll continue de montrer les notes "
                                 u8"écrites. Une note poussée hors de 0..127 ne sonne pas, et l'application "
                                 u8"le dit."));
    // UN BOUTON PAR BUS DÉCLARÉ, et son infobulle dit lequel : « send A » et
    // « send B » n'apprenaient rien.
    for (int bus = 0; bus < sends_.size() && static_cast<size_t>(bus) < sendNames_.size(); ++bus)
        sends_[bus]->setTooltip(tr("Depart vers %1")
                                    .replace("%1", juce::String::fromUTF8(sendNames_[static_cast<size_t>(bus)].c_str())));
    armer_.setTooltip(tr(u8"Écrire l'automation en jouant. Un clic : Touch (la main sur un réglage écrit "
                         u8"tant qu'on la tient). Deux : Latch (elle écrit jusqu'à l'arrêt du transport). "
                         u8"Trois : éteint."));
    phase_.setTooltip(tr(u8"Polarité inversée (Ø) : la piste et ses départs changent de signe. "
                         u8"Deux micros en opposition, un bus qui creuse le mixage."));
    rafraichirSolo();
}

void ChannelStrip::rafraichirSolo() {
    solo_.setToggleState(track_.solo, juce::dontSendNotification);
    // PROTÉGÉ, LE BOUTON LE DIT DE DEUX FAÇONS : son libellé (« S+ ») et sa
    // couleur au repos. Un seul des deux suffirait à un œil qui sait ce qu'il
    // cherche ; il en faut deux à celui qui ne le sait pas.
    solo_.setButtonText(track_.soloSafe ? "S+" : "S");
    if (track_.soloSafe)
        solo_.setColour(juce::TextButton::buttonColourId, vsm::ui::Palette::accentTeal);
    else
        solo_.removeColour(juce::TextButton::buttonColourId);
    solo_.setTooltip(track_.soloSafe
        ? tr(u8"Solo PROTÉGÉ (Alt+clic) : le solo des autres pistes ne fait pas taire "
             u8"celle-ci. Son propre muet reste le sien. À poser sur un retour d'effet, "
             u8"pour qu'un solo garde sa réverbération.")
        : tr(u8"Solo. Ctrl+clic : solo exclusif. Alt+clic : protéger cette piste du solo "
             u8"des autres."));
    solo_.repaint();
}

void ChannelStrip::paint(juce::Graphics& g) {
    g.setColour(vsm::ui::Palette::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f);
    // D39.4 : LE MÊME CONTOUR AMBRE QUE LA LIGNE DE PISTE ET QUE L'EN-TÊTE DE
    // L'ARRANGEMENT. Trois panneaux montrent la même sélection ; leur donner
    // trois marques différentes obligerait à apprendre trois codes pour une
    // seule idée.
    if (choisie_) {
        g.setColour(vsm::ui::Palette::accentAmber.withAlpha(0.85f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f, 2.0f);
    }
    // Bandeau couleur de la piste en haut.
    g.setColour(juce::Colour(track_.colorRgba));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f).removeFromTop(4.0f), 2.0f);
}

void ChannelStrip::resized() {
    auto r = getLocalBounds().reduced(4);
    r.removeFromTop(4); // bandeau couleur
    nameLabel_.setBounds(r.removeFromTop(18));
    trim_.setBounds(r.removeFromTop(18).reduced(4, 1));       // D30.4, en tête de chaîne
    pan_.setBounds(r.removeFromTop(34).reduced(6, 2));
    delay_.setBounds(r.removeFromTop(18).reduced(4, 1));
    transposition_.setBounds(r.removeFromTop(18).reduced(4, 1));

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
    {
        auto rangeeW = r.removeFromBottom(22);
        armer_.setBounds(rangeeW.removeFromLeft(rangeeW.getWidth() / 2).reduced(1, 1));
        phase_.setBounds(rangeeW.reduced(1, 1));   // D23.1
    }

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

void ChannelStrip::applyExternalControl(const std::string& parametre, float valeur) {
    // LE CURSEUR SUIT SANS RIEN DÉCLENCHER : c'est nous qui posons la piste et
    // la passe, pas le rappel du curseur (qui le ferait une seconde fois).
    if (parametre == "mix.volume") {
        track_.volume = valeur;
        volume_.setValue(gainToDb(valeur), juce::dontSendNotification);
    } else if (parametre == "mix.pan") {
        track_.pan = valeur;
        pan_.setValue(valeur, juce::dontSendNotification);
    } else {
        return;
    }
    // LA PASSE : ouverte au premier message si le W est armé et que le
    // transport roule, nourrie ensuite. Un potentiomètre ne se « lâche » pas :
    // la passe court jusqu'à l'arrêt du transport, comme en latch, quel que
    // soit le mode -- Touch n'a pas de sens sans relâchement, et c'est dit
    // dans la feuille de route (D29.3).
    if (passes_.find(parametre) == passes_.end()) ouvrirPasse(parametre);
    noterDansLaPasse(parametre, valeur);
    if (onMixChanged) onMixChanged();
}

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

    // D48 : LE TÉMOIN DE SATURATION, caché tant qu'il n'y a rien à dire.
    // Un voyant allumé en permanence devient un meuble ; celui-ci n'apparaît
    // que lorsqu'il a quelque chose à annoncer, comme le compte de craquements
    // de D41.3 et le témoin « SANS SON » de D43.
    satLabel_.setJustificationType(juce::Justification::centred);
    satLabel_.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    satLabel_.addMouseListener(this, false);
    // `addChildComponent` ET NON `addAndMakeVisible` : ce dernier REND VISIBLE,
    // et annulait donc le `setVisible(false)` qui le précédait. Le témoin vide
    // occupait alors la ligne en permanence et en chassait la phase, qui la
    // partage -- un composant invisible qui reste visible ne se voit pas, il
    // se voit à ce qu'il cache.
    addChildComponent(satLabel_);

    enableButton_.setClickingTogglesState(true);
    enableButton_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentTeal);
    enableButton_.onClick = [this] {
        if (onMasterEnable) onMasterEnable(enableButton_.getToggleState());
    };
    addAndMakeVisible(enableButton_);

    // D23.5 : L'ÉCOUTE EN MONO. Ambre, comme un solo : c'est un état d'écoute
    // qu'on doit remarquer allumé, pas un réglage qu'on laisse.
    monoButton_.setClickingTogglesState(true);
    monoButton_.setColour(juce::TextButton::buttonOnColourId, vsm::ui::Palette::accentAmber);
    monoButton_.onClick = [this] { if (onMonoListen) onMonoListen(monoButton_.getToggleState()); };
    addAndMakeVisible(monoButton_);

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
    addAndMakeVisible(phaseLabel_);

    addAndMakeVisible(meter_);
    retraduire();   // D94
}

void MasterStrip::retraduire() {
    monoButton_.setTooltip(tr(u8"Écoute en mono : L+R repliés après le limiteur, la corrélation "
                              u8"lue devient ce qu'on entend. Jamais dans un export."));
    phaseLabel_.setTooltip(tr("Correlation de phase : +1 en phase, 0 sans rapport, "
                              "negatif = la piste disparait en mono."));
    if (satVue_) poserInfobulleSat();
}

void MasterStrip::poserInfobulleSat() {
    satLabel_.setTooltip(tr(u8"La sortie a dépassé 0 dBFS : ce qui part vers la carte son est écrêté. "
                            u8"Baisser le fader master, ou activer le limiteur. Cliquez pour effacer.")
                         + "\n\n" + phaseLabel_.getText());   // la phase reste lisible, elle cède seulement sa ligne
}

juce::Slider& MasterStrip::addKnob(vsm::audio::plugin::ParamId id, const juce::String& label,
                                   float min, float max, float def, const juce::String& suffix) {
    Knob k;
    k.id = id;
    k.slider = std::make_unique<juce::Slider>();
    k.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k.slider->setName("master." + label);   // D135 : le nom par lequel le banc le désigne (appuyer:)
    // D135 : l'unité existait et ne se montrait jamais ; une décimale suffit à un
    // réglage en dB ou en rapport, deux à la saturation (sans unité, 0 à 1).
    k.slider->setNumDecimalPlacesToDisplay(suffix.isEmpty() ? 2 : 1);
    k.bulle = std::make_unique<vsm::app::ui::BulleDeValeur>(*k.slider);
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
    {
        auto rangee = r.removeFromTop(22).reduced(2, 2);
        enableButton_.setBounds(rangee.removeFromLeft(rangee.getWidth() * 3 / 5).reduced(2, 0));
        monoButton_.setBounds(rangee.reduced(2, 0));   // D23.5
    }
    r.removeFromTop(4);

    // Grille de knobs 2 colonnes.
    auto meterArea = r.removeFromRight(12);
    meter_.setBounds(meterArea.reduced(0, 2));
    // LA SATURATION PREND LA PLACE DE LA PHASE, ELLE N'EN AJOUTE PAS.
    //
    // La tranche master est DÉJÀ trop courte pour ce qu'elle porte : huit
    // potentiomètres sur quatre rangées de 46 pixels, plus un titre, deux
    // boutons et deux étiquettes, dépassent la hauteur disponible. Réserver de
    // la place pour une troisième ligne ne suffisait donc pas -- la grille de
    // knobs a une hauteur fixe et déborde par le bas quoi qu'on réserve : le
    // témoin s'écrivait par-dessus les libellés RATIO et SAT.
    //
    // LA DÉCISION, ET SA RAISON : quand la sortie sature, le témoin occupe la
    // ligne de la PHASE. La corrélation est un diagnostic qu'on va consulter ;
    // la saturation est un fait qu'il faut voir maintenant. Les deux ne se
    // disputent la place que le temps de l'écrêtage, et la phase reste lisible
    // dans l'infobulle du témoin.
    {
        auto bas = getLocalBounds().reduced(6);
        lufsLabel_.setBounds(bas.removeFromBottom(16));
        auto ligneDePhase = bas.removeFromBottom(14);
        phaseLabel_.setBounds(ligneDePhase);
        satLabel_.setBounds(ligneDePhase);
        phaseLabel_.setVisible(!satLabel_.isVisible());
    }
    r.removeFromBottom(30);

    // D59 : LA RANGÉE S'ADAPTE À CE QUI RESTE, ET NE DÉBORDE PLUS.
    //
    // Elle valait 46 pixels quoi qu'il arrive, et le commentaire ci-dessus
    // constatait déjà le résultat : « la grille de knobs a une hauteur fixe et
    // déborde par le bas quoi qu'on réserve ». Personne ne l'avait VU, parce
    // que `VSM_TAILLE` ne faisait rien (D58) et que tous les autoportraits
    // étaient pris à la taille de l'écran, où la place ne manque pas. À la
    // première petite fenêtre, « phase 1.00 » et « -inf LUFS » s'écrivaient
    // par-dessus les libellés RATIO et SAT.
    //
    // LE PLANCHER EST UNE QUESTION DE LISIBILITÉ, pas de place : sous 34
    // pixels, l'étiquette de 12 points et son potentiomètre ne cohabitent
    // plus. La tranche préfère alors DÉFILER (le mélangeur a déjà sa barre)
    // plutôt que d'écraser ; et c'est le sens de `hauteurUtile()`, que le
    // mélangeur consulte pour donner à la tranche la hauteur qu'elle demande.
    const int cols = 2;
    const int rangees = (static_cast<int>(knobs_.size()) + cols - 1) / cols;
    const int knobH = rangees > 0
                        ? juce::jlimit(kHauteurRangeeMinimale, 46, r.getHeight() / rangees)
                        : 46;
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const int cw = r.getWidth() / cols;
        juce::Rectangle<int> cell(r.getX() + col * cw, r.getY() + row * knobH, cw, knobH);
        knobs_[i].label->setBounds(cell.removeFromBottom(12));
        knobs_[i].slider->setBounds(cell.reduced(2));
    }
}

int MasterStrip::hauteurUtile() const {
    const int cols = 2;
    const int rangees = (static_cast<int>(knobs_.size()) + cols - 1) / cols;
    // 6 d'encadrement en haut, 18 de titre, 22 de boutons, 4 d'écart, la
    // grille, puis les deux étiquettes du bas (16 + 14) et 6 d'encadrement.
    return 6 + 18 + 22 + 4 + rangees * kHauteurRangeeMinimale + 30 + 6;
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
            // D35.5 : UN DOSSIER N'A PAS DE TRANCHE, et voici pourquoi.
            //
            // Le mélangeur fabriquait une tranche par piste, dossiers compris.
            // Or un dossier n'est PAS un bus -- `Kind::Group` l'est, et fait
            // descendre le son de ses membres ; un dossier n'est qu'une
            // profondeur, aucun signal n'y passe. Son fader, son panoramique,
            // son trim, ses départs, ses inserts et ses vumètres étaient donc
            // six commandes mortes : on tirait le fader d'un dossier de douze
            // micros et rien ne bougeait. Une commande qui ne fait rien est
            // pire qu'une commande absente, parce qu'elle promet.
            //
            // SON MUET ET SON SOLO, EUX, AGISSENT depuis D35.4 -- et ils
            // restent atteignables là où le dossier vit, dans la liste des
            // pistes. C'est aussi là qu'on le replie, le renomme et le range :
            // le dossier est un objet de RANGEMENT, et sa place est dans la
            // liste, pas dans le mélangeur. Cubase cache ses pistes-dossiers
            // de la MixConsole pour cette raison exacte.
            //
            // CE QUE CELA COÛTE, ET COMMENT C'EST PAYÉ : un membre tu par son
            // dossier n'a plus, dans le mélangeur, de tranche qui l'explique.
            // Sa propre tranche le dit donc à sa place -- son bouton M
            // s'allume quand le silence lui vient d'un dossier (voir
            // `refreshMuteSolo`), sans quoi on aurait une tranche silencieuse
            // dont aucun bouton n'est enfoncé, c'est-à-dire une panne muette.
            if (project_->tracks[i].isFolder()) continue;
            auto* strip = new ChannelStrip(project_->tracks[i], i, sendNames);
            if (project_->tracks[i].kind == vsm::sequencer::Track::Kind::Group) {
                juce::StringArray membres;
                for (const auto& autre : project_->tracks)
                    if (autre.outputGroup == static_cast<int>(i))
                        membres.add(juce::String::fromUTF8(autre.name.c_str()));
                strip->setMembers(membres);
            }
            strip->onMixChanged = [this] { if (onMixChanged) onMixChanged(); };
            strip->onExclusiveSoloRequested = [this](size_t index) { if (onExclusiveSoloRequested) onExclusiveSoloRequested(index); };
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
    master_.onMonoListen = [this](bool on) { if (onMonoListen) onMonoListen(on); };
    master_.masterParamProvider = masterParamProvider;
    master_.syncFromEngine();
    resized();
}

void MixerComponent::retraduire() {
    for (auto* strip : strips_) strip->retraduire();
    master_.retraduire();
}

void MixerComponent::faireVoirLaTranche(size_t trackIndex) {
    ChannelStrip* cible = nullptr;
    for (auto* strip : strips_) if (strip->trackIndex() == trackIndex) cible = strip;
    // UN DOSSIER N'A PAS DE TRANCHE (D35.5) : il n'y a alors rien à montrer, et
    // ce n'est pas une erreur. On ne bouge pas plutôt que de faire défiler au
    // hasard -- un défilement sans raison est plus déroutant qu'aucun.
    if (cible == nullptr || viewport_.getViewWidth() <= 0) return;

    const auto bornes = cible->getBounds();
    const int gauche = viewport_.getViewPositionX();
    const int droite = gauche + viewport_.getViewWidth();
    // JUSTE ASSEZ POUR LA MONTRER ENTIÈRE, et pas plus : recentrer à chaque
    // changement de piste ferait sauter la console sous les doigts alors qu'on
    // travaille sur les tranches voisines.
    if (bornes.getX() < gauche)
        viewport_.setViewPosition(bornes.getX(), viewport_.getViewPositionY());
    else if (bornes.getRight() > droite)
        viewport_.setViewPosition(bornes.getRight() - viewport_.getViewWidth(),
                                   viewport_.getViewPositionY());
}

void MixerComponent::updateMeters(
    const std::function<vsm::audio::engine::TrackMeasurement(size_t)>& trackMeasure,
    double masterLufs, float masterPeak, float masterRms, float masterCorrelation) {
    // D35.5 : PAR `trackIndex()` ET NON PAR LE RANG. Depuis qu'un dossier n'a
    // plus de tranche, la n-ième tranche n'est plus la n-ième piste : lire les
    // vumètres au rang aurait montré, sous un dossier, le niveau de la piste
    // d'à côté -- une erreur qu'on ne voit pas, parce qu'un vumètre qui bouge a
    // l'air juste.
    for (int i = 0; i < strips_.size(); ++i)
        strips_[i]->setMeasurement(trackMeasure(strips_[i]->trackIndex()));
    master_.setMeters(masterLufs, masterPeak, masterRms, masterCorrelation);
}

void MixerComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::background);
}

int MixerComponent::hauteurMinimale() const { return master_.hauteurUtile(); }   // D59

void MixerComponent::resized() {
    auto r = getLocalBounds();
    master_.setBounds(r.removeFromRight(kMasterWidth));
    viewport_.setBounds(r);

    // D17.4 : les tranches masquées ne comptent pas dans la largeur totale.
    // D35.5 : et l'on demande à la TRANCHE quelle piste elle montre.
    const auto masqueeLa = [this](const ChannelStrip* strip) {
        if (project_ == nullptr) return false;
        const size_t i = strip->trackIndex();
        return i < project_->tracks.size() && project_->tracks[i].hidden;
    };
    int visibles = 0;
    for (int i = 0; i < strips_.size(); ++i)
        if (!masqueeLa(strips_[i])) ++visibles;
    stripContainer_.setSize(juce::jmax(r.getWidth(), visibles * kStripWidth), r.getHeight() - 12);
    {
        // D17.4 : une tranche masquée occupe une largeur nulle.
        int x = 0;
        for (int i = 0; i < strips_.size(); ++i) {
            const bool masquee = masqueeLa(strips_[i]);
            const int w = masquee ? 0 : kStripWidth;
            strips_[i]->setBounds(x, 0, w, stripContainer_.getHeight());
            strips_[i]->setVisible(!masquee);
            x += w;
        }
    }
}
