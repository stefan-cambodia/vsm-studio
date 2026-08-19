#include "TestFramework.h"
#include "vsm/audio/dsp/WaveTable.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace vsm::audio::dsp;

namespace {

constexpr double kSampleRate = 48000.0;

/// Énergie du signal aux fréquences qui ne sont PAS des multiples de la
/// fondamentale. Sur une forme d'onde périodique correctement engendrée, elle
/// doit être négligeable : tout ce qui s'y trouve est du repliement.
double inharmonicEnergyRatio(const std::vector<float>& buffer, double fundamentalHz) {
    // Analyse par corrélation à chaque fréquence testée, tous les 20 Hz.
    // Grossier mais suffisant : on cherche un écart d'ordre de grandeur, pas
    // une mesure fine.
    double harmonic = 0.0, inharmonic = 0.0;
    for (double hz = 40.0; hz < kSampleRate * 0.5 - 100.0; hz += 20.0) {
        double real = 0.0, imaginary = 0.0;
        for (size_t i = 0; i < buffer.size(); ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / kSampleRate;
            real += static_cast<double>(buffer[i]) * std::cos(angle);
            imaginary += static_cast<double>(buffer[i]) * std::sin(angle);
        }
        const double power = (real * real + imaginary * imaginary) / (buffer.size() * buffer.size());

        // Est-on proche d'un multiple de la fondamentale ?
        const double ratio = hz / fundamentalHz;
        const double distance = std::abs(ratio - std::round(ratio));
        if (distance < 0.12 && std::round(ratio) >= 1.0) harmonic += power;
        else inharmonic += power;
    }
    return harmonic > 0.0 ? inharmonic / harmonic : 1.0;
}

std::vector<float> renderTone(size_t table, float position, float hz, int samples) {
    const WaveTableBank& bank = WaveTableBank::shared();
    WaveTableOscillator oscillator;
    oscillator.setSampleRate(kSampleRate);
    oscillator.setFrequency(hz);
    std::vector<float> buffer(static_cast<size_t>(samples));
    for (auto& sample : buffer) sample = oscillator.nextSample(bank, table, position);
    return buffer;
}

double rms(const std::vector<float>& buffer) {
    double sum = 0.0;
    for (float sample : buffer) sum += static_cast<double>(sample) * sample;
    return buffer.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(buffer.size()));
}

} // namespace

VSM_TEST(wavetable_bank_has_four_named_tables) {
    const WaveTableBank& bank = WaveTableBank::shared();
    VSM_ASSERT_EQ(bank.tableCount(), size_t(4));
    for (size_t i = 0; i < bank.tableCount(); ++i)
        VSM_ASSERT(std::string(bank.tableName(i)).size() > 0);
}

VSM_TEST(wavetable_shared_bank_is_built_once) {
    // Deux appels doivent rendre le MÊME objet : c'est ce qui évite de
    // reconstruire quelques mégaoctets de tables à chaque instance.
    VSM_ASSERT(&WaveTableBank::shared() == &WaveTableBank::shared());
}

VSM_TEST(wavetable_high_notes_do_not_alias) {
    // LE test de cette brique. Une note aiguë sur une forme riche est
    // exactement le cas où une lecture de table naïve se trahit : les
    // harmoniques au-delà de Nyquist reviennent en sifflements non
    // harmoniques. On compare donc une note grave (référence) et une note
    // très aiguë sur la MÊME forme d'onde riche.
    const auto low = renderTone(0, 1.0f, 110.0f, 24000);   // La2, spectre complet
    const auto high = renderTone(0, 1.0f, 3520.0f, 24000); // La7, cinq octaves plus haut

    const double lowRatio = inharmonicEnergyRatio(low, 110.0);
    const double highRatio = inharmonicEnergyRatio(high, 3520.0);

    // La note aiguë ne doit pas être significativement plus « sale » que la
    // grave. Sans les niveaux de repliement, ce rapport explose.
    VSM_ASSERT(highRatio < 0.05);
    VSM_ASSERT(highRatio < lowRatio * 4.0 + 0.02);
}

VSM_TEST(wavetable_position_sweep_changes_the_timbre) {
    // Balayer la position doit changer le SON, pas seulement le niveau.
    const auto dull = renderTone(0, 0.0f, 220.0f, 12000);
    const auto bright = renderTone(0, 1.0f, 220.0f, 12000);

    auto brightness = [](const std::vector<float>& buffer) {
        double sum = 0.0;
        for (size_t i = 1; i < buffer.size(); ++i) {
            const double difference = static_cast<double>(buffer[i]) - buffer[i - 1];
            sum += difference * difference;
        }
        return sum;
    };
    VSM_ASSERT(brightness(bright) > brightness(dull) * 3.0);
}

VSM_TEST(wavetable_waves_keep_a_comparable_level_across_the_sweep) {
    // Si les formes n'étaient pas normalisées, balayer la table ferait aussi
    // un fondu de volume -- et l'utilisateur croirait à un réglage cassé.
    double minimum = 1e9, maximum = 0.0;
    for (int step = 0; step <= 8; ++step) {
        const float position = static_cast<float>(step) / 8.0f;
        const double level = rms(renderTone(0, position, 220.0f, 8000));
        minimum = std::min(minimum, level);
        maximum = std::max(maximum, level);
    }
    VSM_ASSERT(minimum > 0.001);
    VSM_ASSERT(maximum < minimum * 4.0);
}

VSM_TEST(wavetable_position_interpolates_between_neighbours) {
    // Le fondu entre formes voisines doit être CONTINU : un saut s'entendrait
    // comme un clic quand une enveloppe balaie la table.
    const auto quarter = renderTone(0, 0.30f, 220.0f, 4000);
    const auto middle = renderTone(0, 0.32f, 220.0f, 4000);
    double difference = 0.0, reference = 0.0;
    for (size_t i = 0; i < quarter.size(); ++i) {
        const double d = static_cast<double>(quarter[i]) - middle[i];
        difference += d * d;
        reference += static_cast<double>(quarter[i]) * quarter[i];
    }
    VSM_ASSERT(difference < reference * 0.1); // deux positions voisines, deux sons voisins
}

VSM_TEST(wavetable_tables_sound_different_from_each_other) {
    // Quatre tables qui sonneraient pareil ne serviraient à rien.
    std::vector<std::vector<float>> renders;
    for (size_t table = 0; table < WaveTableBank::shared().tableCount(); ++table)
        renders.push_back(renderTone(table, 0.7f, 220.0f, 8000));

    for (size_t a = 0; a < renders.size(); ++a) {
        for (size_t b = a + 1; b < renders.size(); ++b) {
            double correlation = 0.0, energyA = 0.0, energyB = 0.0;
            for (size_t i = 0; i < renders[a].size(); ++i) {
                correlation += static_cast<double>(renders[a][i]) * renders[b][i];
                energyA += static_cast<double>(renders[a][i]) * renders[a][i];
                energyB += static_cast<double>(renders[b][i]) * renders[b][i];
            }
            const double normalised = std::abs(correlation) / std::sqrt(std::max(1e-12, energyA * energyB));
            VSM_ASSERT(normalised < 0.95);
        }
    }
}

VSM_TEST(wavetable_reading_is_deterministic) {
    const auto a = renderTone(2, 0.4f, 330.0f, 4000);
    const auto b = renderTone(2, 0.4f, 330.0f, 4000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}
