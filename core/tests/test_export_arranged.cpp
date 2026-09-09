#include "TestFramework.h"
#include "vsm/midi/MidiFileWriter.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <variant>

using namespace vsm::sequencer;
using namespace vsm::midi;

// D56.1 de docs/ROADMAP-daw.md — L'EXPORT MIDI ÉCRIT L'ARRANGEMENT.
//
// `toParsedFile` écrit le MATÉRIAU, ce qu'il faut pour `midi/arrangement.mid`
// dans un dossier de projet, où les clips l'accompagnent dans `project.json`.
// Ce n'est pas ce qu'il faut pour un fichier qu'on donne à quelqu'un d'autre :
// celui-là ne reçoit pas les clips, et jouait donc les notes qu'aucun clip ne
// montre tout en perdant les reprises des boucles.

namespace {

int noteOns(const ParsedFile& fichier) {
    int n = 0;
    for (const auto& piste : fichier.tracks)
        for (const auto& ev : piste.events)
            if (std::holds_alternative<NoteOnEvent>(ev.data)) ++n;
    return n;
}

std::vector<Tick> ticksDesNoteOn(const ParsedFile& fichier) {
    std::vector<Tick> ticks;
    for (const auto& piste : fichier.tracks)
        for (const auto& ev : piste.events)
            if (std::holds_alternative<NoteOnEvent>(ev.data)) ticks.push_back(ev.tick);
    return ticks;
}

/// Huit notes, une par mesure ; un seul clip montre les mesures 3 et 4, posé à
/// la mesure 1 et long de deux fenêtres — donc bouclé une fois.
Project projetDecoupe() {
    Project p;
    Track t;
    t.name = "Voix";
    uint64_t id = 1;
    for (int mesure = 0; mesure < 8; ++mesure)
        t.addNote(mesure * 1920, mesure * 1920 + 960,
                   static_cast<uint8_t>(60 + mesure), 100, 0, id);
    Clip c;
    c.sourceStart = 2 * 1920; c.sourceLength = 2 * 1920;
    c.startTick = 0;          c.length = 4 * 1920;
    c.id = 1;
    t.clips.push_back(c);
    p.tracks.push_back(std::move(t));
    return p;
}

} // namespace

VSM_TEST(l_export_arrange_ecrit_ce_que_la_lecture_joue) {
    const Project p = projetDecoupe();

    int jouees = 0;
    for (const auto& e : PlaybackScheduler::build(p, 0, 100 * 1920))
        if (std::holds_alternative<NoteOnEvent>(e.data)) ++jouees;

    // LE MATÉRIAU EN A HUIT, LA LECTURE EN JOUE QUATRE, et c'est l'export qui
    // en écrivait huit : quatre notes que personne n'entend, et la reprise de
    // la boucle perdue.
    VSM_ASSERT_EQ(p.tracks[0].notes.size(), size_t{8});
    VSM_ASSERT_EQ(noteOns(p.toParsedFile()), 8);
    VSM_ASSERT_EQ(jouees, 4);
    VSM_ASSERT_EQ(noteOns(p.toParsedFileArranged()), 4);

    const auto ticks = ticksDesNoteOn(p.toParsedFileArranged());
    VSM_ASSERT_EQ(ticks.size(), size_t{4});
    VSM_ASSERT_EQ(ticks[0], Tick{0});
    VSM_ASSERT_EQ(ticks[1], Tick{1920});
    VSM_ASSERT_EQ(ticks[2], Tick{3840});
    VSM_ASSERT_EQ(ticks[3], Tick{5760});
}

// UN PROJET SANS CLIP EXPORTE LE FICHIER D'AVANT, OCTET POUR OCTET. Une piste
// sans clip donne un passage identité : il n'y a pas un chemin historique à
// côté du chemin des clips, qui divergerait de lui à la première correction.
// Le témoin porte une note muette et une confiance, donc le bloc privé de D6.3
// — la partie la plus facile à perdre en réécrivant l'export.
VSM_TEST(sans_clip_l_export_arrange_est_celui_d_avant) {
    Project p;
    Track t;
    t.name = "Voix";
    uint64_t id = 1;
    t.addNote(0, 480, 60, 100, 0, id);
    t.addNote(960, 1440, 64, 90, 0, id);
    t.notes[1].muted = true;
    t.notes[0].confidence = 0.4f;
    t.controlChanges.push_back(CcPoint{240, 0, 74, 88});
    p.tracks.push_back(std::move(t));
    p.markers.push_back(Marker{960, "Refrain"});

    const auto materiau = MidiFileWriter::write(p.toParsedFile());
    const auto arrange = MidiFileWriter::write(p.toParsedFileArranged());
    VSM_ASSERT_EQ(arrange.size(), materiau.size());
    VSM_ASSERT(arrange == materiau);
}

// LA NOTE QUI DÉPASSE LA FIN DU CLIP EST COUPÉE, jamais laissée pendre : un
// NoteOff au-delà laisserait la note tenue pour toujours chez celui qui ouvre
// le fichier. C'est déjà la règle à la lecture ; l'export la partage.
VSM_TEST(une_note_qui_depasse_le_clip_est_coupee_a_sa_fin) {
    Project p;
    Track t;
    t.name = "Voix";
    uint64_t id = 1;
    t.addNote(0, 3840, 60, 100, 0, id);   // une note de deux mesures
    Clip c;
    c.sourceStart = 0; c.sourceLength = 1920;
    c.startTick = 0;   c.length = 1920;   // un clip d'UNE mesure
    c.id = 1;
    t.clips.push_back(c);
    p.tracks.push_back(std::move(t));

    Tick fin = -1;
    for (const auto& piste : p.toParsedFileArranged().tracks)
        for (const auto& ev : piste.events)
            if (std::holds_alternative<NoteOffEvent>(ev.data)) fin = ev.tick;
    VSM_ASSERT_EQ(fin, Tick{1920});
}

// UN CLIP MUET N'EXPORTE RIEN, comme il ne joue rien.
VSM_TEST(un_clip_muet_n_exporte_pas_ses_notes) {
    Project p = projetDecoupe();
    p.tracks[0].clips[0].muted = true;
    VSM_ASSERT_EQ(noteOns(p.toParsedFileArranged()), 0);
    // Le matériau, lui, est intact : le muet est une propriété du clip.
    VSM_ASSERT_EQ(noteOns(p.toParsedFile()), 8);
}
