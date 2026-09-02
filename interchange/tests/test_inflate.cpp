#include "TestFramework.h"
#include "vsm/interchange/Inflate.h"
#include <string>
#include <vector>

using namespace vsm::interchange;

// La décompression DEFLATE/gzip, écrite ici parce que `vsm_interchange` n'a
// aucune dépendance externe et que c'est une propriété qu'on garde (voir
// docs/CDC-import-daw.md § 3).
//
// Les cas sont COMPRESSÉS À L'AVANCE et inscrits en dur : un test ne doit
// dépendre d'aucun fichier qu'on n'a pas le droit de redistribuer, ni
// d'aucun outil externe présent sur la machine de qui l'exécute.

namespace {
// texte_court : 29 octets -> 43 compresses
const std::vector<uint8_t> kTexteCourtGzip{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xb3, 0x71, 0x4c, 0xca, 0x49, 0x2d, 0xc9, 0xcf, 0xb3, 0xb3, 0xf1, 0xc9, 0x2c, 0x4b, 0x0d, 0x4e, 0x2d, 0xd1, 0xb7, 0xb3, 0xd1, 0x87, 0x89, 0x01, 0x00, 0xb6, 0xc5, 0x5e, 0xd6, 0x1d, 0x00, 0x00, 0x00};
const std::string kTexteCourtClair = "<Ableton><LiveSet/></Ableton>";
// repetitions : 320 octets -> 25 compresses
const std::vector<uint8_t> kRepetitionsGzip{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x73, 0x74, 0x1c, 0x05, 0x94, 0x00, 0x00, 0x77, 0xe4, 0x3c, 0xfb, 0x40, 0x01, 0x00, 0x00};
const std::string kRepetitionsClair = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
// xml_realiste : 188 octets -> 161 compresses
const std::vector<uint8_t> kXmlRealisteGzip{0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x3d, 0x8e, 0xcb, 0x0a, 0xc2, 0x30, 0x10, 0x45, 0xf7, 0xf9, 0x8a, 0x61, 0xf6, 0x1a, 0x5d, 0x08, 0x5d, 0x4c, 0xa6, 0x28, 0x28, 0x08, 0xd6, 0x8d, 0xb5, 0xfb, 0xd8, 0x4e, 0x25, 0xda, 0x26, 0xd0, 0xd4, 0xe2, 0xe7, 0x5b, 0x1f, 0x75, 0x77, 0x2f, 0x9c, 0xfb, 0xa0, 0xf4, 0xd9, 0x36, 0x30, 0x48, 0x17, 0x5d, 0xf0, 0x06, 0x97, 0xf3, 0x05, 0x82, 0xf8, 0x32, 0x54, 0xce, 0x5f, 0x0d, 0x9e, 0xf3, 0xdd, 0x2c, 0xc1, 0x94, 0x15, 0xad, 0x2f, 0x8d, 0xf4, 0xc1, 0x43, 0x66, 0x6f, 0xa1, 0x2b, 0x26, 0x7c, 0x85, 0xac, 0x00, 0xe8, 0xe0, 0x06, 0x39, 0x49, 0xcf, 0x94, 0x77, 0xb6, 0xbc, 0x47, 0xa6, 0xcc, 0x55, 0xee, 0xa3, 0x61, 0x5f, 0x19, 0x4c, 0x90, 0xe9, 0x68, 0x5b, 0x61, 0xda, 0xd6, 0xb5, 0x94, 0xfd, 0x48, 0xbf, 0x2d, 0x14, 0xb6, 0x79, 0x88, 0xc1, 0x8d, 0x8d, 0x11, 0x35, 0x93, 0xfe, 0x32, 0xfa, 0x1f, 0x1e, 0xf5, 0x54, 0xa8, 0xa7, 0x09, 0x45, 0xfa, 0x77, 0x85, 0xd5, 0x0b, 0x05, 0x36, 0xf6, 0x26, 0xbc, 0x00, 0x00, 0x00};
const std::string kXmlRealisteClair = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\">\n  <LiveSet><Tracks><MidiTrack Id=\"8\"><Name><EffectiveName Value=\"Bass\"/></Name></MidiTrack></Tracks></LiveSet>\n</Ableton>\n";
const std::vector<uint8_t> kTexteCourtZlib{0x78, 0x9c, 0xb3, 0x71, 0x4c, 0xca, 0x49, 0x2d, 0xc9, 0xcf, 0xb3, 0xb3, 0xf1, 0xc9, 0x2c, 0x4b, 0x0d, 0x4e, 0x2d, 0xd1, 0xb7, 0xb3, 0xd1, 0x87, 0x89, 0x01, 0x00, 0x97, 0x01, 0x0a, 0x13};} // namespace

VSM_TEST(inflate_lit_un_gzip_court) {
    const auto sortie = Inflate::gzip(kTexteCourtGzip);
    VSM_ASSERT_EQ(std::string(sortie.begin(), sortie.end()), kTexteCourtClair);
}

VSM_TEST(inflate_lit_les_repetitions_par_reference_arriere) {
    // 320 octets compressés en 25 : tout est référence arrière, et ces
    // références SE CHEVAUCHENT. C'est le cas où une copie par bloc donnerait
    // un résultat faux au lieu d'échouer franchement.
    const auto sortie = Inflate::gzip(kRepetitionsGzip);
    VSM_ASSERT_EQ(std::string(sortie.begin(), sortie.end()), kRepetitionsClair);
}

VSM_TEST(inflate_lit_un_xml_realiste) {
    const auto sortie = Inflate::gzip(kXmlRealisteGzip);
    VSM_ASSERT_EQ(std::string(sortie.begin(), sortie.end()), kXmlRealisteClair);
}

VSM_TEST(inflate_lit_aussi_le_format_zlib) {
    const auto sortie = Inflate::zlib(kTexteCourtZlib);
    VSM_ASSERT_EQ(std::string(sortie.begin(), sortie.end()), kTexteCourtClair);
}

VSM_TEST(inflate_reconnait_l_enveloppe_tout_seul) {
    // Chaque résultat est GARDÉ dans une variable avant d'être lu : une
    // première version écrivait `std::string(any(x).begin(), any(x).end())`,
    // ce qui appelle `any` DEUX FOIS et mêle le début d'un temporaire à la fin
    // d'un autre — un comportement indéfini, que la suite a rendu en affichant
    // de la mémoire au hasard. Le test était faux, pas le code.
    const auto deGzip = Inflate::any(kTexteCourtGzip);
    VSM_ASSERT_EQ(std::string(deGzip.begin(), deGzip.end()), kTexteCourtClair);
    const auto deZlib = Inflate::any(kTexteCourtZlib);
    VSM_ASSERT_EQ(std::string(deZlib.begin(), deZlib.end()), kTexteCourtClair);
}

VSM_TEST(inflate_rend_tel_quel_ce_qui_n_est_pas_compresse) {
    // Un projet Live enregistré SANS compression existe. Le refuser serait un
    // défaut, pas une rigueur.
    const std::string clair = "<Ableton>pas compresse</Ableton>";
    const std::vector<uint8_t> octets(clair.begin(), clair.end());
    const auto sortie = Inflate::any(octets);
    VSM_ASSERT_EQ(std::string(sortie.begin(), sortie.end()), clair);
}

// --- Ce qui doit ÉCHOUER, et le dire ---------------------------------------

VSM_TEST(inflate_refuse_un_gzip_tronque_au_lieu_de_rendre_un_bout) {
    // LA RÈGLE DE LA PANNE MUETTE, appliquée à l'import : un flux coupé ne
    // doit jamais rendre un résultat partiel qu'on prendrait pour un succès.
    // C'est la différence entre « ce projet n'a pas pu être lu » et « ce
    // projet est vide » — et la seconde ferait perdre des heures.
    auto tronque = kXmlRealisteGzip;
    tronque.resize(tronque.size() / 2);
    bool leve = false;
    try { Inflate::gzip(tronque); } catch (const InflateError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(inflate_refuse_un_entete_qui_n_est_pas_du_gzip) {
    const std::vector<uint8_t> faux{0x50, 0x4b, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00};
    bool leve = false;
    try { Inflate::gzip(faux); } catch (const InflateError&) { leve = true; }
    VSM_ASSERT(leve);
}

VSM_TEST(inflate_refuse_un_type_de_bloc_reserve) {
    // Le type 3 est réservé par la RFC 1951 : le rencontrer signifie que le
    // flux n'est pas ce qu'on croit.
    const std::vector<uint8_t> brut{0x07};   // dernier bloc, type 3
    bool leve = false;
    try { Inflate::raw(brut); } catch (const InflateError&) { leve = true; }
    VSM_ASSERT(leve);
}
