#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/Track.h"
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// ASSEMBLER LES PRISES (D18.2) — les lanes de Cubase, en un panneau.
///
/// **CE PANNEAU NE DÉTIENT QUE LA LISTE DES TRONÇONS.** Les prises viennent de
/// la piste et se relisent à chaque ouverture ; le matériau n'est touché qu'au
/// moment de composer. Même règle que l'ordre de jeu (D18.4) et que les
/// préférences.
///
/// POURQUOI UNE LISTE ET NON DES LANES SUPERPOSÉES, et c'est une décision
/// assumée : des lanes demandent une seconde vue du piano roll, avec son
/// défilement, son zoom et sa sélection -- c'est-à-dire un second éditeur.
/// Une liste de tronçons dit EXACTEMENT la même chose (« de la mesure 1 à 4,
/// la prise 2 »), se lit d'un coup d'œil, et se corrige sans viser au pixel.
/// Ce qu'elle ne donne pas, c'est de VOIR les passes pour choisir : on les
/// écoute en changeant de prise, ce que le menu Enregistrement fait déjà.
class TakeCompComponent : public juce::Component,
                           private juce::ListBoxModel {
public:
    TakeCompComponent();

    void resized() override;
    void paint(juce::Graphics& g) override;

    /// Republie les prises de la piste choisie. `ticksPerBar` sert à afficher
    /// et à saisir les bornes en MESURES : personne ne compte en ticks.
    void setTake(std::vector<juce::String> takeNames, int activeTake,
                  vsm::midi::Tick ticksPerBar, vsm::midi::Tick lastTick);

    std::function<void(const std::vector<vsm::sequencer::CompSegment>&)> onCompose;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;

    std::vector<juce::String> prises_;
    int active_ = -1;
    vsm::midi::Tick parMesure_ = 1920;
    vsm::midi::Tick fin_ = 1920;
    std::vector<vsm::sequencer::CompSegment> troncons_;

    juce::Label titre_, aide_;
    juce::ComboBox prise_;
    juce::Label deLabel_, aLabel_;
    juce::TextEditor de_, a_;
    juce::TextButton ajouter_ { juce::String::fromUTF8(u8"Ajouter le tronçon") };
    juce::TextButton retirer_ { juce::String::fromUTF8(u8"Retirer") };
    juce::TextButton composer_ { juce::String::fromUTF8(u8"Composer (écrit le matériau)") };
    juce::ListBox liste_ { "troncons", this };

    void rafraichir();
};

} // namespace vsm::app::ui
