#include "TestFramework.h"
#include "vsm/sequencer/GeneralMidi.h"
#include "vsm/sequencer/Project.h"
#include <variant>
#include <vector>
#include <cstring>
#include <set>
#include <string>

using namespace vsm::sequencer;

// D307 : la table General MIDI est complète, numérotée dans l'ordre, et chaque
// programme désigne une machine du parc (un identifiant « vsm.… »).
VSM_TEST(la_table_general_midi_couvre_les_128_programmes) {
    std::set<std::string> noms;
    for (int p = 0; p < 128; ++p) {
        const auto& e = programmeGM(static_cast<uint8_t>(p));
        VSM_ASSERT_EQ(static_cast<int>(e.numero), p);
        VSM_ASSERT(e.nom != nullptr && std::strlen(e.nom) > 0);
        VSM_ASSERT(e.machine != nullptr && std::strncmp(e.machine, "vsm.", 4) == 0);
        noms.insert(e.nom);
    }
    VSM_ASSERT_EQ(noms.size(), static_cast<size_t>(128));   // cent vingt-huit noms distincts
    VSM_ASSERT_EQ(std::string(programmeGM(0).machine), std::string("vsm.piano"));     // le défaut de la norme
    VSM_ASSERT_EQ(std::string(programmeGM(38).machine), std::string("vsm.tb303"));    // Synth Bass 1
    VSM_ASSERT_EQ(std::string(programmeGM(89).machine), std::string("vsm.jupiter8")); // Pad 2 (warm)
    VSM_ASSERT_EQ(std::string(programmeGM(200).machine), std::string(programmeGM(127).machine)); // hors norme : ramené
}

VSM_TEST(le_canal_10_recoit_un_kit) {
    VSM_ASSERT_EQ(std::string(machinePourKitGM(0)), std::string("vsm.drums"));
    VSM_ASSERT_EQ(std::string(machinePourKitGM(25)), std::string("vsm.tr808"));
    VSM_ASSERT_EQ(std::string(machinePourKitGM(24)), std::string("vsm.tr909"));
    VSM_ASSERT_EQ(std::string(machinePourKitGM(32)), std::string("vsm.drums"));
    VSM_ASSERT_EQ(std::string(nomDuKitGM(25)), std::string("TR-808 Kit"));
    VSM_ASSERT_EQ(std::string(nomDuKitGM(0)), std::string("Standard Kit"));
}

// D309 : les profils canoniques -- 45 programmes, les mêmes que l'installateur.
VSM_TEST(les_profils_canoniques_couvrent_45_programmes) {
    int couverts = 0;
    for (int p = 0; p < 128; ++p)
        if (profilCanoniqueGM(static_cast<uint8_t>(p)) != nullptr) ++couverts;
    VSM_ASSERT_EQ(couverts, 45);
    VSM_ASSERT_EQ(std::string(profilCanoniqueGM(0)), std::string("Grand-Piano"));
    VSM_ASSERT_EQ(std::string(profilCanoniqueGM(38)), std::string("Synth-Bass-1"));
    VSM_ASSERT_EQ(std::string(profilCanoniqueGM(89)), std::string("Warm-Pad"));
    VSM_ASSERT(profilCanoniqueGM(1) == nullptr);     // Bright Acoustic Piano : pas de profil
    VSM_ASSERT(profilCanoniqueGM(98) == nullptr);    // FX 3 (crystal) : pas de profil
    size_t n = 0;
    const BanqueGM* b = banquesGM(n);
    VSM_ASSERT_EQ(n, static_cast<size_t>(3));
    VSM_ASSERT_EQ(std::string(b[0].prefixe), std::string("FR3"));
}

// D312 : la table à l'envers rend le plus petit programme de chaque machine.
VSM_TEST(la_table_a_l_envers_designe_chaque_machine_par_son_premier_programme) {
    VSM_ASSERT_EQ(programmeGMPourMachine("vsm.piano"), 0);
    VSM_ASSERT_EQ(programmeGMPourMachine("vsm.tb303"), 38);
    VSM_ASSERT_EQ(programmeGMPourMachine("vsm.jupiter8"), 63);
    VSM_ASSERT_EQ(programmeGMPourMachine("vsm.hurdygurdy"), -1);
    VSM_ASSERT_EQ(programmeGMPourMachine("vsm.generic"), -1);
    VSM_ASSERT_EQ(programmeGMPourMachine(nullptr), -1);
    // Aller-retour : le programme d'une machine désigne cette machine.
    for (int p = 0; p < 128; ++p) {
        const auto& e = programmeGM(static_cast<uint8_t>(p));
        VSM_ASSERT_EQ(std::string(programmeGM(static_cast<uint8_t>(programmeGMPourMachine(e.machine))).machine),
                      std::string(e.machine));
    }
    VSM_ASSERT_EQ(kitGMPourMachine("vsm.drums"), 0);
    VSM_ASSERT_EQ(kitGMPourMachine("vsm.tr808"), 25);
    VSM_ASSERT_EQ(kitGMPourMachine("vsm.tr909"), 24);
    VSM_ASSERT_EQ(kitGMPourMachine("vsm.fmdrums"), -1);
}

// D313 : le nom de la piste désigne la famille quand le fichier ne dit rien.
VSM_TEST(le_nom_de_la_piste_designe_un_programme_quand_le_fichier_se_tait) {
    VSM_ASSERT_EQ(programmeGMPourNom("bass").programme, 33);
    VSM_ASSERT_EQ(programmeGMPourNom("Basse synth\u00e9").programme, 33);
    VSM_ASSERT_EQ(programmeGMPourNom("piano").programme, 0);
    VSM_ASSERT_EQ(programmeGMPourNom("Rhodes").programme, 4);
    VSM_ASSERT_EQ(programmeGMPourNom("Strings").programme, 48);
    VSM_ASSERT_EQ(programmeGMPourNom("vocals").programme, 52);
    VSM_ASSERT_EQ(programmeGMPourNom("Pad chaud").programme, 89);
    VSM_ASSERT(programmeGMPourNom("Batterie").kit);
    VSM_ASSERT(programmeGMPourNom("drums").kit);
    VSM_ASSERT(programmeGMPourNom("Drum Bass").kit);            // la batterie avant la basse
    VSM_ASSERT_EQ(programmeGMPourNom("other").programme, -1);
    VSM_ASSERT_EQ(programmeGMPourNom("Mixdown").programme, -1);
    VSM_ASSERT_EQ(programmeGMPourNom("").programme, -1);
    VSM_ASSERT_EQ(programmeGMPourNom(nullptr).programme, -1);
}

// D388 : le programme d'un profil multi-échantillons.
VSM_TEST(programme_gm_d_un_profil) {
    VSM_ASSERT_EQ(programmeGMPourProfil("MS-E-Piano-FM"), 5);
    VSM_ASSERT_EQ(programmeGMPourProfil("FR3-Harp"), 46);
    VSM_ASSERT_EQ(programmeGMPourProfil("GU-Synth-Bass-2"), 39);
    VSM_ASSERT_EQ(programmeGMPourProfil("Grand-Piano"), 0);          // sans préfixe
    VSM_ASSERT_EQ(programmeGMPourProfil("GU-Concert-Choir"), -1);    // hors table
    VSM_ASSERT_EQ(programmeGMPourProfil("Salamander Grand Piano"), -1);
    VSM_ASSERT_EQ(programmeGMPourProfil("GM-Warm-Pad"), -1);         // préfixe hors liste fermée
    VSM_ASSERT_EQ(programmeGMPourProfil(""), -1);
    VSM_ASSERT_EQ(programmeGMPourProfil(nullptr), -1);
}

VSM_TEST(export_arrange_ecrit_le_programme_du_profil) {
    Project projet;
    Track piste;
    piste.name = "bass";
    piste.channel = 2;
    piste.instrumentId = "vsm.multisample";
    piste.instrumentProfile = "MS-E-Piano-FM";
    Note note;
    note.endTick = 480;
    piste.notes.push_back(note);
    projet.tracks.push_back(piste);
    Track autre = piste;
    autre.name = "choeur";
    autre.channel = 3;
    autre.instrumentProfile = "GU-Concert-Choir";
    projet.tracks.push_back(autre);

    const auto fichier = projet.toParsedFileArranged();
    std::vector<std::pair<int, int>> programmes;   // (canal, programme)
    for (const auto& t : fichier.tracks)
        for (const auto& ev : t.events)
            if (const auto* pc = std::get_if<vsm::midi::ProgramChangeEvent>(&ev.data))
                programmes.push_back({pc->channel, pc->program});
    VSM_ASSERT_EQ(programmes.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(programmes[0].first, 2);
    VSM_ASSERT_EQ(programmes[0].second, 5);
}
