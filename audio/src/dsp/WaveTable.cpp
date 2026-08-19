#include "vsm/audio/dsp/WaveTable.h"
#include "vsm/audio/dsp/Constants.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

namespace {

/// Nombre maximal d'harmoniques engendrées. 1024 = la moitié de la longueur
/// de table : au-delà, la table elle-même ne peut plus les représenter.
constexpr size_t kMaxHarmonics = WaveTableBank::kTableLength / 2;

/// --- Descriptions spectrales des tables ------------------------------------
///
/// Chaque forme d'onde est décrite par ses AMPLITUDES D'HARMONIQUES, pas par
/// ses échantillons. Deux raisons, et elles comptent :
///
///  1. Un spectre se limite proprement -- il suffit de tronquer. Une table
///     d'échantillons, elle, ne se limite qu'en la refiltrant, ce qui demande
///     une transformée et introduit ses propres défauts.
///  2. Le spectre DIT ce que la forme est. « Impaires seules, en 1/n » se lit
///     et se vérifie ; un tableau de 2048 nombres, non.

std::vector<float> harmonicsSaw(size_t count, float tilt) {
    std::vector<float> h(count, 0.0f);
    for (size_t n = 1; n <= count; ++n)
        h[n - 1] = std::pow(1.0f / static_cast<float>(n), tilt);
    return h;
}

std::vector<float> harmonicsOddOnly(size_t count, float tilt) {
    std::vector<float> h(count, 0.0f);
    for (size_t n = 1; n <= count; n += 2)
        h[n - 1] = std::pow(1.0f / static_cast<float>(n), tilt);
    return h;
}

/// Spectre de « formant » : une bosse d'harmoniques autour d'un rang donné,
/// comme celle qui distingue un « a » d'un « i ». C'est ce qui donne aux
/// tables vocales leur caractère parlant.
std::vector<float> harmonicsFormant(size_t count, float centre, float width) {
    std::vector<float> h(count, 0.0f);
    for (size_t n = 1; n <= count; ++n) {
        const float distance = (static_cast<float>(n) - centre) / width;
        h[n - 1] = std::exp(-distance * distance) / std::sqrt(static_cast<float>(n));
    }
    return h;
}

/// Spectre inharmonique de cloche : seuls quelques rangs, choisis hors de la
/// série harmonique. Le résultat n'a pas de hauteur franche -- c'est voulu.
std::vector<float> harmonicsBell(size_t count, float spread) {
    std::vector<float> h(count, 0.0f);
    const float partials[6] = {1.0f, 2.7f, 5.4f, 8.9f, 13.3f, 18.5f};
    for (int i = 0; i < 6; ++i) {
        const float rank = 1.0f + (partials[i] - 1.0f) * spread;
        const auto index = static_cast<size_t>(std::lround(rank));
        if (index >= 1 && index <= count)
            h[index - 1] += 1.0f / (1.0f + static_cast<float>(i) * 0.8f);
    }
    return h;
}

/// Spectre « miroir » : les harmoniques hautes sont plus fortes que les
/// basses. Absent de toute machine analogique du parc -- c'est exactement ce
/// que la synthèse par table apporte.
std::vector<float> harmonicsInverted(size_t count, float amount) {
    std::vector<float> h(count, 0.0f);
    const size_t top = std::min<size_t>(count, 32);
    for (size_t n = 1; n <= top; ++n) {
        const float rising = static_cast<float>(n) / static_cast<float>(top);
        const float falling = 1.0f / static_cast<float>(n);
        h[n - 1] = falling * (1.0f - amount) + rising * amount * 0.35f;
    }
    return h;
}

} // namespace

void WaveTableBank::buildWave(const std::vector<float>& harmonics,
                               std::array<std::vector<float>, kMipLevels>& out) {
    // NORMALISATION EN ÉNERGIE, et pas par la somme des amplitudes.
    //
    // La distinction n'est pas académique : une forme douce (harmoniques en
    // 1/n^4) a une somme d'amplitudes proche de 1, tandis qu'une forme riche
    // (1/n^0.85 sur mille rangs) a une somme énorme. Normaliser par la somme
    // écrasait donc les formes riches à un niveau dérisoire -- balayer la
    // table faisait un fondu de volume au lieu d'un fondu de timbre, et les
    // deux tests correspondants l'ont montré.
    //
    // L'énergie, elle, est ce que l'oreille rapporte au volume : pour une
    // somme de sinusoïdes, elle vaut la somme des carrés des amplitudes
    // divisée par deux (identité de Parseval). C'est cette grandeur qu'on
    // égalise d'une forme à l'autre.
    double energy = 0.0;
    for (float amplitude : harmonics) energy += static_cast<double>(amplitude) * amplitude;
    const float targetRms = 0.4f; // laisse de la marge avant écrêtage
    float normalise = energy > 0.0 ? targetRms / static_cast<float>(std::sqrt(energy * 0.5)) : 1.0f;

    for (size_t level = 0; level < kMipLevels; ++level) {
        // Niveau k : deux fois moins d'harmoniques que le niveau k-1.
        const size_t limit = std::max<size_t>(1, kMaxHarmonics >> level);
        auto& table = out[level];
        table.assign(kTableLength + 1, 0.0f); // +1 : point de bouclage

        const size_t count = std::min(limit, harmonics.size());
        for (size_t n = 1; n <= count; ++n) {
            const float amplitude = harmonics[n - 1] * normalise;
            if (std::abs(amplitude) < 1e-7f) continue;
            const double increment = kTwoPi * static_cast<double>(n) / static_cast<double>(kTableLength);
            for (size_t i = 0; i < kTableLength; ++i)
                table[i] += amplitude * static_cast<float>(std::sin(increment * static_cast<double>(i)));
        }
        // Le point kTableLength répète le premier : l'interpolation linéaire
        // peut alors lire i et i+1 sans test de bouclage dans la boucle
        // chaude. C'est un point de plus en mémoire contre une branche en
        // moins par échantillon.
        table[kTableLength] = table[0];

        // Garde-fou de crête, calculé sur le niveau 0 puis appliqué à TOUS
        // les niveaux avec le même facteur. Certaines formes (les cloches,
        // dont l'énergie tient dans six raies) ont un facteur de crête très
        // élevé : à énergie égale, elles écrêteraient. Appliquer le même
        // facteur partout est essentiel -- un facteur par niveau ferait
        // changer le volume d'une note à l'autre du clavier.
        if (level == 0) {
            float peak = 0.0f;
            for (float sample : table) peak = std::max(peak, std::abs(sample));
            if (peak > 0.95f) {
                const float trim = 0.95f / peak;
                normalise *= trim;
                for (float& sample : table) sample *= trim;
            }
        }
    }
}

WaveTableBank::WaveTableBank() {
    struct Descriptor {
        const char* name;
        std::vector<std::vector<float>> waves; // huit spectres
    };

    std::vector<Descriptor> descriptors;

    // 1. « CLASSIC » -- du doux au mordant. La table de départ de toute
    //    machine à table d'ondes : elle doit contenir des sons utilisables
    //    seuls, pas des curiosités.
    {
        Descriptor d{"CLASSIC", {}};
        const float tilts[kWavesPerTable] = {4.0f, 2.6f, 1.9f, 1.5f, 1.25f, 1.1f, 1.0f, 0.85f};
        for (float tilt : tilts) d.waves.push_back(harmonicsSaw(kMaxHarmonics, tilt));
        descriptors.push_back(std::move(d));
    }

    // 2. « HOLLOW » -- harmoniques impaires seules, du son de clarinette au
    //    carré franc. Creux au milieu du spectre, d'où le nom.
    {
        Descriptor d{"HOLLOW", {}};
        const float tilts[kWavesPerTable] = {5.0f, 3.2f, 2.3f, 1.8f, 1.5f, 1.3f, 1.15f, 1.0f};
        for (float tilt : tilts) d.waves.push_back(harmonicsOddOnly(kMaxHarmonics, tilt));
        descriptors.push_back(std::move(d));
    }

    // 3. « VOX » -- balayage d'une bosse de formant du grave vers l'aigu. Le
    //    son « parle » quand on bouge la position, et c'est ce qu'aucune
    //    machine soustractive du parc ne sait faire.
    {
        Descriptor d{"VOX", {}};
        const float centres[kWavesPerTable] = {2.0f, 3.0f, 4.5f, 6.5f, 9.0f, 12.5f, 17.0f, 23.0f};
        for (float centre : centres) d.waves.push_back(harmonicsFormant(kMaxHarmonics, centre, centre * 0.45f));
        descriptors.push_back(std::move(d));
    }

    // 4. « METAL » -- de la cloche inharmonique au spectre inversé. La table
    //    « impossible » : rien de tout cela n'existe dans un oscillateur
    //    analogique, et c'est la raison d'être de la famille.
    {
        Descriptor d{"METAL", {}};
        for (int i = 0; i < 4; ++i)
            d.waves.push_back(harmonicsBell(kMaxHarmonics, 0.25f + 0.25f * static_cast<float>(i)));
        for (int i = 0; i < 4; ++i)
            d.waves.push_back(harmonicsInverted(kMaxHarmonics, 0.25f + 0.25f * static_cast<float>(i)));
        descriptors.push_back(std::move(d));
    }

    tables_.reserve(descriptors.size());
    for (auto& descriptor : descriptors) {
        Table table;
        table.name = descriptor.name;
        table.waves.resize(kWavesPerTable);
        for (size_t w = 0; w < kWavesPerTable; ++w)
            buildWave(descriptor.waves[w], table.waves[w]);
        tables_.push_back(std::move(table));
    }
}

const WaveTableBank& WaveTableBank::shared() {
    // Initialisation à la première demande, garantie par le langage comme
    // faite une seule fois même si plusieurs fils la demandent ensemble.
    // Appelée depuis `initialize()`, donc jamais depuis le fil audio.
    static const WaveTableBank bank;
    return bank;
}

const char* WaveTableBank::tableName(size_t table) const {
    return table < tables_.size() ? tables_[table].name : "";
}

float WaveTableBank::read(size_t table, float position, float phase, float harmonicLimit) const {
    if (tables_.empty()) return 0.0f;
    const Table& t = tables_[std::min(table, tables_.size() - 1)];

    // Choix du niveau de repliement : le premier dont le nombre
    // d'harmoniques tient sous la limite admissible pour cette note.
    size_t level = 0;
    while (level + 1 < kMipLevels &&
           static_cast<float>(kMaxHarmonics >> level) > harmonicLimit) {
        ++level;
    }

    // Position dans la table : fondu linéaire entre les deux formes voisines.
    const float clamped = std::clamp(position, 0.0f, 1.0f) * static_cast<float>(kWavesPerTable - 1);
    const auto waveIndex = static_cast<size_t>(clamped);
    const size_t nextWave = std::min(waveIndex + 1, kWavesPerTable - 1);
    const float waveFraction = clamped - static_cast<float>(waveIndex);

    // Position dans le cycle : interpolation linéaire entre deux points.
    float wrapped = phase - std::floor(phase);
    const float sampleIndex = wrapped * static_cast<float>(kTableLength);
    const auto i0 = static_cast<size_t>(sampleIndex);
    const float fraction = sampleIndex - static_cast<float>(i0);

    const auto& a = t.waves[waveIndex][level];
    const auto& b = t.waves[nextWave][level];
    const float sampleA = a[i0] + (a[i0 + 1] - a[i0]) * fraction;
    const float sampleB = b[i0] + (b[i0 + 1] - b[i0]) * fraction;
    return sampleA + (sampleB - sampleA) * waveFraction;
}

} // namespace vsm::audio::dsp
