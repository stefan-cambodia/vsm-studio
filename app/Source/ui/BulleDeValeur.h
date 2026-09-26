#pragma once

#include <JuceHeader.h>
#include "Langue.h"
#include <cmath>
#include <functional>

namespace vsm::app::ui {

// ---------------------------------------------------------------------------
// D135 : LA BULLE DE VALEUR des curseurs qui n'ont pas de zone de texte.
//
// POURQUOI. Quatre familles de commandes se réglaient sans voir leur valeur
// (D134) : le volume et le panoramique d'une ligne de piste, le panoramique et
// les départs d'une tranche, les boutons MASTER, les paramètres du panneau
// générique. JUCE sait montrer une bulle pendant le réglage
// (`setPopupDisplayEnabled`) ; elle s'ouvre à l'appui et reste tant qu'on
// tient.
//
// OÙ ELLE EST POSÉE, ET POURQUOI CE N'EST PAS LE BUREAU. Sans parent, JUCE la
// pose sur le bureau, comme une fenêtre transparente -- que l'autoportrait ne
// voit pas, et qu'un bureau sans transparence dessine mal. Elle va donc dans le
// CONTENU de la fenêtre qui porte le curseur, et elle y est reposée quand le
// curseur change de fenêtre (panneaux flottants) : c'est ce qu'écoute
// `componentParentHierarchyChanged`.
//
// DURÉE DE VIE : la bulle tient une référence au curseur. Elle se déclare
// APRÈS lui, pour être détruite AVANT.
// ---------------------------------------------------------------------------
class BulleDeValeur final : private juce::ComponentListener, private juce::Value::Listener {
public:
    explicit BulleDeValeur(juce::Slider& curseur) : curseur_(curseur) {
        curseur_.addComponentListener(this);
        // Le `Value` du curseur, et non `Slider::Listener` : ce dernier se tait
        // sur `setValue(…, dontSendNotification)`, et c'est ainsi que les lignes
        // se resynchronisent sur le modèle -- l'infobulle serait restée à
        // l'ancienne valeur.
        curseur_.getValueObject().addListener(this);
        poser();
    }
    ~BulleDeValeur() override {
        curseur_.getValueObject().removeListener(this);
        curseur_.removeComponentListener(this);
    }
    BulleDeValeur(const BulleDeValeur&) = delete;
    BulleDeValeur& operator=(const BulleDeValeur&) = delete;

    /// D414 : LE SURVOL DIT CE QUE L'APPUI MONTRE. La bulle ne s'ouvre qu'à
    /// l'appui ; un curseur sans libellé (le volume et le panoramique d'une
    /// ligne de piste) ne disait au survol ni ce qu'il règle, ni ce qu'il vaut.
    /// Nommé, il porte l'infobulle « Volume : -0.9 dB (double-clic : valeur
    /// d'usine) », refaite à chaque valeur. Le nom est une FONCTION qui rend le
    /// texte traduit : `rafraichir()`, appelé au changement de langue, le relit.
    void nommer(std::function<juce::String()> nom) {
        nom_ = std::move(nom);
        rafraichir();
    }
    void rafraichir() {
        if (!nom_) return;
        curseur_.setTooltip(tr(u8"%1 : %2 (double-clic : valeur d'usine)")
                                .replace("%1", nom_())
                                .replace("%2", curseur_.getTextFromValue(curseur_.getValue())));
    }

private:
    void valueChanged(juce::Value&) override { rafraichir(); }
    void componentParentHierarchyChanged(juce::Component&) override { poser(); }
    void poser() {
        juce::Component* parent = nullptr;
        if (auto* fenetre = curseur_.findParentComponentOfClass<juce::ResizableWindow>())
            parent = fenetre->getContentComponent();
        curseur_.setPopupDisplayEnabled(true, false, parent);
    }
    juce::Slider& curseur_;
    std::function<juce::String()> nom_;
};

/// Un gain linéaire en décibels, comme le fader du mixeur : « -0.9 dB »,
/// « +1.5 dB », « -inf dB ».
inline juce::String texteDecibels(double gain) {
    if (gain <= 1.0e-5) return "-inf dB";
    const double db = 20.0 * std::log10(gain);
    return juce::String(db > 0.05 ? "+" : "") + juce::String(db, 1) + " dB";
}

/// Un panoramique (-1 à 1) comme on le lit sur une console : « C », « L 35 » /
/// « R 35 » en anglais, « G 35 » / « D 35 » en français.
inline juce::String textePanoramique(double pan) {
    const int pourcent = juce::roundToInt(std::abs(pan) * 100.0);
    if (pourcent == 0) return "C";
    return (pan < 0.0 ? tr(u8"G %1") : tr(u8"D %1")).replace("%1", juce::String(pourcent));
}

/// Un paramètre de machine, comme l'afficheur des façades : deux décimales sous
/// 100, un entier au-delà, et l'unité.
inline juce::String texteParametre(double valeur, const juce::String& unite) {
    juce::String texte = std::abs(valeur) >= 100.0 ? juce::String(juce::roundToInt(valeur))
                                                   : juce::String(valeur, 2);
    if (unite.isNotEmpty()) texte += " " + unite;
    return texte;
}

} // namespace vsm::app::ui
