#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/PlayOrder.h"
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// L'ORDRE DE JEU (D18.4) — la piste d'Arrangement de Cubase, en un panneau.
///
/// **CE PANNEAU NE DÉTIENT QU'UNE CHOSE : L'ORDRE.** Les sections viennent des
/// repères du projet et se relisent à chaque ouverture ; le matériau, lui,
/// n'est touché qu'au moment d'aplatir. C'est la même règle que pour les
/// préférences et le mixeur — une seconde vérité est toujours celle qui ment.
///
/// L'ORDRE N'EST PAS ENREGISTRÉ DANS LE PROJET, et c'est une décision. Il ne
/// décrit rien du morceau : c'est un brouillon qu'on essaie, et dont le
/// résultat s'écrit dans le matériau dès qu'on aplatit. L'écrire dans
/// `project.json` ajouterait au format un objet qui ne survit à rien -- et un
/// projet rouvert avec un ordre de jeu qu'on ne se rappelle pas avoir posé
/// serait une surprise, pas un service.
class PlayOrderComponent : public juce::Component,
                            private juce::ListBoxModel {
public:
    PlayOrderComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    /// Republie les sections depuis le projet. Appelée à chaque ouverture du
    /// panneau : les repères ont pu changer entre-temps.
    void setSections(std::vector<vsm::sequencer::Section> sections, bool tempoSeraLaisse);

    /// L'ordre demandé, dans l'ordre. Vide = rien à aplatir.
    const std::vector<int>& order() const { return ordre_; }

    std::function<void(const std::vector<int>&)> onFlatten;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;

    std::vector<vsm::sequencer::Section> sections_;
    std::vector<int> ordre_;
    bool tempoSeraLaisse_ = false;

    juce::Label titreSections_, titreOrdre_, avertissement_;
    juce::ComboBox sectionAAjouter_;
    juce::TextButton ajouter_ { juce::String::fromUTF8(u8"Ajouter ▸") };
    juce::TextButton retirer_ { juce::String::fromUTF8(u8"Retirer") };
    juce::TextButton monter_ { juce::String::fromUTF8(u8"Monter") };
    juce::TextButton descendre_ { juce::String::fromUTF8(u8"Descendre") };
    juce::TextButton aplatir_ { juce::String::fromUTF8(u8"Aplatir (écrit le matériau)") };
    juce::ListBox liste_ { "ordre", this };

    void rafraichir();
};

} // namespace vsm::app::ui
