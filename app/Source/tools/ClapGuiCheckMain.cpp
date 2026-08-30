// OUVRIR LA FAÇADE NATIVE D'UN PLUGIN CLAP, POUR DE BON (D7.4).
//
// POURQUOI CET OUTIL EXISTE, ET POURQUOI IL EST LA CONDITION DE L'ÉTAPE.
// D7.4 avait DIFFÉRÉ la façade CLAP avec ce motif, écrit noir sur blanc :
// « livrer cent cinquante lignes d'incrustation de fenêtre que personne n'a
// jamais vues tourner, en les déclarant faites, est exactement ce que ce projet
// refuse ailleurs. » La lever demande donc de l'ouvrir, pas de la compiler.
//
// CE QU'IL FAIT : charge un `.clap`, en fabrique la façade par le MÊME chemin
// que le menu de l'application (`vsm::clap::createEditorFor`), la pose dans une
// fenêtre, fait tourner la boucle de messages quelques secondes — ce qui laisse
// les minuteries du plugin battre et sa demande d'agrandissement arriver — puis
// referme et sort.
//
//   vsm-clap-gui-check <fichier.clap> [secondes]
//
// Un code de sortie non nul veut dire quelque chose de précis, et jamais
// « ça n'a pas marché » tout court : voir les messages.

#include <JuceHeader.h>
#include "ClapPluginHost.h"
#include "ClapPluginWindow.h"
#include <cstdio>
#include <cstdlib>
#include "vsm/interchange/NumberText.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) {
        std::fprintf(stderr, "Usage : vsm-clap-gui-check <fichier.clap> [secondes]\n");
        return 1;
    }
    const std::string chemin = argv[1];
    const double secondes = argc >= 3 ? vsm::interchange::numberFromTextOr(argv[2], 4.0) : 4.0;

    std::string erreur;
    auto instrument = vsm::clap::createClapInstrument(chemin, "", erreur);
    if (!instrument) {
        std::fprintf(stderr, "chargement impossible : %s\n", erreur.c_str());
        return 2;
    }
    std::printf("chargé : %s\n", instrument->machineName());

    // ON DEMANDE D'ABORD S'IL Y EN A UNE. Un plugin sans interface n'est pas
    // une panne -- la plupart des machines du parc n'en ont pas de native --
    // et le distinguer d'un échec d'ouverture est tout l'intérêt de la
    // question.
    if (!vsm::clap::hasNativeEditor(*instrument)) {
        std::printf("ce plugin n'a pas d'interface incrustable en X11.\n");
        return 3;
    }

    auto facade = vsm::clap::createEditorFor(*instrument);
    if (facade == nullptr) {
        std::fprintf(stderr, "l'interface s'est annoncée puis n'a pas pu être créée.\n");
        return 4;
    }
    const int largeurInitiale = facade->getWidth();
    const int hauteurInitiale = facade->getHeight();
    std::printf("taille demandée par le plugin : %d x %d\n", largeurInitiale, hauteurInitiale);

    juce::Component* observee = facade.get();
    // `fromUTF8`, comme partout ailleurs (§ 6 bis bis d'ARCHITECTURE.md) : le
    // constructeur ordinaire de `juce::String` lit du Latin-1, et « Façade »
    // sortait « FaÃ§ade » dans la barre de titre.
    auto fenetre = std::make_unique<juce::DocumentWindow>(
        juce::String::fromUTF8(u8"Façade CLAP — ")
            + juce::String::fromUTF8(instrument->machineName()),
        juce::Colours::black, juce::DocumentWindow::closeButton);
    fenetre->setUsingNativeTitleBar(true);
    fenetre->setResizable(true, false);
    fenetre->setContentOwned(facade.release(), true);
    fenetre->centreWithSize(fenetre->getWidth(), fenetre->getHeight());
    fenetre->setVisible(true);

    // LA BOUCLE DE MESSAGES TOURNE VRAIMENT : c'est elle qui fait battre les
    // minuteries que le plugin a demandées, donc qui fait bouger son dessin.
    // Sans elle, on aurait ouvert une fenêtre et rien vu vivre dedans.
    // `runDispatchLoopUntil` n'existe que dans les builds JUCE_MODAL_LOOPS
    // autorisés ; on tourne donc à la main, ce qui revient au même et ne
    // dépend d'aucun réglage de compilation.
    const double echeance = juce::Time::getMillisecondCounterHiRes() + secondes * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < echeance) {
        if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(20)) break;
    }

    const int largeurFinale = observee->getWidth();
    const int hauteurFinale = observee->getHeight();
    std::printf("taille après %.1f s : %d x %d\n", secondes, largeurFinale, hauteurFinale);
    // LE PLUGIN D'ESSAI DEMANDE À GRANDIR AU BOUT DE DEUX SECONDES. Si la
    // taille n'a pas bougé, `clap_host_gui->request_resize` n'est pas arrivé,
    // ou les minuteries ne battent pas -- deux pannes qu'une capture d'écran
    // ne distinguerait pas.
    const bool aGrandi = largeurFinale != largeurInitiale || hauteurFinale != hauteurInitiale;
    std::printf("%s\n", aGrandi
                            ? "le plugin a demandé un redimensionnement, et l'a obtenu."
                            : "aucun redimensionnement demandé par le plugin.");

    fenetre->setVisible(false);
    fenetre.reset();
    std::printf("façade fermée sans incident.\n");
    return 0;
}
