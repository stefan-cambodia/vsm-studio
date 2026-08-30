#pragma once
#include <JuceHeader.h>
#include "vsm/audio/engine/MidiLearnMap.h"
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// LA LISTE DES ASSOCIATIONS MIDI, ET LE MOYEN D'EN DÉFAIRE UNE (D10.2).
///
/// Le MIDI learn existait, et il était **invisible**. On armait, on tournait un
/// bouton, ça marchait -- et ensuite plus rien ne disait ce qui était lié à
/// quoi. Retrouver qu'un potentiomètre pilotait la résonance de la piste 4
/// demandait de les tourner tous, un par un, en regardant l'écran. Défaire une
/// association demandait de la remplacer par une autre, ou d'effacer les
/// quinze.
///
/// LA FENÊTRE NE FAIT QUE MONTRER ET DEMANDER : elle ne détient pas la carte,
/// qui vit dans `AudioEngine`, et elle ne l'écrit pas. C'est la même règle que
/// pour le mixeur -- un panneau n'est jamais une source de vérité.
class MidiLearnWindow : public juce::Component {
public:
    struct Row {
        int controller = 0;
        juce::String description;
    };

    MidiLearnWindow();
    // DÉCLARÉ ET DÉFINI DANS LE .cpp : le contenu défilant est une classe
    // interne incomplète ici, et `unique_ptr` a besoin de sa taille pour la
    // détruire.
    ~MidiLearnWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Republie la liste. Appelée à chaque changement, y compris après un
    /// apprentissage fait ailleurs (un CC tourné pendant que la fenêtre est
    /// ouverte doit s'y voir apparaître).
    void setRows(std::vector<Row> rows);

    /// Affiche « tournez un potentiomètre... », ou rien quand la chaîne
    /// d'apprentissage n'est pas armée.
    void setWaiting(const juce::String& quoi);

    std::function<void(int)> onRemove;   ///< défaire l'association d'un CC
    std::function<void()> onRemoveAll;
    /// L'utilisateur veut apprendre une nouvelle association : à l'application
    /// de proposer les cibles possibles (elle seule connaît la piste choisie).
    std::function<void(juce::Component*)> onLearn;

private:
    class Contenu;
    std::unique_ptr<Contenu> contenu_;
    juce::Viewport defilement_;
    juce::Label vide_;
    juce::TextButton toutEffacer_ { "Tout effacer" };
    juce::TextButton apprendre_ { "Apprendre..." };
    juce::Label attente_;
};

} // namespace vsm::app::ui
