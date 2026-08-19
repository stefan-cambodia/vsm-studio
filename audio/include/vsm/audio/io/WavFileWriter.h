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
    static std::vector<uint8_t> write(const float* left, const float* right, size_t numFrames,
                                       double sampleRate, SampleFormat format);

    static void writeFile(const float* left, const float* right, size_t numFrames,
                           double sampleRate, SampleFormat format, const std::string& path);
};

} // namespace vsm::audio::io
