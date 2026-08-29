#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include <cmath>
#include <mutex>

using namespace vsm::interchange;
using namespace vsm::sequencer;

// D5.5 de docs/ROADMAP-daw.md — LE GEL D'UNE PISTE.
//
// « Une piste gelée sonne identique » : ce n'est pas une formule, c'est une
// égalité qu'on peut vérifier. Ce qui est capturé doit être le signal d'AVANT
// le fader -- volume, panoramique, départs, muet et solo restent vivants,
// sinon geler serait reporter.

namespace {

LoadedBundle bundleDeTest() {
    LoadedBundle bundle;
    bundle.project.ticksPerQuarterNote = 480;
    uint64_t ids = 1;

    Track piste;
    piste.name = "Basse";
    piste.instrumentId = "vsm.minimoog";
    piste.addNote(0, 480, 45, 100, 0, ids);
    piste.addNote(480, 960, 52, 110, 0, ids);
    bundle.project.tracks.push_back(std::move(piste));

    bundle.document = documentFromProject(bundle.project);
    return bundle;
}

RenderOptions options() {
    RenderOptions o;
    o.sampleRate = 8000.0;
    o.blockSize = 256;
    o.durationSeconds = 1.5;
    return o;
}

} // namespace

VSM_TEST(a_frozen_track_captures_exactly_what_it_produced) {
    // LE CRITÈRE, littéralement. On compare le gel au signal de la piste tel
    // que le moteur le produit, canal par canal et échantillon par échantillon.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    LoadedBundle bundle = bundleDeTest();
    vsm::audio::engine::RenderedAudio gel;
    const RenderResult resultat = renderTrackForFreeze(bundle, 0, gel, options());
    VSM_ASSERT(resultat.success);
    VSM_ASSERT(resultat.peakLevel > 0.01f);

    // La référence : la même piste rendue à fond à gauche puis à fond à droite,
    // c'est-à-dire les deux canaux inaltérés (la loi vaut exactement 1 et 0 aux
    // extrémités). Si le gel employait un autre chemin, l'écart se verrait ici.
    auto rendreCanal = [&](float pan, bool prendreGauche) {
        LoadedBundle seule = bundle;
        seule.project.tracks[0].pan = pan;
        seule.project.tracks[0].volume = 1.0f;
        vsm::audio::engine::RenderedAudio sortie;
        renderBundleToBuffer(seule, sortie, options());
        return prendreGauche ? sortie.left : sortie.right;
    };
    const auto attenduG = rendreCanal(-1.0f, true);
    const auto attenduD = rendreCanal(1.0f, false);

    VSM_ASSERT_EQ(gel.left.size(), attenduG.size());
    VSM_ASSERT_EQ(gel.right.size(), attenduD.size());
    for (size_t i = 0; i < gel.left.size(); ++i) {
        // AU BIT PRÈS : aucune division, aucun arrondi n'a été introduit.
        VSM_ASSERT(gel.left[i] == attenduG[i]);
        VSM_ASSERT(gel.right[i] == attenduD[i]);
    }
}

VSM_TEST(freezing_captures_the_inserts_because_they_stop_running_afterwards) {
    // Les inserts sont DANS le fichier gelé -- le moteur cesse de les appliquer
    // sur une piste gelée, et les repasser dessus les appliquerait deux fois.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    LoadedBundle sans = bundleDeTest();
    LoadedBundle avec = bundleDeTest();
    avec.project.tracks[0].effects.push_back({"limiter", {{"effect.limiter.ceiling", -30.0f}}});
    avec.document = documentFromProject(avec.project);

    vsm::audio::engine::RenderedAudio gelSans, gelAvec;
    VSM_ASSERT(renderTrackForFreeze(sans, 0, gelSans, options()).success);
    VSM_ASSERT(renderTrackForFreeze(avec, 0, gelAvec, options()).success);

    float creteSans = 0.0f, creteAvec = 0.0f;
    for (float e : gelSans.left) creteSans = std::max(creteSans, std::abs(e));
    for (float e : gelAvec.left) creteAvec = std::max(creteAvec, std::abs(e));
    VSM_ASSERT(creteSans > 0.05f);
    // Le limiteur à -30 dB (0,032) est bien passé dans le fichier.
    VSM_ASSERT(creteAvec <= 0.04f);
}

VSM_TEST(freezing_ignores_the_fader_the_mute_and_the_sends) {
    // CE QUI RESTE VIVANT APRÈS LE GEL ne doit pas entrer dedans : sinon le
    // volume s'appliquerait deux fois, et une piste muette gèlerait du silence
    // qu'on ne pourrait plus démuter.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    LoadedBundle nu = bundleDeTest();
    LoadedBundle habille = bundleDeTest();
    habille.project.tracks[0].volume = 0.1f;
    habille.project.tracks[0].muted = true;
    habille.project.sends.push_back({"A", "reverb", {}, 1.0f, false});
    habille.project.tracks[0].setSendLevel(0, 1.0f);
    habille.document = documentFromProject(habille.project);

    vsm::audio::engine::RenderedAudio gelNu, gelHabille;
    VSM_ASSERT(renderTrackForFreeze(nu, 0, gelNu, options()).success);
    VSM_ASSERT(renderTrackForFreeze(habille, 0, gelHabille, options()).success);

    VSM_ASSERT_EQ(gelNu.left.size(), gelHabille.left.size());
    for (size_t i = 0; i < gelNu.left.size(); ++i)
        VSM_ASSERT(gelNu.left[i] == gelHabille.left[i]);
}

VSM_TEST(re_freezing_a_frozen_track_starts_again_from_its_material) {
    // Sinon on empilerait des rendus de rendus, et le son dériverait à chaque
    // gel sans que rien ne le dise.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    LoadedBundle frais = bundleDeTest();
    LoadedBundle dejaGele = bundleDeTest();
    dejaGele.project.tracks[0].frozen = true;
    dejaGele.document = documentFromProject(dejaGele.project);

    vsm::audio::engine::RenderedAudio a, b;
    VSM_ASSERT(renderTrackForFreeze(frais, 0, a, options()).success);
    VSM_ASSERT(renderTrackForFreeze(dejaGele, 0, b, options()).success);
    VSM_ASSERT_EQ(a.left.size(), b.left.size());
    for (size_t i = 0; i < a.left.size(); ++i) VSM_ASSERT(a.left[i] == b.left[i]);
}

VSM_TEST(freezing_a_track_that_does_not_exist_says_so) {
    LoadedBundle bundle = bundleDeTest();
    vsm::audio::engine::RenderedAudio sortie;
    const RenderResult resultat = renderTrackForFreeze(bundle, 42, sortie, options());
    VSM_ASSERT(!resultat.success);
    VSM_ASSERT(!resultat.error.empty());
}
