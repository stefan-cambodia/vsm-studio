#pragma once
#include <atomic>
#include <cmath>

namespace vsm::audio::plugin {

/// H42 (30/09/2026) : LE DIAPASON — la fréquence du la4, 440 Hz par défaut.
///
/// POURQUOI IL EXISTE. « Reload » (Peschi) est accordé 12 cents au-dessus de
/// 440 Hz ; rejoué par des machines qui calculent chacune `440 · 2^((n−69)/12)`,
/// chaque note tenue bat contre l'original (docs/CDC-reload-indifferenciable.md
/// § 4). Un studio règle cela une fois pour toutes : c'est le « Master Tune » de
/// Cubase, et il manquait.
///
/// POURQUOI UNE VALEUR DU MOTEUR ET NON UN MEMBRE DE CHAQUE MACHINE. Les
/// conversions note → fréquence vivent dans les VOIX (une soixantaine de types),
/// qui ne voient pas leur machine ; un membre de `ISynthPlugin` aurait demandé de
/// le faire descendre dans chacune. Le diapason est celui du PROJET — le même
/// pour toutes les pistes —, et un processus ne joue qu'un projet à la fois
/// (l'application, `vsm-render`, chaque rendu de la chaîne). Le graphe le pose
/// depuis le projet (`ProcessGraph::setProject`, et le rendu hors ligne
/// d'`interchange`) ; les machines le LISENT là où elles convertissent une note.
///
/// AU BIT PRÈS À 440. `diapason() * std::exp2f(…)` et `440.0f * std::exp2f(…)`
/// font la même multiplication quand la valeur vaut 440 : les empreintes du parc
/// ne bougent pas, et c'est un attendu de H42.
///
/// QUI NE LE SUIT PAS, ET POURQUOI : les boîtes à rythmes et le sampler (leurs
/// réglages d'accord sont ceux d'une pièce, pas d'une note), la guimbarde (sa
/// lame a une fréquence fixe, que la note ne bouge pas). `test_diapason.cpp` les
/// nomme, et vérifie qu'elles rendent au bit près quel que soit le diapason.
///
/// Lecture et écriture relâchées : une valeur flottante isolée, lue au moment
/// d'une note, sans ordre à garantir avec rien d'autre. Sans verrou, sans
/// allocation — la règle de `process()`.
inline std::atomic<float>& diapasonPartage() noexcept {
    static std::atomic<float> la4{440.0f};
    return la4;
}

/// Le la4 courant, en hertz.
inline float diapason() noexcept { return diapasonPartage().load(std::memory_order_relaxed); }

/// Pose le la4. Une valeur non finie ou non positive rend 440 : la validation
/// des bornes (et le message qui dit ce qui est écarté) appartient à l'appelant,
/// qui sait d'où vient la valeur.
inline void setDiapason(float hz) noexcept {
    diapasonPartage().store(std::isfinite(hz) && hz > 0.0f ? hz : 440.0f, std::memory_order_relaxed);
}

/// Les bornes qu'un projet peut demander : de 400 à 480 Hz couvre le diapason
/// baroque (415), l'orchestre (442-446) et les enregistrements accélérés ou
/// ralentis d'un demi-ton au plus.
constexpr float kDiapasonMin = 400.0f;
constexpr float kDiapasonMax = 480.0f;

} // namespace vsm::audio::plugin
