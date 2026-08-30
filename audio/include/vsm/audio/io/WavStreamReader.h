#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vsm::audio::io {

/// LIRE UN MORCEAU D'UN WAV, SANS LE CHARGER EN ENTIER (D8.2).
///
/// `WavFileReader` lit un fichier complet et rend un tableau. C'est ce qu'il
/// faut pour un échantillon de sampler ; c'est ruineux pour une prise de neuf
/// minutes, qui pèse 200 Mo une fois décodée en flottants stéréo. Vingt pistes
/// de ce genre demandent quatre gigaoctets pour un projet dont les fichiers
/// n'en occupent que deux sur le disque.
///
/// Ce lecteur-ci fait l'inverse : il lit l'en-tête UNE fois, retient où
/// commence le chunk `data` et sous quel format, puis sait aller chercher
/// n'importe quelle plage de trames à la demande.
///
/// IL NE VA JAMAIS SUR LE THREAD AUDIO, et rien dans sa forme ne le laisse
/// croire : il ouvre un fichier, il prend un verrou, il fait des `seek`. C'est
/// le thread de diffusion qui l'emploie (voir `engine::StreamedSampleStore`),
/// jamais `processBlock`.
class WavStreamReader {
public:
    struct OpenResult {
        std::shared_ptr<WavStreamReader> reader;
        std::string error;
    };

    /// Ouvre et analyse l'en-tête. Les mêmes refus que `WavFileReader`, et pour
    /// la même raison : un fichier interprété « au mieux » produit un bruit que
    /// personne ne rattache au fichier fautif.
    static OpenResult open(const std::string& path);

    int64_t frames() const { return frames_; }
    double sampleRate() const { return sampleRate_; }
    bool isStereo() const { return channels_ >= 2; }
    const std::string& path() const { return path_; }

    /// Lit `count` trames à partir de `startFrame` dans `left`/`right` (déjà
    /// dimensionnés par l'appelant). Ce qui dépasse la fin du fichier est mis à
    /// zéro. Renvoie le nombre de trames RÉELLEMENT lues dans le fichier, pour
    /// que l'appelant puisse distinguer « la fin » d'une erreur de lecture.
    ///
    /// Un fichier mono remplit les deux canaux à l'identique : le graphe ne
    /// connaît que du stéréo, et laisser la droite à zéro ferait sonner toutes
    /// les prises mono à gauche.
    int64_t readFrames(int64_t startFrame, int64_t count, float* left, float* right);

private:
    std::mutex mutex_;          ///< un seul lecteur à la fois sur le même `ifstream`
    std::ifstream stream_;
    std::vector<uint8_t> scratch_;  ///< octets bruts du bloc en cours de décodage
    std::string path_;
    int64_t dataOffset_ = 0;
    int64_t frames_ = 0;
    double sampleRate_ = 44100.0;
    uint16_t channels_ = 1;
    uint16_t bitsPerSample_ = 16;
    uint16_t formatCode_ = 1;
    size_t frameSize_ = 2;
};

} // namespace vsm::audio::io
