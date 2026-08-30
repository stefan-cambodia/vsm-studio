// Rend les PANNEAUX de l'application en PNG, SANS écran.
//
// MÊME RAISON D'ÊTRE QUE `vsm-panel-preview`, appliquée ailleurs : une fenêtre
// qu'on ne regarde jamais est une fenêtre qu'on ne peut pas juger. Les tests
// disent que la logique est juste — que la table des raccourcis les contient
// tous, que l'inventaire trouve ce qu'il doit — ils ne diront jamais si une
// colonne déborde, si un libellé est tronqué, ou si la page est illisible.
//
// LES QUATRE PANNEAUX DE D10 SONT LES PREMIERS À EN AVOIR BESOIN, parce qu'ils
// ont été écrits d'affilée sans jamais être ouverts. Ce sont des composants
// ordinaires : on les remplit avec des données représentatives et on les
// dessine dans une image, exactement comme une façade de machine.
//
//   vsm-ui-preview <dossier-de-sortie> [échelle]
//
// L'ÉCHELLE reproduit hors écran le réglage « Taille de l'interface » (150 %
// par défaut dans l'application, voir ui/UiScale.h) : c'est à cette taille-là
// qu'il faut juger, puisque c'est celle à laquelle on travaille.

#include <JuceHeader.h>
#include "../ui/BrowserComponent.h"
#include "../ui/MidiLearnWindow.h"
#include "../ui/PreferencesWindow.h"
#include "../ui/ReconstructionWindow.h"
#include "../ui/ShortcutsWindow.h"
#include "vsm/interchange/MidiLearnStore.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool ecrire(juce::Component& composant, const juce::File& dossier, const juce::String& nom,
             int largeur, int hauteur, double echelle) {
    composant.setBounds(0, 0, largeur, hauteur);
    // `resized()` a déjà eu lieu via setBounds ; certains panneaux ne
    // disposent leur contenu qu'après avoir reçu leurs données, d'où l'appel
    // explicite -- c'est ce que fait une vraie fenêtre à l'affichage.
    composant.resized();

    const int l = static_cast<int>(std::lround(largeur * echelle));
    const int h = static_cast<int>(std::lround(hauteur * echelle));
    juce::Image image(juce::Image::ARGB, l, h, true);
    {
        juce::Graphics g(image);
        if (echelle != 1.0)
            g.addTransform(juce::AffineTransform::scale(static_cast<float>(echelle)));
        composant.paintEntireComponent(g, true);
    }
    const juce::File fichier = dossier.getChildFile(nom + ".png");
    fichier.deleteFile();
    juce::FileOutputStream flux(fichier);
    juce::PNGImageFormat png;
    if (!flux.openedOk() || !png.writeImageToStream(image, flux)) return false;
    std::printf("%s (%d x %d)\n", fichier.getFullPathName().toRawUTF8(), l, h);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) {
        std::fprintf(stderr, "Usage : vsm-ui-preview <dossier-de-sortie> [échelle]\n");
        return 1;
    }
    const juce::File sortie =
        juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    sortie.createDirectory();
    const double echelle = argc >= 3 ? juce::jlimit(0.5, 4.0, std::atof(argv[2])) : 1.5;
    int rendus = 0;

    // --- Raccourcis : la page complète, avec une touche personnalisée pour
    // que le bouton « rétablir » et la mention du défaut apparaissent.
    {
        vsm::interchange::ShortcutTable table;
        table.setKey(vsm::interchange::ShortcutId::TransportPlayStop, "P");
        table.setKey(vsm::interchange::ShortcutId::EditQuantize, "");
        vsm::app::ui::ShortcutsWindow panneau;
        panneau.setBounds(0, 0, 640, 620);
        panneau.resized();
        panneau.setTable(&table);
        rendus += ecrire(panneau, sortie, "raccourcis", 640, 620, echelle) ? 1 : 0;
    }

    // --- Associations MIDI : une liste garnie, et la variante vide, qui est
    // ce que l'utilisateur voit en premier.
    {
        vsm::app::ui::MidiLearnWindow panneau;
        panneau.setBounds(0, 0, 560, 420);
        panneau.resized();
        panneau.setRows({});
        rendus += ecrire(panneau, sortie, "associations-midi-vide", 560, 420, echelle) ? 1 : 0;

        panneau.setRows({
            {74, juce::String::fromUTF8(u8"piste 4 · Cutoff")},
            {71, juce::String::fromUTF8(u8"piste 4 · Resonance")},
            {7,  juce::String::fromUTF8(u8"piste 1 · volume")},
            {10, juce::String::fromUTF8(u8"piste 1 · panoramique")},
            {91, juce::String::fromUTF8(u8"piste 2 · départ A")},
            {64, juce::String::fromUTF8(u8"lecture")},
        });
        rendus += ecrire(panneau, sortie, "associations-midi", 560, 420, echelle) ? 1 : 0;
    }

    // --- Préférences.
    {
        vsm::app::ui::PreferencesWindow panneau;
        panneau.setBounds(0, 0, 560, 430);
        panneau.resized();
        // L'ÉTAT, ET NON UNE PHRASE FABRIQUÉE ICI : c'est le panneau qui
        // compose, donc l'aperçu montre ce que l'application montrera.
        vsm::interchange::ReconstructionChain chaine;
        chaine.available = true;
        chaine.chainFolder = "/home/utilisateur/videcode/muz/vsm-studio/analyse";
        panneau.refresh(1.5f, -1, 8, chaine,
                         "/home/utilisateur/videcode/muz/vsm-studio/analyse",
                         "/home/utilisateur/Sons", 30, 6);
        rendus += ecrire(panneau, sortie, "preferences", 560, 430, echelle) ? 1 : 0;
    }

    // --- Navigateur, garni d'un inventaire représentatif.
    {
        using vsm::interchange::BrowserItem;
        using K = vsm::interchange::BrowserItemKind;
        vsm::app::ui::BrowserComponent panneau;
        panneau.setBounds(0, 0, 620, 560);
        panneau.resized();
        panneau.setItems({
            {K::Machine, "Minimoog-style Monosynth", "vsm.minimoog", "Parc VSM"},
            {K::Machine, "TB-303-style Acid Synth",  "vsm.tb303",    "Parc VSM"},
            {K::Machine, "Juno-106-style Polysynth", "vsm.juno106",  "Parc VSM"},
            {K::Preset,  "TB-303 Acid Lead", "/a/TB-303 Acid Lead.synth.json", "Bibliothèque / basses"},
            {K::Preset,  "sub-ronde",        "/a/sub-ronde.synth.json",        "Bibliothèque / basses"},
            {K::Preset,  "choeur-lointain",  "/a/choeur.synth.json",           "Projet / instruments"},
            {K::Profile, "kit-live",         "/a/kit-live.profile.json",       "Bibliothèque / batterie"},
            {K::Sample,  "caisse-claire",    "/a/caisse-claire.wav",           "Bibliothèque / batterie / caisses"},
            {K::Sample,  "voix-couplet",     "/a/voix.wav",                    "Projet / audio"},
        });
        rendus += ecrire(panneau, sortie, "navigateur", 620, 560, echelle) ? 1 : 0;
    }

    // --- Reconstruction, en cours et terminée.
    {
        vsm::app::ui::ReconstructionWindow panneau;
        panneau.setBounds(0, 0, 720, 460);
        panneau.resized();
        panneau.setSource("Sky and Sand.mp3");
        vsm::app::ReconstructionRunner::Progress avancement;
        avancement.step = 3;
        avancement.stepCount = 5;
        avancement.stepLabel = juce::String::fromUTF8(u8"8 machine(s) candidate(s)");
        avancement.recentLines.add(juce::String::fromUTF8(u8"[1/5] Lecture de Sky and Sand.mp3"));
        avancement.recentLines.add(juce::String::fromUTF8(u8"[2/5] Séparation en stems (htdemucs)"));
        avancement.recentLines.add(juce::String::fromUTF8(u8"      classifieur du 2026-08-12, 34 machines"));
        avancement.recentLines.add(juce::String::fromUTF8(u8"[3/5] 8 machine(s) candidate(s)"));
        avancement.recentLines.add(juce::String::fromUTF8(u8"      bass     : vsm.minimoog réglée PASSE DEVANT"));
        avancement.recentLines.add(juce::String::fromUTF8(u8"      other    : vsm.juno106, 6 axes, 40 évaluations"));
        panneau.setProgress(avancement);
        rendus += ecrire(panneau, sortie, "reconstruction", 720, 460, echelle) ? 1 : 0;
    }

    std::printf("%d panneau(x) rendu(s)\n", rendus);
    return rendus > 0 ? 0 : 2;
}
