#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/TimeEdit.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;

// D540 de docs/ROADMAP-daw.md — LE MOTEUR PLANIFIE JUSQU'À LA FIN DE CE QUE LE PROJET FAIT
// ENTENDRE. `ProcessGraph::setProject` s'arrêtait à la fin du MATÉRIAU (`lastUsedTick`) : une copie
// liée posée au-delà, ou la fin d'un morceau où l'on a inséré du temps (D539 ne fait plus glisser
// le matériau d'une piste à clips), ne sonnait ni à la lecture ni à l'export. On ÉCOUTE, à travers
// le vrai graphe, la note qui doit sonner.

namespace {

float crete(const std::vector<float>& buffer) {
    float valeur = 0.0f;
    for (float e : buffer) valeur = std::max(valeur, std::abs(e));
    return valeur;
}

/// La crête de [debut, debut + duree) secondes, le graphe joué depuis `debut`.
float creteEntre(const Project& projet, double debut, double duree) {
    ProcessGraph graphe;
    graphe.prepare(8000.0, 256);
    graphe.setProject(projet);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.seekSeconds(debut);
    graphe.setPlaying(true);
    const int total = static_cast<int>(8000.0 * duree);
    std::vector<float> gauche(static_cast<size_t>(total), 0.0f), droite(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; i += 256) {
        const int n = std::min(256, total - i);
        graphe.processBlock(gauche.data() + i, droite.data() + i, n);
    }
    return crete(gauche);
}

/// Un motif (do à 0, sol à 960, 240 ticks chacun) et sa COPIE LIÉE à 1 920 : le matériau finit à
/// 1 200, la copie à 3 840. 120 BPM : 960 ticks = 1 s.
Project motifEtCopie() {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.addNote(960, 1200, 67, 100, 0, ids);
    Clip motif;
    motif.sourceStart = 0; motif.sourceLength = 1920; motif.startTick = 0; motif.length = 1920;
    Clip copie = motif;
    copie.startTick = 1920;
    piste.clips = {motif, copie};
    p.tracks.push_back(piste);
    p.assignClipIds();
    return p;
}

} // namespace

VSM_TEST(une_copie_liee_au_dela_du_materiau_sonne_dans_le_moteur) {
    const Project p = motifEtCopie();
    VSM_ASSERT(creteEntre(p, 0.0, 0.2) > 0.01f);   // le témoin : le do du motif
    VSM_ASSERT(creteEntre(p, 2.0, 0.2) > 0.01f);   // le do de la copie, à 2 s
}

VSM_TEST(apres_une_insertion_la_fin_d_un_morceau_a_clips_sonne_dans_le_moteur) {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 480, 60, 100, 0, ids);
    piste.addNote(2880, 3360, 67, 100, 0, ids);
    Clip c;
    c.sourceStart = 0; c.sourceLength = 3840; c.startTick = 0; c.length = 3840;
    piste.clips = {c};
    p.tracks.push_back(piste);
    p.assignClipIds();
    insertTime(p, 0, 1920, [&p](Tick t) { return p.ticksToSeconds(t); });
    VSM_ASSERT(creteEntre(p, 2.0, 0.2) > 0.01f);   // le do, poussé à 2 s
    VSM_ASSERT(creteEntre(p, 5.0, 0.2) > 0.01f);   // le sol, poussé à 5 s, au-delà du matériau
}
