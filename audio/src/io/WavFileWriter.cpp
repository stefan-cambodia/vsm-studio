#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vsm::audio::io {

namespace {

void pushU32LE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}
void pushU16LE(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
void pushBytes(std::vector<uint8_t>& v, const char* s, size_t n) { v.insert(v.end(), s, s + n); }

float clampSample(float x) { return std::max(-1.0f, std::min(1.0f, x)); }

/// LE DITHER TPDF (D14.4) : la somme de deux tirages uniformes, en LSB, soit
/// un bruit triangulaire de ± 1 LSB -- celui qui rend l'erreur de quantification
/// indépendante du signal, et donc inaudible comme distorsion. Un générateur
/// déterministe à graine fixe : l'export est reproductible octet pour octet.
struct Dither {
    vsm::util::DeterministicRng rng{0x4449544852ULL};   // "DITHR"
    bool actif = true;
    float lsb = 1.0f / 32767.0f;
    float bruit() { return actif ? (rng.nextUnipolar() - rng.nextUnipolar()) * lsb : 0.0f; }
};

int16_t floatToInt16(float x, Dither& d) { return static_cast<int16_t>(std::lround(clampSample(clampSample(x) + d.bruit()) * 32767.0f)); }

void floatToInt24(float x, uint8_t out[3], Dither& d) {
    int32_t v = static_cast<int32_t>(std::lround(clampSample(clampSample(x) + d.bruit()) * 8388607.0f));
    out[0] = static_cast<uint8_t>(v & 0xFF);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
}

} // namespace

std::vector<uint8_t> WavFileWriter::write(const float* left, const float* right, size_t numFrames,
                                           double sampleRate, SampleFormat format, bool dither) {
    Dither d;
    d.actif = dither && format != SampleFormat::Float32;
    d.lsb = format == SampleFormat::Int24 ? 1.0f / 8388607.0f : 1.0f / 32767.0f;
    uint16_t numChannels = right ? 2 : 1;
    uint16_t bitsPerSample = 16;
    uint16_t formatCode = 1; // WAVE_FORMAT_PCM
    switch (format) {
        case SampleFormat::Int16:   bitsPerSample = 16; formatCode = 1; break;
        case SampleFormat::Int24:   bitsPerSample = 24; formatCode = 1; break;
        case SampleFormat::Float32: bitsPerSample = 32; formatCode = 3; break; // WAVE_FORMAT_IEEE_FLOAT
    }

    uint16_t blockAlign = static_cast<uint16_t>(numChannels * (bitsPerSample / 8));
    uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    uint32_t dataSize = static_cast<uint32_t>(numFrames) * blockAlign;
    bool needsFactChunk = (format == SampleFormat::Float32); // requis par certains lecteurs pour IEEE float
    constexpr uint32_t kFmtChunkSize = 16;

    uint32_t riffSize = 4 /*"WAVE"*/ + (8 + kFmtChunkSize) + (needsFactChunk ? (8 + 4) : 0) + (8 + dataSize);

    std::vector<uint8_t> out;
    out.reserve(44 + dataSize);

    pushBytes(out, "RIFF", 4);
    pushU32LE(out, riffSize);
    pushBytes(out, "WAVE", 4);

    pushBytes(out, "fmt ", 4);
    pushU32LE(out, kFmtChunkSize);
    pushU16LE(out, formatCode);
    pushU16LE(out, numChannels);
    pushU32LE(out, static_cast<uint32_t>(sampleRate));
    pushU32LE(out, byteRate);
    pushU16LE(out, blockAlign);
    pushU16LE(out, bitsPerSample);

    if (needsFactChunk) {
        pushBytes(out, "fact", 4);
        pushU32LE(out, 4);
        pushU32LE(out, static_cast<uint32_t>(numFrames));
    }

    pushBytes(out, "data", 4);
    pushU32LE(out, dataSize);

    for (size_t i = 0; i < numFrames; ++i) {
        switch (format) {
            case SampleFormat::Int16: {
                int16_t li = floatToInt16(left[i], d);
                out.push_back(static_cast<uint8_t>(li & 0xFF));
                out.push_back(static_cast<uint8_t>((li >> 8) & 0xFF));
                if (right) {
                    int16_t ri = floatToInt16(right[i], d);
                    out.push_back(static_cast<uint8_t>(ri & 0xFF));
                    out.push_back(static_cast<uint8_t>((ri >> 8) & 0xFF));
                }
                break;
            }
            case SampleFormat::Int24: {
                uint8_t lb[3];
                floatToInt24(left[i], lb, d);
                out.insert(out.end(), lb, lb + 3);
                if (right) {
                    uint8_t rb[3];
                    floatToInt24(right[i], rb, d);
                    out.insert(out.end(), rb, rb + 3);
                }
                break;
            }
            case SampleFormat::Float32: {
                // LE FLOTTANT NE S'ÉCRÊTE PAS, ET C'EST TOUTE SA RAISON D'ÊTRE.
                //
                // Cette branche appelait `clampSample`. Un WAV 32 bits flottants
                // porte parfaitement les valeurs au-delà de ±1 -- c'est
                // précisément pourquoi on exporte en flottant : pour garder la
                // marge et la rattraper au mastering. Les ramener à ±1 détruit
                // ce que le format sait tenir, et le détruit EN SILENCE.
                //
                // MESURÉ SUR UN VRAI MORCEAU : `children-dream-v7` rend un
                // mixage dont le pic vaut **1,405** ; le fichier flottant écrit
                // en rendait 1,0000, et **602 échantillons sur 20,5 millions**
                // étaient rabotés. Trouvé en vérifiant que la somme des stems
                // redonne le mixage : elle le redonne à -98 dB partout, sauf là
                // où le mixage avait été écrêté à l'écriture.
                //
                // Les formats ENTIERS, eux, continuent de borner : 16 et 24 bits
                // ne savent pas représenter au-delà de l'échelle, et y écrire
                // un dépassement produirait un repliement, c'est-à-dire un son
                // faux plutôt qu'un son fort.
                const float lc = left[i];
                const uint8_t* lp = reinterpret_cast<const uint8_t*>(&lc);
                out.insert(out.end(), lp, lp + 4);
                if (right) {
                    const float rc = right[i];
                    const uint8_t* rp = reinterpret_cast<const uint8_t*>(&rc);
                    out.insert(out.end(), rp, rp + 4);
                }
                break;
            }
        }
    }

    return out;
}

void WavFileWriter::writeFile(const float* left, const float* right, size_t numFrames,
                              double sampleRate, SampleFormat format, const std::string& path,
                              bool dither) {
    std::vector<uint8_t> bytes = write(left, right, numFrames, sampleRate, format, dither);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("WavFileWriter: impossible d'écrire: " + path);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace vsm::audio::io
