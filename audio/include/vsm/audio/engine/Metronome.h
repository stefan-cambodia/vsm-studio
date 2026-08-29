#pragma once
#include <cmath>
#include <cstdint>

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

    void setLevel(float level) { level_ = level; }

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
        const float valeur = std::sin(phase_) * enveloppe * level_;
        phase_ += static_cast<float>(2.0 * M_PI * frequence_ / sampleRate_);
        --restant_;
        return valeur;
    }

private:
    double sampleRate_ = 48000.0;
    float level_ = 0.35f;
    float frequence_ = 1000.0f;
    float phase_ = 0.0f;
    int restant_ = 0;
    int duree_ = 1;
};

/// Les temps qui tombent dans l'intervalle [debut, fin) de ticks.
///
/// Fonction PURE, et c'est ce qui la rend testable sans carte son : elle ne
/// connaît ni le temps réel, ni le graphe. Appelle `emettre(tick, accent)`
/// pour chacun.
template <typename Fn>
void forEachBeatInRange(int64_t debutTick, int64_t finTick, int ticksParNoire,
                         int numerateur, Fn&& emettre) {
    if (ticksParNoire <= 0 || finTick <= debutTick) return;
    const int battusParMesure = numerateur > 0 ? numerateur : 4;
    int64_t premier = (debutTick / ticksParNoire) * ticksParNoire;
    if (premier < debutTick) premier += ticksParNoire;
    for (int64_t tick = premier; tick < finTick; tick += ticksParNoire) {
        const int64_t indice = tick / ticksParNoire;
        emettre(tick, (indice % battusParMesure) == 0);
    }
}

} // namespace vsm::audio::engine
