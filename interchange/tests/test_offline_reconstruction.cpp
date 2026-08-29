#include "TestFramework.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/interchange/ProjectBundle.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vsm::interchange;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

namespace fs = std::filesystem;

/// Dossier temporaire propre, supprimé à la sortie -- un test qui laisse des
/// fichiers derrière lui finit par faire échouer le suivant.
class TempFolder {
public:
    explicit TempFolder(const std::string& name) {
        path_ = fs::temp_directory_path() / ("vsm_test_" + name);
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempFolder() { std::error_code code; fs::remove_all(path_, code); }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    std::string str() const { return path_.string(); }
    std::string file(const std::string& relative) const { return (path_ / relative).string(); }

private:
    fs::path path_;
};

Project buildPlayableProject() {
    Project project;
    project.title = "Test de rendu";
    project.ticksPerQuarterNote = 480;

    uint64_t ids = 1;
    Track bass;
    bass.name = "Basse acide";
    bass.channel = 0;
    bass.instrumentId = "vsm.tb303";
    bass.volume = 0.9f;
    for (int i = 0; i < 4; ++i)
        bass.addNote(i * 240, i * 240 + 200, static_cast<uint8_t>(36 + i * 2), 110, 0, ids);
    project.tracks.push_back(bass);
    return project;
}

} // namespace

VSM_TEST(bundle_round_trips_through_a_folder) {
    // Le parcours complet : écrire un dossier de projet, le relire, et
    // retrouver le morceau -- notes comprises, puisqu'elles voyagent en MIDI.
    TempFolder folder("bundle_roundtrip");
    const Project original = buildPlayableProject();

    const BundleSaveResult saved = saveProjectBundle(original, folder.str());
    VSM_ASSERT(saved.success);
    VSM_ASSERT(std::filesystem::exists(folder.file("project.json")));
    VSM_ASSERT(std::filesystem::exists(folder.file("midi/arrangement.mid")));
    VSM_ASSERT(std::filesystem::exists(folder.file("instruments/track_00.synth.json")));

    const BundleLoadResult loaded = loadProjectBundle(folder.str());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.bundle.project.title, std::string("Test de rendu"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks.size(), size_t{1});
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].name, std::string("Basse acide"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].instrumentId, std::string("vsm.tb303"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].notes.size(), size_t{4});
    VSM_ASSERT_EQ(loaded.bundle.presetsByTrack.size(), size_t{1});
    VSM_ASSERT_NEAR(loaded.bundle.project.tracks[0].volume, 0.9f, 1e-6);
}

VSM_TEST(rendering_a_project_folder_produces_real_audio) {
    TempFolder folder("render_audio");
    saveProjectBundle(buildPlayableProject(), folder.str());

    RenderOptions options;
    options.sampleRate = 48000.0;
    options.tailSeconds = 0.5;
    const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("render.wav"), options);

    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.tracksWithInstrument, size_t{1});
    VSM_ASSERT(result.framesWritten > 1000);
    VSM_ASSERT(result.peakLevel > 0.01f); // il y a vraiment du son, pas un fichier de silence
    VSM_ASSERT(std::filesystem::exists(folder.file("render.wav")));
    VSM_ASSERT(std::filesystem::file_size(folder.file("render.wav")) > 1000);
}

VSM_TEST(rendering_is_deterministic) {
    // La condition posée au § 6 de la roadmap : mêmes fichiers d'entrée =>
    // même WAV. Sans ça, une boucle d'optimisation pilotée par Python
    // comparerait du bruit d'un tour à l'autre.
    TempFolder folder("render_determinism");
    saveProjectBundle(buildPlayableProject(), folder.str());

    RenderOptions options;
    options.tailSeconds = 0.25;
    const RenderResult first = renderProjectFolderToWav(folder.str(), folder.file("a.wav"), options);
    const RenderResult second = renderProjectFolderToWav(folder.str(), folder.file("b.wav"), options);
    VSM_ASSERT(first.success && second.success);

    auto readAll = [](const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    };
    const std::string a = readAll(folder.file("a.wav"));
    const std::string b = readAll(folder.file("b.wav"));
    VSM_ASSERT(!a.empty());
    VSM_ASSERT_EQ(a.size(), b.size());
    VSM_ASSERT(a == b); // octet pour octet
}

VSM_TEST(preset_values_actually_reach_the_machine_and_change_the_sound) {
    // Vérifie que le preset n'est pas seulement lu, mais APPLIQUÉ : deux
    // projets identiques, ne différant que par la coupure du filtre, doivent
    // produire des rendus différents. C'est le test qui distingue une chaîne
    // d'interopérabilité qui marche d'une qui se contente de ne pas planter.
    auto renderWithCutoff = [](const std::string& name, float cutoff) {
        TempFolder folder(name);
        const Project project = buildPlayableProject();
        SynthPreset preset;
        preset.pluginId = "vsm.tb303";
        preset.values["filter.1.cutoff"] = cutoff;
        preset.values["filter.1.resonance"] = 0.9f;
        saveProjectBundle(project, folder.str(), {{0, preset}});

        RenderOptions options;
        options.tailSeconds = 0.25;
        const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("out.wav"), options);
        std::ifstream stream(folder.file("out.wav"), std::ios::binary);
        return std::make_pair(result.peakLevel,
                               std::string((std::istreambuf_iterator<char>(stream)),
                                           std::istreambuf_iterator<char>()));
    };

    const auto dark = renderWithCutoff("preset_dark", 200.0f);
    const auto bright = renderWithCutoff("preset_bright", 6000.0f);
    VSM_ASSERT(!dark.second.empty() && !bright.second.empty());
    VSM_ASSERT(dark.second != bright.second);   // le preset a bien agi
    VSM_ASSERT(bright.first > dark.first);       // filtre ouvert => plus d'énergie
}

VSM_TEST(missing_instrument_renders_silence_and_says_so) {
    // Règle « jamais de reconstruction silencieuse fausse » appliquée jusqu'au
    // rendu : le WAV existe, il est silencieux pour cette piste, et le rapport
    // nomme la machine absente.
    TempFolder folder("missing_instrument");
    Project project = buildPlayableProject();
    project.tracks[0].instrumentId = "com.autre.editeur.synth-absent";
    saveProjectBundle(project, folder.str());

    const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("out.wav"));
    VSM_ASSERT(result.success);           // le rendu aboutit
    VSM_ASSERT_EQ(result.tracksWithInstrument, size_t{0});
    VSM_ASSERT_NEAR(result.peakLevel, 0.0f, 1e-6); // et il est silencieux
    bool mentioned = false;
    for (const auto& warning : result.warnings)
        if (warning.find("com.autre.editeur.synth-absent") != std::string::npos) mentioned = true;
    VSM_ASSERT(mentioned);
}

VSM_TEST(loading_reports_a_missing_preset_without_refusing_the_project) {
    // Un projet auquel il manque un preset doit s'ouvrir avec les réglages par
    // défaut, en le disant -- pas refuser de s'ouvrir.
    TempFolder folder("missing_preset");
    saveProjectBundle(buildPlayableProject(), folder.str());
    std::filesystem::remove(folder.file("instruments/track_00.synth.json"));

    const BundleLoadResult loaded = loadProjectBundle(folder.str());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.bundle.presetsByTrack.size(), size_t{0});
    bool mentioned = false;
    for (const auto& warning : loaded.warnings)
        if (warning.find("Preset introuvable") != std::string::npos) mentioned = true;
    VSM_ASSERT(mentioned);
}

VSM_TEST(loading_fails_clearly_when_the_project_or_midi_is_absent) {
    TempFolder folder("incomplete");
    const BundleLoadResult noProject = loadProjectBundle(folder.str());
    VSM_ASSERT(!noProject.success);
    VSM_ASSERT(noProject.error.find("project.json") != std::string::npos);

    // project.json présent mais MIDI absent : erreur explicite, pas un rendu
    // vide qui laisserait croire à un morceau silencieux.
    std::string error;
    writeTextFile(folder.file("project.json"),
                   R"({"format":"vsm-project","version":1,"midi":{"file":"midi/arrangement.mid"},"tracks":[]})",
                   error);
    const BundleLoadResult noMidi = loadProjectBundle(folder.str());
    VSM_ASSERT(!noMidi.success);
    VSM_ASSERT(noMidi.error.find("MIDI") != std::string::npos);
}

VSM_TEST(render_duration_follows_the_content_and_the_requested_tail) {
    TempFolder folder("duration");
    saveProjectBundle(buildPlayableProject(), folder.str()); // ~1 s de notes à 120 BPM

    RenderOptions options;
    options.tailSeconds = 1.0;
    const RenderResult automatic = renderProjectFolderToWav(folder.str(), folder.file("auto.wav"), options);
    VSM_ASSERT(automatic.success);
    VSM_ASSERT(automatic.renderedSeconds > 1.0);

    options.durationSeconds = 3.0; // durée imposée : elle prime
    const RenderResult fixed = renderProjectFolderToWav(folder.str(), folder.file("fixed.wav"), options);
    VSM_ASSERT(fixed.success);
    VSM_ASSERT_NEAR(fixed.renderedSeconds, 3.0, 1e-9);
    VSM_ASSERT_EQ(fixed.framesWritten, static_cast<size_t>(3.0 * 48000.0));
}

// --- D6.1 : la plage exportée se choisit -----------------------------------
//
// Ce que le test doit prouver n'est pas « un fichier plus court sort » -- ce
// serait vrai d'un rendu à froid, qui est justement ce qu'on refuse. Il doit
// prouver que la plage exportée est BIT À BIT la portion correspondante du
// rendu complet : c'est la seule formulation vérifiable de « ce qu'on y
// entend », queues et compresseurs compris.

VSM_TEST(exported_range_is_exactly_the_matching_slice_of_the_whole_render) {
    TempFolder folder("plage");
    saveProjectBundle(buildPlayableProject(), folder.str());
    const auto chargé = loadProjectBundle(folder.str());
    VSM_ASSERT(chargé.success);

    RenderOptions entier;
    entier.durationSeconds = 4.0;
    vsm::audio::engine::RenderedAudio complet;
    VSM_ASSERT(renderBundleToBuffer(chargé.bundle, complet, entier).success);

    RenderOptions plage = entier;
    plage.startSeconds = 1.5;
    plage.durationSeconds = 2.0;
    vsm::audio::engine::RenderedAudio extrait;
    const RenderResult résultat = renderBundleToBuffer(chargé.bundle, extrait, plage);
    VSM_ASSERT(résultat.success);
    VSM_ASSERT_EQ(extrait.numFrames(), static_cast<size_t>(2.0 * 48000.0));
    VSM_ASSERT_NEAR(résultat.renderedSeconds, 2.0, 1e-9);

    const size_t décalage = static_cast<size_t>(1.5 * 48000.0);
    for (size_t i = 0; i < extrait.numFrames(); ++i) {
        VSM_ASSERT_EQ(extrait.left[i], complet.left[décalage + i]);
        VSM_ASSERT_EQ(extrait.right[i], complet.right[décalage + i]);
    }
}

VSM_TEST(a_range_starting_at_zero_renders_exactly_as_before) {
    // Garde-fou : `startSeconds` par défaut ne doit RIEN changer, sans quoi
    // tout ce qui a été mesuré et gelé jusqu'ici se déplacerait en silence.
    TempFolder folder("plage-zero");
    saveProjectBundle(buildPlayableProject(), folder.str());
    const auto chargé = loadProjectBundle(folder.str());
    VSM_ASSERT(chargé.success);

    RenderOptions options;
    options.durationSeconds = 2.0;
    vsm::audio::engine::RenderedAudio a, b;
    VSM_ASSERT(renderBundleToBuffer(chargé.bundle, a, options).success);
    options.startSeconds = 0.0;
    VSM_ASSERT(renderBundleToBuffer(chargé.bundle, b, options).success);
    VSM_ASSERT_EQ(a.numFrames(), b.numFrames());
    for (size_t i = 0; i < a.numFrames(); ++i) VSM_ASSERT_EQ(a.left[i], b.left[i]);
}

VSM_TEST(a_deduced_duration_is_measured_from_the_start_of_the_range) {
    TempFolder folder("plage-deduite");
    saveProjectBundle(buildPlayableProject(), folder.str());
    const auto chargé = loadProjectBundle(folder.str());
    VSM_ASSERT(chargé.success);

    RenderOptions entier;
    entier.tailSeconds = 1.0;
    vsm::audio::engine::RenderedAudio complet, depuis;
    const RenderResult total = renderBundleToBuffer(chargé.bundle, complet, entier);
    VSM_ASSERT(total.success);

    RenderOptions tardive = entier;
    tardive.startSeconds = 0.5;
    const RenderResult reste = renderBundleToBuffer(chargé.bundle, depuis, tardive);
    VSM_ASSERT(reste.success);
    // Partir plus tard raccourcit d'autant : la fin reste la fin du morceau.
    VSM_ASSERT_NEAR(reste.renderedSeconds, total.renderedSeconds - 0.5, 1e-9);
}

// --- D6.2 : la somme des stems égale le mixage ------------------------------

namespace {

/// Un projet à trois pistes, panoramiquées et à des volumes différents, plus un
/// départ de réverbération : de quoi que la somme veuille dire quelque chose.
/// Une seule piste rendrait le test vrai par construction.
Project buildStemProject() {
    Project project;
    project.title = "Test de stems";
    project.ticksPerQuarterNote = 480;
    vsm::sequencer::SendBusDescription reverb;
    reverb.name = "Reverb";
    reverb.effectType = "reverb";
    project.sends.push_back(std::move(reverb));

    uint64_t ids = 1;
    Track basse;
    basse.name = "Basse";
    basse.channel = 0;
    basse.instrumentId = "vsm.tb303";
    basse.volume = 0.8f;
    basse.pan = -0.4f;
    for (int i = 0; i < 4; ++i)
        basse.addNote(i * 240, i * 240 + 200, static_cast<uint8_t>(36 + i * 2), 110, 0, ids);
    project.tracks.push_back(basse);

    Track lead;
    lead.name = "Lead";
    lead.channel = 1;
    lead.instrumentId = "vsm.tb303";
    lead.volume = 0.6f;
    lead.pan = 0.5f;
    lead.setSendLevel(0, 0.4f);
    for (int i = 0; i < 3; ++i)
        lead.addNote(i * 320, i * 320 + 260, static_cast<uint8_t>(60 + i * 3), 90, 1, ids);
    project.tracks.push_back(lead);

    Track nappe;
    nappe.name = "Nappe";
    nappe.channel = 2;
    nappe.instrumentId = "vsm.tb303";
    nappe.volume = 1.0f;
    nappe.addNote(0, 1400, 48, 70, 2, ids);
    project.tracks.push_back(nappe);
    return project;
}

} // namespace

VSM_TEST(the_offline_render_carries_the_send_buses) {
    // TROUVÉ EN ÉCRIVANT D6.2 : les niveaux de départ étaient bien posés, mais
    // aucun effet ne l'était sur les bus. Tout ce qui partait vers la
    // réverbération tombait dans un bus vide, et l'export perdait la
    // réverbération sans rien dire. Le test compare deux rendus qui ne
    // diffèrent QUE par le niveau de départ.
    TempFolder avecDossier("depart-oui");
    saveProjectBundle(buildStemProject(), avecDossier.str());
    const auto avec = loadProjectBundle(avecDossier.str());
    VSM_ASSERT(avec.success);

    TempFolder sansDossier("depart-non");
    Project sansDepart = buildStemProject();
    sansDepart.tracks[1].setSendLevel(0, 0.0f);
    saveProjectBundle(sansDepart, sansDossier.str());
    const auto sans = loadProjectBundle(sansDossier.str());
    VSM_ASSERT(sans.success);

    RenderOptions options;
    options.durationSeconds = 3.0;
    vsm::audio::engine::RenderedAudio a, b;
    VSM_ASSERT(renderBundleToBuffer(avec.bundle, a, options).success);
    VSM_ASSERT(renderBundleToBuffer(sans.bundle, b, options).success);

    double écart = 0.0;
    for (size_t i = 0; i < a.numFrames(); ++i) écart += std::abs(a.left[i] - b.left[i]);
    VSM_ASSERT(écart > 0.001);
}

VSM_TEST(the_sum_of_the_stems_is_the_mix) {
    TempFolder folder("stems");
    saveProjectBundle(buildStemProject(), folder.str());
    const auto chargé = loadProjectBundle(folder.str());
    VSM_ASSERT(chargé.success);

    RenderOptions options;
    options.durationSeconds = 3.0;

    const StemResult stems = renderStems(chargé.bundle, StemGranularity::Tracks, options);
    VSM_ASSERT(stems.success);
    VSM_ASSERT_EQ(stems.stems.size(), static_cast<size_t>(3));

    vsm::audio::engine::RenderedAudio mixage;
    VSM_ASSERT(renderBundleToBuffer(chargé.bundle, mixage, options).success);

    // LE MIXAGE N'EST PAS SILENCIEUX : sans cette vérification, un projet muet
    // ferait passer le test en additionnant des zéros.
    float crête = 0.0f;
    for (const float v : mixage.left) crête = std::max(crête, std::abs(v));
    VSM_ASSERT(crête > 0.01f);

    for (size_t i = 0; i < mixage.numFrames(); ++i) {
        float g = 0.0f, d = 0.0f;
        for (const auto& stem : stems.stems) {
            g += stem.audio.left[i];
            d += stem.audio.right[i];
        }
        // Les mêmes opérations dans le même ordre : l'égalité est numérique,
        // pas approchée. Une tolérance masquerait exactement le genre de faute
        // que ce test existe pour attraper -- une piste oubliée à faible
        // volume, un départ compté deux fois.
        VSM_ASSERT_NEAR(g, mixage.left[i], 1e-6);
        VSM_ASSERT_NEAR(d, mixage.right[i], 1e-6);
    }
}

VSM_TEST(a_stem_carries_its_own_send_return) {
    // Le stem du lead doit contenir SA réverbération, sinon il n'est pas
    // utilisable seul -- c'est toute la différence avec le gel (D5.5).
    TempFolder folder("stems-depart");
    Project avec = buildStemProject();
    saveProjectBundle(avec, folder.str());
    const auto chargéAvec = loadProjectBundle(folder.str());
    VSM_ASSERT(chargéAvec.success);

    TempFolder sansDossier("stems-sans-depart");
    Project sans = buildStemProject();
    sans.tracks[1].setSendLevel(0, 0.0f);
    saveProjectBundle(sans, sansDossier.str());
    const auto chargéSans = loadProjectBundle(sansDossier.str());
    VSM_ASSERT(chargéSans.success);

    RenderOptions options;
    options.durationSeconds = 3.0;
    const StemResult a = renderStems(chargéAvec.bundle, StemGranularity::Tracks, options);
    const StemResult b = renderStems(chargéSans.bundle, StemGranularity::Tracks, options);
    VSM_ASSERT(a.success && b.success);

    double écart = 0.0;
    for (size_t i = 0; i < a.stems[1].audio.numFrames(); ++i)
        écart += std::abs(a.stems[1].audio.left[i] - b.stems[1].audio.left[i]);
    VSM_ASSERT(écart > 0.001);
}

VSM_TEST(stems_are_written_one_file_per_track_with_readable_names) {
    TempFolder folder("stems-fichiers");
    saveProjectBundle(buildStemProject(), folder.str());
    const auto chargé = loadProjectBundle(folder.str());
    VSM_ASSERT(chargé.success);

    RenderOptions options;
    options.durationSeconds = 1.0;
    const std::string sortie = folder.file("stems");
    const StemResult écrits =
        renderStemsToFolder(chargé.bundle, sortie, StemGranularity::Tracks, options);
    VSM_ASSERT(écrits.success);
    VSM_ASSERT(std::filesystem::exists(std::filesystem::path(sortie) / "01 - Basse.wav"));
    VSM_ASSERT(std::filesystem::exists(std::filesystem::path(sortie) / "02 - Lead.wav"));
    VSM_ASSERT(std::filesystem::exists(std::filesystem::path(sortie) / "03 - Nappe.wav"));
}

// --- D0.3 : le rendu contient ce que le projet décrit -----------------------
//
// Les inserts étaient écrits dans `project.json` et jamais posés sur le graphe
// de rendu : un projet portant une distorsion se rendait proprement, sans le
// moindre avertissement, et sans la distorsion. Ces deux tests comparent deux
// rendus qui ne diffèrent que par ce que le projet décrit -- si l'effet n'est
// pas appliqué, les deux fichiers sont identiques et le test tombe.

namespace {

/// Projet minimal qui SONNE : une machine simple, une note tenue.
vsm::sequencer::Project projetQuiSonne() {
    vsm::sequencer::Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);
    vsm::sequencer::Track track;
    track.name = "Essai";
    track.instrumentId = "vsm.minimoog";
    uint64_t ids = 1;
    track.addNote(0, 960, 60, 100, 0, ids);
    project.tracks.push_back(track);
    return project;
}

/// Rend le projet et rend son énergie totale, qui suffit à dire « ce n'est pas
/// le même son » sans dépendre d'un échantillon particulier.
double energieDuRendu(const vsm::sequencer::Project& project) {
    vsm::interchange::LoadedBundle bundle;
    bundle.project = project;
    bundle.document = vsm::interchange::documentFromProject(project);

    TempFolder dossier("rendu_effets");
    vsm::interchange::RenderOptions options;
    options.durationSeconds = 1.5;
    const auto result =
        vsm::interchange::renderBundleToWav(bundle, dossier.file("rendu.wav"), options);
    VSM_ASSERT(result.success);
    return static_cast<double>(result.peakLevel);
}

} // namespace

VSM_TEST(an_insert_effect_described_by_the_project_is_actually_rendered) {
    const vsm::sequencer::Project nu = projetQuiSonne();
    const double creteNue = energieDuRendu(nu);
    VSM_ASSERT(creteNue > 0.01);   // sans quoi le test ne prouverait rien

    // Un passe-bas à 20 Hz, entièrement traité : il ne reste presque rien
    // d'une note à 262 Hz. Les réglages sont posés UN PAR UN et nommés, et
    // c'est délibéré : la première version poussait tous les paramètres à leur
    // minimum, ce qui mettait aussi le mélange sec/traité à zéro -- l'effet
    // était donc branché, appliqué, et parfaitement inaudible. Un test qui
    // passe avec un effet transparent ne prouve rien.
    vsm::sequencer::Project traite = nu;
    auto filtre = vsm::audio::effect::EffectFactory::create("filter");
    VSM_ASSERT(filtre != nullptr);
    filtre->setParameter(0 /* Cutoff */, 20.0f);
    filtre->setParameter(3 /* Mix */, 1.0f);
    traite.tracks[0].effects.push_back(vsm::interchange::describeEffect("filter", *filtre));

    const double creteTraitee = energieDuRendu(traite);
    VSM_ASSERT(creteTraitee < creteNue * 0.5);
}

VSM_TEST(the_master_strip_described_by_the_project_is_actually_rendered) {
    const vsm::sequencer::Project nu = projetQuiSonne();
    const double creteNue = energieDuRendu(nu);

    // Tranche master ACTIVÉE avec un limiteur bas : la crête doit descendre.
    vsm::sequencer::Project mixe = nu;
    vsm::audio::engine::MasterBus bus;
    bus.setParameter(vsm::audio::engine::MasterBus::kEnabled, 1.0f);
    bus.setParameter(vsm::audio::engine::MasterBus::kLimiterCeilingDb, -12.0f);
    mixe.masterParameters = vsm::interchange::describeMasterBus(bus);

    const double creteMixee = energieDuRendu(mixe);
    VSM_ASSERT(creteMixee < creteNue);
}

// --- D2.6 : le rendu hors ligne joue les pistes audio -----------------------
//
// C'est ce qui rend le critère de la phase vérifiable : une reconstruction doit
// se jouer ENTIÈRE, voix comprise, sans qu'une note de sampler porte un fichier
// de 47 Mo.

VSM_TEST(the_offline_render_plays_an_audio_track) {
    TempFolder dossier("projet_audio");
    std::filesystem::create_directories(dossier.file("samples"));

    // Un fichier reconnaissable : une seconde de silence, puis une seconde de
    // signal fort. On saura donc, en lisant le rendu, si le fichier a été posé
    // au bon endroit ET s'il a été lu à la bonne vitesse.
    constexpr double kSr = 48000.0;
    std::vector<float> gauche(static_cast<size_t>(kSr * 2.0), 0.0f);
    std::vector<float> droite(gauche.size(), 0.0f);
    for (size_t i = static_cast<size_t>(kSr); i < gauche.size(); ++i) {
        gauche[i] = 0.5f;
        droite[i] = 0.5f;
    }
    vsm::audio::io::WavFileWriter::writeFile(gauche.data(), droite.data(), gauche.size(), kSr,
                                              vsm::audio::io::SampleFormat::Float32,
                                              dossier.file("samples/voix.wav"));

    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);      // 120 BPM : une noire = 0,5 s
    Track voix;
    voix.kind = Track::Kind::Audio;
    voix.name = "Voix";
    voix.audio = {"samples/voix.wav", kSr, static_cast<int64_t>(gauche.size()), 2};
    project.tracks.push_back(voix);

    LoadedBundle bundle;
    bundle.project = project;
    bundle.document = documentFromProject(project);
    bundle.folderPath = dossier.str();

    RenderOptions options;
    options.sampleRate = kSr;
    options.durationSeconds = 2.0;
    const RenderResult result = renderBundleToWav(bundle, dossier.file("rendu.wav"), options);
    VSM_ASSERT(result.success);
    // La piste compte comme sonorisée : sans cela, un rendu muet passerait pour
    // un projet sans instrument plutôt que pour une piste audio non chargée.
    VSM_ASSERT_EQ(result.tracksWithInstrument, size_t(1));
    VSM_ASSERT(result.peakLevel > 0.1f);
}

VSM_TEST(a_missing_audio_file_is_named_not_silently_ignored) {
    TempFolder dossier("projet_audio_absent");
    Project project;
    project.ticksPerQuarterNote = 480;
    Track voix;
    voix.kind = Track::Kind::Audio;
    voix.name = "Voix";
    voix.audio = {"samples/introuvable.wav", 48000.0, 48000, 2};
    project.tracks.push_back(voix);

    LoadedBundle bundle;
    bundle.project = project;
    bundle.document = documentFromProject(project);
    bundle.folderPath = dossier.str();

    RenderOptions options;
    options.durationSeconds = 0.5;
    const RenderResult result = renderBundleToWav(bundle, dossier.file("rendu.wav"), options);
    // Le rendu ABOUTIT -- un projet incomplet doit s'ouvrir et se dire
    // incomplet -- mais il NOMME ce qui manque.
    VSM_ASSERT(result.success);
    VSM_ASSERT(!result.warnings.empty());
    bool nomme = false;
    for (const auto& avertissement : result.warnings)
        if (avertissement.find("introuvable.wav") != std::string::npos) nomme = true;
    VSM_ASSERT(nomme);
}
