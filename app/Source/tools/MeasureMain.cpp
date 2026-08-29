// Mesure un fichier audio EXACTEMENT comme le mixeur de l'application le
// mesure : crête, valeur efficace, LUFS intégré, corrélation de phase
// (étape D4.7 de docs/ROADMAP-daw.md).
//
// POURQUOI CET OUTIL EXISTE. Le critère de la phase D4 est que « le mixage fait
// dans l'application et le mixage fait par `analyse/` sur les mêmes stems
// donnent le même LUFS à 0,1 près ». Deux moitiés d'un projet qui mesurent
// différemment ne peuvent pas se comparer : la reconstruction paraîtrait
// meilleure ou pire qu'elle n'est, selon celle des deux qu'on croit. Il faut
// donc un point de comparaison, et il doit employer LE code du mixeur -- pas
// une redite écrite pour l'occasion, qui ne prouverait que sa propre justesse.
//
//     ./vsm-measure fichier.wav [autre.wav ...]
//
// Sortie : une ligne par fichier, en JSON d'une ligne, pour que la chaîne
// Python la lise sans avoir à analyser du texte libre.

#include "vsm/audio/dsp/LufsMeter.h"
#include "vsm/audio/engine/Mixer.h"

#include <JuceHeader.h>

#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage : vsm-measure fichier.wav [autre.wav ...]\n");
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    int echecs = 0;
    for (int a = 1; a < argc; ++a) {
        const juce::File fichier(juce::File::getCurrentWorkingDirectory()
                                      .getChildFile(juce::String(argv[a])));
        std::unique_ptr<juce::AudioFormatReader> lecteur(formats.createReaderFor(fichier));
        if (lecteur == nullptr) {
            std::printf("{\"file\":\"%s\",\"error\":\"illisible\"}\n", argv[a]);
            ++echecs;
            continue;
        }

        const int64_t trames = lecteur->lengthInSamples;
        const int canaux = static_cast<int>(lecteur->numChannels);
        juce::AudioBuffer<float> tampon(std::max(2, canaux), static_cast<int>(trames));
        tampon.clear();
        lecteur->read(&tampon, 0, static_cast<int>(trames), 0, true, canaux > 1);
        // Un fichier MONO se mesure comme un stéréo dont les deux canaux sont
        // identiques : c'est ce que fait le moteur d'une piste mono, et c'est
        // aussi ce qui donne une corrélation de +1 plutôt qu'une division par
        // zéro.
        if (canaux == 1) tampon.copyFrom(1, 0, tampon, 0, 0, static_cast<int>(trames));

        const float* gauche = tampon.getReadPointer(0);
        const float* droite = tampon.getReadPointer(1);

        double sommeL2 = 0.0, sommeR2 = 0.0;
        float crete = 0.0f;
        vsm::audio::dsp::LufsMeter lufs;
        lufs.prepare(lecteur->sampleRate);
        for (int64_t i = 0; i < trames; ++i) {
            const float l = gauche[i], r = droite[i];
            crete = std::max(crete, std::max(std::abs(l), std::abs(r)));
            sommeL2 += static_cast<double>(l) * l;
            sommeR2 += static_cast<double>(r) * r;
            lufs.processStereo(l, r);
        }
        const double rms = trames > 0 ? std::sqrt((sommeL2 + sommeR2) / (2.0 * trames)) : 0.0;
        const float correlation = vsm::audio::engine::phaseCorrelation(
            gauche, droite, static_cast<int>(trames));

        std::printf("{\"file\":\"%s\",\"sampleRate\":%.0f,\"frames\":%lld,"
                     "\"peak\":%.9f,\"rms\":%.9f,\"lufs\":%.6f,\"correlation\":%.9f}\n",
                     argv[a], lecteur->sampleRate, static_cast<long long>(trames),
                     static_cast<double>(crete), rms, lufs.integratedLufs(),
                     static_cast<double>(correlation));
    }
    return echecs == 0 ? 0 : 1;
}
