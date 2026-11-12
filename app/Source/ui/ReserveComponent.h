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
        /// D628.1 : l'ORIGINE du fichier, en secondes — sa référence de temps BWF (bloc `bext`) divisée par SA
        /// fréquence ; < 0 : le fichier n'en porte pas (« sans origine », jamais « au début »).
        double origine = -1.0;
    };
    /// D628.1 : l'origine d'un fichier lu (sa référence de temps BWF / sa fréquence), ou −1 s'il n'a pas de bloc `bext`.
    static double origineDuFichier(const juce::AudioFormatReader& lecteur);

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
    /// D629.1 : « 3.000 s », ou « — » pour un fichier sans origine (ou absent) — la colonne et le relevé.
    static juce::String texteOrigine(const Ligne& l);
    static juce::String nomDeLEtat(Etat etat);

    std::function<void()> onNettoyer, onRafraichir;
    /// D608.1 : « Relier... » sur une ligne MANQUANT choisie — le chemin manquant, tel que le projet l'écrit.
    std::function<void(const juce::String&)> onRelier;
    /// Choisit la ligne de ce fichier (le verbe de banc `reserve-choisir:`) ; faux s'il n'y est pas. D630.1 : `aussi` —
    /// l'AJOUTE au choix, comme un Ctrl+clic (le verbe `reserve-choisir-aussi:`).
    bool choisir(const juce::String& fichier, bool aussi = false);
    juce::String fichierChoisi() const;
    /// D630.1 : toutes les lignes choisies, dans l'ordre de la liste.
    juce::StringArray fichiersChoisis() const;
    int nombreChoisies() const { return liste_.getNumSelectedRows(); }
    bool relierActif() const { return relier_.isEnabled(); }
    /// D618.1 : « Poser à la tête » sur une ligne choisie dont le fichier est sur le disque — le chemin, tel que le projet
    /// l'écrit ou que le dossier le contient.
    /// D630.1 : TOUTES les lignes choisies, dans l'ordre de la liste — un geste.
    std::function<void(const juce::StringArray&)> onPoser;
    bool poserActif() const { return poser_.isEnabled(); }
    /// D628.1 : « Poser à l'origine » sur une ligne choisie dont le fichier est sur le disque et porte une origine.
    std::function<void(const juce::StringArray&)> onPoserOrigine;   // D630.1 : toutes les lignes choisies
    bool poserOrigineActif() const { return poserOrigine_.isEnabled(); }
    /// L'origine de la ligne choisie (secondes), ou −1 : aucune ligne, ou un fichier sans origine.
    double origineChoisie() const;
    /// D617.1 : « Chercher dans un dossier... » — actif dès qu'une ligne MANQUE.
    std::function<void()> onChercher;
    bool chercherActif() const { return chercher_.isEnabled(); }
    /// D617.1 : ce que la dernière recherche a trouvé (les ambigus et les introuvables nommés) ; vide : rien à dire.
    void setRemarque(const juce::String& texte);
    juce::String remarque() const { return remarque_.getText(); }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void selectedRowsChanged(int derniere) override;   // D608.1 : « Relier... » suit la ligne choisie
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    /// Les bords des sept colonnes pour une largeur donnée — la même règle pour l'en-tête et les lignes (D629.1 : sept).
    static std::vector<juce::Range<int>> colonnes(int largeur);

    std::vector<Ligne> lignes_;
    bool sansDossier_ = false;
    juce::Rectangle<int> enTete_;
    juce::Label titre_, total_, remarque_;
    juce::TextButton nettoyer_, rafraichir_, relier_, chercher_, poser_, poserOrigine_;
    juce::ListBox liste_ { "reserve", this };
};

} // namespace vsm::app::ui
