#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include <cmath>
#include "vsm/audio/io/WavFileWriter.h"
#include <filesystem>
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

// --------------------------------------------------------------------------
// D18.1 — REPORTER LA SÉLECTION EN AUDIO, ET CE QU'UN REPORT NE PEUT PAS ÊTRE.
//
// LE CRITÈRE ÉCRIT DANS LA FEUILLE DE ROUTE DISAIT « le report de la sélection
// est identique au rendu du morceau sur cette plage ». LA MESURE L'A RÉFUTÉ,
// deux fois, et la seconde fois est la plus instructive.
//
//  1. Premier essai, clips voisins : écart 0,459. C'est la QUEUE de la note du
//     clip d'avant, qui sonne encore au début de la plage. Elle doit être
//     absente : le report est posé À CÔTÉ de la piste d'origine, qui continue
//     de jouer, et une queue reportée s'entendrait deux fois.
//  2. Second essai, clips écartés d'une seconde de silence : écart 0,426
//     ENCORE, alors que plus rien ne sonnait. Les deux rendus contiennent bien
//     la note (crêtes 0,327 et 0,358) : elle n'est pas au même endroit du
//     cycle. UNE MACHINE A DE LA MÉMOIRE — phase d'oscillateur, charge de
//     filtre, état d'enveloppe — et une note précédée d'une autre ne sonne pas
//     échantillon pour échantillon comme la même note jouée à froid.
//
// Ce n'est donc pas un défaut à corriger, c'est ce qu'est un report de
// sélection, chez Cubase comme ici. Le rendu part de zéro (D6.1), ce qui met
// les EFFETS dans l'état où l'oreille les attend ; rien ne peut mettre la
// MACHINE dans l'état que lui aurait donné un matériau qu'on a justement
// exclu — et le voudrait-on qu'il faudrait rendre ce matériau, c'est-à-dire
// ne plus reporter une sélection.
//
// Ce qui est donc vérifié ici : le report CONTIENT ce qu'on a choisi, il ne
// contient PAS ce qu'on n'a pas choisi, et l'écart au morceau est nommé.
// --------------------------------------------------------------------------

namespace {

LoadedBundle bundleDeuxClipsEcartes() {
    LoadedBundle bundle;
    bundle.project.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    Track piste;
    piste.name = "Basse";
    piste.instrumentId = "vsm.minimoog";
    piste.addNote(0, 240, 45, 100, 0, ids);         // 0 à 0,25 s
    piste.addNote(1440, 1680, 52, 110, 0, ids);     // 1,5 à 1,75 s
    Clip premier;
    premier.id = 1;
    premier.sourceStart = 0;    premier.sourceLength = 480;
    premier.startTick = 0;      premier.length = 480;
    Clip second;
    second.id = 2;
    second.sourceStart = 1440;  second.sourceLength = 480;
    second.startTick = 1440;    second.length = 480;
    piste.clips = {premier, second};
    bundle.project.tracks.push_back(std::move(piste));
    bundle.document = documentFromProject(bundle.project);
    return bundle;
}

LoadedBundle sansLePremierClip(LoadedBundle bundle) {
    bundle.project.tracks[0].clips = {bundle.project.tracks[0].clips[1]};
    bundle.document = documentFromProject(bundle.project);
    return bundle;
}

float crete(const std::vector<float>& s) {
    float c = 0.0f;
    for (float e : s) c = std::max(c, std::abs(e));
    return c;
}

} // namespace

VSM_TEST(the_bounce_contains_the_chosen_clip_and_not_what_was_left_out) {
    RenderOptions o = options();
    o.sampleRate = 8000.0;
    o.tailSeconds = 0.0;

    // Sur la plage du clip CHOISI : il sonne.
    o.startSeconds = 1.5;
    o.durationSeconds = 0.5;
    vsm::audio::engine::RenderedAudio choisi;
    VSM_ASSERT(renderTrackForFreeze(sansLePremierClip(bundleDeuxClipsEcartes()), 0, choisi, o).success);
    VSM_ASSERT(crete(choisi.left) > 1.0e-4f);

    // Sur la plage du clip ÉCARTÉ : silence. C'est la moitié qui garantit que
    // le report ne fait pas entrer ce qu'on a désélectionné.
    o.startSeconds = 0.0;
    o.durationSeconds = 0.2;
    vsm::audio::engine::RenderedAudio ecarte;
    VSM_ASSERT(renderTrackForFreeze(sansLePremierClip(bundleDeuxClipsEcartes()), 0, ecarte, o).success);
    VSM_ASSERT(crete(ecarte.left) < 1.0e-6f);
}

VSM_TEST(a_bounced_selection_is_not_the_song_and_the_reason_is_the_machines_memory) {
    // CE TEST EXISTE POUR EMPÊCHER UNE FAUSSE RÉPARATION. Si quelqu'un le voit
    // échouer un jour parce que l'écart est devenu nul, c'est que le report
    // aura recommencé à rendre ce qu'on n'avait pas choisi.
    RenderOptions o = options();
    o.sampleRate = 8000.0;
    o.startSeconds = 1.5;
    o.durationSeconds = 0.5;
    o.tailSeconds = 0.0;

    vsm::audio::engine::RenderedAudio entiere, choisie;
    VSM_ASSERT(renderTrackForFreeze(bundleDeuxClipsEcartes(), 0, entiere, o).success);
    VSM_ASSERT(renderTrackForFreeze(sansLePremierClip(bundleDeuxClipsEcartes()), 0, choisie, o).success);
    VSM_ASSERT_EQ(choisie.left.size(), entiere.left.size());

    double pire = 0.0;
    for (size_t i = 0; i < entiere.left.size(); ++i)
        pire = std::max(pire, std::abs(static_cast<double>(choisie.left[i] - entiere.left[i])));
    std::printf("      [D18.1] report contre morceau : ecart %.3e (cretes %.4f et %.4f)\n",
                pire, crete(entiere.left), crete(choisie.left));

    // Les deux contiennent la note...
    VSM_ASSERT(crete(entiere.left) > 1.0e-4f);
    VSM_ASSERT(crete(choisie.left) > 1.0e-4f);
    // ...et pourtant elles diffèrent : c'est la mémoire de la machine.
    VSM_ASSERT(pire > 1.0e-3);
}

// ---------------------------------------------------------------------------
// D33.4 — GELER UNE PISTE AUDIO.
//
// L'attendu, écrit avant la mesure : « moins de -120 dBFS entre le gel et le
// vif, et NON zéro : le gel traverse son propre rendu isolé, le vif traverse
// le graphe complet, et deux chemins de mixage flottants ne donnent pas le
// dernier bit. Exiger zéro serait un critère qu'on contourne. »
// ---------------------------------------------------------------------------

namespace {

/// Un projet d'une seule piste AUDIO, dont le matériau est fabriqué en mémoire
/// plutôt que lu : le gel doit marcher sur une piste audio comme sur une piste
/// MIDI, et cela n'a rien à voir avec la façon dont le fichier est arrivé.
LoadedBundle bundleAudio() {
    LoadedBundle bundle;
    bundle.project.ticksPerQuarterNote = 480;
    Track piste;
    piste.name = "Prise";
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "prise.wav";
    piste.audio.sampleRate = 8000.0;
    piste.audio.frames = 8000;
    bundle.project.tracks.push_back(std::move(piste));
    bundle.document = documentFromProject(bundle.project);

    // LE MATÉRIAU EST UN VRAI FICHIER, écrit dans un dossier temporaire : le
    // rendu résout les chemins RELATIFS au dossier du bundle, et un tampon
    // fourni en mémoire ne passerait pas par le même chemin que ce qu'on
    // prétend mesurer.
    const std::filesystem::path dossier =
        std::filesystem::temp_directory_path() / "vsm-gel-audio";
    std::filesystem::create_directories(dossier);
    std::vector<float> g(8000), d(8000);
    for (size_t i = 0; i < 8000; ++i) {
        const auto v = static_cast<float>(0.7 * std::sin(2.0 * 3.14159265358979 * 220.0
                                                          * static_cast<double>(i) / 8000.0));
        g[i] = v;
        d[i] = v;
    }
    vsm::audio::io::WavFileWriter::writeFile(g.data(), d.data(), 8000, 8000.0,
                                              vsm::audio::io::SampleFormat::Float32,
                                              (dossier / "prise.wav").string());
    bundle.folderPath = dossier.string();
    return bundle;
}

float dBFS(float ecart) {
    return ecart <= 0.0f ? -240.0f : 20.0f * std::log10(ecart);
}

} // namespace

VSM_TEST(an_audio_track_can_be_frozen_and_the_freeze_matches_the_live_render) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    LoadedBundle bundle = bundleAudio();
    vsm::audio::engine::RenderedAudio gel;
    const RenderResult resultat = renderTrackForFreeze(bundle, 0, gel, options());
    // LE GEL D'UNE PISTE AUDIO N'EST PAS REFUSÉ : c'est tout l'objet de
    // l'étape, et la restriction ne vivait que dans un test de l'interface.
    VSM_ASSERT(resultat.success);
    VSM_ASSERT(resultat.peakLevel > 0.1f);

    // LE VIF, ET LE PIÈGE QUE LA PREMIÈRE VERSION DE CE TEST A PAYÉ.
    //
    // Comparer le gel au rendu du projet TEL QUEL donne un écart énorme, et
    // c'est normal : le gel capture le signal d'AVANT le fader et le
    // panoramique (D5.5), donc chaque canal inaltéré ; le rendu tel quel passe
    // par la loi de panoramique, qui vaut 0,707 sur les deux canaux au centre.
    // L'écart mesuré n'était pas un défaut du gel, c'était la différence que
    // le gel EST censé avoir. Le critère « moins de -120 dBFS » avait donc été
    // écrit contre la mauvaise référence.
    //
    // La bonne référence est celle de D5.5 : la même piste rendue à fond à
    // gauche puis à fond à droite, où la loi vaut exactement 1 et 0.
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

    const size_t n = std::min(gel.left.size(), attenduG.size());
    VSM_ASSERT(n > 1000);
    float pire = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        pire = std::max(pire, std::fabs(gel.left[i] - attenduG[i]));
        pire = std::max(pire, std::fabs(gel.right[i] - attenduD[i]));
    }
    // Une piste AUDIO ne traverse ni instrument ni insert : le chemin est le
    // même des deux côtés, et l'écart est donc NUL, pas seulement petit.
    // C'est plus fort que ce que la feuille de route demandait.
    VSM_ASSERT(dBFS(pire) < -120.0f);
    VSM_ASSERT_EQ(pire, 0.0f);
}
