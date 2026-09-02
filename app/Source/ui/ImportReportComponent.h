#pragma once
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/interchange/DawImport.h"

#include <JuceHeader.h>

namespace vsm::app::ui {

// LE RAPPORT D'IMPORT, MONTRÉ DANS LA FENÊTRE ET NON DANS UNE ALERTE.
//
// POURQUOI UN COMPOSANT PLUTÔT QU'UNE BOÎTE DE MESSAGE. Le rapport d'import
// n'est pas une notification : c'est une PARTIE DU RÉSULTAT, au même titre que
// les notes (§ 0 de docs/CDC-import-daw.md). Il dit ce qui a été repris, ce qui
// a été approché et ce qui a été perdu. Une boîte de message traite ce texte
// comme une nouvelle qu'on chasse d'un clic ; or le musicien en a besoin au
// moment où son morceau ne sonne pas comme chez lui -- une heure plus tard, pas
// à la seconde où il vient de cliquer sur « Importer ».
//
// Trois conséquences de forme, et chacune découle de cette phrase :
//
//   - le panneau DÉFILE, et il REPLIE ses lignes à la largeur disponible. Un
//     projet de trente pistes fait trente lignes, qu'une alerte tronque sans le
//     dire ; et les lignes des lecteurs sont longues à dessein -- elles
//     expliquent, elles ne codent pas -- donc elles doivent se replier plutôt
//     que de fuir hors du cadre ;
//   - les avertissements RESSORTENT, et la couleur vient de la GRAVITÉ que le
//     lecteur a posée sur chaque ligne (DawImportReport::Gravite) -- jamais
//     d'une devinette sur la prose : la première version cherchait « ignor »
//     dans le texte et peignait le récapitulatif en avertissement ;
//   - il vit DANS la fenêtre principale, et non dans une fenêtre flottante.
//     Ce n'est pas un goût : l'autoportrait (VSM_CAPTURE) photographie le
//     composant de contenu, donc ni une alerte asynchrone ni une PanelWindow
//     n'y figurent. Un rapport affiché ailleurs serait un écran qu'on ne peut
//     pas regarder, c'est-à-dire un écran qu'on ne peut pas juger.
//
// L'OVERLAY SE GÈRE SEUL : il suit la taille de son parent par
// `parentSizeChanged` et passe devant en devenant visible. `MainComponent` ne
// le mentionne pas dans son `resized()` -- c'est ce qui réalise vraiment « le
// reste de la disposition ne le connaît pas ».
//
// Il reste consultable après coup par *Fichier ▸ Voir le dernier rapport
// d'import* : le panneau garde son dernier contenu et `reopen()` le remontre.
class ImportReportComponent : public juce::Component {
public:
    ImportReportComponent();

    // Montre un rapport d'import réussi (même partiellement).
    void showReport(const vsm::interchange::DawImportReport& rapport);

    // LE TON D'UNE LIGNE fournie par un client extérieur. Le panneau est né
    // pour le rapport d'IMPORT ; le rapport de RECONSTRUCTION (§ 4.3 de
    // docs/CDC-detection-multipiste.md) est son deuxième vrai client — c'est
    // cette deuxième venue, et pas une envie de généralité, qui justifie
    // cette entrée. `resume` est la ligne de tête, en gras.
    enum class Ton { resume, info, attention, perte };
    struct LigneExterne {
        juce::String texte;
        Ton ton = Ton::info;
    };

    // Montre un rapport composé PAR L'APPELANT : un titre, un sous-titre, des
    // lignes tonalisées. Même défilement, même repli, même bouton Copier.
    void showLines(const juce::String& titre, const juce::String& sousTitre,
                   const juce::Array<LigneExterne>& lignes);

    // Montre un ÉCHEC, avec le message du lecteur EN ENTIER. Celui d'un `.cpr`
    // nomme les deux chemins praticables (Track Archive XML, MIDI Type 1) : le
    // tronquer transformerait une explication utile en une porte fermée.
    void showFailure(const juce::String& titre, const juce::String& message);

    // Remontre le dernier contenu — rapport OU échec. Le panneau garde ce
    // qu'il a affiché plutôt que de le faire regarnir par l'appelant : sinon
    // un `MainComponent` qui ne mémoriserait que le dernier succès rouvrirait
    // un rapport vide après un échec, et ce vide passerait pour un fait.
    void reopen();

    // Vrai dès qu'un rapport a été montré au moins une fois dans la session :
    // c'est ce qui décide si l'entrée de menu « Voir le dernier rapport » a
    // quelque chose à montrer.
    bool hasReport() const { return !source_.isEmpty(); }

    // Le texte du rapport, non replié. Exposé parce que c'est aussi ce que le
    // bouton « Copier » met dans le presse-papiers, et ce qu'un test peut lire
    // sans avoir à déchiffrer une image.
    juce::String reportText() const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Ligne {
        juce::String texte;
        juce::Colour couleur;
        bool titre = false;
    };

    // Le composant intérieur : c'est LUI que le Viewport fait défiler, donc
    // c'est lui qui peint les lignes.
    class Liste : public juce::Component {
    public:
        explicit Liste(const juce::Array<Ligne>& lignes) : lignes_(lignes) {}
        void paint(juce::Graphics&) override;

    private:
        const juce::Array<Ligne>& lignes_;
    };

    juce::Rectangle<int> cadre() const;
    void disposer();
    void reconstruireLaListe();
    void envelopper(const Ligne& source, int largeurMax);
    // Le rituel commun aux trois entrées : mise en page, visibilité, premier
    // plan, clavier (pour qu'Échap ferme vraiment).
    void montrer();
    void fermer();

    juce::String titre_;
    juce::String sousTitre_;
    // `source_` est le rapport tel qu'il a été écrit : une entrée par fait.
    // `lignes_` en est le repli à la largeur du moment, refait quand la
    // largeur change. Garder les deux évite de perdre le texte d'origine --
    // c'est lui qu'on copie, et lui qu'on replierait autrement à la mauvaise
    // largeur.
    juce::Array<Ligne> source_;
    juce::Array<Ligne> lignes_;
    // La largeur du dernier repli : un redimensionnement qui ne la change pas
    // (un drag vertical, par exemple) ne re-mesure rien. -1 = contenu neuf.
    int largeurRepliee_ = -1;
    Liste liste_{lignes_};
    juce::Viewport vue_;
    juce::TextButton fermer_;
    juce::TextButton copier_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportReportComponent)
};

} // namespace vsm::app::ui
