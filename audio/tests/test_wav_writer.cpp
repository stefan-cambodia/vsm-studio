#include "TestFramework.h"
#include "vsm/audio/io/WavFileWriter.h"
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
