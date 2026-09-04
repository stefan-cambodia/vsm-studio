#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vsm::audio::io {

/// Formats couverts par la section 25 du cahier des charges ("24-bit,
/// 32-bit float"). 16-bit ajouté en plus pour la compatibilité la plus
/// large possible (lecteurs qui ne gèrent pas le 24-bit).
enum class SampleFormat { Int16, Int24, Float32 };

/// Écrit un fichier WAV (RIFF/PCM ou IEEE float) à partir de deux buffers
/// non entrelacés (gauche/droite). `right == nullptr` produit un fichier
/// mono. Suppose une plateforme little-endian avec `float` IEEE754 32 bits
/// (vrai pour toutes les cibles visées section 29 : x86_64/ARM64 sur
/// Windows/macOS/Linux).
class WavFileWriter {
public:
    /// `dither` (D14.4) : un bruit TPDF de ± 1 LSB, triangulaire, ajouté AVANT
    /// l'arrondi des formats entiers -- ce que Cubase et Live font à l'export.
    /// Sans lui, une queue de réverbération à −80 dB devient une distorsion de
    /// quantification (un sinus de 1 LSB tronqué est un carré, avec sa
    /// troisième harmonique à un tiers). Déterministe (graine fixe) : deux
    /// exports du même rendu sont identiques octet pour octet. Sans effet sur
    /// le flottant, qui n'arrondit rien.
    static std::vector<uint8_t> write(const float* left, const float* right, size_t numFrames,
                                       double sampleRate, SampleFormat format, bool dither = true);

    static void writeFile(const float* left, const float* right, size_t numFrames,
                           double sampleRate, SampleFormat format, const std::string& path,
                           bool dither = true);
};

} // namespace vsm::audio::io
