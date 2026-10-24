#pragma once
#include <JuceHeader.h>
#include "ArrangementComponent.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "BulleDeValeur.h"   // D445 : lireNombreSaisi
#include "vsm/sequencer/Project.h"
#include <cmath>

// D598 de docs/ROADMAP-daw.md — LA LIGNE D'INFORMATION DES CLIPS (l'*Info Line* de la fenêtre de projet de Cubase).
//
// Les notes avaient la leur (D29.4, `PianoRollToolbar::applyInfoLine`) ; les clips n'avaient que des gestes. Cette bande,
// au-dessus de l'arrangement (sous la ligne d'ensemble de D591), dit des clips choisis leur nombre, le nom du premier,
// son début, sa longueur, son gain — et les trois se TAPENT. Les refus se disent, comme ceux du piano roll (D449).
//
// UN COMPOSANT À PART, posé par `MainComponent` comme la ligne d'ensemble : éteinte (l'état d'origine), le centre ne
// bouge pas d'un pixel. Ce que la bande affiche et ce que le relevé dit se lisent sur le MÊME état (`Etat`).

namespace vsm::app::ui {

/// Hauteur de la bande, en pixels logiques : celle de la ligne d'ensemble.
constexpr int kLigneInfoClips = ArrangementComponent::kRulerHeight;

class LigneInfoClips : public juce::Component {
public:
    explicit LigneInfoClips(ArrangementComponent& arrangement) : arrangement_(arrangement) {
        setName("Ligne d'information des clips");
        for (auto* champ : {&debut_, &longueur_, &gain_}) {
            champ->setEditable(false, true, false);   // double-clic pour taper, comme la ligne du piano roll
            champ->setJustificationType(juce::Justification::centred);
            champ->setColour(juce::Label::outlineColourId, vsm::ui::Palette::border);
            champ->setFont(juce::FontOptions(13.0f));
            addAndMakeVisible(*champ);
        }
        debut_.setName("info-clips.debut");     // les noms du banc (saisir:, D444)
        longueur_.setName("info-clips.longueur");
        gain_.setName("info-clips.gain");
        debut_.onTextChange = [this] { appliquer(0); };
        longueur_.onTextChange = [this] { appliquer(1); };
        gain_.onTextChange = [this] { appliquer(2); };
        retraduire();
    }

    void setProject(const vsm::sequencer::Project* projet) { projet_ = projet; etat_ = {}; suivre(); }

    /// Le refus ou le geste, pour la ligne d'état.
    std::function<void(const juce::String&)> onStatut;

    void retraduire() {
        debut_.setTooltip(tr(u8"Début du premier clip choisi : mesure.temps (« 17.3 », « 17.3+120 » en ticks). Toute la "
                             u8"sélection suit. Double-clic pour taper."));
        longueur_.setTooltip(tr(u8"Longueur de chaque clip choisi, en ticks. Double-clic pour taper."));
        gain_.setTooltip(tr(u8"Gain de chaque clip audio choisi, en dB (de −24 à +24). Un clip MIDI n'en a pas. "
                            u8"Double-clic pour taper."));
        etat_ = {};
        suivre();
    }

    /// Appelé par la minuterie d'interface : relit l'état, et ne refait les textes que s'il a changé (et jamais
    /// pendant qu'on tape dans un champ).
    void suivre() {
        const Etat e = lire();
        if (e == etat_) return;
        etat_ = e;
        if (!debut_.isBeingEdited()) debut_.setText(e.combien == 0 ? juce::String::fromUTF8("\xe2\x80\x94") : e.debut, juce::dontSendNotification);
        if (!longueur_.isBeingEdited())
            longueur_.setText(e.combien == 0 ? juce::String::fromUTF8("\xe2\x80\x94") : juce::String(e.longueur), juce::dontSendNotification);
        if (!gain_.isBeingEdited())
            gain_.setText(e.combien == 0 || !e.audio ? juce::String::fromUTF8("\xe2\x80\x94") : juce::String(e.gainDb, 1) + " dB",
                          juce::dontSendNotification);
        debut_.setEnabled(e.combien > 0);
        longueur_.setEnabled(e.combien > 0);
        gain_.setEnabled(e.combien > 0 && e.audio);   // D598 : un clip MIDI n'a pas de gain qui s'entende
        repaint();
    }

    /// `VSM_INFO_CLIPS : montrée oui ; N clip(s) ; début … ; longueur … ; gain …` — l'état que la bande affiche.
    juce::String releverInfoClips() const;

    void paint(juce::Graphics& g) override {
        namespace Palette = vsm::ui::Palette;
        g.fillAll(Palette::panel);
        g.setColour(Palette::border);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        g.setFont(juce::FontOptions(13.0f));
        const Etat& e = etat_;
        g.setColour(Palette::textSecondary);
        g.drawText(e.combien == 0 ? tr(u8"Clips : aucun clip choisi")
                                  : tr(u8"Clips : %1").replace("%1", e.nom)
                                        + (e.combien > 1 ? juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(e.combien)
                                                         : juce::String()),
                   6, 0, kLargeurNom - 10, getHeight(), juce::Justification::centredLeft, true);
        const auto etiquette = [&](const juce::Label& champ, const juce::String& mot) {
            g.drawText(mot, champ.getX() - kLargeurMot, 0, kLargeurMot - 4, getHeight(), juce::Justification::centredRight);
        };
        etiquette(debut_, tr(u8"Début"));
        etiquette(longueur_, tr(u8"Longueur"));
        etiquette(gain_, tr(u8"Gain"));
        juce::StringArray marques;
        if (e.muet) marques.add(tr(u8"muet"));
        if (e.verrouille) marques.add(tr(u8"verrouillé"));
        if (!marques.isEmpty())
            g.drawText(marques.joinIntoString(", "), gain_.getRight() + 10, 0, 160, getHeight(),
                       juce::Justification::centredLeft);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(0, 2);
        r.removeFromLeft(kLargeurNom);
        r.removeFromLeft(kLargeurMot);
        debut_.setBounds(r.removeFromLeft(90));
        r.removeFromLeft(kLargeurMot);
        longueur_.setBounds(r.removeFromLeft(80));
        r.removeFromLeft(kLargeurMot);
        gain_.setBounds(r.removeFromLeft(80));
    }

private:
    static constexpr int kLargeurNom = 230;
    static constexpr int kLargeurMot = 74;

    struct Etat {
        size_t combien = 0;
        juce::String nom, debut;
        vsm::midi::Tick longueur = 0;
        float gainDb = 0.0f;
        bool audio = false, muet = false, verrouille = false;
        bool operator==(const Etat& o) const {
            return combien == o.combien && nom == o.nom && debut == o.debut && longueur == o.longueur
                   && gainDb == o.gainDb && audio == o.audio && muet == o.muet && verrouille == o.verrouille;
        }
    };

    Etat lire() const {
        Etat e;
        if (projet_ == nullptr) return e;
        size_t piste = 0;
        const auto* clip = arrangement_.premierClipChoisi(piste);
        if (clip == nullptr) return e;
        e.combien = arrangement_.clipsChoisis();
        const auto& track = projet_->tracks[piste];
        e.nom = juce::String::fromUTF8((clip->name.empty() ? track.name : clip->name).c_str());
        const auto bb = projet_->timeSignatureMap.barBeatAt(clip->startTick, projet_->ticksPerQuarterNote);
        e.debut = juce::String(static_cast<long long>(bb.bar + 1)) + "." + juce::String(static_cast<long long>(bb.beat + 1));
        if (bb.tickInBeat != 0) e.debut += "+" + juce::String(static_cast<long long>(bb.tickInBeat));
        e.longueur = clip->length;
        e.audio = track.kind == vsm::sequencer::Track::Kind::Audio;
        e.gainDb = clip->gain > 0.0f ? std::round(200.0f * std::log10(clip->gain)) / 10.0f : -24.0f;
        e.muet = clip->muted;
        e.verrouille = clip->locked || track.locked;
        return e;
    }

    void refuser(const juce::String& modele, const juce::String& saisi) {
        // D449 : LE REFUS SE DIT, et le champ reprend la valeur du clip.
        if (onStatut) onStatut(modele.replace("%1", saisi));
        std::fputs((juce::String::fromUTF8("VSM_INFO_CLIPS : refus \xe2\x80\x94 ") + modele.replace("%1", saisi) + "\n").toRawUTF8(), stderr);
        etat_ = {};
        suivre();
    }

    void appliquer(int champ) {
        if (projet_ == nullptr || arrangement_.clipsChoisis() == 0) return;
        size_t piste = 0;
        const auto* premier = arrangement_.premierClipChoisi(piste);
        if (premier == nullptr) return;
        if (champ == 0) {
            // « 17.3 » ou « 17.3+120 » : la sélection entière suit l'écart du premier départ (`nudgeSelection`).
            const juce::String texte = debut_.getText().trim();
            const juce::String position = texte.upToFirstOccurrenceOf("+", false, false);
            const vsm::midi::Tick reste = texte.contains("+")
                ? static_cast<vsm::midi::Tick>(texte.fromFirstOccurrenceOf("+", false, false).getLargeIntValue()) : 0;
            int64_t mesure = 0, temps = 0;
            if (!vsm::sequencer::parseBarBeat(position.toStdString(), mesure, temps) || reste < 0) {
                refuser(tr(u8"Début refusé : « %1 » — mesure.temps, par exemple 17.3 ou 17.3+120"), texte);
                return;
            }
            const vsm::midi::Tick voulu =
                projet_->timeSignatureMap.tickAtBarBeat(mesure, temps, projet_->ticksPerQuarterNote) + reste;
            if (voulu != premier->startTick) arrangement_.nudgeSelection(voulu - premier->startTick);
        } else if (champ == 1) {
            const juce::String texte = longueur_.getText().trim();
            const auto ticks = texte.getLargeIntValue();
            if (ticks <= 0 || !texte.containsOnly("0123456789")) {
                refuser(tr(u8"Longueur refusée : « %1 » — un nombre de ticks, plus grand que 0"), texte);
                return;
            }
            arrangement_.donnerLongueurAuxClipsChoisis(static_cast<vsm::midi::Tick>(ticks));
        } else {
            const juce::String texte = gain_.getText().trim();
            const double db = lireNombreSaisi(texte.upToFirstOccurrenceOf("dB", false, true));   // D445 : la virgule
            if (!texte.containsAnyOf("0123456789") || !std::isfinite(db) || db < -24.0 || db > 24.0) {
                refuser(tr(u8"Gain refusé : « %1 » — de −24 à +24 dB"), texte);
                return;
            }
            arrangement_.poserGainDesClipsChoisis(static_cast<float>(db));
        }
        etat_ = {};
        suivre();
    }

    ArrangementComponent& arrangement_;
    const vsm::sequencer::Project* projet_ = nullptr;
    juce::Label debut_, longueur_, gain_;
    Etat etat_;
};

// Défini hors de la classe, en colonne 0 : `tools/inventaire_langue.py` y reconnaît une fonction de banc (D598).
inline juce::String LigneInfoClips::releverInfoClips() const {
    if (!isVisible()) return juce::String::fromUTF8("VSM_INFO_CLIPS : montr\xc3\xa9" "e non");
    const Etat e = lire();
    if (e.combien == 0) return juce::String::fromUTF8("VSM_INFO_CLIPS : montr\xc3\xa9" "e oui ; aucun clip choisi");
    return juce::String::fromUTF8("VSM_INFO_CLIPS : montr\xc3\xa9" "e oui ; ") + juce::String(e.combien)
           + " clip(s) ; " + juce::String::fromUTF8("d\xc3\xa9" "but ") + e.debut + " ; longueur "
           + juce::String(e.longueur) + " ; gain " + (e.audio ? juce::String(e.gainDb, 1) + " dB" : juce::String::fromUTF8("\xe2\x80\x94"))
           + (e.muet ? " ; muet" : "") + (e.verrouille ? juce::String::fromUTF8(" ; verrouill\xc3\xa9") : juce::String());
}

} // namespace vsm::app::ui
