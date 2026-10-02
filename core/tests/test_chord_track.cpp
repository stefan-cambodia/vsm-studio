#include "TestFramework.h"
#include "vsm/sequencer/ChordTrack.h"
#include "vsm/sequencer/PlayOrder.h"
#include "vsm/sequencer/TimeEdit.h"
#include <string>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D532.3 de docs/ROADMAP-daw.md — LA LIGNE D'ACCORDS, le modèle.
//
// Le symbole est le format : il doit faire l'aller-retour pour CHAQUE accord que la
// ligne peut porter, et refuser ce qu'il ne sait pas lire. Le calage suit la règle des
// gammes — la plus proche, le grave à égalité — et compte ce qu'il ne peut pas faire.

namespace {
ChordEvent accord(Tick tick, uint8_t racine, ChordType type, int basse = -1) {
    ChordEvent a;
    a.tick = tick;
    a.root = racine;
    a.type = type;
    a.bass = basse;
    return a;
}

Note note(uint64_t id, Tick debut, uint8_t hauteur) {
    Note n;
    n.id = id;
    n.startTick = debut;
    n.endTick = debut + 240;
    n.number = hauteur;
    n.velocity = 100;
    return n;
}
} // namespace

// LES 13 TYPES × 12 FONDAMENTALES × (sans basse, et chaque basse) : 1 716 accords, chacun
// relu à l'identique. Un suffixe oublié dans la table, ou deux types au même suffixe,
// tombent ici.
VSM_TEST(chaque_accord_fait_l_aller_retour_par_son_symbole) {
    size_t lus = 0;
    for (ChordType type : allChordTypes())
        for (uint8_t racine = 0; racine < 12; ++racine)
            for (int basse = -1; basse < 12; ++basse) {
                const ChordEvent a = accord(960, racine, type, basse);
                ChordEvent relu = accord(960, 0, ChordType::Major);
                VSM_ASSERT(parseChordSymbol(chordSymbol(a), relu));
                VSM_ASSERT(relu == a);
                ++lus;
            }
    VSM_ASSERT_EQ(lus, static_cast<size_t>(13 * 12 * 13));
}

VSM_TEST(les_symboles_s_ecrivent_comme_on_les_lit) {
    VSM_ASSERT_EQ(chordSymbol(accord(0, 9, ChordType::Minor7, 7)), std::string("Am7/G"));
    VSM_ASSERT_EQ(chordSymbol(accord(0, 6, ChordType::Minor7Flat5)), std::string("F#m7b5"));
    VSM_ASSERT_EQ(chordSymbol(accord(0, 0, ChordType::Power)), std::string("C5"));
    VSM_ASSERT_EQ(chordSymbol(accord(0, 7, ChordType::Dominant7)), std::string("G7"));
}

// LES BÉMOLS SE LISENT, ET S'ÉCRIVENT EN DIÈSES.
VSM_TEST(un_bemol_se_lit_comme_son_diese) {
    ChordEvent a;
    VSM_ASSERT(parseChordSymbol("Bb7", a));
    VSM_ASSERT_EQ(chordSymbol(a), std::string("A#7"));
    VSM_ASSERT(parseChordSymbol("Ebm/Gb", a));
    VSM_ASSERT_EQ(chordSymbol(a), std::string("D#m/F#"));
}

// UN SYMBOLE INCONNU EST REFUSÉ, et la sortie n'est pas touchée : deviner « Cmaj13 » en
// « Cmaj7 » ferait jouer à la ligne un accord que personne n'a écrit.
VSM_TEST(un_symbole_inconnu_est_refuse_sans_toucher_la_sortie) {
    const ChordEvent avant = accord(480, 2, ChordType::Sus4, 5);
    for (const char* faux : {"", "H", "Cmaj13", "C/", "C/X", "Am7/G#x", "c", "Cmin", " C"}) {
        ChordEvent sortie = avant;
        VSM_ASSERT(!parseChordSymbol(faux, sortie));
        VSM_ASSERT(sortie == avant);
    }
}

VSM_TEST(l_accord_en_vigueur_est_le_dernier_commence) {
    std::vector<ChordEvent> ligne;
    setChordAt(ligne, accord(1920, 7, ChordType::Major));
    setChordAt(ligne, accord(0, 0, ChordType::Major));
    setChordAt(ligne, accord(960, 9, ChordType::Minor));
    VSM_ASSERT_EQ(ligne.size(), static_cast<size_t>(3));
    VSM_ASSERT(ligne[0].tick == 0 && ligne[1].tick == 960 && ligne[2].tick == 1920);   // triée
    std::vector<ChordEvent> decalee = ligne;
    for (auto& a : decalee) a.tick += 100;
    VSM_ASSERT(chordAt(decalee, 50) == nullptr);                 // avant le premier
    VSM_ASSERT_EQ(chordAt(ligne, 960)->root, static_cast<uint8_t>(9));   // au tick exact
    VSM_ASSERT_EQ(chordAt(ligne, 1500)->root, static_cast<uint8_t>(9));  // entre deux
    VSM_ASSERT_EQ(chordAt(ligne, 99999)->root, static_cast<uint8_t>(7)); // après le dernier
    // Poser au tick d'un autre le REMPLACE.
    setChordAt(ligne, accord(960, 5, ChordType::Major7));
    VSM_ASSERT_EQ(ligne.size(), static_cast<size_t>(3));
    VSM_ASSERT_EQ(chordSymbol(*chordAt(ligne, 960)), std::string("Fmaj7"));
    VSM_ASSERT(removeChordAt(ligne, 960));
    VSM_ASSERT(!removeChordAt(ligne, 960));
    VSM_ASSERT_EQ(chordAt(ligne, 1500)->root, static_cast<uint8_t>(0));
}

// LE CALAGE : une note de l'accord reste ; une autre va à la plus proche, le grave à
// égalité ; une note avant le premier accord reste et se COMPTE ; une note non choisie ne
// bouge pas.
VSM_TEST(caler_sur_les_accords_suit_la_regle_des_gammes_et_compte_ce_qu_il_laisse) {
    std::vector<ChordEvent> ligne;
    setChordAt(ligne, accord(480, 0, ChordType::Major));        // C : do mi sol
    setChordAt(ligne, accord(1920, 9, ChordType::Minor, 7));    // Am/G : la do mi + sol
    std::vector<Note> notes = {
        note(1, 480, 64),    // mi, dans C : reste
        note(2, 600, 66),    // fa# : mi (64) et sol (67) à 2 et 1 -> sol
        note(3, 700, 62),    // ré : do (60) et mi (64) à égale distance -> le grave, do
        note(4, 0, 61),      // avant le premier accord : reste, compté
        note(5, 2000, 66),   // fa#, sous Am/G : sol (67) à 1 -> sol (la basse compte)
        note(6, 800, 61),    // non choisie : ne bouge pas
    };
    const ChordSnapReport bilan = snapNotesToChords(notes, {1, 2, 3, 4, 5}, ligne, {ClipPassage{}});
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 64);
    VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 67);
    VSM_ASSERT_EQ(static_cast<int>(notes[2].number), 60);
    VSM_ASSERT_EQ(static_cast<int>(notes[3].number), 61);
    VSM_ASSERT_EQ(static_cast<int>(notes[4].number), 67);
    VSM_ASSERT_EQ(static_cast<int>(notes[5].number), 61);
    VSM_ASSERT_EQ(bilan.moved, static_cast<size_t>(3));
    VSM_ASSERT_EQ(bilan.alreadyInChord, static_cast<size_t>(1));
    VSM_ASSERT_EQ(bilan.withoutChord, static_cast<size_t>(1));
    // Rien de choisi, rien de fait.
    const ChordSnapReport rien = snapNotesToChords(notes, {}, ligne, {ClipPassage{}});
    VSM_ASSERT_EQ(rien.moved + rien.alreadyInChord + rien.withoutChord, static_cast<size_t>(0));
}

// LA LIGNE D'ACCORDS SUIT LES GESTES DE TEMPS (D532.3, ajout) : laissée en place, elle
// décrocherait de ses notes en silence, et « caler sur les accords » calerait sur
// l'harmonie d'une autre mesure.
namespace {
double enSecondes(Tick tick) { return static_cast<double>(tick) / 960.0; }

/// C à 0, Am à 1920, F à 2880, G à 3840 — et une note à chaque changement.
Project projetAccorde() {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track t;
    uint64_t ids = 1;
    for (Tick debut : {Tick(0), Tick(1920), Tick(2880), Tick(3840)}) t.addNote(debut, debut + 480, 60, 100, 0, ids);
    p.tracks.push_back(t);
    setChordAt(p.chords, accord(0, 0, ChordType::Major));
    setChordAt(p.chords, accord(1920, 9, ChordType::Minor));
    setChordAt(p.chords, accord(2880, 5, ChordType::Major));
    setChordAt(p.chords, accord(3840, 7, ChordType::Major));
    return p;
}

std::string ligne(const Project& p) {
    std::string texte;
    for (const auto& a : p.chords) texte += std::to_string(a.tick) + ":" + chordSymbol(a) + " ";
    return texte;
}
} // namespace

VSM_TEST(inserer_du_temps_fait_glisser_les_accords_d_apres) {
    Project p = projetAccorde();
    VSM_ASSERT(insertTime(p, 1920, 960, enSecondes) > 0);
    VSM_ASSERT_EQ(ligne(p), std::string("0:C 2880:Am 3840:F 4800:G "));
}

// SUPPRIMER [2400, 3360) : Am (1920) reste, F (2880) est DANS la plage — mais c'est lui qui
// était en vigueur à la fin de la plage, et la musique d'après la coupe était sous F : il
// est reposé au point de coupe. G glisse de 960.
VSM_TEST(supprimer_du_temps_repose_au_raccord_l_accord_en_vigueur_a_la_fin) {
    Project p = projetAccorde();
    VSM_ASSERT(deleteTime(p, 2400, 3360, enSecondes) > 0);
    VSM_ASSERT_EQ(ligne(p), std::string("0:C 1920:Am 2400:F 2880:G "));
    // La note qui jouait à 3840 sous G joue à 2880, toujours sous G.
    VSM_ASSERT_EQ(chordSymbol(*chordAt(p.chords, p.tracks[0].notes.back().startTick)), std::string("G"));
    // Une plage qui finit PILE sur un accord ne repose rien : il glisse de lui-même.
    Project q = projetAccorde();
    VSM_ASSERT(deleteTime(q, 2400, 3840, enSecondes) > 0);
    VSM_ASSERT_EQ(ligne(q), std::string("0:C 1920:Am 2400:G "));
    // Une plage sans changement d'accord ne touche à rien d'autre que le glissement.
    Project r = projetAccorde();
    VSM_ASSERT(deleteTime(r, 2000, 2400, enSecondes) > 0);
    VSM_ASSERT_EQ(ligne(r), std::string("0:C 1920:Am 2480:F 3440:G "));
}

// APLATIR B, A : sections A [0, 1920) et B [1920, 4320). B arrive à 0, ouverte par Am (en
// vigueur à son entrée), avec F et G décalés ; A suit à 2400, ouverte par C.
VSM_TEST(aplatir_l_ordre_de_lecture_emporte_les_accords_de_chaque_section) {
    Project p = projetAccorde();
    p.markers.push_back({0, "A"});
    p.markers.push_back({1920, "B"});
    VSM_ASSERT(flattenPlayOrder(p, {1, 0}));
    VSM_ASSERT_EQ(ligne(p), std::string("0:Am 960:F 1920:G 2400:C "));
    // Une section qui commence ENTRE deux accords s'ouvre par celui qui y était en vigueur.
    Project q = projetAccorde();
    q.markers.push_back({0, "A"});
    q.markers.push_back({2400, "B"});
    VSM_ASSERT(flattenPlayOrder(q, {1}));
    VSM_ASSERT_EQ(ligne(q), std::string("0:Am 480:F 1440:G "));
}

// D532.3 bis : LE CALAGE REGARDE OÙ LA NOTE SONNE, pas où elle est rangée. Une note est du
// matériau ; un clip est une fenêtre sur lui, posée ailleurs, et deux clips peuvent lire la
// même note. Les accords sont sur la ligne de temps.
namespace {
/// C (do mi sol) à 0, D (ré fa# la) à 1920.
std::vector<ChordEvent> cPuisD() {
    std::vector<ChordEvent> ligne;
    setChordAt(ligne, accord(0, 0, ChordType::Major));
    setChordAt(ligne, accord(1920, 2, ChordType::Major));
    return ligne;
}

/// Un fa (65) de matériau au tick 0, lu par des clips posés aux ticks donnés.
Track pisteDuFa(std::vector<Tick> poses, bool muet = false) {
    Track piste;
    piste.notes = {note(1, 0, 65)};
    for (Tick pose : poses) {
        Clip clip;
        clip.sourceStart = 0;
        clip.sourceLength = 1920;
        clip.startTick = pose;
        clip.length = 1920;
        clip.muted = muet;
        piste.clips.push_back(clip);
    }
    return piste;
}

ChordSnapReport caler(Track& piste, const std::vector<ChordEvent>& ligne) {
    return snapNotesToChords(piste.notes, {1}, ligne, clipPassages(piste, 1920));
}
} // namespace

// Rangé à 0 (sous C, il irait à mi, 64), le fa SONNE à 1920, sous D : il va à fa# (66).
VSM_TEST(un_clip_deplace_cale_sa_note_sur_l_accord_ou_elle_sonne) {
    Track piste = pisteDuFa({1920});
    const ChordSnapReport bilan = caler(piste, cPuisD());
    VSM_ASSERT_EQ(static_cast<int>(piste.notes[0].number), 66);
    VSM_ASSERT_EQ(bilan.moved, static_cast<size_t>(1));
    // Le témoin : le même clip laissé en place cale sous C.
    Track enPlace = pisteDuFa({0});
    caler(enPlace, cPuisD());
    VSM_ASSERT_EQ(static_cast<int>(enPlace.notes[0].number), 64);
}

// Deux clips lisent le même fa, l'un sous C, l'autre sous D : pas de bonne réponse — il
// reste, et il est compté. Sous la même harmonie deux fois, il se cale.
VSM_TEST(une_note_entendue_sous_deux_harmonies_reste_et_se_compte) {
    Track piste = pisteDuFa({0, 1920});
    const ChordSnapReport bilan = caler(piste, cPuisD());
    VSM_ASSERT_EQ(static_cast<int>(piste.notes[0].number), 65);
    VSM_ASSERT_EQ(bilan.ambiguous, static_cast<size_t>(1));
    VSM_ASSERT_EQ(bilan.moved, static_cast<size_t>(0));

    std::vector<ChordEvent> deuxFoisC;
    setChordAt(deuxFoisC, accord(0, 0, ChordType::Major));
    setChordAt(deuxFoisC, accord(1920, 0, ChordType::Major));
    Track memeHarmonie = pisteDuFa({0, 1920});
    const ChordSnapReport cale = caler(memeHarmonie, deuxFoisC);
    VSM_ASSERT_EQ(static_cast<int>(memeHarmonie.notes[0].number), 64);
    VSM_ASSERT_EQ(cale.moved, static_cast<size_t>(1));

    // Csus2 (do ré sol) et Gsus4 (sol do ré) : mêmes classes, donc une seule harmonie.
    std::vector<ChordEvent> memesClasses;
    setChordAt(memesClasses, accord(0, 0, ChordType::Sus2));
    setChordAt(memesClasses, accord(1920, 7, ChordType::Sus4));
    Track sus = pisteDuFa({0, 1920});
    VSM_ASSERT_EQ(caler(sus, memesClasses).moved, static_cast<size_t>(1));

    // Entendue AVANT le premier accord et SOUS un accord : ambiguë aussi.
    std::vector<ChordEvent> tardif;
    setChordAt(tardif, accord(1920, 2, ChordType::Major));
    Track avantEtSous = pisteDuFa({0, 1920});
    VSM_ASSERT_EQ(caler(avantEtSous, tardif).ambiguous, static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(avantEtSous.notes[0].number), 65);
}

// Une note qu'aucun clip ne fait entendre — hors fenêtre, ou sous un clip muet — reste, et
// elle est comptée muette : caler ce qu'on n'entend pas, c'est deviner.
VSM_TEST(une_note_entendue_nulle_part_reste_et_se_compte) {
    Track muet = pisteDuFa({0}, true);
    const ChordSnapReport bilan = caler(muet, cPuisD());
    VSM_ASSERT_EQ(bilan.unheard, static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(muet.notes[0].number), 65);

    Track horsFenetre = pisteDuFa({0});
    horsFenetre.notes[0].startTick = 2000;   // la fenêtre lit [0, 1920)
    horsFenetre.notes[0].endTick = 2200;
    VSM_ASSERT_EQ(caler(horsFenetre, cPuisD()).unheard, static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(horsFenetre.notes[0].number), 65);
}
