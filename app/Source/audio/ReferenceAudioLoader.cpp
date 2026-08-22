#include "ReferenceAudioLoader.h"

#include <limits>
#include <new>

namespace vsm::app {
namespace {

/// Le gestionnaire de formats, construit une fois.
///
/// `registerBasicFormats()` inscrit ce que JUCE sait décoder d'après ses
/// options de compilation : WAV et AIFF toujours, FLAC et Ogg Vorbis par
/// défaut, MP3 seulement si `JUCE_USE_MP3AUDIOFORMAT=1` (voir
/// `app/CMakeLists.txt`, où le choix est fait et justifié). Tout le reste de ce
/// fichier INTERROGE ce gestionnaire au lieu d'écrire une liste en dur : une
/// option de compilation qui changerait ferait autrement mentir le sélecteur de
/// fichiers, qui proposerait un format que l'application refuserait ensuite.
juce::AudioFormatManager& formatManager() {
    struct Holder {
        juce::AudioFormatManager manager;
        Holder() { manager.registerBasicFormats(); }
    };
    static Holder holder;
    return holder.manager;
}

/// Décodage par JUCE. Rend un message d'échec plutôt qu'un tampon vide.
ReferenceAudioResult decodeWithJuce(const juce::File& file) {
    ReferenceAudioResult result;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager().createReaderFor(file));
    if (reader == nullptr) {
        result.error = "format non reconnu (formats acceptes : "
                     + referenceAudioFormatList() + ")";
        return result;
    }

    const juce::int64 frames = reader->lengthInSamples;
    if (frames <= 0) {
        result.error = "fichier sans echantillon";
        return result;
    }
    if (frames > static_cast<juce::int64>(std::numeric_limits<int>::max())) {
        result.error = "enregistrement trop long pour etre charge d'un bloc";
        return result;
    }
    if (reader->sampleRate <= 0.0) {
        // Une fréquence nulle rendrait la lecture indéterminée : la piste de
        // référence rééchantillonne d'après ce champ, et le mensonge ne se
        // verrait qu'à l'oreille, sous forme de transposition.
        result.error = "frequence d'echantillonnage absente ou invalide";
        return result;
    }

    // Deux canaux au maximum, comme le lecteur WAV du moteur : une piste de
    // référence est stéréo, et replier six canaux d'un 5.1 en silence serait
    // pire que de dire qu'on n'en garde que deux.
    const int channels = static_cast<int>(juce::jmin(reader->numChannels, 2u));
    if (channels <= 0) {
        result.error = "fichier sans canal audio";
        return result;
    }

    try {
        juce::AudioBuffer<float> decoded(channels, static_cast<int>(frames));
        // Cette surcharge convertit elle-même l'entier en float quand le
        // format n'est pas déjà flottant : c'est le point où l'on paie la
        // conversion UNE fois, hors du thread audio.
        reader->read(&decoded, 0, static_cast<int>(frames), 0, true, channels > 1);

        const auto* gauche = decoded.getReadPointer(0);
        result.buffer.left.assign(gauche, gauche + frames);
        if (channels > 1) {
            const auto* droite = decoded.getReadPointer(1);
            result.buffer.right.assign(droite, droite + frames);
        }
    } catch (const std::bad_alloc&) {
        // Pas de plafond arbitraire sur la durée : c'est la mémoire de la
        // machine qui tranche, et elle le dit ici plutôt que d'emporter
        // l'application.
        result.error = "enregistrement trop long pour tenir en memoire ("
                     + juce::String(frames / juce::jmax(1.0, reader->sampleRate) / 60.0, 1)
                     + " min)";
        return result;
    }

    result.buffer.sampleRate = reader->sampleRate;
    result.buffer.sourcePath = file.getFullPathName().toStdString();
    result.decoder = reader->getFormatName();
    result.success = true;
    return result;
}

} // namespace

juce::String referenceAudioFilePatterns() {
    return formatManager().getWildcardForAllFormats();
}

juce::String referenceAudioFormatList() {
    juce::StringArray noms;
    for (const auto* format : formatManager()) {
        noms.addIfNotAlreadyThere(format->getFormatName());
    }
    return noms.joinIntoString(", ");
}

ReferenceAudioResult loadReferenceAudioFile(const juce::File& file) {
    const bool ressembleAUnWav = file.getFileExtension().equalsIgnoreCase(".wav");

    if (ressembleAUnWav) {
        auto natif = vsm::audio::io::WavFileReader::readFile(file.getFullPathName().toStdString());
        if (natif.success && !natif.buffer.empty()) {
            ReferenceAudioResult result;
            result.success = true;
            result.buffer = std::move(natif.buffer);
            result.buffer.sourcePath = file.getFullPathName().toStdString();
            result.decoder = "WAV natif";
            return result;
        }

        // Le lecteur du moteur est STRICT par choix, et il a raison de l'être.
        // Mais son refus ne doit pas devenir le refus de l'application : un WAV
        // qu'il ne sait pas lire peut très bien être un WAV compressé que JUCE
        // décode. On essaie donc, et si JUCE échoue à son tour, le message
        // porte les DEUX refus -- celui qui explique vraiment est parfois le
        // premier.
        auto parJuce = decodeWithJuce(file);
        if (!parJuce.success) {
            const juce::String premier = natif.error.empty()
                ? juce::String("fichier sans echantillon")
                : juce::String::fromUTF8(natif.error.c_str());
            parJuce.error = "lecteur WAV du moteur : " + premier
                          + "\nlecteur JUCE : " + parJuce.error;
        }
        return parJuce;
    }

    return decodeWithJuce(file);
}

} // namespace vsm::app
