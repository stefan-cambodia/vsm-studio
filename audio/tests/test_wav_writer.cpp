#include "TestFramework.h"
#include "vsm/audio/io/WavFileWriter.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vsm::audio::io;

namespace {

uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

struct ParsedWav {
    uint16_t formatCode = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> data;
};

/// Petit parseur WAV "juste ce qu'il faut" pour vérifier ce que
/// WavFileWriter produit -- pas une implémentation de lecture générale
/// (celle-ci n'est pas un besoin de la Phase 2, seule l'écriture l'est).
ParsedWav parseWav(const std::vector<uint8_t>& bytes) {
    ParsedWav result;
    size_t pos = 12; // après "RIFF" + taille(4) + "WAVE"
    while (pos + 8 <= bytes.size()) {
        std::string id(reinterpret_cast<const char*>(bytes.data() + pos), 4);
        uint32_t chunkSize = readU32LE(bytes.data() + pos + 4);
        const uint8_t* chunkData = bytes.data() + pos + 8;

        if (id == "fmt ") {
            result.formatCode = readU16LE(chunkData + 0);
            result.numChannels = readU16LE(chunkData + 2);
            result.sampleRate = readU32LE(chunkData + 4);
            result.bitsPerSample = readU16LE(chunkData + 14);
        } else if (id == "data") {
            result.data.assign(chunkData, chunkData + chunkSize);
        }
        pos += 8 + chunkSize;
    }
    return result;
}

} // namespace

VSM_TEST(wav_writer_produces_valid_riff_header) {
    std::vector<float> left = {0.0f, 0.5f, -0.5f, 1.0f};
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Int16);

    VSM_ASSERT(bytes.size() > 44);
    VSM_ASSERT_EQ(std::string(reinterpret_cast<const char*>(bytes.data()), 4), std::string("RIFF"));
    VSM_ASSERT_EQ(std::string(reinterpret_cast<const char*>(bytes.data() + 8), 4), std::string("WAVE"));
}

VSM_TEST(wav_writer_int16_roundtrip_within_quantization_error) {
    std::vector<float> left = {0.0f, 0.25f, -0.25f, 0.9f, -0.9f, 1.0f, -1.0f};
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 48000.0, SampleFormat::Int16);
    ParsedWav parsed = parseWav(bytes);

    VSM_ASSERT_EQ(parsed.formatCode, static_cast<uint16_t>(1)); // WAVE_FORMAT_PCM
    VSM_ASSERT_EQ(parsed.numChannels, static_cast<uint16_t>(1));
    VSM_ASSERT_EQ(parsed.bitsPerSample, static_cast<uint16_t>(16));
    VSM_ASSERT_EQ(parsed.sampleRate, static_cast<uint32_t>(48000));
    VSM_ASSERT_EQ(parsed.data.size(), left.size() * 2);

    for (size_t i = 0; i < left.size(); ++i) {
        int16_t raw;
        std::memcpy(&raw, parsed.data.data() + i * 2, 2);
        float decoded = static_cast<float>(raw) / 32767.0f;
        VSM_ASSERT_NEAR(decoded, left[i], 2.0 / 32767.0); // tolérance de quantification (0.5 LSB + marge)
    }
}

VSM_TEST(wav_writer_float32_roundtrip_is_exact) {
    std::vector<float> left = {0.0f, 0.123456f, -0.987654f, 1.0f, -1.0f};
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Float32);
    ParsedWav parsed = parseWav(bytes);

    VSM_ASSERT_EQ(parsed.formatCode, static_cast<uint16_t>(3)); // WAVE_FORMAT_IEEE_FLOAT
    VSM_ASSERT_EQ(parsed.bitsPerSample, static_cast<uint16_t>(32));
    VSM_ASSERT_EQ(parsed.data.size(), left.size() * 4);

    for (size_t i = 0; i < left.size(); ++i) {
        float decoded;
        std::memcpy(&decoded, parsed.data.data() + i * 4, 4);
        VSM_ASSERT_NEAR(decoded, left[i], 1e-7);
    }
}

VSM_TEST(wav_writer_stereo_interleaves_correctly) {
    std::vector<float> left = {1.0f, 0.5f};
    std::vector<float> right = {-1.0f, -0.5f};
    auto bytes = WavFileWriter::write(left.data(), right.data(), left.size(), 44100.0, SampleFormat::Int16);
    ParsedWav parsed = parseWav(bytes);

    VSM_ASSERT_EQ(parsed.numChannels, static_cast<uint16_t>(2));
    VSM_ASSERT_EQ(parsed.data.size(), static_cast<size_t>(2 * 2 * 2)); // 2 frames * 2 canaux * 2 octets

    int16_t l0, r0, l1, r1;
    std::memcpy(&l0, parsed.data.data() + 0, 2);
    std::memcpy(&r0, parsed.data.data() + 2, 2);
    std::memcpy(&l1, parsed.data.data() + 4, 2);
    std::memcpy(&r1, parsed.data.data() + 6, 2);

    VSM_ASSERT(l0 > 0);
    VSM_ASSERT(r0 < 0);
    VSM_ASSERT(l1 > 0);
    VSM_ASSERT(r1 < 0);
}

VSM_TEST(wav_writer_clamps_out_of_range_samples) {
    std::vector<float> left = {2.0f, -2.0f}; // hors [-1, 1]
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Int16);
    ParsedWav parsed = parseWav(bytes);

    int16_t s0, s1;
    std::memcpy(&s0, parsed.data.data() + 0, 2);
    std::memcpy(&s1, parsed.data.data() + 2, 2);
    VSM_ASSERT_EQ(s0, static_cast<int16_t>(32767));
    VSM_ASSERT_EQ(s1, static_cast<int16_t>(-32767));
}

VSM_TEST(wav_writer_int24_has_correct_block_align) {
    std::vector<float> left = {0.1f, -0.1f, 0.9f};
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 96000.0, SampleFormat::Int24);
    ParsedWav parsed = parseWav(bytes);

    VSM_ASSERT_EQ(parsed.bitsPerSample, static_cast<uint16_t>(24));
    VSM_ASSERT_EQ(parsed.data.size(), left.size() * 3);
}

// ---------------------------------------------------------------------------
// D14.4 — LE DITHER : un sinus à −90 dB (un LSB en 16 bits) tronqué devient un
// carré, avec sa troisième harmonique à un tiers ; dithérisé, elle disparaît
// dans un bruit indépendant du signal. Et le dither est reproductible.
// ---------------------------------------------------------------------------

namespace {
double magnitudeDe(const std::vector<float>& x, double hz, double sr) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(x.size()));
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / sr;
        re += w * x[i] * std::cos(ph); im += w * x[i] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}
std::vector<float> relireInt16(const std::vector<uint8_t>& wav, size_t frames) {
    // Les données commencent après l'en-tête de 44 octets d'un PCM sans fact.
    std::vector<float> x(frames);
    for (size_t i = 0; i < frames; ++i) {
        const size_t o = 44 + i * 2;
        const int16_t v = static_cast<int16_t>(wav[o] | (wav[o + 1] << 8));
        x[i] = static_cast<float>(v) / 32767.0f;
    }
    return x;
}
} // namespace

VSM_TEST(dither_turns_quantisation_distortion_into_noise_and_stays_reproducible) {
    const double sr = 48000.0;
    const size_t n = 48000;
    std::vector<float> sinus(n);
    const float a = 3.16e-5f;   // −90 dB : un LSB de 16 bits
    for (size_t i = 0; i < n; ++i) sinus[i] = a * static_cast<float>(std::sin(2.0 * M_PI * 441.0 * static_cast<double>(i) / sr));
    const auto brut = WavFileWriter::write(sinus.data(), nullptr, n, sr, SampleFormat::Int16, false);
    const auto dith = WavFileWriter::write(sinus.data(), nullptr, n, sr, SampleFormat::Int16, true);
    const auto xb = relireInt16(brut, n), xd = relireInt16(dith, n);
    const double h1b = magnitudeDe(xb, 441.0, sr), h3b = magnitudeDe(xb, 1323.0, sr);
    const double h1d = magnitudeDe(xd, 441.0, sr), h3d = magnitudeDe(xd, 1323.0, sr);
    std::printf("    [banc dither] -90 dB en 16 bits : troisieme harmonique / fondamentale = %.3f tronque, %.3f dithérisé\n",
                h3b / std::max(1e-12, h1b), h3d / std::max(1e-12, h1d));
    // Le profil complet, harmoniques 2 à 12, rapportées à la fondamentale.
    double pireBrut = 0.0, pireDith = 0.0;
    std::printf("    [banc dither] profil (brut | dithérisé) :");
    for (int k = 2; k <= 12; ++k) {
        const double rb = magnitudeDe(xb, 441.0 * k, sr) / std::max(1e-12, h1b);
        const double rd = magnitudeDe(xd, 441.0 * k, sr) / std::max(1e-12, h1d);
        std::printf(" h%d %.3f|%.3f", k, rb, rd);
        pireBrut = std::max(pireBrut, rb); pireDith = std::max(pireDith, rd);
    }
    std::printf("\n    [banc dither] pire harmonique : brut %.3f, dithérisé %.3f\n", pireBrut, pireDith);
    VSM_ASSERT(pireBrut > 0.1);
    VSM_ASSERT(pireDith < 0.03);
    // Reproductible octet pour octet, et sans effet sur le flottant.
    VSM_ASSERT(dith == WavFileWriter::write(sinus.data(), nullptr, n, sr, SampleFormat::Int16, true));
    VSM_ASSERT(WavFileWriter::write(sinus.data(), nullptr, n, sr, SampleFormat::Float32, true)
               == WavFileWriter::write(sinus.data(), nullptr, n, sr, SampleFormat::Float32, false));
}
