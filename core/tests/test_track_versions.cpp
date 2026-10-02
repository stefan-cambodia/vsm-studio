#include "TestFramework.h"
#include "vsm/sequencer/Track.h"
#include <string>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D532.2 de docs/ROADMAP-daw.md — LES VERSIONS DE PISTE, le modèle.
//
// Une version est un état de la MATIÈRE d'une piste ; la version active vit dans la
// piste, sa copie rangée est périmée (l'invariant des prises). Ce fichier garde que
// l'échange ne perd rien, champ par champ, et les trois cas limites tranchés par écrit :
// la prise active, la piste gelée, la suppression de la version qu'on entend.

namespace {
/// Une matière où CHAQUE champ porte une valeur qui dit d'où elle vient : `marque`
/// décale tout, si bien que deux matières ne se ressemblent sur aucun champ.
void remplir(Track& t, int marque) {
    uint64_t ids = static_cast<uint64_t>(marque) * 100;
    t.notes.clear();
    t.addNote(marque * 10, marque * 10 + 240, static_cast<uint8_t>(40 + marque), 100, 0, ids);
    t.controlChanges = {{marque * 10, 0, 7, static_cast<uint8_t>(marque)}};
    t.pitchBends = {{marque * 10, 0, static_cast<int16_t>(marque * 100)}};
    t.polyAftertouch = {{marque * 10, 0, 60, static_cast<uint8_t>(marque)}};
    t.channelPressure = {{marque * 10, 0, static_cast<uint8_t>(marque)}};
    t.programChanges = {{marque * 10, 0, static_cast<uint8_t>(marque)}};
    t.audio.path = "prise" + std::to_string(marque) + ".wav";
    Clip clip;
    clip.startTick = marque * 10;
    clip.name = "clip " + std::to_string(marque);
    t.clips = {clip};
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points.push_back({0, static_cast<float>(marque) / 10.0f, false, 0.0f});
    t.automation = {courbe};
}

/// Vrai si la matière de `t` est celle que `remplir(…, marque)` a posée — champ par champ.
bool porte(const Track& t, int marque) {
    return t.notes.size() == 1 && t.notes[0].number == 40 + marque
        && t.controlChanges.size() == 1 && t.controlChanges[0].value == marque
        && t.pitchBends.size() == 1 && t.pitchBends[0].value == marque * 100
        && t.polyAftertouch.size() == 1 && t.polyAftertouch[0].pressure == marque
        && t.channelPressure.size() == 1 && t.channelPressure[0].pressure == marque
        && t.programChanges.size() == 1 && t.programChanges[0].program == marque
        && t.audio.path == "prise" + std::to_string(marque) + ".wav"
        && t.clips.size() == 1 && t.clips[0].name == "clip " + std::to_string(marque)
        && t.automation.size() == 1 && t.automation[0].points.size() == 1
        && t.automation[0].points[0].value == static_cast<float>(marque) / 10.0f;
}

bool vide(const Track& t) {
    return t.notes.empty() && t.controlChanges.empty() && t.pitchBends.empty() && t.polyAftertouch.empty()
        && t.channelPressure.empty() && t.programChanges.empty() && t.audio.empty() && t.clips.empty()
        && t.automation.empty();
}
} // namespace

VSM_TEST(une_piste_sans_version_n_en_porte_aucune) {
    Track t;
    VSM_ASSERT(t.versions.empty());
    VSM_ASSERT_EQ(t.activeVersion, -1);
    VSM_ASSERT(!selectVersion(t, 0));   // rien à choisir
}

// LA PREMIÈRE CRÉATION RANGE CE QUI ÉTAIT LÀ : sans cela, « Nouvelle version » sur une
// partie reconstruite l'effacerait.
VSM_TEST(une_nouvelle_version_part_vide_et_l_origine_est_rangee) {
    Track t;
    remplir(t, 1);
    VSM_ASSERT_EQ(createVersion(t, "Essai", false), 1);
    VSM_ASSERT_EQ(t.versions.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(t.versions[0].name, std::string("Version 1"));
    VSM_ASSERT_EQ(t.versions[1].name, std::string("Essai"));
    VSM_ASSERT_EQ(t.activeVersion, 1);
    VSM_ASSERT(vide(t));
    VSM_ASSERT(selectVersion(t, 0));
    VSM_ASSERT(porte(t, 1));
}

VSM_TEST(une_version_dupliquee_part_de_la_matiere_courante) {
    Track t;
    remplir(t, 2);
    VSM_ASSERT_EQ(createVersion(t, "Copie", true), 1);
    VSM_ASSERT(porte(t, 2));
    remplir(t, 3);                      // on retravaille la copie
    VSM_ASSERT(selectVersion(t, 0));
    VSM_ASSERT(porte(t, 2));            // l'originale n'a pas bougé
    VSM_ASSERT(selectVersion(t, 1));
    VSM_ASSERT(porte(t, 3));            // la copie a gardé son travail
}

// UN CHAMP OUBLIÉ DANS L'ÉCHANGE partirait avec la version suivante sans prévenir :
// les neuf sont remplis de valeurs distinctes et relus après deux allers-retours.
VSM_TEST(chaque_champ_de_la_matiere_suit_sa_version) {
    Track t;
    remplir(t, 4);
    createVersion(t, "B", false);
    remplir(t, 5);
    createVersion(t, "C", false);
    remplir(t, 6);
    for (int tour = 0; tour < 2; ++tour) {
        VSM_ASSERT(selectVersion(t, 0)); VSM_ASSERT(porte(t, 4));
        VSM_ASSERT(selectVersion(t, 2)); VSM_ASSERT(porte(t, 6));
        VSM_ASSERT(selectVersion(t, 1)); VSM_ASSERT(porte(t, 5));
    }
    // Le NOM ne se périme pas, lui : renommer la version active tient au changement.
    t.versions[1].name = "Refrain doublé";
    VSM_ASSERT(selectVersion(t, 0));
    VSM_ASSERT_EQ(t.versions[1].name, std::string("Refrain doublé"));
}

// LA PRISE ACTIVE EST RANGÉE DANS SON TIROIR, et n'est plus dite active : laissée active
// sur la matière d'une autre version, le changement de prise suivant l'aurait écrasée.
VSM_TEST(une_prise_active_est_rangee_avant_de_changer_de_version) {
    Track t;
    remplir(t, 1);
    Take prise;
    prise.name = "Passe 2";
    uint64_t ids = 900;
    prise.notes.push_back(Note{});
    prise.notes.back().number = 77;
    prise.notes.back().id = ids++;
    pushTake(t, prise);                 // prise 0 = origine, prise 1 active
    VSM_ASSERT_EQ(t.activeTake, 1);
    t.notes[0].number = 78;             // retouchée pendant qu'on l'écoute
    createVersion(t, "Sans la passe", false);
    VSM_ASSERT_EQ(t.activeTake, -1);
    VSM_ASSERT_EQ(t.takes[1].notes.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(t.takes[1].notes[0].number), 78);   // la retouche est dans la prise
    VSM_ASSERT(selectVersion(t, 0));
    VSM_ASSERT_EQ(static_cast<int>(t.notes[0].number), 78);            // et dans la version
}

VSM_TEST(une_piste_gelee_ne_change_pas_de_version_et_le_dit) {
    Track t;
    remplir(t, 1);
    createVersion(t, "B", true);
    remplir(t, 2);
    t.frozen = true;
    VSM_ASSERT(!selectVersion(t, 0));
    VSM_ASSERT_EQ(t.activeVersion, 1);
    VSM_ASSERT(porte(t, 2));
    VSM_ASSERT_EQ(createVersion(t, "C", false), -1);
    VSM_ASSERT_EQ(t.versions.size(), static_cast<size_t>(2));
}

// SUPPRIMER CE QU'ON ENTEND fait d'abord entendre la voisine ; la DERNIÈRE retirée vide
// le tiroir et laisse sa matière sur la piste.
VSM_TEST(retirer_la_version_active_passe_a_la_voisine_et_la_derniere_garde_sa_matiere) {
    Track t;
    remplir(t, 1);
    createVersion(t, "B", false);
    remplir(t, 2);
    createVersion(t, "C", false);
    remplir(t, 3);
    VSM_ASSERT(removeVersion(t, 2));    // C, active : B sort
    VSM_ASSERT_EQ(t.versions.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(t.activeVersion, 1);
    VSM_ASSERT(porte(t, 2));
    VSM_ASSERT(removeVersion(t, 0));    // l'origine, pas active : B recule d'un rang
    VSM_ASSERT_EQ(t.activeVersion, 0);
    VSM_ASSERT_EQ(t.versions[0].name, std::string("B"));
    VSM_ASSERT(porte(t, 2));
    VSM_ASSERT(removeVersion(t, 0));    // la dernière
    VSM_ASSERT(t.versions.empty());
    VSM_ASSERT_EQ(t.activeVersion, -1);
    VSM_ASSERT(porte(t, 2));            // ce qu'on entendait reste
    VSM_ASSERT(!removeVersion(t, 0));
}
