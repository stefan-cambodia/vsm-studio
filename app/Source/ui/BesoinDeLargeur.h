#pragma once

#include <JuceHeader.h>

#include <algorithm>

namespace vsm::app::ui {

/// D382 : CE QU'UN TEXTE DEMANDE DE LARGEUR, rapporté à ce que sa case offre.
/// 1 = tient juste ; au-delà, `juce::Label` le comprime, puis le coupe par « … »
/// passé 1 / son échelle minimale.
///
/// LE PLUS GRAND DE DEUX RAPPORTS, et c'est la correction de D382. D379 ne
/// comptait que le texte entier sur « largeur × lignes » : « STIFFNESS » (55 px)
/// dans une case de 27 px à deux lignes sortait à 1,02, « tient presque », quand
/// la photo le montrait coupé. `drawFittedText` ne coupe pas un MOT entre deux
/// lignes : le mot le plus long doit tenir sur UNE.
inline float besoinDeLargeur(const juce::String& texte, const juce::Font& police, juce::Rectangle<int> zone) {
    if (zone.getWidth() <= 0 || zone.getHeight() <= 0) return 99.0f;
    const float largeur = static_cast<float>(zone.getWidth());
    const int lignes = std::max(1, static_cast<int>(static_cast<float>(zone.getHeight()) / police.getHeight()));
    const float total = juce::GlyphArrangement::getStringWidth(police, texte);
    float mot = 0.0f;
    for (const auto& m : juce::StringArray::fromTokens(texte, " \n", ""))
        mot = std::max(mot, juce::GlyphArrangement::getStringWidth(police, m));
    return std::max(mot / largeur, total / (largeur * static_cast<float>(lignes)));
}

}  // namespace vsm::app::ui
