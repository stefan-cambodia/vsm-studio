// Vérifie, SANS carte son ni interface, que l'enregistrement audio en flux tient
// la distance et rend EXACTEMENT ce qu'on lui a donné (étape D3.4 de
// docs/ROADMAP-daw.md).
//
// Pourquoi cet outil existe. Le critère de D3.4 est « 10 minutes s'enregistrent
// sans décrochage ; le fichier est relu tel quel ». Ce n'est pas une propriété
// qu'on peut lire dans le code : elle dépend du disque, de la taille du tampon
// et de la vitesse du thread d'écriture. Elle ne demande en revanche NI micro NI
// écran -- on peut fabriquer le signal d'entrée. C'est la même raison d'être que
// `vsm-audio-import-check` : ce qu'on ne peut pas mesurer, on ne peut pas le
// promettre.
//
//     ./vsm-disk-record-check [secondes] [taille-de-bloc] [frequence] [vitesse]
//
// Le programme fabrique un signal reconnaissable échantillon par échantillon,
// le pousse par blocs comme le ferait le rappel audio, ferme le fichier, le
// relit et compare. Code de retour non nul à la première différence.
//
// LA VITESSE, ET POURQUOI ELLE N'EST PAS INFINIE. Pousser les blocs aussi vite
// que la machine le permet ne mesure rien d'utile : aucun producteur réel n'est
// plus rapide que la carte son, et un tampon d'une seconde déborde forcément
// face à un producteur illimité -- ce qui dirait seulement qu'on a écrit une
// boucle sans frein. On pousse donc à un MULTIPLE du temps réel (vingt fois par
// défaut) : c'est vingt fois plus dur que l'usage, ce qui laisse une marge
// honnête, et dix minutes d'audio se vérifient en trente secondes.

#include "audio/DiskRecorder.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include "vsm/interchange/NumberText.h"

namespace {

/// Un signal dont CHAQUE échantillon est différent des autres, pour qu'un
/// décalage d'une seule trame se voie. Une sinusoïde pure ne le permettrait
/// pas : elle se répète.
float echantillon(int64_t index, int canal) {
    const double t = static_cast<double>(index);
    const double base = std::sin(t * 0.0007 + canal * 1.7) * 0.6
                        + std::sin(t * 0.000031) * 0.35;
    return static_cast<float>(base);
}

} // namespace

int main(int argc, char** argv) {
    const double secondes   = argc >= 2 ? vsm::interchange::numberFromTextOr(argv[1], 10.0) : 10.0;
    const int    bloc       = argc >= 3 ? std::atoi(argv[2]) : 512;
    const double frequence  = argc >= 4 ? vsm::interchange::numberFromTextOr(argv[3], 48000.0) : 48000.0;
    const double vitesse    = argc >= 5 ? vsm::interchange::numberFromTextOr(argv[4], 20.0) : 20.0;
    const int    canaux     = 2;

    if (secondes <= 0.0 || bloc <= 0 || frequence <= 0.0 || vitesse <= 0.0) {
        std::printf("Usage : vsm-disk-record-check [secondes] [taille-de-bloc] "
                     "[frequence] [vitesse]\n");
        return 2;
    }

    const juce::File fichier = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("vsm-disk-record-check.wav");
    DiskRecorder enregistreur;
    juce::String erreur;
    if (!enregistreur.start(fichier, frequence, canaux, erreur)) {
        std::printf("ECHEC ouverture : %s\n", erreur.toRawUTF8());
        return 1;
    }

    const int64_t total = static_cast<int64_t>(secondes * frequence);
    std::vector<float> gauche(static_cast<size_t>(bloc), 0.0f);
    std::vector<float> droite(static_cast<size_t>(bloc), 0.0f);
    const float* canauxEcrits[2] = { gauche.data(), droite.data() };

    const double debut = juce::Time::getMillisecondCounterHiRes();
    int64_t ecrits = 0;
    while (ecrits < total) {
        const int n = static_cast<int>(std::min<int64_t>(bloc, total - ecrits));
        for (int i = 0; i < n; ++i) {
            gauche[static_cast<size_t>(i)] = echantillon(ecrits + i, 0);
            droite[static_cast<size_t>(i)] = echantillon(ecrits + i, 1);
        }
        enregistreur.write(canauxEcrits, n);
        ecrits += n;

        // CADENCE. On attend juste ce qu'il faut pour ne pas dépasser la vitesse
        // demandée, et seulement quand l'avance dépasse deux millisecondes --
        // dormir à chaque bloc coûterait plus que le test lui-même.
        const double duAudio = static_cast<double>(ecrits) / frequence / vitesse;
        const double duReel = (juce::Time::getMillisecondCounterHiRes() - debut) * 0.001;
        const double avance = duAudio - duReel;
        if (avance > 0.002) juce::Thread::sleep(static_cast<int>(avance * 1000.0));
    }
    const int64_t trames = enregistreur.stop();
    const double duree = (juce::Time::getMillisecondCounterHiRes() - debut) * 0.001;
    const uint64_t perdus = enregistreur.droppedBlocks();

    std::printf("Ecrit  : %.1f s (%lld trames, %d canaux, %.0f Hz) en %.2f s reelles"
                 " -- soit %.1fx le temps reel\n",
                secondes, static_cast<long long>(trames), canaux, frequence, duree,
                duree > 0.0 ? secondes / duree : 0.0);
    std::printf("Perdus : %llu bloc(s)\n", static_cast<unsigned long long>(perdus));

    if (perdus > 0) {
        std::printf("ECHEC : le disque n'a pas suivi -- le fichier a des trous.\n");
        return 1;
    }
    if (trames != total) {
        std::printf("ECHEC : %lld trames ecrites pour %lld attendues.\n",
                    static_cast<long long>(trames), static_cast<long long>(total));
        return 1;
    }

    // RELECTURE. C'est la seule moitié qui compte vraiment : un fichier écrit
    // sans erreur mais décalé d'une trame, ou tronqué, serait un enregistrement
    // faux, et rien dans l'écriture ne l'aurait signalé.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> lecteur(formats.createReaderFor(fichier));
    if (lecteur == nullptr) {
        std::printf("ECHEC : fichier illisible.\n");
        return 1;
    }
    std::printf("Relu   : %lld trames, %u canaux, %.0f Hz, %d bits\n",
                static_cast<long long>(lecteur->lengthInSamples), lecteur->numChannels,
                lecteur->sampleRate, static_cast<int>(lecteur->bitsPerSample));

    if (lecteur->lengthInSamples != total) {
        std::printf("ECHEC : le fichier fait %lld trames au lieu de %lld.\n",
                    static_cast<long long>(lecteur->lengthInSamples),
                    static_cast<long long>(total));
        return 1;
    }

    // Vingt-quatre bits : la marge de comparaison est celle du quantum, pas
    // celle du flottant. 2^-23 vaut 1,2e-7 ; on tolère deux fois cela.
    constexpr float kTolerance = 2.5e-7f;
    juce::AudioBuffer<float> tampon(canaux, bloc);
    int64_t lu = 0;
    double ecartMax = 0.0;
    int64_t pireIndex = -1;
    while (lu < total) {
        const int n = static_cast<int>(std::min<int64_t>(bloc, total - lu));
        lecteur->read(&tampon, 0, n, lu, true, true);
        for (int c = 0; c < canaux; ++c) {
            const float* donnees = tampon.getReadPointer(c);
            for (int i = 0; i < n; ++i) {
                const double ecart = std::abs(donnees[i] - echantillon(lu + i, c));
                if (ecart > ecartMax) { ecartMax = ecart; pireIndex = lu + i; }
            }
        }
        lu += n;
    }

    std::printf("Ecart  : %.3e (pire trame %lld, tolerance %.1e)\n",
                ecartMax, static_cast<long long>(pireIndex), kTolerance);
    if (ecartMax > kTolerance) {
        std::printf("ECHEC : le fichier relu n'est pas ce qui a ete ecrit.\n");
        return 1;
    }

    fichier.deleteFile();
    std::printf("OK : %.0f s enregistrees sans perte, relues au quantum pres.\n", secondes);
    return 0;
}
