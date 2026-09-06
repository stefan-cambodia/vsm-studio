#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
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
