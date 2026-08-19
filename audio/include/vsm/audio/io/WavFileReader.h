#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vsm::audio::io {

/// Un échantillon chargé en mémoire : canaux séparés, toujours en float.
///
/// La conversion vers le float a lieu UNE FOIS, au chargement : le chemin
/// audio ne doit jamais avoir à décoder du 16 ou du 24 bits en pleine lecture.
/// `sampleRate` est celui du FICHIER, conservé tel quel -- c'est au lecteur de
/// rééchantillonner, et il ne peut le faire correctement que s'il sait d'où il
/// part.
struct SampleBuffer {
    std::vector<float> left;
    std::vector<float> right;   ///< vide = mono
    double sampleRate = 44100.0;
    std::string sourcePath;     ///< chemin d'origine, pour l'état sauvegardé

    size_t numFrames() const { return left.size(); }
    bool isStereo() const { return !right.empty(); }
    bool empty() const { return left.empty(); }
};

using SampleBufferPtr = std::shared_ptr<const SampleBuffer>;

/// Lecteur WAV : PCM entier 8/16/24/32 bits et IEEE float 32/64 bits, mono ou
/// multicanal (au-delà de deux canaux, seuls les deux premiers sont conservés
/// -- une machine stéréo n'a rien à faire des six canaux d'un fichier 5.1, et
/// les mélanger silencieusement serait pire).
///
/// STRICT PAR CHOIX : un fichier tronqué, un en-tête incohérent ou un format
/// compressé sont REFUSÉS avec un message, jamais interprétés au mieux. Un
/// échantillon lu de travers produirait un bruit que personne ne rattacherait
/// au fichier fautif.
class WavFileReader {
public:
    struct Result {
        bool success = false;
        SampleBuffer buffer;
        std::string error;
    };

    static Result read(const std::vector<uint8_t>& bytes);
    static Result readFile(const std::string& path);
};

} // namespace vsm::audio::io
