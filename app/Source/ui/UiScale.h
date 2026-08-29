#pragma once

#include <JuceHeader.h>

namespace vsm::app::ui {

// ---------------------------------------------------------------------------
// ÉCHELLE DE L'INTERFACE — un seul réglage, un seul endroit.
//
// POURQUOI CE MODULE EXISTE. Les tailles de police du DAW étaient écrites en
// dur, une par une, dans une quarantaine d'appels `setFont(...)` répartis dans
// quinze composants -- 9, 9,5, 10, 11, 12 points. À l'usage, c'est trop petit
// à lire, et il n'y avait AUCUN endroit où le corriger : agrandir aurait
// voulu dire retoucher quarante valeurs, en espérant n'en oublier aucune.
//
// POURQUOI L'ÉCHELLE GLOBALE, ET PAS SEULEMENT LA POLICE. Grossir le texte
// SEUL casserait les façades de machines : elles sont des répliques de faces
// avant, où chaque légende est calculée à partir de la hauteur de SA case
// (voir `MachinePanelComponent::layoutControls`, qui borne la police entre 8
// et 11 points selon la place disponible). Un texte plus grand dans une case
// inchangée déborde ou se fait tronquer, et l'utilisateur y perdrait ce qu'il
// venait chercher : de la lisibilité.
//
// L'échelle globale de JUCE agrandit texte ET cases dans le même rapport :
// rien ne déborde, aucune mise en page ne bouge, et les fenêtres flottantes
// suivent puisque le facteur s'applique à tout le bureau de l'application.
//
// Le réglage est CONSERVÉ d'une exécution à l'autre : un confort visuel qu'il
// faudrait redemander à chaque lancement n'en serait pas un.
// ---------------------------------------------------------------------------
class UiScale {
public:
    // Les paliers proposés au menu. 1,0 est la taille d'origine ; le pas de
    // 25 % est assez large pour qu'on VOIE la différence -- des paliers plus
    // fins obligeraient à en essayer cinq pour percevoir un changement.
    static const juce::Array<float>& steps();

    // Facteur courant, borné aux extrêmes des paliers.
    static float current();

    // Applique le facteur à toute l'application et l'enregistre.
    static void apply(float factor);

    // À appeler UNE fois au démarrage, avant de construire la moindre
    // fenêtre : le facteur détermine la taille physique de celles-ci, et
    // JUCE ne redimensionne pas rétroactivement ce qui existe déjà.
    static void applySavedAtStartup();

    // Libellé d'un palier pour le menu (« 125 % »).
    static juce::String label(float factor);

    // Le fichier de préférences de l'application, exposé parce qu'il n'y en a
    // qu'un et que d'autres réglages doivent y vivre -- à commencer par le
    // choix du périphérique audio, qui n'était pas conservé : le sélecteur
    // était rouvert vierge à chaque lancement, et il fallait rechoisir sa carte
    // à chaque fois.
    static juce::PropertiesFile& properties();

private:
};

} // namespace vsm::app::ui
