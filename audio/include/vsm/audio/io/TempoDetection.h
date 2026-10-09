#pragma once
#include <cstdint>
#include <functional>
#include <string>

// D545.2 de docs/ROADMAP-daw.md — LE TEMPO D'UNE PRISE.
//
// Le tempo d'un clip ne se connaissait que DÉDUIT d'un nombre de mesures donné à la main (D12.6). Celui-ci
// le CHERCHE, en deux temps — le principe de librosa, que la chaîne d'analyse emploie déjà sur un mélange,
// avec un affinage qu'un DAW exige : on cale une boucle au dixième de BPM, pas à trois près.
//   1. GROSSIER : une enveloppe de force d'attaque (le flux d'énergie par bandes, le même découpage que
//      `detectOnsets`), son autocorrélation entre `minBpm` et `maxBpm`, pondérée par un a priori log-normal
//      centré sur 120 BPM (une octave d'écart-type) — c'est lui qui tranche entre un tempo et son double ;
//   2. FIN : le pic retenu, interpolé, puis ses MULTIPLES mesurés de même ; la période s'ajuste sur eux par
//      moindres carrés, et son erreur se divise par le nombre de périodes.
// Un bruit, une nappe tenue n'ont pas de tempo : sous un seuil de confiance, rien n'est rendu, et la raison
// est dite. Ni le premier temps de la mesure, ni un tempo qui varie.
namespace vsm::audio::io {

struct TempoEstimate {
    bool found = false;
    double bpm = 0.0;
    double confidence = 0.0;   ///< le pic d'autocorrélation retenu, normalisé (0 à 1)
    std::string reason;        ///< pourquoi il n'y a pas de tempo, quand `found` est faux
};

TempoEstimate estimateTempo(const std::function<bool(int64_t, float&, float&)>& frameAt, int64_t frames,
                            double sampleRate, double minBpm = 40.0, double maxBpm = 240.0);

/// Le seuil de confiance sous lequel aucun tempo n'est rendu.
constexpr double kTempoConfidenceFloor = 0.2;

} // namespace vsm::audio::io
