#include "TrackListComponent.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

using namespace vsm::sequencer;
using namespace vsm::ui;

namespace {
/// Construit la liste affichée dans le combo "instrument" à partir des
/// plugins RÉELLEMENT enregistrés auprès de PluginRegistry (pas une liste
/// statique) : un nouveau plugin Phase 3+ apparaît ici automatiquement, dès
/// que registerBuiltInPlugins() l'a référencé (voir Main.cpp).
std::vector<std::pair<std::string, std::string>> availableInstruments() {
    auto list = vsm::audio::plugin::PluginRegistry::instance().listAvailable();
    std::sort(list.begin(), list.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; }); // tri par nom affiché
    return list;
}
}

// ---------------------------------------------------------------------------
// TrackRowComponent
// ---------------------------------------------------------------------------

TrackRowComponent::TrackRowComponent(Track& track, size_t trackIndex,
                                      const std::vector<std::pair<int, std::string>>& groupes,
                                      const juce::String& sourceName)
    : track_(track), index_(trackIndex), sourceName_(sourceName),
      audio_(track.kind == Track::Kind::Audio) {
    addAndMakeVisible(nameLabel_);
    nameLabel_.setText(track_.name.empty() ? vsm::app::ui::tr(u8"Piste %1").replace("%1", juce::String(static_cast<int>(trackIndex) + 1))
                                         : juce::String::fromUTF8(track_.name.c_str()),
                        juce::dontSendNotification);
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this] {
        // D36.1 : le signal part AVANT l'écriture. L'historique mémorise l'état
        // d'avant ; signaler après ferait photographier le nom déjà changé.
        debutEdition(u8"Renommer la piste");
        track_.name = nameLabel_.getText().toStdString();
        if (onChanged) onChanged();
        if (onRenamed) onRenamed();
    };

    addAndMakeVisible(channelLabel_);
    channelLabel_.setText(audio_ ? juce::String("Audio")
                                  : juce::String("Ch " + juce::String(track_.channel + 1)),
                           juce::dontSendNotification);
    channelLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    channelLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    // D11.5 : LE CANAL MIDI SE SAISIT. Il était attribué à la création
    // (`n % 16`) et affiché sans qu'on puisse y toucher. Un nombre de 1 à
    // 16 ; tout autre texte rend l'ancien. Le planning du moteur suit.
    if (!audio_) {
        channelLabel_.setEditable(false, true, false);   // l'infobulle : `poserTextes()` (D94)
        channelLabel_.onTextChange = [this] {
            const int saisi = channelLabel_.getText().retainCharacters("0123456789").getIntValue();
            if (saisi >= 1 && saisi <= 16 && saisi - 1 != static_cast<int>(track_.channel)) {
                debutEdition(u8"Canal MIDI");
                track_.channel = static_cast<uint8_t>(saisi - 1);
                if (onChanged) onChanged();
            }
            channelLabel_.setText("Ch " + juce::String(track_.channel + 1), juce::dontSendNotification);
        };
    }

    // UNE PISTE AUDIO N'A PAS D'INSTRUMENT, et lui présenter un sélecteur de
    // machine serait lui promettre un choix sans effet : son matériau est un
    // fichier, pas des notes. Elle affiche donc ce fichier -- ou le fait qu'elle
    // n'en a pas encore, ce qui est exactement ce qu'on a besoin de savoir avant
    // d'appuyer sur Rec.
    if (audio_) {
        addAndMakeVisible(audioSourceLabel_);
        audioSourceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        audioSourceLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
        refreshAudioSource();
    } else if (track_.isFolder()) {
        // D19.4 : UN DOSSIER NE JOUE RIEN. Ni instrument, ni fichier, ni bus :
        // c'est un rangement. Lui laisser un sélecteur de machine serait lui
        // promettre un choix sans effet, comme pour un groupe.
        addAndMakeVisible(audioSourceLabel_);
        audioSourceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        audioSourceLabel_.setColour(juce::Label::textColourId, Palette::accentAmber);
        // son texte : `poserTextes()` (D94)
    } else if (track_.publishesInstrumentOutput()) {
        // D18.7b : UNE PISTE QUI PUBLIE LA SORTIE D'UNE AUTRE N'A PAS
        // D'INSTRUMENT À ELLE, et le graphe ignore délibérément celui qu'on lui
        // mettrait. Lui laisser le sélecteur « (Aucun) » était donc offrir un
        // réglage sans effet -- la pire espèce, celle qui se règle et ne fait
        // rien. Elle dit ce qu'elle porte, et d'où ça vient.
        addAndMakeVisible(audioSourceLabel_);
        audioSourceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        audioSourceLabel_.setColour(juce::Label::textColourId, Palette::accentTeal);
    } else if (track_.kind == Track::Kind::Group) {
        // UN BUS DE GROUPE N'A PAS D'INSTRUMENT NON PLUS : il additionne les
        // pistes routées vers lui. Un sélecteur « (Aucun) » lui promettait
        // un choix sans effet ; il dit ce qu'il est.
        addAndMakeVisible(audioSourceLabel_);
        audioSourceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        audioSourceLabel_.setColour(juce::Label::textColourId, Palette::accentAmber);
    } else {
        addAndMakeVisible(instrumentBox_);
        instrumentBox_.addItem(vsm::app::ui::tr("(Aucun)"), 1);   // D83
        auto instruments = availableInstruments();
        int selectedId = 1;
        for (int i = 0; i < static_cast<int>(instruments.size()); ++i) {
            const auto& [pluginId, displayName] = instruments[static_cast<size_t>(i)];
            // D103 : le nom de la machine dans la langue de l'interface ; la clé
            // est le nom enregistré, en français.
            instrumentBox_.addItem(vsm::app::ui::tr(juce::String::fromUTF8(displayName.c_str())),
                                   i + 2); // id JUCE 1-based, 1 = "(Aucun)"
            if (pluginId == track_.instrumentId) selectedId = i + 2;
        }
        instrumentBox_.setSelectedId(selectedId, juce::dontSendNotification);
        instruments_ = instruments;   // D103 : pour reposer les noms à la bascule
        instrumentBox_.onChange = [this, instruments] {
            int idx = instrumentBox_.getSelectedItemIndex();
            std::string pluginId = (idx <= 0 || idx > static_cast<int>(instruments.size()))
                                        ? ""
                                        : instruments[static_cast<size_t>(idx - 1)].first;
            if (pluginId == track_.instrumentId) return;
            debutEdition(u8"Machine de la piste");
            track_.instrumentId = pluginId;
            if (onInstrumentChanged) onInstrumentChanged(index_, pluginId);
        };
    }

    if (track_.isFolder()) {
        addAndMakeVisible(folderButton_);
        auto rafraichir = [this] {
            folderButton_.setButtonText(track_.folded ? juce::String::fromUTF8(u8"▸")
                                                       : juce::String::fromUTF8(u8"▾"));
        };
        rafraichir();
        folderButton_.onClick = [this, rafraichir] {
            // LE REPLI EST DANS LE FICHIER (`folded`, format version 2), donc
            // c'est de l'état de MORCEAU et non d'écran : il se photographie.
            // Qu'il s'annule aussi est la conséquence, assumée -- un Ctrl+Z qui
            // redéploie un dossier surprend une fois ; un rangement perdu à la
            // reprise après coupure coûte plus cher.
            debutEdition(u8"Replier le dossier");
            track_.folded = !track_.folded;
            rafraichir();
            if (onChanged) onChanged();
        };
    }

    addAndMakeVisible(muteButton_);
    addAndMakeVisible(soloButton_);
    addAndMakeVisible(armButton_);
    muteButton_.setClickingTogglesState(true);
    soloButton_.setClickingTogglesState(true);
    armButton_.setClickingTogglesState(true);
    muteButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);
    soloButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    armButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);

    // LE MÊME BOUTON QUE DANS LA TRANCHE DU MÉLANGEUR, ET DÉSORMAIS LE MÊME
    // COMPORTEMENT (D36.1). Le muet, le solo, le volume et le panoramique
    // existent aux deux endroits ; jusqu'ici ils s'annulaient dans l'un et pas
    // dans l'autre, et rien à l'écran ne le laissait deviner.
    muteButton_.onClick = [this] { if (onGesteMuet) onGesteMuet(index_); };
    soloButton_.onClick = [this] { if (onGesteSolo) onGesteSolo(index_); };
    // ARMEMENT (D3.3). `Track::armed` était écrit ici et LU PAR PERSONNE : on
    // pouvait armer une piste, et rien n'arrivait -- d'où un bouton désactivé
    // qui l'avouait. Il agit maintenant sur deux choses à la fois, et c'est
    // voulu : la piste armée reçoit les notes du clavier À L'ÉCOUTE, et les
    // reçoit aussi PAR ÉCRIT pendant une prise. Jouer sur une piste et
    // enregistrer sur une autre n'aurait aucun sens.
    armButton_.setToggleState(track_.armed, juce::dontSendNotification);
    armButton_.onClick = [this] {
        track_.armed = armButton_.getToggleState();
        if (onArmChanged) onArmChanged();
    };

    // OÙ VA CETTE PISTE (D4.2). Un groupe, lui, va toujours au master : les
    // groupes imbriqués demanderaient un ordre topologique pour un besoin que
    // rien n'a exprimé, et proposer le choix laisserait croire le contraire.
    if (track_.kind != Track::Kind::Group) {
        addAndMakeVisible(outputBox_);
        outputBox_.addItem("-> Master", 1);
        int selection = 1;
        for (size_t i = 0; i < groupes.size(); ++i) {
            outputBox_.addItem("-> " + juce::String(groupes[i].second), static_cast<int>(i) + 2);
            if (groupes[i].first == track_.outputGroup) selection = static_cast<int>(i) + 2;
        }
        outputBox_.setSelectedId(selection, juce::dontSendNotification);
        outputBox_.onChange = [this, groupes] {
            const int choix = outputBox_.getSelectedItemIndex();
            debutEdition(u8"Sortie de la piste");
            track_.outputGroup = (choix <= 0 || choix > static_cast<int>(groupes.size()))
                                     ? -1
                                     : groupes[static_cast<size_t>(choix - 1)].first;
            if (onOutputChanged) onOutputChanged();
        };
    }

    addAndMakeVisible(volumeSlider_);
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setRange(0.0, 1.5, 0.001);
    volumeSlider_.setValue(track_.volume, juce::dontSendNotification);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider_.onDragStart = [this] { glisseEnCours_ = true; debutEdition("Volume"); };
    volumeSlider_.onDragEnd = [this] { glisseEnCours_ = false; };
    volumeSlider_.onValueChange = [this] {
        // UN PAS PAR GESTE, PAS UN PAR PIXEL. Le glissé a déjà signalé à son
        // départ ; ce qui reste ici est la molette, le clavier et la saisie,
        // que `onDragStart` ne voit jamais -- et qui, sans cette ligne,
        // resteraient inannulables tout en ayant l'air couvertes.
        if (!glisseEnCours_) debutEdition("Volume");
        track_.volume = static_cast<float>(volumeSlider_.getValue());
        if (onChanged) onChanged();
    };

    addAndMakeVisible(panSlider_);
    panSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(track_.pan, juce::dontSendNotification);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider_.onDragStart = [this] { glisseEnCours_ = true; debutEdition(u8"Panoramique"); };
    panSlider_.onDragEnd = [this] { glisseEnCours_ = false; };
    panSlider_.onValueChange = [this] {
        if (!glisseEnCours_) debutEdition(u8"Panoramique");
        track_.pan = static_cast<float>(panSlider_.getValue());
        if (onChanged) onChanged();
    };

    setInterceptsMouseClicks(true, true);
    poserTextes();   // D94
}

void TrackRowComponent::poserTextes() {
    using vsm::app::ui::tr;
    if (!audio_)
        channelLabel_.setTooltip(tr(u8"Canal MIDI (1 à 16) — double-clic pour le changer"));
    if (audio_)
        refreshAudioSource();
    else if (track_.isFolder())
        audioSourceLabel_.setText(tr(u8"dossier (ne joue rien)"), juce::dontSendNotification);
    else if (track_.publishesInstrumentOutput())
        audioSourceLabel_.setText((sourceName_.isEmpty() ? tr(u8"sortie n° %1") : tr(u8"sortie n° %1 de %2"))
                                      .replace("%1", juce::String(track_.outputIndex))
                                      .replace("%2", sourceName_),
                                  juce::dontSendNotification);
    else if (track_.kind == Track::Kind::Group)
        audioSourceLabel_.setText(tr(u8"bus de groupe"), juce::dontSendNotification);
    if (track_.isFolder())
        folderButton_.setTooltip(tr(u8"Replier ou déployer le dossier. N'affecte que la VUE : "
                                    u8"les pistes rangées dedans continuent de jouer."));
    armButton_.setTooltip(
        audio_ ? tr("Armer la piste : la prochaine prise ecrit l'entree audio dans un "
                    "fichier du dossier du projet. Une seule piste audio a la fois.")
               : tr("Armer la piste : elle recoit alors le clavier MIDI, "
                    "a l'ecoute comme a l'enregistrement."));
    if (track_.kind != Track::Kind::Group)
        outputBox_.setTooltip(tr("Ou va cette piste : le master, ou un groupe."));
}

void TrackRowComponent::refreshMix() {
    volumeSlider_.setValue(track_.volume, juce::dontSendNotification);
    panSlider_.setValue(track_.pan, juce::dontSendNotification);
}

bool TrackRowComponent::choisirMachine(const juce::String& pluginId) {
    const auto instruments = availableInstruments();
    for (int i = 0; i < static_cast<int>(instruments.size()); ++i)
        if (juce::String(instruments[static_cast<size_t>(i)].first) == pluginId) {
            instrumentBox_.setSelectedId(i + 2, juce::sendNotificationSync);
            return true;
        }
    return false;
}

void TrackRowComponent::renommer(const juce::String& nom) {
    nameLabel_.setText(nom, juce::sendNotificationSync);
}

void TrackRowComponent::reglerVolume(float valeur) {
    volumeSlider_.setValue(valeur, juce::sendNotificationSync);
}

void TrackRowComponent::refreshMuteSolo() {
    // `dontSendNotification` : on REFLÈTE la piste, on ne la modifie pas. Avec
    // une notification, rafraîchir la liste depuis le mélangeur rappellerait
    // le mélangeur, et les deux panneaux se renverraient la balle.
    muteButton_.setToggleState(track_.muted, juce::dontSendNotification);
    soloButton_.setToggleState(track_.solo, juce::dontSendNotification);
}

void TrackRowComponent::poserMuet(bool muet) {
    track_.muted = muet;
    muteButton_.setToggleState(muet, juce::dontSendNotification);
}

void TrackRowComponent::poserSolo(bool solo) {
    track_.solo = solo;
    soloButton_.setToggleState(solo, juce::dontSendNotification);
}

void TrackRowComponent::debutEdition(const juce::String& libelle) {
    if (onEditStarted) onEditStarted(libelle);
}

void TrackRowComponent::retraduire() {
    // D83 : ce que la ligne écrit une fois -- le nom de repli « Piste N » et
    // l'entrée « (Aucun) » de sa machine -- suit la bascule de langue. La
    // sélection se lit AVANT de renommer : JUCE rend 0 pour une entrée choisie
    // dont le texte ne correspond plus (D78).
    refreshName();
    poserTextes();   // D94 : infobulles et mentions
    if (instrumentBox_.getNumItems() > 0) {
        // D103 : les noms des machines aussi, et la sélection relue AVANT.
        const int choisie = instrumentBox_.getSelectedId();
        instrumentBox_.changeItemText(1, vsm::app::ui::tr("(Aucun)"));
        for (size_t i = 0; i < instruments_.size(); ++i)
            instrumentBox_.changeItemText(static_cast<int>(i) + 2, vsm::app::ui::tr(
                juce::String::fromUTF8(instruments_[i].second.c_str())));
        instrumentBox_.setSelectedId(choisie, juce::dontSendNotification);
    }
}

void TrackRowComponent::refreshName() {
    nameLabel_.setText(track_.name.empty() ? vsm::app::ui::tr(u8"Piste %1").replace("%1", juce::String(static_cast<int>(index_) + 1))
                                         : juce::String::fromUTF8(track_.name.c_str()),
                       juce::dontSendNotification);
}

void TrackRowComponent::refreshAudioSource() {
    if (!audio_) return;
    const juce::String chemin(track_.audio.path);
    // D51 : LA FRÉQUENCE DU FICHIER QUAND ELLE N'EST PAS CELLE DE LA SESSION.
    // Le chargeur la mesure depuis D2 (`AudioTrackLoadResult::resampled`) et
    // son en-tête dit que « l'interface doit pouvoir l'écrire » ; aucun
    // composant ne la lisait. Un fichier rééchantillonné n'est plus celui
    // qu'on a posé : le dire à côté de son nom, en permanence, est le seul
    // endroit où l'on regarde en se demandant ce que joue cette piste.
    const auto khz = [](double hz) {
        return juce::String(hz / 1000.0, hz >= 100000.0 || std::fmod(hz, 1000.0) == 0.0 ? 0 : 1);
    };
    const bool converti = fileSampleRate_ > 0.0 && sessionSampleRate_ > 0.0
                       && std::abs(fileSampleRate_ - sessionSampleRate_) > 0.5;
    juce::String mention;
    if (converti)
        mention += juce::String(u8" · ") + khz(fileSampleRate_) + juce::String(u8" → ")
                 + khz(sessionSampleRate_) + " kHz";
    // LA DIFFUSION DISQUE AUSSI (D8.2) : l'autre champ que le chargeur
    // remplissait et que personne ne lisait. Elle se dit quand elle a lieu et
    // se tait sinon -- un « résident » écrit sur chaque ligne deviendrait un
    // meuble, et c'est le cas rare qu'il faut voir.
    using vsm::app::ui::tr;
    if (audioStreamed_) mention += tr(u8" · disque");
    audioSourceLabel_.setText(
        chemin.isEmpty() ? tr(u8"(aucun fichier — armer et enregistrer)")
                         : chemin.fromLastOccurrenceOf("/", false, false) + mention,
        juce::dontSendNotification);
    audioSourceLabel_.setTooltip(
        chemin.isEmpty()
            ? tr(u8"Cette piste audio n'a pas encore de matériau.")
            : converti ? chemin + "\n\n"
                             + tr(u8"Ce fichier est enregistré à %1 Hz et la session tourne à %2 Hz : "
                                  u8"il est rééchantillonné à la lecture comme à l'export. Ce que vous "
                                  u8"entendez n'est donc pas exactement le fichier posé.")
                                   .replace("%1", juce::String(fileSampleRate_, 0))
                                   .replace("%2", juce::String(sessionSampleRate_, 0))
                       : chemin);
    if (!chemin.isEmpty() && audioStreamed_)
        audioSourceLabel_.setTooltip(
            audioSourceLabel_.getTooltip() + "\n\n"
            + tr(u8"Ce matériau est DIFFUSÉ depuis le disque (au-delà de vingt secondes) et non "
                 u8"tenu en mémoire : %1 Mo résidents. Le fichier doit rester accessible pendant "
                 u8"toute la séance.")
                  .replace("%1", juce::String(static_cast<double>(audioResidentBytes_) / (1024.0 * 1024.0), 1)));
}

void TrackRowComponent::setAudioSourceRate(double fileRate, double sessionRate, bool streamed,
                                            size_t residentBytes) {
    if (!audio_) return;
    fileSampleRate_ = fileRate;
    sessionSampleRate_ = sessionRate;
    audioStreamed_ = streamed;
    audioResidentBytes_ = residentBytes;
    refreshAudioSource();
}

void TrackRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.setColour(selected_ ? Palette::panelRaised : Palette::panel);
    g.fillRect(bounds);

    // D38.1 : LA SÉLECTION SE VOIT, ET IL A FALLU LA MESURER POUR S'EN
    // APERCEVOIR. Elle ne tenait qu'à l'écart entre `panel` (#1f1f24) et
    // `panelRaised` (#26262c) : sept unités de gris par canal. Trois lignes
    // choisies et trois lignes ordinaires donnaient une capture dont la
    // différence était indiscernable -- et cette sélection commande désormais
    // la SUPPRESSION. Une sélection qu'on ne voit pas est exactement le
    // « faire quelque chose d'invisible » contre quoi D38.4 a été écrite.
    if (selected_) {
        g.setColour(Palette::accentAmber.withAlpha(0.85f));
        g.drawRect(bounds, 2);
    }

    // Bandeau de couleur de piste (à gauche), comme sur une console hardware
    g.setColour(juce::Colour(track_.colorRgba));
    g.fillRect(bounds.removeFromLeft(6));

    // LE CADENAS (D16.5), écrit en toutes lettres dans le coin haut droit
    // plutôt qu'en pictogramme : la piste continue de sonner, seul son
    // MONTAGE est refusé, et un dessin de cadenas laisserait croire qu'elle
    // est coupée. Même choix que le « gelé » de l'arrangement.
    if (track_.locked) {
        g.setColour(Palette::accentAmber);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(vsm::app::ui::tr(u8"verrouillée"), bounds.removeFromTop(18).reduced(6, 2),
                    juce::Justification::centredRight);
    }

    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(getHeight() - 1), static_cast<float>(getWidth()),
               static_cast<float>(getHeight() - 1), 1.0f);
}

// D30.2 : UNE PISTE DÉSACTIVÉE LE DIT, en toutes lettres comme le verrou, et
// sa ligne est GRISÉE -- elle n'est plus dans le morceau, et rien ne doit
// laisser croire qu'un de ses réglages agit encore. C'est le seul des états de
// cette phase qui RETIRE quelque chose : ne pas le montrer ferait chercher
// pendant dix minutes pourquoi une piste ne sonne pas.
//
// DANS `paintOverChildren` ET NON DANS `paint`, et la première capture l'a
// prouvé : le voile posé dans `paint` est recouvert par le nom, le sélecteur
// de machine et les boutons, qui se dessinent APRÈS leur parent. La ligne
// paraissait à peine plus sombre, c'est-à-dire pas désactivée du tout.
void TrackRowComponent::paintOverChildren(juce::Graphics& g) {
    if (!track_.disabled) return;
    g.setColour(Palette::panel.withAlpha(0.62f));
    g.fillRect(getLocalBounds());
    g.setColour(Palette::accentRed);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(vsm::app::ui::tr(u8"désactivée"), getLocalBounds().removeFromTop(18).reduced(8, 2),
                juce::Justification::centredRight);
}

void TrackRowComponent::resized() {
    auto area = getLocalBounds().reduced(12, 8);
    area.removeFromLeft(6); // laisse la place au bandeau de couleur peint dans paint()

    auto topRow = area.removeFromTop(22);
    // D19.4 : le chevron du dossier prend le début de la ligne du nom.
    if (track_.isFolder()) {
        folderButton_.setBounds(topRow.removeFromLeft(24));
        topRow.removeFromLeft(4);
        nameLabel_.setBounds(topRow.removeFromLeft(112));
    } else {
        nameLabel_.setBounds(topRow.removeFromLeft(140));
    }
    topRow.removeFromLeft(8);
    channelLabel_.setBounds(topRow.removeFromLeft(50));

    area.removeFromTop(4);
    auto secondRow = area.removeFromTop(24);
    // D18.7b : UNE PISTE QUI PUBLIE NE S'ARME PAS -- elle n'a pas d'instrument
    // à qui livrer le clavier, et un bouton d'armement y serait un troisième
    // réglage sans effet. La place qu'il libère va au texte, parce qu'entre
    // « ça tient dans la case » et « ça se lit », c'est la lisibilité qui prime.
    const bool publie = track_.publishesInstrumentOutput();
    // D19.4 : UN DOSSIER NE TOUCHE À AUCUN SIGNAL, donc il n'a ni fader, ni
    // panoramique, ni muet, ni solo, ni armement, ni sortie. Cubase donne un
    // muet à ses dossiers ; ce serait ici un bus déguisé, et le rangement
    // cesserait d'être gratuit — on ne pourrait plus replier huit micros sans
    // se demander si l'on vient de changer le mélange.
    const bool dossier = track_.isFolder();
    armButton_.setVisible(!publie && !dossier);
    muteButton_.setVisible(!dossier);
    soloButton_.setVisible(!dossier);
    volumeSlider_.setVisible(!dossier);
    panSlider_.setVisible(!dossier);
    outputBox_.setVisible(!dossier && track_.kind != Track::Kind::Group);

    const int largeurTexte = (publie || dossier) ? 170 + 4 + 28 : 170;
    if (audio_ || track_.kind == Track::Kind::Group || publie || dossier)
        audioSourceLabel_.setBounds(secondRow.removeFromLeft(largeurTexte));
    else instrumentBox_.setBounds(secondRow.removeFromLeft(largeurTexte));
    if (!dossier) {
        secondRow.removeFromLeft(8);
        muteButton_.setBounds(secondRow.removeFromLeft(28));
        secondRow.removeFromLeft(4);
        soloButton_.setBounds(secondRow.removeFromLeft(28));
        if (!publie) {
            secondRow.removeFromLeft(4);
            armButton_.setBounds(secondRow.removeFromLeft(28));
        }
    }

    area.removeFromTop(6);
    auto thirdRow = area.removeFromTop(20);
    if (dossier) return;
    volumeSlider_.setBounds(thirdRow.removeFromLeft(170));
    thirdRow.removeFromLeft(8);
    panSlider_.setBounds(thirdRow.removeFromLeft(90));
    thirdRow.removeFromLeft(8);
    if (track_.kind != Track::Kind::Group) outputBox_.setBounds(thirdRow.removeFromLeft(130));
}

// ---------------------------------------------------------------------------
// TrackListComponent
// ---------------------------------------------------------------------------

TrackListComponent::TrackListComponent() {
    // D19.2 : LE FILTRE. La parité pousse le nombre de pistes vers le haut —
    // D18.7b en ajoute cinq pour une seule boîte à rythmes, D19.3 une par
    // pièce de batterie — et faire défiler pour retrouver « Caisse claire »
    // n'est plus tenable.
    addAndMakeVisible(filterBox_);
    filterBox_.setTextToShowWhenEmpty(vsm::app::ui::tr(u8"Filtrer les pistes..."), Palette::textSecondary);
    filterBox_.setFont(juce::Font(juce::FontOptions(13.0f)));
    filterBox_.onTextChange = [this] { resized(); repaint(); };

    addAndMakeVisible(emptyLabel_);
    emptyLabel_.setJustificationType(juce::Justification::centredTop);
    emptyLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    emptyLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    emptyLabel_.setVisible(false);
    emptyLabel_.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);

    // Barre d'outils du Track Editor : ajouter / supprimer une piste. Ces
    // deux boutons comblent le manque d'ergonomie identifié -- le modèle et
    // le moteur sont multi-pistes depuis les Phases 1-2, il ne manquait que
    // l'affordance UI pour créer/retirer une piste sans passer par un import.
    addAndMakeVisible(addButton_);
    addAndMakeVisible(removeButton_);
    addButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    removeButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);
    addButton_.onClick = [this] { if (onAddTrack) onAddTrack(); };
    removeButton_.onClick = [this] {
        if (project_ != nullptr && !project_->tracks.empty() && onRemoveTrack)
            onRemoveTrack(selectedIndex_);
    };
    // D73 : la langue est déjà posée au démarrage ; les libellés écrits
    // dans les initialiseurs de membres, eux, sont en français. On les
    // repose ici plutôt que de les dupliquer.
    retraduire();
}

void TrackListComponent::loadProject(Project& project) {
    project_ = &project;
    rows_.clear();
    // LA PISTE CHOISIE SURVIT À LA RECONSTRUCTION DE LA LISTE : cette
    // fonction est rappelée à chaque republication du projet, et remettre la
    // sélection à zéro faisait surligner la première piste pendant que le
    // piano roll et le rack en montraient une autre.
    if (selectedIndex_ >= project.tracks.size()) selectedIndex_ = 0;
    // D38.1 : LA SÉLECTION AUSSI SURVIT, MAIS PURGÉE. Une piste supprimée
    // laisserait sinon son numéro dans l'ensemble, et le geste suivant tairait
    // sa VOISINE -- un index survivant à ce qu'il désigne est la façon la plus
    // silencieuse de se tromper de piste.
    {
        std::set<size_t> propre;
        for (size_t i : selection_) if (i < project.tracks.size()) propre.insert(i);
        propre.insert(selectedIndex_);
        selection_ = std::move(propre);
        if (ancreSelection_ >= project.tracks.size()) ancreSelection_ = selectedIndex_;
    }

    // La liste des groupes, calculée UNE fois : chaque ligne la reçoit pour
    // remplir son sélecteur de sortie.
    std::vector<std::pair<int, std::string>> groupes;
    for (size_t i = 0; i < project_->tracks.size(); ++i)
        if (project_->tracks[i].kind == Track::Kind::Group)
            groupes.emplace_back(static_cast<int>(i),
                                  project_->tracks[i].name.empty()
                                      ? "Groupe " + std::to_string(i + 1)
                                      : project_->tracks[i].name);

    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        juce::String nomSource;
        const int source = project_->tracks[i].outputSourceTrack;
        if (source >= 0 && static_cast<size_t>(source) < project_->tracks.size())
            nomSource = juce::String(project_->tracks[static_cast<size_t>(source)].name);
        auto* row = rows_.add(new TrackRowComponent(project_->tracks[i], i, groupes, nomSource));
        rowContainer_.addAndMakeVisible(row);
        row->onSelected = [this](size_t idx) { selectTrackIndex(idx); };
        row->onSelectedWithMods = [this](size_t idx, juce::ModifierKeys mods) {
            cliqueSurLaLigne(idx, mods);
        };
        row->onEditStarted = [this](const juce::String& libelle) {
            if (onEditStarted) onEditStarted(libelle);
        };
        row->onChanged = [this] { if (onTracksChanged) onTracksChanged(); };
        row->onRenamed = [this] { if (onRenamed) onRenamed(); };
        row->onGesteMuet = [this](size_t i) { basculerMuet(i); };
        row->onGesteSolo = [this](size_t i) { basculerSolo(i); };
        row->onArmChanged = [this] { if (onArmChanged) onArmChanged(); };
        row->onOutputChanged = [this] { if (onOutputChanged) onOutputChanged(); };
        row->onInstrumentChanged = [this](size_t idx, const std::string& pluginId) {
            if (onInstrumentChanged) onInstrumentChanged(idx, pluginId);
        };
    }
    if (!rows_.isEmpty()) rafraichirDessinDeLaSelection();
    removeButton_.setEnabled(!rows_.isEmpty());

    resized();
    faireVoirLaPiste(selectedIndex_);
}

void TrackListComponent::renommer(size_t index, const juce::String& nom) {
    if (index < static_cast<size_t>(rows_.size())) rows_[static_cast<int>(index)]->renommer(nom);
}

bool TrackListComponent::choisirMachine(size_t index, const juce::String& pluginId) {
    if (index >= static_cast<size_t>(rows_.size())) return false;
    return rows_[static_cast<int>(index)]->choisirMachine(pluginId);
}

void TrackListComponent::reglerVolume(size_t index, float valeur) {
    if (index < static_cast<size_t>(rows_.size())) rows_[static_cast<int>(index)]->reglerVolume(valeur);
}

void TrackListComponent::refreshMuteSolo() {
    for (auto* row : rows_) row->refreshMuteSolo();
}

void TrackListComponent::refreshMix() {
    for (auto* row : rows_) row->refreshMix();
}

void TrackListComponent::refreshFromTracks() {
    for (auto* row : rows_) { row->refreshName(); row->refreshMix(); row->refreshMuteSolo(); }
}

void TrackListComponent::armer(size_t index) {
    // D110 : ce que fait la souris sur le bouton R -- ni plus, ni moins.
    if (index < static_cast<size_t>(rows_.size())) rows_[static_cast<int>(index)]->armerPourCapture();
}

void TrackListComponent::basculerMuet(size_t index) {
    if (index >= static_cast<size_t>(rows_.size()) || project_ == nullptr) return;
    const std::set<size_t> cible = selectionPourUnGesteSur(index);
    const bool etat = !project_->tracks[index].muted;
    // UN SEUL PAS POUR LE LOT (D38.2). Un pas par piste s'annulerait piste par
    // piste : taire six micros de batterie demanderait six Ctrl+Z pour revenir,
    // ce qui n'est pas annuler le geste, c'est le défaire à la main.
    if (onEditStarted) onEditStarted(u8"Muet");
    for (size_t i : cible)
        if (i < static_cast<size_t>(rows_.size())) rows_[static_cast<int>(i)]->poserMuet(etat);
    if (onTracksChanged) onTracksChanged();
}

void TrackListComponent::basculerSolo(size_t index) {
    if (index >= static_cast<size_t>(rows_.size()) || project_ == nullptr) return;
    const std::set<size_t> cible = selectionPourUnGesteSur(index);
    const bool etat = !project_->tracks[index].solo;
    if (onEditStarted) onEditStarted("Solo");
    for (size_t i : cible)
        if (i < static_cast<size_t>(rows_.size())) rows_[static_cast<int>(i)]->poserSolo(etat);
    if (onTracksChanged) onTracksChanged();
}

void TrackListComponent::faireVoirLaPiste(size_t idx) {
    // LA PISTE CHOISIE SE VOIT : une piste choisie ailleurs (l'arrangement,
    // le mixeur, un autoportrait) peut être hors de la fenêtre de la liste --
    // un projet en parité en a onze --, et rien ne disait laquelle était
    // sélectionnée. On fait défiler juste assez pour la montrer entière.
    if (idx >= static_cast<size_t>(rows_.size())) return;
    // Avant la première mise en page (un autoportrait choisit sa piste au
    // démarrage), la fenêtre n'a pas de hauteur : on retient la demande et
    // resized() la sert.
    if (viewport_.getViewHeight() <= 0) { aMontrer_ = static_cast<int>(idx); return; }
    aMontrer_ = -1;
    const auto bornes = rows_[static_cast<int>(idx)]->getBounds();

    const int haut = viewport_.getViewPositionY();
    const int bas = haut + viewport_.getViewHeight();
    if (bornes.getY() < haut)
        viewport_.setViewPosition(0, bornes.getY());
    else if (bornes.getBottom() > bas)
        viewport_.setViewPosition(0, std::max(0, bornes.getBottom() - viewport_.getViewHeight()));
}

void TrackListComponent::refreshTrackRow(size_t idx) {
    if (idx >= static_cast<size_t>(rows_.size())) return;
    rows_[static_cast<int>(idx)]->refreshAudioSource();
    rows_[static_cast<int>(idx)]->refreshName();
}

void TrackListComponent::setAudioSourceRate(size_t idx, double fileRate, double sessionRate,
                                            bool streamed, size_t residentBytes) {
    if (idx >= static_cast<size_t>(rows_.size())) return;
    rows_[static_cast<int>(idx)]->setAudioSourceRate(fileRate, sessionRate, streamed, residentBytes);
}

void TrackListComponent::selectTrackIndex(size_t idx) {
    if (idx >= static_cast<size_t>(rows_.size())) return;
    selectedIndex_ = idx;
    ancreSelection_ = idx;      // D38.1 : un clic simple repose l'ancre
    selection_ = { idx };
    rafraichirDessinDeLaSelection();
    faireVoirLaPiste(idx);
    if (onTrackSelected) onTrackSelected(idx);
}

void TrackListComponent::rafraichirDessinDeLaSelection() {
    for (int r = 0; r < rows_.size(); ++r)
        rows_[r]->setSelected(selection_.count(static_cast<size_t>(r)) > 0);
    // UN SEUL ENDROIT ANNONCE, celui qui redessine : toutes les façons de
    // changer la sélection passent par ici, et lier l'annonce au dessin est ce
    // qui garantit qu'aucune ne l'oublie -- la leçon de D36, appliquée d'avance.
    if (onSelectionChanged) onSelectionChanged();
}

void TrackListComponent::setSelectedTracks(std::set<size_t> tracks, size_t active) {
    const size_t n = static_cast<size_t>(rows_.size());
    if (active >= n) return;
    // AUCUN INDEX HORS BORNES N'ENTRE : une piste supprimée laisserait sinon
    // son numéro dans la sélection, et le geste suivant tairait sa voisine.
    std::set<size_t> propre;
    for (size_t i : tracks) if (i < n) propre.insert(i);
    propre.insert(active);      // la piste active est TOUJOURS de la sélection
    selection_ = std::move(propre);
    selectedIndex_ = active;
    rafraichirDessinDeLaSelection();
    faireVoirLaPiste(active);
    if (onTrackSelected) onTrackSelected(active);
}

void TrackListComponent::etendreSelection(int delta) {
    const size_t n = static_cast<size_t>(rows_.size());
    if (n == 0 || project_ == nullptr) return;
    size_t i = selectedIndex_;
    // La VOISINE VISIBLE, comme `selectNeighbourTrack` : une piste masquée ne
    // s'ajoute pas à un lot qu'on ne peut pas regarder.
    while (true) {
        if (delta < 0 && i == 0) return;
        if (delta > 0 && i + 1 >= n) return;
        i = static_cast<size_t>(static_cast<long>(i) + delta);
        if (i >= project_->tracks.size() || !project_->tracks[i].hidden) break;
    }
    const size_t de = std::min(ancreSelection_, i);
    const size_t a  = std::max(ancreSelection_, i);
    std::set<size_t> plage;
    for (size_t k = de; k <= a && k < n; ++k)
        if (k >= project_->tracks.size() || !project_->tracks[k].hidden) plage.insert(k);
    selection_ = std::move(plage);
    selectedIndex_ = i;
    selection_.insert(i);
    rafraichirDessinDeLaSelection();
    faireVoirLaPiste(i);
    if (onTrackSelected) onTrackSelected(i);
}

void TrackListComponent::choisirToutesLesPistes() {
    const size_t n = static_cast<size_t>(rows_.size());
    if (n == 0 || project_ == nullptr) return;
    std::set<size_t> toutes;
    for (size_t i = 0; i < n; ++i)
        if (i >= project_->tracks.size() || !project_->tracks[i].hidden) toutes.insert(i);
    if (toutes.empty()) return;
    selection_ = std::move(toutes);
    selection_.insert(selectedIndex_);
    rafraichirDessinDeLaSelection();
}

const std::set<size_t>& TrackListComponent::selectionPourUnGesteSur(size_t index) {
    // D38.4 : LA RÈGLE DE CUBASE, ET LA RAISON DE LA PRÉFÉRER. Six pistes
    // choisies, on clique le M d'une septième : ou bien on tait la septième
    // seule (et on la choisit), ou bien on l'ajoute au lot. Le second choix
    // fait agir sur six pistes que l'on ne regarde pas -- le geste porte alors
    // sur ce qu'on a oublié d'avoir sélectionné, ce qui est exactement la
    // surprise qu'une sélection est censée éviter.
    if (selection_.count(index) == 0) selectTrackIndex(index);
    return selection_;
}

void TrackListComponent::cliqueSurLaLigne(size_t index, juce::ModifierKeys mods) {
    const size_t n = static_cast<size_t>(rows_.size());
    if (index >= n) return;
    if (mods.isShiftDown()) {
        // ÉTENDRE DEPUIS L'ANCRE, qui est le dernier clic SIMPLE : étendre
        // depuis la piste active ferait grandir la sélection à chaque Maj+clic
        // au lieu de la redessiner.
        const size_t de = std::min(ancreSelection_, index);
        const size_t a  = std::max(ancreSelection_, index);
        std::set<size_t> plage;
        for (size_t i = de; i <= a && i < n; ++i) plage.insert(i);
        selection_ = std::move(plage);
        selectedIndex_ = index;
        rafraichirDessinDeLaSelection();
        faireVoirLaPiste(index);
        if (onTrackSelected) onTrackSelected(index);
        return;
    }
    if (mods.isCommandDown()) {
        if (selection_.count(index) > 0 && selection_.size() > 1) {
            // ON PEUT EN RETIRER, MAIS JAMAIS LA DERNIÈRE : une liste sans
            // piste active n'a rien à montrer au piano roll ni au rack.
            selection_.erase(index);
            if (selectedIndex_ == index) selectedIndex_ = *selection_.begin();
        } else {
            selection_.insert(index);
            selectedIndex_ = index;
        }
        ancreSelection_ = index;
        rafraichirDessinDeLaSelection();
        faireVoirLaPiste(selectedIndex_);
        if (onTrackSelected) onTrackSelected(selectedIndex_);
        return;
    }
    selectTrackIndex(index);
}

bool TrackListComponent::masqueeParLeFiltre(size_t index) const {
    const juce::String motif = filterBox_.getText().trim();
    if (motif.isEmpty()) return false;   // vidé, tout revient
    if (project_ == nullptr || index >= project_->tracks.size()) return false;
    // SANS TENIR COMPTE DE LA CASSE : on tape « caisse » pour trouver
    // « Caisse claire », et personne ne devrait avoir à deviner la majuscule.
    return !juce::String(project_->tracks[index].name).containsIgnoreCase(motif);
}

void TrackListComponent::resized() {
    auto area = getLocalBounds();

    auto toolbar = area.removeFromTop(kToolbarHeight).reduced(8, 6);
    removeButton_.setBounds(toolbar.removeFromRight(96));
    toolbar.removeFromRight(6);
    addButton_.setBounds(toolbar);

    // D19.2 : SA PROPRE LIGNE plutôt que serré entre deux boutons. Entre « ça
    // tient dans la case » et « ça se lit », c'est la lisibilité qui prime.
    filterBox_.setBounds(area.removeFromTop(kFilterHeight).reduced(8, 3));

    viewport_.setBounds(area);
    // D17.4 : les pistes masquées ne comptent pas dans la hauteur totale, sans
    // quoi la liste garderait un blanc à leur place.
    int visibles = 0;
    for (int i = 0; i < rows_.size(); ++i)
        if (project_ == nullptr || static_cast<size_t>(i) >= project_->tracks.size()
            || (!project_->tracks[static_cast<size_t>(i)].hidden
                && !masqueeParLeFiltre(static_cast<size_t>(i))
                && !vsm::sequencer::hiddenByCollapsedFolder(*project_, static_cast<size_t>(i))))
            ++visibles;
    int totalHeight = visibles * kRowHeight;
    // LE DÉFILEMENT SURVIT À LA MISE EN PAGE. `setBounds(0, 0, …)` remettait le
    // conteneur en haut à chaque redimensionnement -- et chaque republication
    // du projet passe par ici : la liste sautait en haut pendant qu'on
    // travaillait la onzième piste. On ne change que la taille, et l'on
    // rend au viewport la position qu'il avait.
    const auto position = viewport_.getViewPosition();
    rowContainer_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(), totalHeight);
    viewport_.setViewPosition(position);

    {
        // D17.4 : UNE PISTE MASQUÉE OCCUPE UNE HAUTEUR NULLE, et les rangées
        // restent indexées comme les pistes. Ne pas les construire aurait
        // décalé `rows_[idx]`, dont la sélection, le glisser-déposer et le
        // rafraîchissement se servent partout.
        int y = 0;
        for (int i = 0; i < rows_.size(); ++i) {
            // DEUX RAISONS DE NE PAS PARAÎTRE, et elles ne se mélangent pas :
            // `hidden` appartient au MORCEAU et se sauvegarde (D17.4), le
            // filtre appartient à la SÉANCE et ne s'écrit nulle part.
            const bool masquee = project_ != nullptr
                                 && static_cast<size_t>(i) < project_->tracks.size()
                                 && (project_->tracks[static_cast<size_t>(i)].hidden
                                     || masqueeParLeFiltre(static_cast<size_t>(i))
                                     || vsm::sequencer::hiddenByCollapsedFolder(
                                            *project_, static_cast<size_t>(i)));
            const int h = masquee ? 0 : kRowHeight;
            // D19.4 : LES PISTES D'UN DOSSIER SONT EN RETRAIT. C'est ce qui
            // rend l'arborescence lisible d'un coup d'œil, et c'est la seule
            // chose que la profondeur change à l'écran.
            const int retrait = project_ != nullptr
                                        && static_cast<size_t>(i) < project_->tracks.size()
                                    ? std::min(4, project_->tracks[static_cast<size_t>(i)].folderDepth) * 14
                                    : 0;
            rows_[i]->setBounds(retrait, y, rowContainer_.getWidth() - retrait, h);
            rows_[i]->setVisible(!masquee);
            y += h;
        }
    }
    // D19.2 : DIRE POURQUOI C'EST VIDE. Le nombre compte : « aucune des 9 »
    // apprend du même coup que les pistes sont toujours là.
    const bool filtre = filterBox_.getText().trim().isNotEmpty();
    const bool rienAMontrer = filtre && visibles == 0 && !rows_.isEmpty();
    emptyLabel_.setVisible(rienAMontrer);
    if (rienAMontrer) {
        emptyLabel_.setText(vsm::app::ui::tr(u8"Aucune des %1 pistes ne porte ce nom.\n"
                                             u8"Elles jouent toujours — videz le filtre.")
                                .replace("%1", juce::String(rows_.size())),
                             juce::dontSendNotification);
        emptyLabel_.setBounds(viewport_.getBounds().reduced(10).withHeight(60));
    }
    if (aMontrer_ >= 0) faireVoirLaPiste(static_cast<size_t>(aMontrer_));
}

void TrackListComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::panel);

    // LA PISTE SURVOLÉE PENDANT UN GLISSER (D10.1). Sans ce retour, on lâche à
    // l'aveugle et on découvre après coup sur laquelle -- ce qui, pour un
    // preset, veut dire qu'on vient de changer le son de la mauvaise.
    if (dropRow_ >= 0 && dropRow_ < rows_.size()) {
        auto zone = rows_[dropRow_]->getBounds()
                        .translated(viewport_.getX(), viewport_.getY() - viewport_.getViewPositionY());
        g.setColour(juce::Colours::gold.withAlpha(0.25f));
        g.fillRect(zone);
        g.setColour(juce::Colours::gold);
        g.drawRect(zone, 2);
    }
}

// --- D10.1 : recevoir ce que le navigateur laisse tomber --------------------

int TrackListComponent::trackIndexAt(juce::Point<int> position) const {
    // La position est relative à CE composant ; les lignes vivent dans le
    // conteneur du `Viewport`, qui a son propre défilement.
    const auto dansConteneur = position - viewport_.getPosition()
                               + juce::Point<int>(0, viewport_.getViewPositionY());
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i]->getBounds().contains(dansConteneur)) return i;
    return -1;
}

bool TrackListComponent::isInterestedInDragSource(const SourceDetails& details) {
    // Seul le navigateur produit ces descriptions. Accepter n'importe quoi
    // ferait clignoter la liste sous des glissers qui ne la concernent pas.
    return details.description.toString().startsWith("vsm-browser:");
}

void TrackListComponent::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void TrackListComponent::itemDragMove(const SourceDetails& details) {
    const int rang = trackIndexAt(details.localPosition);
    if (rang == dropRow_) return;
    dropRow_ = rang;
    repaint();
}

void TrackListComponent::itemDragExit(const SourceDetails&) {
    dropRow_ = -1;
    repaint();
}

void TrackListComponent::itemDropped(const SourceDetails& details) {
    const int rang = trackIndexAt(details.localPosition);
    dropRow_ = -1;
    repaint();
    if (rang < 0) return;
    selectTrackIndex(static_cast<size_t>(rang));
    if (onBrowserItemDropped) onBrowserItemDropped(static_cast<size_t>(rang),
                                                    details.description.toString());
}

void TrackListComponent::retraduire() {
    addButton_.setButtonText(vsm::app::ui::tr("+ Ajouter une piste"));
    removeButton_.setButtonText(vsm::app::ui::tr("Supprimer"));
    filterBox_.setTextToShowWhenEmpty(vsm::app::ui::tr(u8"Filtrer les pistes..."),
                                       Palette::textSecondary);
    // D94 : l'infobulle du filtre, que le constructeur posait en français.
    filterBox_.setTooltip(vsm::app::ui::tr(
        u8"Ne montre que les pistes dont le nom contient ce texte.\n"
        u8"N'AFFECTE PAS LE SON : les pistes filtrées continuent de jouer. "
        u8"Rien n'est écrit dans le projet — videz le champ et tout revient."));
    for (auto* ligne : rows_) ligne->retraduire();   // D83
    resized();   // D94 : « aucune des N pistes » se refait à la disposition
    repaint();
}
