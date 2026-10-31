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
    /// D86 : repose les textes fixes dans la langue courante.
    void retraduire();
    PlayOrderComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    /// Republie les sections depuis le projet. Appelée à chaque ouverture du
    /// panneau : les repères ont pu changer entre-temps.
    void setSections(std::vector<vsm::sequencer::Section> sections, bool tempoSeraLaisse);

    /// L'ordre demandé, dans l'ordre. Vide = rien à aplatir.
    const std::vector<int>& order() const { return ordre_; }

    std::function<void(const std::vector<int>&)> onFlatten;

    /// D606.1 : LA LECTURE SUIT L'ORDRE (le mode Arrangement de Cubase). La case, état de SESSION comme l'ordre ;
    /// `onChainChanged` à chaque changement de l'ordre ou de la case — l'application republie la chaîne au moteur.
    bool suitLOrdre() const { return suivre_.getToggleState(); }
    void setSuitLOrdre(bool oui) { suivre_.setToggleState(oui, juce::sendNotification); }
    /// Pose l'ordre d'un coup (le verbe de banc `ordre:`) ; les indices hors des sections sont refusés, et dits.
    bool poserOrdre(const std::vector<int>& ordre);
    const std::vector<vsm::sequencer::Section>& sections() const { return sections_; }
    /// L'élément que la lecture joue (−1 : aucun) — éclairé dans la liste.
    void setElementJoue(int element);
    int elementJoue() const { return elementJoue_; }
    juce::String texteDeLaCase() const { return suivre_.getButtonText(); }
    juce::String infobulleDeLaListe() const { return infobulleListe_; }   // D612.1
    std::function<void()> onChainChanged;
    /// D612.1 : ALLER À UN ÉLÉMENT — le double-clic d'une ligne (l'indice dans l'ordre) ; `allerA` est le même geste
    /// pour le verbe de banc `ordre-aller:`.
    std::function<void(int ligne)> onAller;
    bool allerA(int ligne);

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;   // D612.1

    std::vector<vsm::sequencer::Section> sections_;
    std::vector<int> ordre_;
    bool tempoSeraLaisse_ = false;
    int elementJoue_ = -1;
    juce::String infobulleListe_;   // D612.1 : posée par `retraduire`, lue par le relevé

    juce::Label titreSections_, titreOrdre_, avertissement_;
    juce::ComboBox sectionAAjouter_;
    juce::TextButton ajouter_ { juce::String::fromUTF8(u8"Ajouter ▸") };
    juce::TextButton retirer_ { juce::String::fromUTF8(u8"Retirer") };
    juce::TextButton monter_ { juce::String::fromUTF8(u8"Monter") };
    juce::TextButton descendre_ { juce::String::fromUTF8(u8"Descendre") };
    juce::TextButton aplatir_ { juce::String::fromUTF8(u8"Aplatir (écrit le matériau)") };
    juce::ListBox liste_ { "ordre", this };
    juce::ToggleButton suivre_;   // D606.1

    void rafraichir();
};

} // namespace vsm::app::ui
