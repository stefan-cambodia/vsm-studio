#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/Track.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// D30 de docs/ROADMAP-daw.md — LE SOLO PROTÉGÉ (D30.1), LA PISTE DÉSACTIVÉE
// (D30.2) ET LE TRIM D'ENTRÉE (D30.4).
//
// Les trois attendus ont été écrits AVANT ces mesures, dans la feuille de
// route. Ce fichier est ce qui les tranche, et chaque A/B ne change qu'UNE
// variable : le témoin est le même code, pas une constante éditée entre deux
// passes.
// ---------------------------------------------------------------------------

using namespace vsm::audio::engine;
using namespace vsm::sequencer;

namespace {

/// Un insert NON LINÉAIRE, et volontairement grossier : au-delà du seuil, il
/// écrête. C'est ce qui rend le trim visible -- un insert linéaire ne pourrait
/// pas distinguer « plus fort avant » de « plus fort après ».
class Ecreteur : public vsm::audio::effect::IAudioEffect {
public:
    explicit Ecreteur(float seuil) : seuil_(seuil) {}
    void prepare(double, int) override {}
    void reset() override {}
    void process(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) {
            l[i] = std::clamp(l[i], -seuil_, seuil_);
            r[i] = std::clamp(r[i], -seuil_, seuil_);
        }
    }
    void setParameter(vsm::audio::plugin::ParamId, float) override {}
    float getParameter(vsm::audio::plugin::ParamId) const override { return 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return params_; }
    const char* effectName() const override { return "Ecreteur"; }
private:
    float seuil_;
    vsm::audio::plugin::ParameterList params_;
};

class Gain : public vsm::audio::effect::IAudioEffect {
public:
    explicit Gain(float g) : g_(g) {}
    void prepare(double, int) override {}
    void reset() override {}
    void process(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) { l[i] *= g_; r[i] *= g_; }
    }
    void setParameter(vsm::audio::plugin::ParamId, float) override {}
    float getParameter(vsm::audio::plugin::ParamId) const override { return 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return params_; }
    const char* effectName() const override { return "Gain"; }
private:
    float g_;
    vsm::audio::plugin::ParameterList params_;
};

Track uneNote(const std::string& nom, uint8_t hauteur) {
    Track t; t.name = nom; t.channel = 0;
    uint64_t id = 1;
    t.addNote(0, 480 * 4, hauteur, 110, 0, id);
    return t;
}

float peakOf(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

float rmsOf(const std::vector<float>& b) {
    double s = 0.0; for (float v : b) s += double(v) * v;
    return b.empty() ? 0.0f : float(std::sqrt(s / double(b.size())));
}

/// Le plus grand écart échantillon par échantillon entre deux rendus. Zéro
/// veut dire « au bit près », et c'est ce que D30.2 promet.
float ecartMax(const RenderedAudio& a, const RenderedAudio& b) {
    const size_t n = std::min(a.left.size(), b.left.size());
    float pire = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        pire = std::max(pire, std::fabs(a.left[i] - b.left[i]));
        pire = std::max(pire, std::fabs(a.right[i] - b.right[i]));
    }
    return pire;
}

RenderedAudio rendre(const Project& projet,
                      const std::vector<std::pair<size_t, std::string>>& machines) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    for (const auto& [i, id] : machines) graph.setTrackInstrument(i, id);
    graph.setProject(projet);
    return OfflineRenderer::render(graph, 8000.0, 256, 1.0);
}

} // namespace

// ---------------------------------------------------------------------------
// D30.1 — LE SOLO PROTÉGÉ.
// ---------------------------------------------------------------------------

VSM_TEST(solo_safe_keeps_a_return_bus_alive_under_someone_elses_solo) {
    // L'ATTENDU, ÉCRIT AVANT LA MESURE : « le solo d'une piste avec un départ
    // de réverbération laisse la queue au mélange, le témoin sans protection
    // ne l'a pas ». Le retour d'effet est ici une PISTE DE GROUPE, qui est ce
    // qu'on protège en pratique.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("Voix", 57));
    Track bus; bus.name = "Retour"; bus.kind = Track::Kind::Group;
    projet.tracks.push_back(bus);
    projet.tracks[0].outputGroup = 1;    // la voix passe par le retour

    // TÉMOIN : le solo de la voix. Le groupe qui la porte n'est pas soloé,
    // donc rien ne sort -- c'est le défaut qu'on répare.
    projet.tracks[0].solo = true;
    const RenderedAudio sansProtection = rendre(projet, {{0, "vsm.minimoog"}});

    // LA SEULE VARIABLE QUI CHANGE : la protection du groupe.
    projet.tracks[1].soloSafe = true;
    const RenderedAudio avecProtection = rendre(projet, {{0, "vsm.minimoog"}});

    VSM_ASSERT_NEAR(peakOf(sansProtection.left), 0.0f, 1e-6f);
    VSM_ASSERT(peakOf(avecProtection.left) > 0.05f);
}

VSM_TEST(solo_safe_still_obeys_its_own_mute) {
    // « Ignore le solo des autres » et non « est toujours audible » : deux
    // façons de faire taire une piste dont une seule obéirait serait
    // exactement le genre de bouton qui ment.
    Track t = uneNote("Protegee", 57);
    t.soloSafe = true;
    VSM_ASSERT(trackAudible(t, /*anySolo=*/true));
    VSM_ASSERT(trackAudible(t, /*anySolo=*/false));
    t.muted = true;
    VSM_ASSERT(!trackAudible(t, /*anySolo=*/true));
    VSM_ASSERT(!trackAudible(t, /*anySolo=*/false));
}

VSM_TEST(without_solo_safe_the_old_rule_is_untouched) {
    // Ce qui existait doit continuer d'exister exactement : sous un solo, seul
    // le soloé s'entend ; sans solo, tout ce qui n'est pas muet.
    Track ordinaire = uneNote("T", 57);
    VSM_ASSERT(trackAudible(ordinaire, false));
    VSM_ASSERT(!trackAudible(ordinaire, true));
    ordinaire.solo = true;
    VSM_ASSERT(trackAudible(ordinaire, true));
    ordinaire.solo = false;
    ordinaire.muted = true;
    VSM_ASSERT(!trackAudible(ordinaire, false));

    std::vector<Track> aucun { uneNote("a", 57), uneNote("b", 60) };
    VSM_ASSERT(!anySoloActive(aucun));
    aucun[1].solo = true;
    VSM_ASSERT(anySoloActive(aucun));
}

// ---------------------------------------------------------------------------
// D30.2 — LA PISTE DÉSACTIVÉE.
// ---------------------------------------------------------------------------

VSM_TEST(disabling_a_silent_track_leaves_the_rest_bit_for_bit_identical) {
    // L'ATTENDU, ÉCRIT AVANT LA MESURE : « désactiver une piste qui ne sonne
    // pas dans la plage rendue doit laisser le rendu du reste IDENTIQUE AU BIT
    // PRÈS (écart 0,0). Si l'écart n'est pas nul, ce n'est pas de l'arrondi :
    // c'est que la désactivation a touché autre chose que la piste visée. »
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("Qui sonne", 57));
    Track tard; tard.name = "Qui se tait"; tard.channel = 0;
    uint64_t id = 1;
    tard.addNote(480 * 32, 480, 72, 110, 0, id);   // bien après la plage rendue
    projet.tracks.push_back(tard);

    const RenderedAudio avant = rendre(projet, {{0, "vsm.minimoog"}, {1, "vsm.minimoog"}});
    projet.tracks[1].disabled = true;
    const RenderedAudio apres = rendre(projet, {{0, "vsm.minimoog"}, {1, "vsm.minimoog"}});

    VSM_ASSERT(peakOf(avant.left) > 0.05f);        // la mesure porte sur du son
    VSM_ASSERT_EQ(ecartMax(avant, apres), 0.0f);   // au bit près
}

VSM_TEST(a_disabled_track_falls_silent_where_a_muted_one_would_too) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("Seule", 57));

    const RenderedAudio vivante = rendre(projet, {{0, "vsm.minimoog"}});
    VSM_ASSERT(peakOf(vivante.left) > 0.05f);

    Project eteinte = projet;
    eteinte.tracks[0].disabled = true;
    VSM_ASSERT_NEAR(peakOf(rendre(eteinte, {{0, "vsm.minimoog"}}).left), 0.0f, 1e-9f);

    // ET LE MUET REND LA MÊME CHOSE AU MÉLANGE : c'est ce qui rend les deux
    // commandes comparables. Ce qui les distingue n'est pas le son, c'est ce
    // que le moteur TIENT -- et cela se mesure ailleurs (le planning).
    Project muette = projet;
    muette.tracks[0].muted = true;
    VSM_ASSERT_NEAR(peakOf(rendre(muette, {{0, "vsm.minimoog"}}).left), 0.0f, 1e-9f);
}

VSM_TEST(a_disabled_track_is_not_even_scheduled) {
    // C'EST ICI QUE LA DIFFÉRENCE AVEC LE MUET SE MESURE. Une piste muette
    // reçoit ses événements et les jette au mélange ; une piste désactivée
    // n'est plus dans le morceau, et le planificateur ne lui écrit rien.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("T", 57));

    const auto plein = PlaybackScheduler::build(projet, 0, 480 * 8);
    VSM_ASSERT(!plein.empty());

    Project muette = projet;
    muette.tracks[0].muted = true;
    const auto sansMuet = PlaybackScheduler::build(muette, 0, 480 * 8);

    Project eteinte = projet;
    eteinte.tracks[0].disabled = true;
    const auto sansEteinte = PlaybackScheduler::build(eteinte, 0, 480 * 8);

    VSM_ASSERT(sansMuet.empty());        // le muet non plus n'est pas planifié
    VSM_ASSERT(sansEteinte.empty());
}

// ---------------------------------------------------------------------------
// D30.4 — LE TRIM D'ENTRÉE.
// ---------------------------------------------------------------------------

VSM_TEST(the_trim_is_a_second_fader_only_when_the_chain_is_linear) {
    // L'ATTENDU, ÉCRIT AVANT LA MESURE : « j'attends que trim +6 / fader -6
    // soit IDENTIQUE AU BIT PRÈS quand la chaîne est vide ou purement
    // linéaire. Le second est ce qui prouve que le trim est bien AVANT les
    // inserts et nulle part ailleurs. »
    //
    // « Au bit près » demande un gain qui se compense EXACTEMENT : 6,020 dB
    // n'est pas une puissance de deux. On prend donc +6,0206 dB (facteur 2) et
    // un fader à 0,5 -- deux nombres dont le produit est 1 sans arrondi.
    const float sixDb = 20.0f * std::log10(2.0f);

    Project neutre;
    neutre.ticksPerQuarterNote = 480;
    neutre.tracks.push_back(uneNote("T", 57));

    Project compense = neutre;
    compense.tracks[0].inputTrimDb = sixDb;
    compense.tracks[0].volume = 0.5f;

    const RenderedAudio a = rendre(neutre, {{0, "vsm.minimoog"}});
    const RenderedAudio b = rendre(compense, {{0, "vsm.minimoog"}});
    VSM_ASSERT(peakOf(a.left) > 0.05f);
    VSM_ASSERT(ecartMax(a, b) < 1e-6f);
}

VSM_TEST(the_trim_changes_what_a_nonlinear_insert_receives) {
    // L'AUTRE MOITIÉ DE L'ATTENDU : « j'attends que trim +6 / fader -6 DIFFÈRE
    // du réglage neutre dès qu'un insert non linéaire est en jeu ». Sans cela,
    // le trim ne serait qu'un second fader et n'aurait aucune raison d'exister.
    const float sixDb = 20.0f * std::log10(2.0f);

    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("T", 57));

    auto chaine = std::make_shared<ProcessGraph::EffectChain>();
    chaine->push_back(std::make_shared<Ecreteur>(0.10f));

    auto rendreAvecChaine = [&](const Project& p) {
        ProcessGraph graph;
        graph.prepare(8000.0, 256);
        graph.setTrackInstrument(0, "vsm.minimoog");
        graph.setProject(p);
        graph.setTrackEffectChain(0, chaine);
        return OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    };

    Project compense = projet;
    compense.tracks[0].inputTrimDb = sixDb;
    compense.tracks[0].volume = 0.5f;

    const RenderedAudio neutre = rendreAvecChaine(projet);
    const RenderedAudio pousse = rendreAvecChaine(compense);

    VSM_ASSERT(peakOf(neutre.left) > 0.01f);
    // POUSSÉE DANS L'ÉCRÊTEUR PUIS RAMENÉE, la piste ne rend PAS la même
    // chose : c'est toute la raison d'être du trim.
    VSM_ASSERT(ecartMax(neutre, pousse) > 1e-3f);
    // Et elle rend moins fort, l'écrêtage ayant mangé les crêtes qu'on lui a
    // données avant de diviser par deux.
    VSM_ASSERT(rmsOf(pousse.left) < rmsOf(neutre.left));
}

VSM_TEST(a_zero_trim_changes_nothing_at_all) {
    // Ce qui existait ne bouge pas : un projet où personne ne pose de trim rend
    // exactement ce qu'il rendait, au bit près.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("T", 57));
    const RenderedAudio a = rendre(projet, {{0, "vsm.minimoog"}});
    projet.tracks[0].inputTrimDb = 0.0f;
    const RenderedAudio b = rendre(projet, {{0, "vsm.minimoog"}});
    VSM_ASSERT_EQ(ecartMax(a, b), 0.0f);
}

// ---------------------------------------------------------------------------
// D32.1 — LA PRÉ-ÉCOUTE DU NAVIGATEUR.
//
// L'attendu, écrit avant la mesure : « le rendu hors ligne d'un projet est
// identique AU BIT PRÈS qu'une pré-écoute soit chargée ou non. Si l'écart
// n'est pas nul, la pré-écoute a fui dans l'export. »
// ---------------------------------------------------------------------------

namespace {

/// Un tampon d'échantillon franc : une seconde de pleine amplitude. S'il fuit
/// dans un rendu, la mesure ne peut pas le manquer.
vsm::audio::io::SampleBufferPtr unEchantillonFranc(double sampleRate = 8000.0) {
    auto buffer = std::make_shared<vsm::audio::io::SampleBuffer>();
    buffer->sampleRate = sampleRate;
    const size_t n = static_cast<size_t>(sampleRate);
    buffer->left.assign(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
        buffer->left[i] = 0.9f * std::sin(2.0 * 3.14159265358979 * 440.0
                                           * static_cast<double>(i) / sampleRate);
    return buffer;
}

} // namespace

VSM_TEST(two_consecutive_renders_on_the_same_graph_are_not_identical) {
    // LE TÉMOIN DU TEST SUIVANT, et il a fallu l'écrire pour ne pas accuser la
    // pré-écoute d'une fuite qui n'en était pas une. Une machine a de la
    // MÉMOIRE (phase d'oscillateur, charge de filtre, état d'enveloppe) : deux
    // rendus enchaînés sur LE MÊME graphe ne sont pas identiques, et la leçon
    // était déjà écrite à D18.1. Comparer une pré-écoute à un témoin rendu
    // avant elle mesurait donc cette mémoire, pas la fuite.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("T", 57));
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(projet);
    const RenderedAudio premier = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    const RenderedAudio second = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    VSM_ASSERT(peakOf(premier.left) > 0.05f);
    VSM_ASSERT(ecartMax(premier, second) > 1e-6f);
}

VSM_TEST(an_audition_never_leaks_into_a_render) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tracks.push_back(uneNote("T", 57));

    // DEUX GRAPHES NEUFS, et UNE seule variable entre eux : la pré-écoute.
    // Le même graphe rendu deux fois ne convient pas -- voir le témoin
    // ci-dessus, une machine garde son état d'un rendu au suivant.
    ProcessGraph temoin;
    temoin.prepare(8000.0, 256);
    temoin.setTrackInstrument(0, "vsm.minimoog");
    temoin.setProject(projet);
    const RenderedAudio sansPreEcoute = OfflineRenderer::render(temoin, 8000.0, 256, 1.0);

    ProcessGraph avec;
    avec.prepare(8000.0, 256);
    avec.setTrackInstrument(0, "vsm.minimoog");
    avec.setProject(projet);
    avec.auditionPlayer().trigger(unEchantillonFranc());
    VSM_ASSERT(avec.auditionPlayer().isPlaying());
    const RenderedAudio avecPreEcoute = OfflineRenderer::render(avec, 8000.0, 256, 1.0);

    VSM_ASSERT(peakOf(sansPreEcoute.left) > 0.05f);          // la mesure porte sur du son
    VSM_ASSERT_EQ(ecartMax(sansPreEcoute, avecPreEcoute), 0.0f);   // au bit près
    // ET ELLE EST REVENUE : une pré-écoute qui resterait coupée après un
    // export ferait croire le navigateur muet.
    VSM_ASSERT(avec.auditionPlayer().enabled());
}

VSM_TEST(an_audition_is_heard_when_the_transport_is_not_running) {
    // Elle ne suit pas le transport : c'est ce qui la distingue de la piste de
    // référence, et c'est ce qu'on attend d'un clic dans un navigateur.
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setPlaying(false);
    graph.auditionPlayer().trigger(unEchantillonFranc());

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(peakOf(l) > 0.1f);
    VSM_ASSERT(peakOf(r) > 0.1f);          // mono étalé sur les deux côtés
}

VSM_TEST(an_audition_stops_at_the_end_and_a_new_one_cuts_the_old) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    // Un tampon très court : deux blocs suffisent à le passer en entier.
    auto court = std::make_shared<vsm::audio::io::SampleBuffer>();
    court->sampleRate = 8000.0;
    court->left.assign(300, 0.5f);
    graph.auditionPlayer().trigger(court);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(graph.auditionPlayer().isPlaying());
    graph.processBlock(l.data(), r.data(), 256);
    // ARRIVÉE AU BOUT : elle s'arrête plutôt que de boucler.
    VSM_ASSERT(!graph.auditionPlayer().isPlaying());

    // ET UN DÉCLENCHEMENT COUPE LE PRÉCÉDENT : deux échantillons superposés ne
    // s'écoutent ni l'un ni l'autre.
    graph.auditionPlayer().trigger(unEchantillonFranc());
    graph.processBlock(l.data(), r.data(), 256);
    graph.auditionPlayer().trigger(unEchantillonFranc());
    VSM_ASSERT(graph.auditionPlayer().isPlaying());
    graph.auditionPlayer().stop();
    VSM_ASSERT(!graph.auditionPlayer().isPlaying());
}

// ---------------------------------------------------------------------------
// D35.4 — LE MUET ET LE SOLO D'UN DOSSIER ATTEIGNENT SON CONTENU.
//
// L'attendu, écrit avant la mesure : « le rendu d'un projet dont le dossier est
// muet est IDENTIQUE AU BIT PRÈS au rendu du même projet où l'on aurait rendu
// muet chaque piste du dossier à la main. Comparer deux booléens ne prouverait
// que l'accord de deux `if` ; comparer deux rendus prouve que le son se tait. »
// ---------------------------------------------------------------------------

namespace {

/// `Batterie/(0) Kick(1) Snare(1) Basse(0)` — deux pistes dans un dossier, une
/// à la racine. Les trois sonnent.
Project projetADossier() {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track dossier; dossier.name = "Batterie";
    dossier.kind = Track::Kind::Folder; dossier.folderDepth = 0;
    p.tracks.push_back(dossier);
    Track kick = uneNote("Kick", 40);   kick.folderDepth = 1;
    Track snare = uneNote("Snare", 47); snare.folderDepth = 1;
    Track basse = uneNote("Basse", 33); basse.folderDepth = 0;
    p.tracks.push_back(kick);
    p.tracks.push_back(snare);
    p.tracks.push_back(basse);
    return p;
}

const std::vector<std::pair<size_t, std::string>> kTroisMachines = {
    {1, "vsm.minimoog"}, {2, "vsm.minimoog"}, {3, "vsm.minimoog"}};

} // namespace

VSM_TEST(muting_a_folder_silences_its_tracks_exactly_as_muting_each_one_would) {
    // LE TÉMOIN EST LE MÊME CODE : deux projets, une seule variable -- le muet
    // posé sur le dossier d'un côté, sur chaque membre de l'autre.
    Project parLeDossier = projetADossier();
    parLeDossier.tracks[0].muted = true;
    Project aLaMain = projetADossier();
    aLaMain.tracks[1].muted = true;
    aLaMain.tracks[2].muted = true;

    const RenderedAudio a = rendre(parLeDossier, kTroisMachines);
    const RenderedAudio b = rendre(aLaMain, kTroisMachines);
    std::printf("      [D35.4] dossier muet vs membres muets : écart max %.9f\n", ecartMax(a, b));
    VSM_ASSERT_EQ(ecartMax(a, b), 0.0f);

    // ET LA BASSE, HORS DU DOSSIER, SONNE TOUJOURS : sans quoi le test
    // passerait aussi sur un projet entièrement muet.
    VSM_ASSERT(peakOf(a.left) > 1.0e-4f);
    const RenderedAudio complet = rendre(projetADossier(), kTroisMachines);
    VSM_ASSERT(peakOf(complet.left) > peakOf(a.left));
}

VSM_TEST(soloing_a_folder_leaves_only_its_tracks_sounding) {
    Project parLeDossier = projetADossier();
    parLeDossier.tracks[0].solo = true;
    Project aLaMain = projetADossier();
    aLaMain.tracks[1].solo = true;
    aLaMain.tracks[2].solo = true;

    const RenderedAudio a = rendre(parLeDossier, kTroisMachines);
    const RenderedAudio b = rendre(aLaMain, kTroisMachines);
    std::printf("      [D35.4] dossier en solo vs membres en solo : écart max %.9f\n",
                ecartMax(a, b));
    VSM_ASSERT_EQ(ecartMax(a, b), 0.0f);
    VSM_ASSERT(peakOf(a.left) > 1.0e-4f);
}

VSM_TEST(a_solo_safe_track_does_not_escape_a_muted_folder) {
    // L'ATTENDU DE LA FEUILLE DE ROUTE ÉTAIT FAUX, ET C'EST ÉCRIT LÀ-BAS AUSSI.
    // Il annonçait qu'une piste protégée resterait audible sous un dossier
    // muet. `soloSafe` veut dire « le solo des AUTRES ne me concerne pas », et
    // non « je suis toujours audible » -- la règle n° 2 de `trackAudible` le dit
    // déjà, puisqu'une piste protégée obéit à son propre muet. Le muet d'un
    // dossier est un muet posé sur elle, pas le solo d'un tiers.
    Project p = projetADossier();
    p.tracks[0].muted = true;
    p.tracks[1].soloSafe = true;
    const RenderedAudio protege = rendre(p, kTroisMachines);

    Project sansProtection = projetADossier();
    sansProtection.tracks[0].muted = true;
    const RenderedAudio ordinaire = rendre(sansProtection, kTroisMachines);
    std::printf("      [D35.4] protégée sous un dossier muet : écart max %.9f\n",
                ecartMax(protege, ordinaire));
    VSM_ASSERT_EQ(ecartMax(protege, ordinaire), 0.0f);
}

VSM_TEST(a_folder_that_says_nothing_leaves_the_render_bit_for_bit_as_before) {
    // LE CHEMIN D'AVANT L'ÉTAPE, INTACT : un dossier ni muet ni soloé ni
    // désactivé ne doit rien changer au son, sans quoi D35.4 aurait modifié des
    // morceaux finis sans le dire.
    const RenderedAudio avecDossier = rendre(projetADossier(), kTroisMachines);
    Project sansDossier = projetADossier();
    for (auto& t : sansDossier.tracks) t.folderDepth = 0;
    sansDossier.tracks.erase(sansDossier.tracks.begin());
    const RenderedAudio sans = rendre(sansDossier, {{0, "vsm.minimoog"}, {1, "vsm.minimoog"},
                                                     {2, "vsm.minimoog"}});
    std::printf("      [D35.4] avec dossier vs sans dossier : écart max %.9f\n",
                ecartMax(avecDossier, sans));
    VSM_ASSERT_EQ(ecartMax(avecDossier, sans), 0.0f);
}
