#include "TestFramework.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/audio/io/WavFileWriter.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::io;

namespace {
std::vector<float> ramp(size_t n) {
    std::vector<float> data(n);
    for (size_t i = 0; i < n; ++i)
        data[i] = static_cast<float>(std::sin(0.05 * static_cast<double>(i))) * 0.8f;
    return data;
}
} // namespace

VSM_TEST(wav_reader_round_trips_float32) {
    // Aller-retour exact : le float 32 bits ne perd rien, donc toute
    // différence signalerait un défaut de lecture, pas d'arrondi.
    const auto left = ramp(1000), right = ramp(1000);
    const auto bytes = WavFileWriter::write(left.data(), right.data(), left.size(), 48000.0,
                                             SampleFormat::Float32);
    const auto result = WavFileReader::read(bytes);
    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.buffer.numFrames(), left.size());
    VSM_ASSERT(result.buffer.isStereo());
    VSM_ASSERT_NEAR(result.buffer.sampleRate, 48000.0, 1e-9);
    for (size_t i = 0; i < left.size(); ++i) VSM_ASSERT_NEAR(result.buffer.left[i], left[i], 1e-9);
}

VSM_TEST(wav_reader_round_trips_int16_and_int24) {
    const auto left = ramp(500);
    for (auto format : {SampleFormat::Int16, SampleFormat::Int24}) {
        const auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, format);
        const auto result = WavFileReader::read(bytes);
        VSM_ASSERT(result.success);
        VSM_ASSERT(!result.buffer.isStereo()); // mono demandé, mono relu
        VSM_ASSERT_EQ(result.buffer.numFrames(), left.size());
        // Tolérance : la quantification du format, PLUS l'asymétrie des deux
        // conventions usuelles -- l'écriture divise par la pleine échelle
        // positive (32767, pour qu'un +1,0 ne déborde pas), la lecture par la
        // pleine échelle négative (32768, pour qu'aucun échantillon ne
        // dépasse 1,0). L'écart qui en résulte vaut 0,003 % ; le corriger
        // d'un côté ferait déborder ou écrêter de l'autre.
        const double tolerance = (format == SampleFormat::Int16) ? 1.0 / 16000.0 : 1.0 / 4000000.0;
        for (size_t i = 0; i < left.size(); ++i)
            VSM_ASSERT_NEAR(result.buffer.left[i], left[i], tolerance);
    }
}

VSM_TEST(wav_reader_skips_unknown_chunks) {
    // Les fichiers produits par de vrais outils contiennent souvent LIST,
    // fact, smpl... entre `fmt ` et `data`. Ne pas les sauter, c'est ne pas
    // savoir lire la moitié des échantillons du monde réel.
    const auto left = ramp(64);
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Int16);

    // Insère un chunk "LIST" juste après l'en-tête RIFF/WAVE.
    const std::vector<uint8_t> listChunk = {'L','I','S','T', 4,0,0,0, 'I','N','F','O'};
    bytes.insert(bytes.begin() + 12, listChunk.begin(), listChunk.end());

    const auto result = WavFileReader::read(bytes);
    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.buffer.numFrames(), left.size());
}

VSM_TEST(wav_reader_refuses_what_it_cannot_read) {
    // Refus explicite : un échantillon lu de travers produirait un bruit que
    // personne ne rattacherait au fichier fautif.
    VSM_ASSERT(!WavFileReader::read({}).success);
    VSM_ASSERT(!WavFileReader::read(std::vector<uint8_t>(100, 0)).success);

    const auto left = ramp(64);
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Int16);
    auto corrupted = bytes;
    corrupted[0] = 'X'; // plus de "RIFF"
    const auto badHeader = WavFileReader::read(corrupted);
    VSM_ASSERT(!badHeader.success);
    VSM_ASSERT(badHeader.error.find("RIFF") != std::string::npos);

    // Format compressé annoncé (code 85 = MP3 dans un conteneur WAV).
    auto compressed = bytes;
    compressed[20] = 85;
    const auto badFormat = WavFileReader::read(compressed);
    VSM_ASSERT(!badFormat.success);
    VSM_ASSERT(badFormat.error.find("compressé") != std::string::npos);
}

VSM_TEST(wav_reader_survives_a_truncated_data_chunk) {
    // Fichier coupé en cours d'écriture : on lit ce qui existe vraiment, sans
    // sortir des limites -- et sans prétendre que le fichier est complet.
    const auto left = ramp(200);
    auto bytes = WavFileWriter::write(left.data(), nullptr, left.size(), 44100.0, SampleFormat::Int16);
    bytes.resize(bytes.size() / 2);

    const auto result = WavFileReader::read(bytes);
    VSM_ASSERT(result.success);
    VSM_ASSERT(result.buffer.numFrames() > 0);
    VSM_ASSERT(result.buffer.numFrames() < left.size());
}

VSM_TEST(wav_reader_reports_a_missing_file) {
    const auto result = WavFileReader::readFile("/chemin/qui/nexiste/pas.wav");
    VSM_ASSERT(!result.success);
    VSM_ASSERT(!result.error.empty());
}
