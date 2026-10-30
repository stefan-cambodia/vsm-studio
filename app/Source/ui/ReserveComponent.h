#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// D607.1 — LA RÉSERVE DES FICHIERS (le *Pool* de Cubase), en un panneau.
///
/// **CE PANNEAU NE DÉTIENT RIEN.** L'application lui donne ses lignes, relues du projet
/// (`collectMediaDesignations`) et du disque à chaque ouverture et à « Rafraîchir » — la règle de l'ordre de jeu et
/// des préférences : une seconde vérité est toujours celle qui ment. Il ne change pas le projet ; son seul geste
/// destructeur est le nettoyage de D564.1, avec sa confirmation et sa corbeille.
///
/// LES LIGNES SONT PEINTES (`paintListBoxItem`), donc invisibles au relevé de textes : le relevé de banc
/// (`VSM_RESERVE`) lit la MÊME source que la peinture — `lignes()` et `texteDe` (la leçon de D149 et D152).
class ReserveComponent : public juce::Component,
                          private juce::ListBoxModel {
public:
    enum class Etat { Joue, EnReserve, Manquant, Inutilise, GardePourLHistoire };
    struct Ligne {
        juce::String fichier;      ///< le chemin tel que le projet l'écrit (relatif à son dossier)
        double duree = 0.0;        ///< en secondes ; 0 : inconnue
        double frequence = 0.0;    ///< en hertz ; 0 : inconnue
        int canaux = 0;
        int64_t octets = -1;       ///< −1 : absent du disque
        juce::String designePar;   ///< déjà dans la langue courante ; « — » : rien
        Etat etat = Etat::Inutilise;
    };

    ReserveComponent();
    /// Repose les textes fixes dans la langue courante.
    void retraduire();

    /// `sansDossier` : un projet jamais enregistré n'a pas de réserve — la fenêtre le dit, sans liste.
    void setLignes(std::vector<Ligne> lignes, bool sansDossier);
    const std::vector<Ligne>& lignes() const { return lignes_; }
    /// La ligne de total : « 4 fichiers · 2,1 Mo · 1 manquant · 1 inutilisé ».
    juce::String total() const;
    /// Les colonnes d'une ligne, telles que la peinture les écrit : durée, format, taille, état.
    static juce::String texteDuree(const Ligne& l);
    static juce::String texteFormat(const Ligne& l);
    static juce::String texteTaille(const Ligne& l);
    static juce::String nomDeLEtat(Etat etat);

    std::function<void()> onNettoyer, onRafraichir;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    /// Les bords des six colonnes pour une largeur donnée — la même règle pour l'en-tête et les lignes.
    static std::vector<juce::Range<int>> colonnes(int largeur);

    std::vector<Ligne> lignes_;
    bool sansDossier_ = false;
    juce::Rectangle<int> enTete_;
    juce::Label titre_, total_;
    juce::TextButton nettoyer_, rafraichir_;
    juce::ListBox liste_ { "reserve", this };
};

} // namespace vsm::app::ui
