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
/// D573.1 : `droiteMuette` — la gauche porte le sinus, la droite le silence : une source au côté non nul, que la largeur
/// stéréo resserre (une source mono n'a pas de côté). Un autre dossier : les deux fichiers coexistent.
LoadedBundle bundleAudio(bool droiteMuette = false) {
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
        std::filesystem::temp_directory_path() / (droiteMuette ? "vsm-gel-audio-gauche" : "vsm-gel-audio");
    std::filesystem::create_directories(dossier);
    std::vector<float> g(8000), d(8000);
    for (size_t i = 0; i < 8000; ++i) {
        const auto v = static_cast<float>(0.7 * std::sin(2.0 * 3.14159265358979 * 220.0
                                                          * static_cast<double>(i) / 8000.0));
        g[i] = v;
        d[i] = droiteMuette ? 0.0f : v;
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

// D571.2 — LA LOI DE PANORAMIQUE DU PROJET, MESURÉE PAR LE RENDU HORS LIGNE (le chemin de l'export, D332). La même
// prise mono, au centre puis tournée à fond à gauche, sous les quatre lois : au centre, la crête tombe de 3,01, 4,52 et
// 6,02 dB sous celle de 0 dB ; à fond, les quatre rendus sont identiques AU BIT — c'est aussi ce qui garde le gel
// (D5.5), qui s'appuie sur les extrémités de la loi.
VSM_TEST(the_pan_law_of_the_project_sets_the_centre_and_leaves_the_ends_alone) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    using vsm::sequencer::PanLaw;
    auto rendre = [&](PanLaw loi, float pan) {
        LoadedBundle b = bundleAudio();
        b.project.panLaw = loi;
        b.project.tracks[0].pan = pan;
        b.project.tracks[0].volume = 1.0f;
        vsm::audio::engine::RenderedAudio sortie;
        VSM_ASSERT(renderBundleToBuffer(b, sortie, options()).success);
        return sortie;
    };
    auto crete = [](const std::vector<float>& x) {
        float c = 0.0f;
        for (float v : x) c = std::max(c, std::fabs(v));
        return c;
    };
    const auto zero = rendre(PanLaw::ZeroDb, 0.0f);
    const float reference = crete(zero.left);
    VSM_ASSERT(reference > 0.1f);
    const std::pair<PanLaw, double> attendus[] = {
        {PanLaw::Minus3dB, -3.0103}, {PanLaw::Minus4_5dB, -4.5154}, {PanLaw::Minus6dB, -6.0206}};
    for (const auto& [loi, db] : attendus) {
        const auto r = rendre(loi, 0.0f);
        const double ecart = 20.0 * std::log10(static_cast<double>(crete(r.left)) / reference);
        VSM_ASSERT_NEAR(ecart, db, 0.05);
        VSM_ASSERT_NEAR(crete(r.left), crete(r.right), 1e-6);   // au centre, les deux canaux à égalité
    }
    const auto gaucheTemoin = rendre(PanLaw::Minus3dB, -1.0f);   // le témoin des extrémités
    for (PanLaw loi : {PanLaw::ZeroDb, PanLaw::Minus4_5dB, PanLaw::Minus6dB}) {
        const auto g = rendre(loi, -1.0f);
        VSM_ASSERT(g.left == gaucheTemoin.left);
        VSM_ASSERT(g.right == gaucheTemoin.right);
    }
}

// D572.2 — LES DÉPARTS QUI SUIVENT LE PANORAMIQUE, par le rendu hors ligne. La prise tournée à fond à GAUCHE, son fader
// au silence, un départ PRÉ-FADER à 1 vers un égaliseur neutre : on n'entend que le retour du bus. Sans l'option, le
// départ prend le signal d'avant le panoramique — les deux canaux du retour égaux, c'est le témoin ; avec elle, le canal
// droit du retour tombe à zéro et le gauche garde tout. Sous −6 dB, tournée au centre, le retour perd 6,02 dB.
VSM_TEST(sends_that_follow_the_pan_carry_it_into_the_bus) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    auto rendre = [&](bool suit, float pan, vsm::sequencer::PanLaw loi) {
        LoadedBundle b = bundleAudio();
        b.project.panLaw = loi;
        b.project.sends.clear();
        vsm::sequencer::SendBusDescription bus;
        bus.name = "Neutre";
        bus.effectType = "eq";
        bus.preFader = true;
        b.project.sends.push_back(bus);
        auto& piste = b.project.tracks[0];
        piste.pan = pan;
        piste.volume = 0.0f;
        piste.sendLevels = {1.0f};
        piste.sendsFollowPan = suit;
        vsm::audio::engine::RenderedAudio sortie;
        VSM_ASSERT(renderBundleToBuffer(b, sortie, options()).success);
        return sortie;
    };
    auto crete = [](const std::vector<float>& x) {
        float c = 0.0f;
        for (float v : x) c = std::max(c, std::fabs(v));
        return c;
    };
    using vsm::sequencer::PanLaw;
    const auto temoin = rendre(false, -1.0f, PanLaw::Minus3dB);
    VSM_ASSERT(crete(temoin.left) > 0.1f);
    VSM_ASSERT_NEAR(crete(temoin.right), crete(temoin.left), 1e-6);   // centré : le panoramique n'y est pas
    const auto lie = rendre(true, -1.0f, PanLaw::Minus3dB);
    VSM_ASSERT_NEAR(crete(lie.left), crete(temoin.left), 1e-6);
    VSM_ASSERT(crete(lie.right) < 1e-6f);
    const auto centreSans = rendre(false, 0.0f, PanLaw::Minus6dB);
    const auto centreLie = rendre(true, 0.0f, PanLaw::Minus6dB);
    VSM_ASSERT_NEAR(20.0 * std::log10(static_cast<double>(crete(centreLie.left)) / crete(centreSans.left)), -6.0206, 0.05);
}

// D573.1 — LA LARGEUR STÉRÉO, par le rendu hors ligne (le chemin de l'export, D332). La gauche porte un sinus, la droite
// le silence, au centre sous la loi par défaut : `G' = (1 + w)/2`, `D' = (1 − w)/2` sous la crête de la gauche à 100 %.
// L'attendu écrit avant (ROADMAP-daw, D573) : la gauche à 0 / −1,16 / −2,50 / −4,08 / −6,02 dB, la droite au plancher
// puis à −18,06 / −12,04 / −8,52 / −6,02 dB pour 100, 75, 50, 25 et 0 % ; à 0 %, les deux canaux égaux. Et une source
// MONO n'a pas de côté : à 0 %, elle rend AU BIT ce qu'elle rend à 100 %.
VSM_TEST(the_stereo_width_narrows_the_image_before_the_pan) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    auto rendre = [&](bool droiteMuette, float largeur) {
        LoadedBundle b = bundleAudio(droiteMuette);
        b.project.tracks[0].stereoWidth = largeur;
        b.project.tracks[0].volume = 1.0f;
        vsm::audio::engine::RenderedAudio sortie;
        VSM_ASSERT(renderBundleToBuffer(b, sortie, options()).success);
        return sortie;
    };
    auto crete = [](const std::vector<float>& x) {
        float c = 0.0f;
        for (float v : x) c = std::max(c, std::fabs(v));
        return c;
    };
    const auto entiere = rendre(true, 1.0f);
    const double reference = crete(entiere.left);
    VSM_ASSERT(reference > 0.1);
    VSM_ASSERT(crete(entiere.right) < 1e-6f);
    const struct { float w; double g, d; } attendus[] = {
        {0.75f, -1.1598, -18.0618}, {0.5f, -2.4988, -12.0412}, {0.25f, -4.0824, -8.5194}, {0.0f, -6.0206, -6.0206}};
    for (const auto& a : attendus) {
        const auto r = rendre(true, a.w);
        VSM_ASSERT_NEAR(20.0 * std::log10(crete(r.left) / reference), a.g, 0.05);
        VSM_ASSERT_NEAR(20.0 * std::log10(crete(r.right) / reference), a.d, 0.05);
    }
    const auto mono = rendre(true, 0.0f);
    VSM_ASSERT_NEAR(20.0 * std::log10(crete(mono.left) / crete(mono.right)), 0.0, 1e-4);
    const auto sourceMonoEntiere = rendre(false, 1.0f);
    const auto sourceMonoResserree = rendre(false, 0.0f);
    VSM_ASSERT(sourceMonoEntiere.left == sourceMonoResserree.left);
    VSM_ASSERT(sourceMonoEntiere.right == sourceMonoResserree.right);
}

// D573.1 — LE GEL CAPTE LA SOURCE D'AVANT LE PANNEAU (D5.5) : la largeur, comme le panoramique, reste VIVANTE après le
// gel, et le gel ne doit pas la porter — sinon une piste gelée la subirait deux fois. LA POLARITÉ AUSSI (D23.1 : le
// signe du fader, qui reste vivant comme lui) : relevé en écrivant ce test, le gel la gardait, et une piste gelée
// l'appliquait une seconde fois — sa polarité revenait à l'endroit. Le gel d'une piste resserrée ET inversée rend donc
// exactement la source : la gauche le sinus, la droite le silence.
VSM_TEST(freezing_leaves_the_width_and_the_polarity_alive) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    LoadedBundle source = bundleAudio(true);
    vsm::audio::engine::RenderedAudio brut;
    VSM_ASSERT(renderTrackForFreeze(source, 0, brut, options()).success);
    LoadedBundle reglee = bundleAudio(true);
    reglee.project.tracks[0].stereoWidth = 0.5f;
    reglee.project.tracks[0].invertPhase = true;
    vsm::audio::engine::RenderedAudio gel;
    VSM_ASSERT(renderTrackForFreeze(reglee, 0, gel, options()).success);
    VSM_ASSERT(gel.left.size() == brut.left.size());
    VSM_ASSERT(gel.left == brut.left);
    VSM_ASSERT(gel.right == brut.right);
}

// D573.2 — LA FORME DE CHAQUE FONDU, par le rendu hors ligne (le chemin de l'export, D332). Un clip d'une seconde sur un
// sinus de 202 Hz à 8 kHz — la trame 1 000 et la trame 7 000 tombent sur ses crêtes —, un fondu d'entrée et un de sortie
// d'un quart de seconde (2 000 trames) : la trame 1 000 est le MILIEU de l'entrée, la 7 000 celui de la sortie. Le gain
// s'y lit exactement, rapporté au même rendu SANS fondu. L'attendu écrit avant (ROADMAP-daw, D573) : entrée « lente »
// −12,04 dB (0,5²), sortie « rapide » −3,01 dB (√0,5) ; le témoin à une seule forme (« lente » aux deux) −12,04 dB à la
// sortie.
namespace {
LoadedBundle bundleFondus(bool avecFondus, FadeShape entree, FadeShape sortie) {
    LoadedBundle bundle;
    bundle.project.ticksPerQuarterNote = 480;
    Track piste;
    piste.name = "Fondus";
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "fondus.wav";
    piste.audio.sampleRate = 8000.0;
    piste.audio.frames = 8000;
    Clip clip;
    clip.startTick = 0;
    clip.length = 960;   // une seconde à 120 à la noire
    if (avecFondus) {
        clip.fadeInSeconds = 0.25;
        clip.fadeOutSeconds = 0.25;
    }
    clip.fadeShape = entree;
    clip.fadeOutShape = sortie;
    piste.clips.push_back(clip);
    bundle.project.tracks.push_back(std::move(piste));
    bundle.document = documentFromProject(bundle.project);
    const std::filesystem::path dossier = std::filesystem::temp_directory_path() / "vsm-fondus-formes";
    std::filesystem::create_directories(dossier);
    std::vector<float> g(8000);
    for (size_t i = 0; i < 8000; ++i)
        g[i] = static_cast<float>(0.7 * std::sin(2.0 * 3.14159265358979 * 202.0 * static_cast<double>(i) / 8000.0));
    vsm::audio::io::WavFileWriter::writeFile(g.data(), g.data(), 8000, 8000.0, vsm::audio::io::SampleFormat::Float32,
                                              (dossier / "fondus.wav").string());
    bundle.folderPath = dossier.string();
    return bundle;
}
} // namespace

VSM_TEST(each_fade_sounds_its_own_shape) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    auto rendre = [&](bool avecFondus, FadeShape entree, FadeShape sortie) {
        LoadedBundle b = bundleFondus(avecFondus, entree, sortie);
        vsm::audio::engine::RenderedAudio r;
        VSM_ASSERT(renderBundleToBuffer(b, r, options()).success);
        return r;
    };
    const auto plein = rendre(false, FadeShape::Linear, FadeShape::Linear);
    VSM_ASSERT(plein.left.size() > 7000);
    VSM_ASSERT(std::fabs(plein.left[1000]) > 0.3f);   // une crête, pas un passage à zéro
    VSM_ASSERT(std::fabs(plein.left[7000]) > 0.3f);
    auto gainDb = [&](const vsm::audio::engine::RenderedAudio& r, size_t i) {
        return 20.0 * std::log10(static_cast<double>(r.left[i]) / static_cast<double>(plein.left[i]));
    };
    const auto deux = rendre(true, FadeShape::Slow, FadeShape::Fast);
    const auto temoin = rendre(true, FadeShape::Slow, FadeShape::Slow);
    std::printf("      [D573.2] milieu de l'entrée %.4f dB, de la sortie %.4f dB ; témoin « lente » aux deux : %.4f / %.4f dB\n",
                gainDb(deux, 1000), gainDb(deux, 7000), gainDb(temoin, 1000), gainDb(temoin, 7000));
    VSM_ASSERT_NEAR(gainDb(deux, 1000), -12.0412, 0.1);
    VSM_ASSERT_NEAR(gainDb(deux, 7000), -3.0103, 0.1);
    VSM_ASSERT_NEAR(gainDb(temoin, 1000), -12.0412, 0.1);
    VSM_ASSERT_NEAR(gainDb(temoin, 7000), -12.0412, 0.1);
}

// D576.1 — LES INSERTS APRÈS LE FADER, par le rendu hors ligne. La prise (un sinus de crête 0,7, −3,10 dBFS, les deux
// canaux égaux), le fader à −12 dB, au centre (−3,01 dB), un limiteur au plafond de −6 dBFS. L'attendu écrit avant
// (ROADMAP-daw, D576) : AVANT le fader (le témoin), le limiteur ramène la crête à −6, puis −12 et −3,01 — **−21,0 dBFS**
// à 0,3 dB près ; APRÈS le fader, le signal arrive à −15,1, sous le plafond, et le limiteur se tait — la crête d'entrée
// moins 12 et 3,01 dB, à 0,1 dB près. Et un départ POST-fader prend la sortie de l'insert d'après.
VSM_TEST(a_post_fader_insert_hears_the_fader) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    auto crete = [](const std::vector<float>& x) {
        float c = 0.0f;
        for (float v : x) c = std::max(c, std::fabs(v));
        return c;
    };
    auto rendre = [&](bool apres, bool depart) {
        LoadedBundle b = bundleAudio();
        auto& piste = b.project.tracks[0];
        piste.volume = std::pow(10.0f, -12.0f / 20.0f);
        TrackEffect limiteur;
        limiteur.type = "limiter";
        limiteur.parameters = {{"effect.limiter.ceiling", -6.0f}};
        limiteur.postFader = apres;
        piste.effects = {limiteur};
        if (depart) {   // un départ post-fader à 1 vers un égaliseur neutre, la piste tournée à fond à gauche
            b.project.sends.clear();
            vsm::sequencer::SendBusDescription bus;
            bus.name = "Neutre";
            bus.effectType = "eq";
            b.project.sends.push_back(bus);
            piste.sendLevels = {1.0f};
            piste.pan = -1.0f;
        }
        vsm::audio::engine::RenderedAudio sortie;
        VSM_ASSERT(renderBundleToBuffer(b, sortie, options()).success);
        return sortie;
    };
    const double entree = 20.0 * std::log10(0.7);
    const auto avant = rendre(false, false);
    const auto apres = rendre(true, false);
    const double dbAvant = 20.0 * std::log10(static_cast<double>(crete(avant.left)));
    const double dbApres = 20.0 * std::log10(static_cast<double>(crete(apres.left)));
    std::printf("      [D576.1] avant le fader %.3f dBFS, après %.3f dBFS (entrée %.3f)\n", dbAvant, dbApres, entree);
    VSM_ASSERT_NEAR(dbAvant, -6.0 - 12.0 - 3.0103, 0.3);
    VSM_ASSERT_NEAR(dbApres, entree - 12.0 - 3.0103, 0.1);
    // Le départ post-fader : tournée à gauche, le droit n'a que le retour du bus — la sortie de l'insert d'après.
    const auto departApres = rendre(true, true);
    const auto departAvant = rendre(false, true);
    VSM_ASSERT_NEAR(20.0 * std::log10(static_cast<double>(crete(departApres.right))), entree - 12.0, 0.1);
    VSM_ASSERT_NEAR(20.0 * std::log10(static_cast<double>(crete(departAvant.right))), -6.0 - 12.0, 0.3);
}
