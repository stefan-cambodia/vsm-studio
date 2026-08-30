#include "TestFramework.h"
#include "vsm/interchange/MidiLearnStore.h"
#include <string>

using namespace vsm::interchange;
using namespace vsm::audio::engine;

// D10.2 de docs/ROADMAP-daw.md — LE MIDI LEARN PERSISTANT.
//
// « Un potentiomètre physique s'en souvient d'une session à l'autre. » Ce qui
// se perdait n'était pas une préférence de confort : c'était le travail de
// câblage d'un studio, refait à chaque lancement.

namespace {

MidiLearnTarget cible(MidiLearnKind kind, size_t piste = 0, uint32_t param = 0) {
    MidiLearnTarget t;
    t.kind = kind;
    t.trackIndex = piste;
    t.paramId = param;
    t.valid = true;
    return t;
}

} // namespace

VSM_TEST(a_learned_mapping_survives_a_round_trip) {
    MidiLearnMap carte;
    MidiLearnTarget filtre = cible(MidiLearnKind::InstrumentParam, 3, 12);
    filtre.min = 20.0f;
    filtre.max = 18000.0f;
    carte.bind(74, filtre);
    carte.bind(7, cible(MidiLearnKind::TrackVolume, 2));
    carte.bind(64, cible(MidiLearnKind::TransportPlay));
    MidiLearnTarget depart = cible(MidiLearnKind::TrackSend, 5);
    depart.slot = 2;
    carte.bind(91, depart);

    const auto relue = midiLearnFromJson(midiLearnToJson(carte));
    VSM_ASSERT(relue.success);
    VSM_ASSERT_EQ(relue.discarded, size_t{0});
    VSM_ASSERT_EQ(relue.map.size(), size_t{4});

    MidiLearnTarget sortie;
    float valeur = 0.0f;
    VSM_ASSERT(relue.map.resolve(74, 127, sortie, valeur));
    VSM_ASSERT(sortie.kind == MidiLearnKind::InstrumentParam);
    VSM_ASSERT_EQ(sortie.trackIndex, size_t{3});
    VSM_ASSERT_EQ(sortie.paramId, uint32_t{12});
    // La plage aussi : sans elle, le potentiomètre retrouverait son paramètre
    // mais le règlerait entre 0 et 1 au lieu de 20 Hz à 18 kHz.
    VSM_ASSERT_NEAR(valeur, 18000.0f, 0.01f);

    VSM_ASSERT(relue.map.resolve(91, 0, sortie, valeur));
    VSM_ASSERT(sortie.kind == MidiLearnKind::TrackSend);
    VSM_ASSERT_EQ(static_cast<int>(sortie.slot), 2);
}

VSM_TEST(nothing_saved_yet_is_not_an_error) {
    // AU PREMIER LANCEMENT IL N'Y A RIEN, et ce n'est pas une panne. Traiter
    // l'absence comme une erreur ferait apparaître un message au premier
    // démarrage de chaque installation.
    const auto vide = midiLearnFromJson("");
    VSM_ASSERT(vide.success);
    VSM_ASSERT_EQ(vide.map.size(), size_t{0});
    VSM_ASSERT(vide.error.empty());
}

VSM_TEST(an_unknown_kind_is_discarded_and_counted_never_guessed) {
    // Un fichier écrit par une version future peut nommer une cible qu'on ne
    // connaît pas. La deviner -- « ça ressemble à un volume » -- donnerait un
    // potentiomètre qui pilote autre chose que ce qu'on croit, ce qui est pire
    // qu'un potentiomètre inerte.
    const std::string texte = R"({"format":"vsm.midilearn.v1","mappings":[
        {"controller":10,"kind":"trackVolume","track":1},
        {"controller":11,"kind":"quelqueChoseDeFutur","track":1},
        {"controller":200,"kind":"trackVolume","track":1}
    ]})";
    const auto lu = midiLearnFromJson(texte);
    VSM_ASSERT(lu.success);
    VSM_ASSERT_EQ(lu.map.size(), size_t{1});
    // Et le compte est RENDU : l'application peut le dire au lieu de laisser
    // chercher pourquoi un bouton ne répond plus.
    VSM_ASSERT_EQ(lu.discarded, size_t{2});
}

VSM_TEST(broken_json_says_so_instead_of_losing_everything_in_silence) {
    const auto lu = midiLearnFromJson("{ceci n'est pas du JSON");
    VSM_ASSERT(!lu.success);
    VSM_ASSERT(!lu.error.empty());
}

VSM_TEST(the_kinds_are_written_by_name_not_by_number) {
    // GARDE-FOU DE FORMAT. Un `enum class` se réordonne un jour -- on insère
    // une valeur au milieu --, et si le fichier portait des numéros, toutes les
    // associations déjà enregistrées se mettraient à piloter autre chose. En
    // silence.
    MidiLearnMap carte;
    carte.bind(1, cible(MidiLearnKind::TransportLoop));
    const std::string json = midiLearnToJson(carte);
    VSM_ASSERT(json.find("transportLoop") != std::string::npos);
}

VSM_TEST(a_mapping_can_be_undone_one_at_a_time) {
    // « Un moyen d'en défaire une » : le critère de D10.2 le demande, et
    // `clearAll()` n'y répond pas -- perdre les quinze autres pour en corriger
    // une seule n'est pas une correction.
    MidiLearnMap carte;
    carte.bind(74, cible(MidiLearnKind::InstrumentParam, 0, 1));
    carte.bind(75, cible(MidiLearnKind::InstrumentParam, 0, 2));
    VSM_ASSERT_EQ(carte.size(), size_t{2});
    carte.clearController(74);
    VSM_ASSERT_EQ(carte.size(), size_t{1});
    VSM_ASSERT(!carte.hasController(74));
    VSM_ASSERT(carte.hasController(75));
}

VSM_TEST(a_target_says_what_it_is_in_words) {
    // Le libellé est ici, pas dans l'interface : le même texte sert à
    // l'affichage et aux tests, et deux formulations finiraient par se
    // contredire.
    VSM_ASSERT(describeMidiLearnTarget(cible(MidiLearnKind::TrackVolume, 2)).find("piste 3")
               != std::string::npos);
    VSM_ASSERT(describeMidiLearnTarget(cible(MidiLearnKind::TrackVolume, 2)).find("volume")
               != std::string::npos);
    // LE TRANSPORT N'APPARTIENT À AUCUNE PISTE : afficher « piste 1 » à côté
    // de « lecture » serait faux.
    const std::string lecture = describeMidiLearnTarget(cible(MidiLearnKind::TransportPlay, 0));
    VSM_ASSERT(lecture.find("piste") == std::string::npos);
    VSM_ASSERT(lecture.find("lecture") != std::string::npos);
    // Et le nom d'un paramètre vient de la machine quand on l'a sous la main.
    VSM_ASSERT(describeMidiLearnTarget(cible(MidiLearnKind::InstrumentParam, 0, 12), "Cutoff")
                   .find("Cutoff") != std::string::npos);
}
