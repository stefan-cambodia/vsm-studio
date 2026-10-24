#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "vsm/sequencer/TimeSignatureMap.h"

namespace vsm::audio::engine {

/// LE MÉTRONOME. Un clic sur chaque temps, plus aigu sur le premier de la
/// mesure.
///
/// POURQUOI IL N'EST PAS UNE MACHINE DU PARC. Toutes les autres sources de son
/// de ce projet sont des instruments : elles reçoivent des notes, se règlent,
/// s'exportent en preset, figent leur rendu par une empreinte. Le métronome
/// n'est rien de tout cela -- il ne joue pas de musique, il compte. Le faire
/// passer par une piste et un instrument obligerait à inventer des notes qui
/// n'existent pas dans le morceau, à les écarter de l'export, et à expliquer
/// pourquoi une piste ne s'entend pas au rendu. Il vit donc dans le graphe,
/// après les pistes et avant le master, comme la piste de référence A/B.
///
/// IL NE S'EXPORTE JAMAIS. Un clic dans un fichier rendu serait une faute
/// grossière, et le rendu hors ligne ne l'active pas.
class Metronome {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        phase_ = 0.0f;
        restant_ = 0;
    }

    /// LE NIVEAU (D16.6), ATOMIQUE : il est écrit par le fil de l'interface
    /// (un curseur qu'on tire) et lu par le fil audio à chaque échantillon.
    /// Un `float` nu était une course de données, bénigne en pratique et
    /// interdite en droit -- et le curseur n'existait pas encore, ce qui est
    /// la seule raison pour laquelle personne ne l'avait payée.
    void setLevel(float level) { level_.store(level, std::memory_order_relaxed); }
    float level() const { return level_.load(std::memory_order_relaxed); }

    /// Déclenche un clic. `accent` distingue le premier temps de la mesure.
    void trigger(bool accent) {
        // Deux hauteurs franches plutôt qu'un timbre : on doit reconnaître le
        // premier temps sans y penser, en jouant.
        frequence_ = accent ? 1600.0f : 1000.0f;
        restant_ = static_cast<int>(sampleRate_ * 0.022);   // 22 ms
        duree_ = restant_;
        phase_ = 0.0f;
    }

    bool active() const { return restant_ > 0; }

    /// Un échantillon de clic, enveloppe comprise. Zéro quand il n'y a rien.
    float nextSample() {
        if (restant_ <= 0) return 0.0f;
        const float avancement = 1.0f - static_cast<float>(restant_) / static_cast<float>(duree_);
        // Décroissance exponentielle : une coupure franche « claque » et se
        // confond avec une percussion du morceau.
        const float enveloppe = std::exp(-6.0f * avancement);
        const float valeur = std::sin(phase_) * enveloppe * level();
        phase_ += static_cast<float>(2.0 * M_PI * frequence_ / sampleRate_);
        --restant_;
        return valeur;
    }

private:
    double sampleRate_ = 48000.0;
    std::atomic<float> level_{0.35f};
    float frequence_ = 1000.0f;
    float phase_ = 0.0f;
    int restant_ = 0;
    int duree_ = 1;
};

/// Les clics qui tombent dans l'intervalle [debut, fin) de ticks.
///
/// Fonction PURE, et c'est ce qui la rend testable sans carte son : elle ne
/// connaît ni le temps réel, ni le graphe. Appelle `emettre(tick, accent)`
/// pour chacun, dans l'ordre.
///
/// D590.2 et D590.3 : LE CLIC BAT L'UNITÉ DE LA SIGNATURE, ET L'ACCENT TOMBE OÙ
/// LA RÈGLE MET LA MESURE. Le pas est `ticksPerBeat` (la croche en 6/8, la
/// blanche en 2/2), et les mesures se comptent depuis chaque changement de
/// signature, comme `TimeSignatureMap::barBeatAt` — avant D590, le clic battait
/// la noire et comptait ses accents depuis le tick 0 avec le numérateur lu au
/// début du bloc : en 6/8, trois clics et un accent toutes les deux mesures.
/// Avant zéro (le décompte), le premier segment se prolonge à rebours : les
/// mesures s'y comptent depuis le tick 0, à la signature du début.
template <typename Fn>
void forEachClickInRange(const vsm::sequencer::TimeSignatureMap& carte, int ticksParNoire,
                         int64_t debutTick, int64_t finTick, Fn&& emettre) {
    if (ticksParNoire <= 0 || finTick <= debutTick) return;
    const auto ppq = static_cast<uint16_t>(ticksParNoire);
    const auto& changements = carte.changes();
    // La division et le reste ARRONDIS VERS LE BAS : avant zéro, `/` et `%`
    // tronquent vers zéro, et le premier temps d'un décompte serait manqué.
    const auto plancher = [](int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); };
    for (size_t i = 0; i < changements.size(); ++i) {
        const int64_t origine = changements[i].tick;
        const int64_t bas = i == 0 ? debutTick : std::max<int64_t>(debutTick, origine);
        const int64_t haut = i + 1 < changements.size() ? std::min<int64_t>(finTick, changements[i + 1].tick) : finTick;
        if (bas >= haut) continue;
        const int64_t pas = std::max<int64_t>(1, carte.ticksPerBeat(origine, ppq));
        const int64_t mesure = pas * std::max<int64_t>(1, carte.numeratorAt(origine));
        for (int64_t tick = origine + (plancher(bas - origine - 1, pas) + 1) * pas; tick < haut; tick += pas)
            emettre(tick, (tick - origine) - plancher(tick - origine, mesure) * mesure == 0);
    }
}

} // namespace vsm::audio::engine
