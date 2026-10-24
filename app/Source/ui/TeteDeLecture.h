#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace vsm::app::ui {

/// D588.2 : LA LARGEUR DE LA TÊTE DE LECTURE (*Cursor Width*), une préférence du poste, lue par les six vues qui la
/// peignent : l'arrangement, la grille et la règle du piano roll, les lanes d'automation, des contrôleurs MIDI et du
/// tempo. 0 = d'origine — chaque vue garde son dessin au pixel près (un trait de 1,5 px dans les trois premières, une
/// barre de 2 px dans les lanes), et aucune photo de banc ne bouge ; 3 ou 5 = une barre de cette largeur, calée sur
/// les pixels et centrée sur la colonne du tick (la leçon de D587.3 : un trait posé à une demi-colonne n'en remplit
/// aucune).
inline std::atomic<int> largeurDeTete_{0};
inline void setLargeurDeTete(int pixels) { largeurDeTete_.store(pixels == 3 || pixels == 5 ? pixels : 0); }
inline int largeurDeTete() { return largeurDeTete_.load(); }

/// Les largeurs d'origine, lues par la peinture ET par le relevé `relever-largeur-tete` : il dit ce qui sera peint.
inline constexpr float kTeteDOrigineVues = 1.5f;    ///< l'arrangement, la grille et la règle du piano roll
inline constexpr float kTeteDOrigineLanes = 2.0f;   ///< automation, contrôleurs MIDI, tempo

/// La largeur qu'une vue peindra, son dessin d'origine valant `origine`.
inline float largeurDeTetePeinte(float origine) { return largeurDeTete() > 0 ? static_cast<float>(largeurDeTete()) : origine; }

/// Peint la tête LARGE à l'abscisse `x` (la couleur déjà posée) et rend vrai ; rend faux à la largeur d'origine,
/// et la vue peint alors son dessin d'avant.
inline bool peindreTeteLarge(juce::Graphics& g, float x, int haut, int hauteur) {
    const int largeur = largeurDeTete();
    if (largeur <= 0) return false;
    g.fillRect(juce::roundToInt(x) - largeur / 2, haut, largeur, hauteur);
    return true;
}

} // namespace vsm::app::ui
