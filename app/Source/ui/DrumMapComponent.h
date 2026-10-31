#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsm::app::ui {

/// D609.2 — LA FENÊTRE DE LA CARTE DE BATTERIE (l'éditeur de *Drum Map* de Cubase), pour UNE piste.
///
/// **ELLE NE DÉTIENT RIEN** : l'application lui donne la piste (`setPiste`) à l'ouverture et après chaque changement ;
/// chaque choix d'une note de sortie remonte par `onChoix` (note écrite, note jouée — ou −1 : « elle-même »), et c'est
/// l'application qui écrit le projet, un pas d'annulation par changement.
///
/// Une ligne par note que la piste ÉCRIT, et par note que la carte déplace déjà ; à gauche la note écrite et son nom
/// General MIDI (la convention des parties écrites, et des projets reconstruits), à droite la note jouée, choisie
/// parmi les pièces que la MACHINE nomme (« tom grave (45) »).
class DrumMapComponent : public juce::Component {
public:
    DrumMapComponent();
    void retraduire();

    void setPiste(size_t index, const vsm::sequencer::Track& piste);
    /// La note jouée choisie pour `ecrite` (le verbe de banc `carte:41>45`), par la MÊME liste que la souris ; −1 :
    /// elle-même. Faux si la note n'a pas de ligne, ou si la machine ne nomme pas `jouee`.
    bool choisir(int ecrite, int jouee);
    /// Les lignes telles que la fenêtre les écrit, pour le relevé (`VSM_CARTE`).
    juce::StringArray lignesPourReleve() const;
    size_t piste() const { return piste_; }

    std::function<void(int ecrite, int jouee)> onChoix;
    std::function<void()> onIdentite;
    /// D610.1 : « Adapter General MIDI à la machine » ; la remarque dit ce qui reste sans équivalent.
    std::function<void()> onAdapter;
    void setRemarque(const juce::String& texte);
    juce::String remarque() const { return remarque_.getText(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Rangee {
        int ecrite = 0;
        std::unique_ptr<juce::Label> nom;
        std::unique_ptr<juce::ComboBox> sortie;
    };
    juce::String nomEcrit(int note) const;
    juce::String nomJoue(int note) const;

    size_t piste_ = 0;
    std::string instrument_;
    std::vector<Rangee> rangees_;
    juce::Label titre_, aide_, remarque_;
    juce::TextButton identite_, adapter_;
    juce::Component contenu_;
    juce::Viewport defilement_;
};

} // namespace vsm::app::ui
