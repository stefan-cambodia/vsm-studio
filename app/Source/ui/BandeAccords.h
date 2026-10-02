#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/ChordTrack.h"
#include <algorithm>
#include <vector>

// D532.3 bis de docs/ROADMAP-daw.md — LA BANDE D'ACCORDS, LA MÊME SOUS LES DEUX RÈGLES.
//
// Une ligne d'accords par projet (`Project::chords`) : l'arrangement la montre sous sa règle,
// le piano roll sous la sienne, et c'est au piano roll qu'on écrit — là qu'on veut l'harmonie
// sous les yeux. Une seule mise en page pour les deux vues ET pour le relevé du banc
// (`VSM_ACCORDS`) : un relevé qui recalculerait la place à sa façon dirait ce que la peinture
// ne fait pas (la leçon des panneaux peints de D149 et D152).
//
// LA BANDE N'EXISTE QUE SI LE PROJET PORTE UN ACCORD : un projet sans accord garde sa
// disposition au pixel, et les bancs qui le photographient avec lui.

namespace vsm::app::ui {

/// Hauteur de la bande, en pixels logiques (l'échelle d'interface s'y applique).
constexpr int kBandeAccords = 20;

inline int hauteurBandeAccords(const std::vector<vsm::sequencer::ChordEvent>& accords) {
    return accords.empty() ? 0 : kBandeAccords;
}

/// Un accord tel que la bande le montre.
struct AccordDessine {
    juce::String symbole;
    float xTrait = 0.0f;   ///< le début de l'accord (hors de la zone s'il a commencé avant)
    float xTexte = 0.0f;   ///< où son symbole s'écrit
    bool ecrit = false;    ///< faux : pas la place — le trait seul
};

inline juce::Font policeBandeAccords() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }

/// LA MISE EN PAGE. Un accord vaut jusqu'au suivant : celui qui a commencé à gauche de la zone
/// visible y est encore EN VIGUEUR, et son symbole s'écrit au bord gauche — défiler ne doit pas
/// faire disparaître l'harmonie qu'on entend. Le symbole s'écrit ENTIER ou pas du tout : la
/// place va jusqu'à l'accord suivant, et un « Am7/G » rogné en « Am » serait un autre accord.
template <typename VersX>
std::vector<AccordDessine> mettreEnPageAccords(const std::vector<vsm::sequencer::ChordEvent>& accords,
                                               float xMin, float xMax, VersX tickToX) {
    std::vector<AccordDessine> dessines;
    const juce::Font police = policeBandeAccords();
    for (size_t i = 0; i < accords.size(); ++i) {
        const float debut = tickToX(accords[i].tick);
        const float fin = i + 1 < accords.size() ? tickToX(accords[i + 1].tick) : xMax;
        if (fin <= xMin || debut >= xMax) continue;   // hors de la zone visible
        AccordDessine d;
        d.symbole = juce::String(vsm::sequencer::chordSymbol(accords[i]));
        d.xTrait = debut;
        d.xTexte = std::max(debut, xMin) + 4.0f;
        const float place = std::min(fin, xMax) - d.xTexte - 3.0f;
        d.ecrit = police.getStringWidthFloat(d.symbole) <= place;
        dessines.push_back(d);
    }
    return dessines;
}

/// Peint la bande dans `zone` ; `xMin` est le bord gauche de la ligne de temps (après les
/// en-têtes de piste ou le clavier).
template <typename VersX>
void peindreBandeAccords(juce::Graphics& g, juce::Rectangle<int> zone, float xMin,
                         const std::vector<vsm::sequencer::ChordEvent>& accords, VersX tickToX) {
    namespace Palette = vsm::ui::Palette;
    if (accords.empty() || zone.isEmpty()) return;
    g.setColour(Palette::panelRaised);
    g.fillRect(zone);
    g.setColour(Palette::border);
    g.drawHorizontalLine(zone.getBottom() - 1, static_cast<float>(zone.getX()), static_cast<float>(zone.getRight()));
    g.setFont(policeBandeAccords());
    for (const auto& d : mettreEnPageAccords(accords, xMin, static_cast<float>(zone.getRight()), tickToX)) {
        if (d.xTrait >= xMin) {
            g.setColour(Palette::textSecondary);
            g.drawVerticalLine(static_cast<int>(d.xTrait), static_cast<float>(zone.getY()),
                               static_cast<float>(zone.getBottom()));
        }
        if (!d.ecrit) continue;
        g.setColour(Palette::textPrimary);
        g.drawText(d.symbole, juce::Rectangle<float>(d.xTexte, static_cast<float>(zone.getY()),
                                                     static_cast<float>(zone.getRight()) - d.xTexte,
                                                     static_cast<float>(zone.getHeight())),
                   juce::Justification::centredLeft, false);
    }
}

/// Le relevé du banc : « vue : N accord(s) — symbole@x, … » (« (pas la place) » quand le
/// symbole ne s'écrit pas), lu sur la MÊME mise en page que la peinture.
template <typename VersX>
juce::String releverBandeAccords(const juce::String& vue, const std::vector<vsm::sequencer::ChordEvent>& accords,
                                 int hauteurBande, float xMin, float xMax, VersX tickToX) {
    juce::StringArray morceaux;
    for (const auto& d : mettreEnPageAccords(accords, xMin, xMax, tickToX))
        morceaux.add(d.symbole + "@" + juce::String(juce::roundToInt(d.xTrait))
                     + (d.ecrit ? juce::String() : juce::String(u8" (pas la place)")));
    return "VSM_ACCORDS : " + vue + " : bande " + juce::String(hauteurBande) + " px, "
           + juce::String(static_cast<int>(accords.size())) + " accord(s) au projet, "
           + juce::String(morceaux.size()) + juce::String(u8" dans la vue")
           + (morceaux.isEmpty() ? juce::String() : juce::String(u8" — ") + morceaux.joinIntoString(", ")) + "\n";
}

} // namespace vsm::app::ui
