// D534 : LA RÉSOLUTION UNIQUE DES COURBES D'AUTOMATION.
//
// L'export ne résolvait que les réglages de machine : toute l'automation de
// mixage se perdait dans le fichier exporté, en silence. Ces tests tiennent la
// traduction que les deux chemins partagent désormais, une famille par test, et
// le dernier la mesure là où elle comptait : dans un RENDU hors ligne.

#include "TestFramework.h"
#include "vsm/interchange/AutomationResolution.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include <cmath>
#include <string>

using vsm::audio::engine::AutomationTarget;
using vsm::interchange::resolveAutomation;
using vsm::sequencer::AutomationCurve;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

AutomationCurve courbe(const std::string& nom, float de, float a, float courbure = 0.0f) {
    AutomationCurve c;
    c.parameter = nom;
    c.points.push_back({0, de, false, courbure});
    c.points.push_back({1920, a, false, 0.0f});
    return c;
}

Project projetAUnePiste(const std::string& machine) {
    Project p;
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track t;
    t.name = "Essai";
    t.instrumentId = machine;
    uint64_t ids = 1;
    if (!machine.empty()) t.addNote(0, 1920, 60, 100, 0, ids);
    p.tracks.push_back(t);
    return p;
}

bool ditLaCourbe(const std::vector<std::string>& phrases, const std::string& nom, const std::string& pourquoi) {
    for (const auto& phrase : phrases)
        if (phrase.find("« " + nom + " »") != std::string::npos && phrase.find(pourquoi) != std::string::npos)
            return true;
    return false;
}

} // namespace

VSM_TEST(the_four_mix_families_resolve_to_their_engine_targets) {
    Project p = projetAUnePiste("vsm.minimoog");
    p.tracks[0].automation = {courbe("mix.volume", 1.0f, 0.0f), courbe("mix.pan", -1.0f, 1.0f),
                              courbe("mix.trim", 0.0f, -6.0f), courbe("mix.send.2", 0.0f, 0.5f)};
    const auto r = resolveAutomation(p);
    VSM_ASSERT_EQ(r.lanes.size(), static_cast<size_t>(4));
    VSM_ASSERT(r.warnings.empty());
    VSM_ASSERT(r.lanes[0].target == AutomationTarget::TrackVolume);
    VSM_ASSERT(r.lanes[1].target == AutomationTarget::TrackPan);
    VSM_ASSERT(r.lanes[2].target == AutomationTarget::TrackTrim);
    VSM_ASSERT(r.lanes[3].target == AutomationTarget::TrackSend);
    VSM_ASSERT_EQ(r.lanes[3].targetSlot, static_cast<size_t>(1));
    // Les familles de mixage ne sont pas bornées : le trim garde ses décibels.
    VSM_ASSERT(r.lanes[2].points().back().value == -6.0f);
}

// L'export écartait TOUTE courbe d'une piste sans machine (« automation sans
// instrument, ignorée ») : un fondu sur une piste audio, un bus ou un VCA.
VSM_TEST(a_mix_curve_resolves_on_a_track_without_a_machine) {
    Project p = projetAUnePiste("");
    p.tracks[0].kind = Track::Kind::Vca;
    p.tracks[0].automation = {courbe("mix.volume", 1.0f, 0.0f)};
    const auto r = resolveAutomation(p);
    VSM_ASSERT_EQ(r.lanes.size(), static_cast<size_t>(1));
    VSM_ASSERT(r.lanes[0].target == AutomationTarget::TrackVolume);
    VSM_ASSERT(r.warnings.empty());
}

VSM_TEST(master_and_insert_curves_reach_their_settings) {
    Project p = projetAUnePiste("vsm.minimoog");
    vsm::sequencer::TrackEffect filtre;
    filtre.type = "filter";
    p.tracks[0].effects.push_back(filtre);
    p.tracks[0].automation = {courbe("master.Stereo Width", 1.0f, 0.0f),
                              courbe("insert.1.effect.filter.cutoff", 200.0f, 2000.0f)};
    const auto r = resolveAutomation(p);
    VSM_ASSERT_EQ(r.lanes.size(), static_cast<size_t>(2));
    VSM_ASSERT(r.warnings.empty());
    VSM_ASSERT(r.lanes[0].target == AutomationTarget::MasterParam);
    VSM_ASSERT_EQ(r.lanes[0].targetParam,
                  static_cast<vsm::audio::plugin::ParamId>(vsm::audio::engine::MasterBus::kStereoWidth));
    const auto profil = vsm::interchange::buildSemanticProfile(vsm::interchange::effectSemanticPluginId("filter"));
    const auto* coupure = profil.findBySemanticId("effect.filter.cutoff");
    VSM_ASSERT(coupure != nullptr);
    VSM_ASSERT(r.lanes[1].target == AutomationTarget::InsertParam);
    VSM_ASSERT_EQ(r.lanes[1].targetSlot, static_cast<size_t>(0));
    VSM_ASSERT_EQ(r.lanes[1].targetParam, coupure->paramId);
}

VSM_TEST(a_machine_curve_resolves_and_is_bounded_to_the_parameter_range) {
    Project p = projetAUnePiste("vsm.minimoog");
    const auto profil = vsm::interchange::buildSemanticProfile("vsm.minimoog");
    const auto* coupure = profil.findBySemanticId("filter.1.cutoff");
    VSM_ASSERT(coupure != nullptr);
    p.tracks[0].automation = {courbe("filter.1.cutoff", coupure->maximum * 10.0f, coupure->minimum)};
    const auto r = resolveAutomation(p);
    VSM_ASSERT_EQ(r.lanes.size(), static_cast<size_t>(1));
    VSM_ASSERT(r.lanes[0].target == AutomationTarget::InstrumentParam);
    VSM_ASSERT_EQ(r.lanes[0].targetParam, coupure->paramId);
    VSM_ASSERT(r.lanes[0].points().front().value == coupure->maximum);
}

// L'export perdait aussi la courbure (D17.7) : un fondu courbe en lecture sortait
// droit du fichier.
VSM_TEST(the_curvature_of_each_point_is_carried) {
    Project p = projetAUnePiste("vsm.minimoog");
    p.tracks[0].automation = {courbe("mix.volume", 1.0f, 0.0f, 0.6f)};
    const auto r = resolveAutomation(p);
    VSM_ASSERT_EQ(r.lanes.size(), static_cast<size_t>(1));
    VSM_ASSERT(r.lanes[0].points().front().bend == 0.6f);
}

// Rien n'est écarté sans le dire : chaque courbe non résolue a sa phrase, qui la
// nomme et dit pourquoi — et une courbe vide n'est pas une perte.
VSM_TEST(an_unresolved_curve_is_said_with_its_reason) {
    Project p = projetAUnePiste("vsm.minimoog");
    AutomationCurve vide;
    vide.parameter = "mix.pan";
    p.tracks[0].automation = {courbe("mix.send.9", 0.0f, 1.0f), courbe("master.Nulle Part", 0.0f, 1.0f),
                              courbe("insert.1.effect.filter.cutoff", 0.0f, 1.0f),
                              courbe("osc.9.inexistant", 0.0f, 1.0f), vide};
    Project sansMachine = projetAUnePiste("");
    sansMachine.tracks[0].automation = {courbe("filter.1.cutoff", 0.0f, 1.0f)};
    const auto r = resolveAutomation(p);
    const auto s = resolveAutomation(sansMachine);
    VSM_ASSERT(r.lanes.empty());
    VSM_ASSERT_EQ(r.warnings.size(), static_cast<size_t>(4));
    VSM_ASSERT(ditLaCourbe(r.warnings, "mix.send.9", "pas de départ de ce numéro"));
    VSM_ASSERT(ditLaCourbe(r.warnings, "master.Nulle Part", "la tranche master n'a pas ce réglage"));
    VSM_ASSERT(ditLaCourbe(r.warnings, "insert.1.effect.filter.cutoff", "la piste n'a pas d'insert de ce numéro"));
    VSM_ASSERT(ditLaCourbe(r.warnings, "osc.9.inexistant", "la machine n'a pas ce paramètre"));
    VSM_ASSERT(s.lanes.empty());
    VSM_ASSERT(ditLaCourbe(s.warnings, "filter.1.cutoff", "la piste n'a pas de machine"));
    VSM_ASSERT(r.warnings.front().rfind("Piste 1 (Essai) : ", 0) == 0);
}

// LÀ OÙ ÇA COMPTAIT : dans le RENDU. Un fondu de volume de 1 à 0 sur la durée
// éteint la fin du fichier ; avant D534, le rendu était égal au bit à celui de
// la même piste sans courbe.
VSM_TEST(an_exported_volume_fade_actually_fades) {
    auto rendre = [](const Project& p) {
        vsm::interchange::LoadedBundle bundle;
        bundle.project = p;
        bundle.document = vsm::interchange::documentFromProject(p);
        vsm::interchange::RenderOptions options;
        options.durationSeconds = 1.0;
        vsm::audio::engine::RenderedAudio sortie;
        const auto res = vsm::interchange::renderBundleToBuffer(bundle, sortie, options);
        VSM_ASSERT(res.success);
        return sortie;
    };
    auto energie = [](const vsm::audio::engine::RenderedAudio& a, size_t de, size_t a_) {
        double e = 0.0;
        for (size_t i = de; i < a_ && i < a.left.size(); ++i)
            e += double(a.left[i]) * a.left[i] + double(a.right[i]) * a.right[i];
        return e;
    };
    Project temoin = projetAUnePiste("vsm.minimoog");
    Project fondu = temoin;
    fondu.tracks[0].automation = {courbe("mix.volume", 1.0f, 0.0f)};   // 1 920 ticks = 2 s à 120
    const auto t = rendre(temoin);
    const auto f = rendre(fondu);
    const size_t n = t.left.size();
    VSM_ASSERT(n > 0 && f.left.size() == n);
    const double debutT = energie(t, 0, n / 4), finT = energie(t, 3 * n / 4, n);
    const double debutF = energie(f, 0, n / 4), finF = energie(f, 3 * n / 4, n);
    VSM_ASSERT(debutT > 0.0 && finT > 0.0);   // sans quoi le test ne prouverait rien
    // La courbe descend de 1 à 0,5 sur la seconde rendue : le dernier quart est
    // vers 0,56 de gain (0,31 en énergie), quand le témoin garde le sien.
    VSM_ASSERT(finF / finT < 0.45);
    VSM_ASSERT(debutF / debutT > 0.8);
}

// TROUVÉ EN MESURANT : un export en STEMS jetait tout ce que ses rendus
// avertissaient. Deux stems, une courbe non résolue : la phrase arrive au rapport,
// et une seule fois — chaque rendu de stem la redit.
VSM_TEST(stems_report_what_their_renders_warn_once_each) {
    Project p = projetAUnePiste("vsm.minimoog");
    p.tracks.push_back(p.tracks[0]);
    p.tracks[1].name = "Autre";
    p.tracks[0].automation = {courbe("osc.9.inexistant", 0.0f, 1.0f)};
    vsm::interchange::LoadedBundle bundle;
    bundle.project = p;
    bundle.document = vsm::interchange::documentFromProject(p);
    vsm::interchange::RenderOptions options;
    options.durationSeconds = 0.25;
    const auto stems = vsm::interchange::renderStems(bundle, vsm::interchange::StemGranularity::Tracks, options);
    VSM_ASSERT(stems.success);
    VSM_ASSERT_EQ(stems.stems.size(), static_cast<size_t>(2));
    size_t dites = 0;
    for (const auto& phrase : stems.warnings)
        if (phrase.find("« osc.9.inexistant »") != std::string::npos) ++dites;
    VSM_ASSERT_EQ(dites, static_cast<size_t>(1));
}
