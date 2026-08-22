#pragma once
#include <JuceHeader.h>
#include "vsm/audio/io/WavFileReader.h"

// Chargement de l'ENREGISTREMENT D'ORIGINE (piste de référence A/B) dans les
// formats courants : WAV, AIFF, FLAC, Ogg Vorbis et MP3.
//
// POURQUOI CE CODE EST DANS `app/` ET NON DANS `audio/`. Le moteur audio n'a
// aucune dépendance externe, et c'est une règle absolue : `WavFileReader` sait
// lire un WAV parce qu'un WAV se décode avec quelques lignes de code et aucune
// bibliothèque. Un FLAC ou un MP3, non. Décoder ces formats demande un codec,
// et le seul dont ce dépôt dispose déjà est celui de JUCE -- qui vit dans la
// couche interface. Le décodage a donc lieu ICI, et le moteur reçoit ce qu'il
// a toujours reçu : un `SampleBuffer` en float, déjà décodé.
//
// CONSÉQUENCE ASSUMÉE : `vsm-render` et les tests, qui ne connaissent pas
// JUCE, restent limités au WAV. C'est cohérent -- ce sont des chemins de
// calcul, pas des points d'entrée d'utilisateur ; c'est l'interface qui reçoit
// des fichiers venus du disque de quelqu'un.
namespace vsm::app {

/// Résultat d'un chargement, avec de quoi le raconter en cas d'échec.
struct ReferenceAudioResult {
    bool success = false;
    vsm::audio::io::SampleBuffer buffer;
    juce::String error;
    /// Qui a décodé le fichier : « WAV natif » ou le nom du format JUCE
    /// (« MP3 file », « FLAC file »...). Affiché après un chargement réussi :
    /// savoir par où le son est passé fait partie de savoir ce qu'on écoute.
    juce::String decoder;
};

/// Filtre du sélecteur de fichiers, sous la forme attendue par juce::FileChooser.
juce::String referenceAudioFilePatterns();

/// Liste lisible des formats acceptés, pour les messages d'erreur.
juce::String referenceAudioFormatList();

/// Charge un fichier en mémoire.
///
/// Un WAV passe D'ABORD par `WavFileReader`, le lecteur du moteur : c'est le
/// chemin déjà testé, et ses refus sont explicites. Si celui-ci refuse -- un
/// WAV compressé, un en-tête exotique --, JUCE est essayé à son tour plutôt que
/// d'abandonner, et le message final porte les deux refus. Tout autre format va
/// directement à JUCE.
ReferenceAudioResult loadReferenceAudioFile(const juce::File& file);

} // namespace vsm::app
