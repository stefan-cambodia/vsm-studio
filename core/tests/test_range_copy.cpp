#include "TestFramework.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ChordTrack.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/RangeCopy.h"
#include "vsm/sequencer/TimeEdit.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

using namespace vsm::sequencer;
using vsm::midi::Tick;

// D535.3 de docs/ROADMAP-daw.md — COPIER UNE PLAGE SUR TOUTES LES PISTES, ET LA COLLER EN
// INSÉRANT. Le critère est ce qu'on ENTEND (le planificateur réel, en couples (ms, hauteur) par
// piste — la leçon de D537 : l'ordre seul ne voit pas le temps), pas ce que contient le modèle.

namespace {

/// Deux pistes à 120 BPM (1 920 ticks = 2 000 ms). La première a des clips, dont un DÉPLACÉ qui
/// lit un matériau lointain ; la seconde n'en a pas, et porte un volume (CC 7) posé au début, une
/// coupure (CC 74) dans la mesure 2 et une courbe qui monte à travers elle. Trois accords.
Project projet() {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track clips;
    clips.name = "Clips";
    uint64_t ids = 1;
    clips.addNote(0, 480, 60, 100, 0, ids);
    clips.addNote(1920, 2400, 62, 100, 0, ids);
    clips.addNote(3840, 4320, 64, 100, 0, ids);
    clips.addNote(9000, 9240, 72, 100, 0, ids);   // le matériau lointain
    const auto clip = [](Tick source, Tick debut, Tick longueur) {
        Clip c;
        c.sourceStart = source; c.sourceLength = longueur; c.startTick = debut; c.length = longueur;
        return c;
    };
    clips.clips = {clip(0, 0, 1920), clip(1920, 1920, 480), clip(9000, 2400, 480), clip(3840, 3840, 1920)};
    p.tracks.push_back(clips);
    Track nue;
    nue.name = "Sans clip";
    nue.addNote(0, 480, 48, 100, 0, ids);
    nue.addNote(1920, 2400, 50, 100, 0, ids);
    nue.addNote(3840, 4320, 52, 100, 0, ids);
    nue.controlChanges = {{0, 0, 7, 100}, {2000, 0, 74, 90}};
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points = {{1920, 0.0f, false, 0.0f}, {3840, 1.0f, false, 0.0f}};
    nue.automation.push_back(courbe);
    p.tracks.push_back(nue);
    for (const auto& [tick, symbole] : {std::pair<Tick, const char*>{0, "C"}, {1920, "G"}, {3840, "F"}}) {
        ChordEvent a;
        a.tick = tick;
        parseChordSymbol(symbole, a);
        setChordAt(p.chords, a);
    }
    p.assignClipIds();
    p.assignTrackUids();
    return p;
}

/// (piste, ms, hauteur) de chaque note ENTENDUE, triés.
std::vector<std::tuple<size_t, int, int>> entendu(const Project& p) {
    std::vector<std::tuple<size_t, int, int>> notes;
    for (const auto& e : PlaybackScheduler::build(p, 0, 1000000))
        if (const auto* on = std::get_if<vsm::midi::NoteOnEvent>(&e.data))
            notes.emplace_back(e.trackIndex, static_cast<int>(std::lround(e.timeSeconds * 1000.0)),
                               static_cast<int>(on->note));
    std::sort(notes.begin(), notes.end());
    return notes;
}

using Entendu = std::vector<std::tuple<size_t, int, int>>;

std::function<double(Tick)> secondes(const Project& p) {
    return [&p](Tick t) { return p.ticksToSeconds(t); };
}

std::string accords(const Project& p) {
    std::string s;
    for (const auto& a : p.chords) s += std::to_string(a.tick) + ":" + chordSymbol(a) + " ";
    return s;
}

} // namespace

// LE TÉMOIN : ce que le projet fait entendre avant tout geste.
VSM_TEST(le_projet_d_essai_fait_entendre_ce_qu_on_croit) {
    const Project p = projet();
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 2000, 62}, {0, 2500, 72}, {0, 4000, 64},
                                      {1, 0, 48}, {1, 2000, 50}, {1, 4000, 52}}));
}

// COPIER LA MESURE 2, COLLER À LA MESURE 3 : la mesure 2 s'entend deux fois, le reste une mesure
// plus tard — le clip déplacé compris, sur les deux pistes.
VSM_TEST(copier_la_mesure_2_et_la_coller_a_la_mesure_3_la_fait_entendre_deux_fois) {
    Project p = projet();
    const RangeClipboard copie = copyRange(p, 1920, 3840, secondes(p));
    VSM_ASSERT_EQ(copie.length, Tick(1920));
    const RangePasteReport bilan = pasteRangeInserting(p, copie, 3840, secondes(p));
    VSM_ASSERT_EQ(bilan.tracksPasted, size_t(2));
    VSM_ASSERT_EQ(bilan.tracksMissing, size_t(0));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 2000, 62}, {0, 2500, 72}, {0, 4000, 62}, {0, 4500, 72},
                                      {0, 6000, 64}, {1, 0, 48}, {1, 2000, 50}, {1, 4000, 50}, {1, 6000, 52}}));
    // La piste SANS clip le reste : lui en donner un la ferait taire ailleurs.
    VSM_ASSERT(p.tracks[1].clips.empty());
    // LA COURBE : à 3 840 elle vaut ce qu'elle valait à 1 920 ; à 5 760, ce qu'elle valait à 3 840.
    VSM_ASSERT_NEAR(automationValueAt(p.tracks[1].automation[0], 3840), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(p.tracks[1].automation[0], 5760), 1.0f, 1e-6f);
    // LES CONTRÔLEURS : la coupure voyage (2 000 → 3 920), le volume d'avant ouvre la copie.
    bool coupureCopiee = false, volumeOuvre = false;
    for (const auto& c : p.tracks[1].controlChanges) {
        if (c.tick == 3920 && c.controller == 74 && c.value == 90) coupureCopiee = true;
        if (c.tick == 3840 && c.controller == 7 && c.value == 100) volumeOuvre = true;
    }
    VSM_ASSERT(coupureCopiee && volumeOuvre);
    // LES ACCORDS : la plage (G), puis l'accord qui était à la tête (F) reprend après.
    VSM_ASSERT_EQ(accords(p), std::string("0:C 1920:G 3840:G 5760:F "));
}

// COLLER AU MILIEU D'UN CLIP LE COUPE : ce qui le précède reste, ce qui le suit glisse.
VSM_TEST(coller_au_milieu_d_un_clip_le_coupe) {
    Project p = projet();
    const RangeClipboard copie = copyRange(p, 1920, 3840, secondes(p));
    pasteRangeInserting(p, copie, 4800, secondes(p));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 2000, 62}, {0, 2500, 72}, {0, 4000, 64}, {0, 5000, 62},
                                      {0, 5500, 72}, {1, 0, 48}, {1, 2000, 50}, {1, 4000, 52}, {1, 5000, 50}}));
    bool premiereMoitie = false, secondeMoitie = false;
    for (const auto& c : p.tracks[0].clips) {
        if (c.startTick == 3840 && c.length == 960) premiereMoitie = true;
        if (c.startTick == 4800 + 1920 && c.length == 960) secondeMoitie = true;
    }
    VSM_ASSERT(premiereMoitie && secondeMoitie);
}

// UNE PISTE SUPPRIMÉE ENTRE LA COPIE ET LE COLLAGE est laissée, et comptée.
VSM_TEST(une_piste_supprimee_depuis_la_copie_est_laissee_et_comptee) {
    Project p = projet();
    const RangeClipboard copie = copyRange(p, 1920, 3840, secondes(p));
    removeTrack(p, 0);
    const RangePasteReport bilan = pasteRangeInserting(p, copie, 3840, secondes(p));
    VSM_ASSERT_EQ(bilan.tracksPasted, size_t(1));
    VSM_ASSERT_EQ(bilan.tracksMissing, size_t(1));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 48}, {0, 2000, 50}, {0, 4000, 50}, {0, 6000, 52}}));
}

// UNE PLAGE VIDE NE COLLE RIEN, et ne touche à rien.
VSM_TEST(une_plage_vide_ne_colle_rien) {
    Project p = projet();
    const auto avant = entendu(p);
    const RangeClipboard copie = copyRange(p, 1920, 1920, secondes(p));
    VSM_ASSERT(copie.empty());
    const RangePasteReport bilan = pasteRangeInserting(p, copie, 3840, secondes(p));
    VSM_ASSERT_EQ(bilan.tracksPasted + bilan.tracksMissing + bilan.notes + bilan.clips, size_t(0));
    VSM_ASSERT(entendu(p) == avant);
}

// COUPER = copier puis supprimer le temps ; recoller au début remet la plage devant.
VSM_TEST(couper_puis_coller_au_debut_remet_la_plage_devant) {
    Project p = projet();
    const RangeClipboard copie = copyRange(p, 1920, 3840, secondes(p));
    deleteTime(p, 1920, 3840, secondes(p));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 2000, 64}, {1, 0, 48}, {1, 2000, 52}}));
    pasteRangeInserting(p, copie, 0, secondes(p));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 62}, {0, 500, 72}, {0, 2000, 60}, {0, 4000, 64},
                                      {1, 0, 50}, {1, 2000, 48}, {1, 4000, 52}}));
}

// UN CLIP MUET COPIÉ RESTE MUET, et ses notes sont collées sous lui.
VSM_TEST(un_clip_muet_copie_reste_muet_et_garde_ses_notes) {
    Project p = projet();
    for (auto& c : p.tracks[0].clips)
        if (c.startTick == 1920) c.muted = true;
    const RangeClipboard copie = copyRange(p, 1920, 3840, secondes(p));
    pasteRangeInserting(p, copie, 3840, secondes(p));
    // SOUS LUI, c'est-à-dire dans SA FENÊTRE — depuis D539, le collage d'une piste à clips pose ses
    // notes dans un matériau neuf, et la fenêtre du clip collé les y lit.
    const Clip* muet = nullptr;
    for (const auto& c : p.tracks[0].clips)
        if (c.muted && c.startTick == 3840) muet = &c;
    VSM_ASSERT(muet != nullptr);
    size_t sousLui = 0;
    for (const auto& n : p.tracks[0].notes)
        if (n.number == 62 && n.startTick >= muet->sourceStart && n.startTick < muet->sourceStart + muet->sourceLength)
            ++sousLui;
    VSM_ASSERT_EQ(sousLui, size_t(1));
    // Et on ne l'entend pas : le ré de la copie se tait, comme celui de la plage.
    for (const auto& [piste, ms, hauteur] : entendu(p)) VSM_ASSERT(!(piste == 0 && hauteur == 62));
}

// COLLER AU MILIEU D'UN SEGMENT : ce qui suit le trou reprend comme avant. Collée à 2 880 — où la
// courbe montait (0,5) et où G était en vigueur —, la copie de la mesure 3 (F, courbe à 1,0) est
// suivie, à 4 800, de la courbe à 0,5 et de G. Sans ces deux raccords, la courbe resterait à 1,0
// et F tiendrait jusqu'à 5 760 : le premier test, qui collait PILE sur un point et sur un accord,
// ne pouvait pas le voir (`insertTime` y fait déjà le travail).
VSM_TEST(coller_au_milieu_d_un_segment_raccorde_la_courbe_et_l_accord_qui_suivent) {
    Project p = projet();
    const float avantLaTete = automationValueAt(p.tracks[1].automation[0], 2879);
    const RangeClipboard copie = copyRange(p, 3840, 5760, secondes(p));
    pasteRangeInserting(p, copie, 2880, secondes(p));
    const auto& courbe = p.tracks[1].automation[0];
    VSM_ASSERT_NEAR(automationValueAt(courbe, 2879), avantLaTete, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 2880), 1.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 4800), 0.5f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 5760), 1.0f, 1e-6f);
    VSM_ASSERT_EQ(accords(p), std::string("0:C 1920:G 2880:F 4800:G 5760:F "));
}

// D539, attendu 3 : COLLER SUR UNE PISTE À COPIE LIÉE. Le motif (do à 0, sol à 960) et sa copie
// liée à 1 920 lisent le même matériau ; coller la première noire à 1 920 fait entendre la plage,
// et la copie, reculée d'une plage, reste intacte.
VSM_TEST(coller_sur_une_piste_a_copie_liee_laisse_la_copie_intacte) {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.addNote(960, 1200, 67, 100, 0, ids);
    Clip motif, copie;
    motif.sourceStart = 0; motif.sourceLength = 1920; motif.startTick = 0;    motif.length = 1920;
    copie = motif; copie.startTick = 1920;
    piste.clips = {motif, copie};
    p.tracks.push_back(piste);
    p.assignClipIds();
    p.assignTrackUids();
    const RangeClipboard plage = copyRange(p, 0, 960, secondes(p));
    pasteRangeInserting(p, plage, 1920, secondes(p));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 1000, 67}, {0, 2000, 60}, {0, 3000, 60}, {0, 4000, 67}}));
}

// D539, attendu 4 : UNE FENÊTRE OUVERTE (`sourceLength` nul : « jusqu'au bout du matériau ») posée
// APRÈS la tête est bornée à ce qu'elle lisait — sinon, glissée sans être coupée, elle lirait aussi
// le matériau neuf du collage. (Une fenêtre ouverte qui commence AVANT la tête est coupée, donc
// bornée, par `insertTime` : un premier essai sur ce cas-là ne pouvait rien voir.)
VSM_TEST(une_fenetre_ouverte_n_avale_pas_le_materiau_neuf_du_collage) {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.addNote(960, 1200, 64, 100, 0, ids);
    Clip motif, ouvert;
    motif.sourceStart = 0; motif.sourceLength = 1920; motif.startTick = 0; motif.length = 1920;
    ouvert.sourceStart = 0; ouvert.sourceLength = 0; ouvert.startTick = 3840; ouvert.length = 0;
    piste.clips = {motif, ouvert};
    p.tracks.push_back(piste);
    p.assignClipIds();
    p.assignTrackUids();
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 1000, 64}, {0, 4000, 60}, {0, 5000, 64}}));   // le témoin
    const RangeClipboard plage = copyRange(p, 0, 480, secondes(p));
    pasteRangeInserting(p, plage, 1920, secondes(p));
    VSM_ASSERT(entendu(p) == (Entendu{{0, 0, 60}, {0, 1000, 64}, {0, 2000, 60}, {0, 4500, 60}, {0, 5500, 64}}));
}
