#pragma once
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/audio/io/WavFileWriter.h"
#include <string>
#include <vector>

// Reconstruction hors ligne (Phase 7, P8) : `dossier de projet -> render.wav`,
// sans interface, sans carte son, sans réseau.
//
// C'est la moitié DAW de la boucle décrite au § 9 de la roadmap :
//
//   original.wav -> Python -> (MIDI + presets + project.json)
//                -> VSM -> ProcessGraph -> OfflineRenderer -> reconstructed.wav
//                -> Python (comparaison, ajustement, on recommence)
//
// Le rendu passe par le MÊME `ProcessGraph::processBlock()` que la lecture
// temps réel (ARCHITECTURE.md § 5) : ce qu'on obtient ici est exactement ce
// qu'on entendrait dans l'application, à l'échantillon près. Sans cette
// propriété, une boucle d'optimisation pilotée par Python optimiserait un son
// que personne n'entend jamais.

namespace vsm::interchange {

struct RenderOptions {
    double sampleRate = 48000.0;
    int blockSize = 512;
    /// Durée ajoutée après la dernière note, pour laisser les résonances et
    /// les queues de réverbération s'éteindre au lieu d'être coupées net.
    double tailSeconds = 2.0;
    /// 0 = déduire du contenu du projet (dernière note + queue).
    double durationSeconds = 0.0;
    vsm::audio::io::SampleFormat format = vsm::audio::io::SampleFormat::Float32;
};

struct RenderResult {
    bool success = false;
    std::string error;
    double renderedSeconds = 0.0;
    size_t framesWritten = 0;
    float peakLevel = 0.0f;
    /// Pistes réellement sonorisées / pistes du projet : un projet dont
    /// l'instrument manque rend du silence, et doit le dire.
    size_t tracksWithInstrument = 0;
    size_t trackCount = 0;
    std::vector<std::string> warnings;

    std::string summary() const;
};

/// Rend un projet chargé vers un fichier WAV. Applique les presets sémantiques
/// aux machines avant de rendre ; chaque paramètre non pris en charge par la
/// machine cible est rapporté (jamais appliqué en douce).
RenderResult renderBundleToWav(const LoadedBundle& bundle, const std::string& wavPath,
                                const RenderOptions& options = {});

/// Enchaîne chargement et rendu -- la fonction qu'appelle l'outil en ligne de
/// commande, et celle qu'un script Python pilote en pratique.
RenderResult renderProjectFolderToWav(const std::string& folderPath, const std::string& wavPath,
                                       const RenderOptions& options = {});

} // namespace vsm::interchange
