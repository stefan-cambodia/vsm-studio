// Vérifie, SANS interface ni carte son, ce que l'application ferait d'un
// fichier d'enregistrement d'origine.
//
// Pourquoi cet outil existe : le chargement de la référence est du code
// d'interface, et l'interface ne se teste pas sans écran. Or ce qu'on veut
// savoir de lui -- « ce MP3 est-il décodé, à quelle fréquence, en combien de
// canaux, sur quelle durée » -- ne demande aucune fenêtre. Le même code que
// celui du menu Fichier est appelé ici, sur le même fichier, et il imprime ce
// qu'il a compris. C'est la même raison d'être que `vsm-panel-preview` : ce
// qu'on ne peut pas regarder, on ne peut pas le juger.
//
//     ./vsm-audio-import-check morceau.mp3 [autre.flac ...]
//     ./vsm-audio-import-check --wav sortie.wav morceau.mp3
//
// Sortie : une ligne par fichier, et un code de retour non nul si l'un d'eux
// a été refusé. Avec `--wav`, le tampon décodé est ÉCRIT tel quel : c'est le
// seul moyen de vérifier qu'un décodeur ne décale pas le son (un MP3 porte un
// silence d'amorce que tous les décodeurs ne retirent pas de la même façon),
// et un décalage sur la piste de référence ferait paraître fausse une
// reconstruction juste.

#include "audio/ReferenceAudioLoader.h"

#include <cmath>
#include <cstdio>

namespace {

/// Écrit le tampon décodé, pour pouvoir le comparer ailleurs.
bool ecrireWav(const juce::File& sortie, const vsm::audio::io::SampleBuffer& tampon) {
    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> flux(sortie.createOutputStream().release());
    if (flux == nullptr) return false;
    flux->setPosition(0);
    flux->truncate();
    const int canaux = tampon.isStereo() ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> ecrivain(
        format.createWriterFor(flux.get(), tampon.sampleRate, static_cast<unsigned>(canaux), 24, {}, 0));
    if (ecrivain == nullptr) return false;
    flux.release();   // l'écrivain possède le flux désormais

    juce::AudioBuffer<float> bloc(canaux, static_cast<int>(tampon.numFrames()));
    bloc.copyFrom(0, 0, tampon.left.data(), static_cast<int>(tampon.numFrames()));
    if (canaux > 1) bloc.copyFrom(1, 0, tampon.right.data(), static_cast<int>(tampon.numFrames()));
    return ecrivain->writeFromAudioSampleBuffer(bloc, 0, bloc.getNumSamples());
}

} // namespace

int main(int argc, char* argv[]) {
    juce::File sortieWav;
    int premier = 1;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--wav") {
        sortieWav = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
        premier = 3;
    }
    if (argc <= premier) {
        std::printf("Usage : vsm-audio-import-check [--wav sortie.wav] <fichier audio> [...]\n"
                    "Formats acceptes : %s\n"
                    "Filtre du selecteur : %s\n",
                    vsm::app::referenceAudioFormatList().toRawUTF8(),
                    vsm::app::referenceAudioFilePatterns().toRawUTF8());
        return 2;
    }

    int echecs = 0;
    for (int i = premier; i < argc; ++i) {
        const juce::File fichier = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile(juce::String::fromUTF8(argv[i]));
        const auto resultat = vsm::app::loadReferenceAudioFile(fichier);

        if (!resultat.success || resultat.buffer.empty()) {
            std::printf("REFUSE   %s\n         %s\n",
                        fichier.getFileName().toRawUTF8(),
                        resultat.error.toRawUTF8());
            ++echecs;
            continue;
        }

        // La crête sert de preuve de vie : un décodeur qui rend le bon nombre
        // d'échantillons mais du silence passerait sinon pour un succès.
        float crete = 0.0f;
        for (float v : resultat.buffer.left) crete = std::max(crete, std::abs(v));
        for (float v : resultat.buffer.right) crete = std::max(crete, std::abs(v));

        const double duree = static_cast<double>(resultat.buffer.numFrames())
                           / juce::jmax(1.0, resultat.buffer.sampleRate);
        std::printf("OK       %s\n"
                    "         decodeur %s, %.1f kHz, %s, %d:%02d, %zu echantillons, crete %.3f\n",
                    fichier.getFileName().toRawUTF8(),
                    resultat.decoder.toRawUTF8(),
                    resultat.buffer.sampleRate / 1000.0,
                    resultat.buffer.isStereo() ? "stereo" : "mono",
                    static_cast<int>(duree) / 60, static_cast<int>(duree) % 60,
                    resultat.buffer.numFrames(), crete);

        if (sortieWav != juce::File()) {
            const bool ecrit = ecrireWav(sortieWav, resultat.buffer);
            std::printf("         %s %s\n", ecrit ? "ecrit dans" : "ECHEC d'ecriture dans",
                        sortieWav.getFullPathName().toRawUTF8());
            if (!ecrit) ++echecs;
        }
    }
    return echecs == 0 ? 0 : 1;
}
