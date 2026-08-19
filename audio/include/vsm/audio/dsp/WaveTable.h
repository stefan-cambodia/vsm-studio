#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace vsm::audio::dsp {

/// Banque de tables d'ondes, avec repliement maîtrisé.
///
/// POURQUOI CETTE BRIQUE EXISTE : lire une forme d'onde dans une table est
/// trivial ; le faire SANS REPLIEMENT ne l'est pas. Une table contenant une
/// dent de scie riche, lue à 4 kHz, produit des harmoniques bien au-delà de
/// Nyquist qui reviennent se plier dans le grave sous forme de sifflements
/// métalliques, non harmoniques, et parfaitement audibles.
///
/// LA SOLUTION RETENUE : chaque forme d'onde est stockée en PLUSIEURS
/// VERSIONS, chacune limitée à un nombre d'harmoniques décroissant (niveau 0 :
/// toutes ; niveau k : deux fois moins qu'au niveau k-1). À la lecture, on
/// choisit le niveau dont la plus haute harmonique reste sous Nyquist pour la
/// note jouée. C'est la technique classique dite « mip-map », et c'est le seul
/// point de cette brique qui coûte de la mémoire : tout le reste est une
/// simple lecture interpolée.
///
/// CE QUE CETTE BRIQUE NE FAIT PAS : elle ne charge aucun fichier. Les tables
/// sont ENGENDRÉES par calcul, à partir de spectres décrits dans le code.
/// C'est une contrainte du projet (aucune dépendance externe, construction
/// hors-ligne), et c'est aussi ce qui garantit que deux machines à jour
/// produisent exactement les mêmes tables.
class WaveTableBank {
public:
    /// Longueur d'une forme d'onde. 2048 points : au-delà, on paie de la
    /// mémoire pour une précision que l'interpolation linéaire ne rend pas.
    static constexpr size_t kTableLength = 2048;
    /// Formes d'onde par table. Huit : c'est assez pour un balayage qui
    /// s'entend comme un mouvement continu, pas comme une suite de sauts.
    static constexpr size_t kWavesPerTable = 8;
    /// Niveaux de repliement. Dix niveaux couvrent tout le clavier MIDI :
    /// du niveau 0 (note grave, spectre complet) au niveau 9 (note très
    /// aiguë, presque une sinusoïde).
    static constexpr size_t kMipLevels = 10;

    /// Banque partagée, engendrée une seule fois pour tout le processus.
    ///
    /// Le calcul (quelques millions d'appels à sin) prend quelques
    /// millisecondes : c'est négligeable une fois, et rédhibitoire à chaque
    /// création d'instance. L'objet étant CONSTANT après construction, le
    /// partager entre instances et entre fils d'exécution est sûr.
    static const WaveTableBank& shared();

    /// Nombre de tables de la banque.
    size_t tableCount() const { return tables_.size(); }
    /// Nom de la table, pour la façade et les documents.
    const char* tableName(size_t table) const;

    /// Lit un échantillon.
    ///
    /// `position` (0..1) balaie les huit formes de la table, avec fondu entre
    /// les deux formes voisines -- c'est ce fondu qui fait le mouvement.
    /// `phase` (0..1) est la position dans le cycle.
    /// `harmonicLimit` est le nombre d'harmoniques encore admissibles pour la
    /// note jouée : la lecture choisit toute seule le niveau de repliement.
    float read(size_t table, float position, float phase, float harmonicLimit) const;

private:
    WaveTableBank();

    struct Table {
        const char* name;
        /// [forme][niveau][point]
        std::vector<std::array<std::vector<float>, kMipLevels>> waves;
    };
    std::vector<Table> tables_;

    /// Engendre une forme d'onde à partir de ses amplitudes d'harmoniques,
    /// pour tous les niveaux de repliement.
    static void buildWave(const std::vector<float>& harmonics,
                          std::array<std::vector<float>, kMipLevels>& out);
};

/// Oscillateur à table d'ondes : une phase, et de quoi lire la banque en
/// restant sous Nyquist. Ne possède PAS la banque -- il la lit.
class WaveTableOscillator {
public:
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void setFrequency(float hz) { frequencyHz_ = hz > 0.0f ? hz : 1.0f; }
    void reset(double phase = 0.0) { phase_ = phase - static_cast<double>(static_cast<long long>(phase)); }

    /// Avance d'un échantillon et rend la valeur lue.
    float nextSample(const WaveTableBank& bank, size_t table, float position) {
        // Harmoniques encore admissibles : celles qui restent sous Nyquist.
        // Une marge de 0.9 évite de flirter avec la limite, où le filtre
        // anti-repliement de la carte son n'aide plus.
        const float limit = 0.9f * static_cast<float>(sampleRate_) * 0.5f / frequencyHz_;
        const float sample = bank.read(table, position, static_cast<float>(phase_), limit);
        phase_ += static_cast<double>(frequencyHz_) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return sample;
    }

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float frequencyHz_ = 440.0f;
};

} // namespace vsm::audio::dsp
