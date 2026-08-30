#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/engine/SampleStore.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/sequencer/Project.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <vector>

// INVARIANT N° 2 DE `ROADMAP-daw.md` § 6, ENFIN MESURÉ.
//
// « `process()` reste sans allocation, sans verrou, sans I/O — y compris quand
// une piste audio lit 47 Mo depuis le disque. » D2.2 en faisait déjà un critère
// (« un test compte les allocations ») et ce test n'existait pas : la règle
// était tenue par la relecture, c'est-à-dire par l'attention de celui qui
// écrivait. C'est exactement le genre de garantie qu'on perd sans s'en
// apercevoir, et D8.2 vient de refaire tout ce chemin-là.
//
// COMMENT ON COMPTE. `operator new` global est une fonction REMPLAÇABLE : la
// définir ici la substitue pour tout le binaire de tests. Elle ne fait
// qu'incrémenter un compteur et déléguer à `malloc`, donc le reste de la suite
// ne s'en aperçoit pas.
//
// LE COMPTEUR EST PROPRE À CHAQUE THREAD, et ce n'est pas un détail : le thread
// de diffusion disque, lui, a parfaitement le droit d'allouer — c'est même
// pour cela qu'il existe. Un compteur global le prendrait pour une faute du
// thread audio et ferait échouer le test au hasard, selon le moment où le
// disque a répondu.

namespace {
thread_local int g_allocations = 0;
thread_local bool g_counting = false;

/// Compte, puis rend la main. Rien d'autre : un `operator new` qui ferait quoi
/// que ce soit d'autre changerait ce qu'il mesure.
struct Compteur {
    Compteur() { g_allocations = 0; g_counting = true; }
    ~Compteur() { g_counting = false; }
    int total() const { return g_allocations; }
};
} // namespace

void* operator new(std::size_t taille) {
    if (g_counting) ++g_allocations;
    void* p = std::malloc(taille ? taille : 1);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t taille) { return ::operator new(taille); }
void* operator new(std::size_t taille, std::align_val_t alignement) {
    if (g_counting) ++g_allocations;
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(alignement), taille ? taille : 1) != 0)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t taille, std::align_val_t a) { return ::operator new(taille, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using namespace vsm::audio::engine;
using namespace vsm::sequencer;
namespace fs = std::filesystem;

namespace {

Project projetAvecNotes(size_t pistes) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (size_t t = 0; t < pistes; ++t) {
        Track piste;
        piste.channel = static_cast<uint8_t>(t % 16);
        for (int n = 0; n < 4; ++n)
            piste.addNote(0, 480 * 64, static_cast<uint8_t>(48 + 4 * n), 100, 0, ids);
        projet.tracks.push_back(piste);
    }
    return projet;
}

/// Un WAV assez long pour que le chargeur choisisse la DIFFUSION plutôt que la
/// résidence (seuil : vingt secondes).
std::string ecrireLongFichier(const std::string& nom, double secondes) {
    const auto chemin = fs::temp_directory_path() / nom;
    const auto trames = static_cast<size_t>(48000.0 * secondes);
    std::vector<float> gauche(trames), droite(trames);
    for (size_t i = 0; i < trames; ++i) {
        gauche[i] = 0.2f * static_cast<float>((i % 480) / 480.0);
        droite[i] = -gauche[i];
    }
    vsm::audio::io::WavFileWriter::writeFile(gauche.data(), droite.data(), trames, 48000.0,
                                              vsm::audio::io::SampleFormat::Float32,
                                              chemin.string());
    return chemin.string();
}

} // namespace

VSM_TEST(the_counter_itself_counts_something) {
    // GARDE-FOU DU GARDE-FOU, ET IL A SERVI DÈS LE PREMIER LANCEMENT. Écrit
    // d'abord avec un `std::vector` local, il a échoué : le compilateur
    // optimisant élimine une allocation dont il voit qu'elle ne sert à rien
    // (l'élision d'allocation est expressément permise depuis C++14). Les trois
    // tests suivants passaient alors pour la pire des raisons -- ils
    // mesuraient un compteur qui ne comptait rien.
    //
    // La taille est donc `volatile` : le compilateur ne peut plus prouver quoi
    // que ce soit sur cette allocation, donc plus l'écarter.
    static volatile std::size_t taille = 1024;
    Compteur compteur;
    float* tampon = new float[taille];
    tampon[0] = 1.0f;
    volatile float lu = tampon[0];
    (void)lu;
    delete[] tampon;
    VSM_ASSERT(compteur.total() > 0);
}

VSM_TEST(process_block_allocates_nothing_with_machines) {
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    for (size_t t = 0; t < 8; ++t) graphe.setTrackInstrument(t, "vsm.minimoog");
    graphe.setProject(projetAvecNotes(8));
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);

    std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
    // RODAGE HORS COMPTAGE : la première fois, les machines montent leurs
    // tables et leurs voix, et elles ont le droit -- c'est `initialize` qui
    // aurait dû le faire, et le premier bloc qui achève de le faire. Ce qu'on
    // interdit, c'est le régime permanent.
    for (int i = 0; i < 20; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);

    Compteur compteur;
    for (int i = 0; i < 200; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    VSM_ASSERT_EQ(compteur.total(), 0);
}

VSM_TEST(process_block_allocates_nothing_with_a_resident_audio_track) {
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);

    auto source = std::make_shared<AudioTrackSource>();
    source->setMemorySamples(std::vector<float>(48000 * 4, 0.1f),
                              std::vector<float>(48000 * 4, -0.1f));
    AudioClipSpan span;
    span.lengthFrames = 48000 * 4;
    source->clips.push_back(span);

    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste;
    piste.kind = Track::Kind::Audio;
    projet.tracks.push_back(piste);
    graphe.setProject(projet);
    graphe.setTrackAudio(0, source);
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);

    std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
    for (int i = 0; i < 10; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);

    Compteur compteur;
    for (int i = 0; i < 200; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    VSM_ASSERT_EQ(compteur.total(), 0);
}

VSM_TEST(process_block_allocates_nothing_while_streaming_from_disk) {
    // LE CAS QUE L'INVARIANT NOMME EXPLICITEMENT, et le seul que la relecture
    // seule ne suffisait plus à garantir depuis D8.2 : la piste ne tient plus
    // en mémoire, ses échantillons arrivent du disque, et `process()` ne doit
    // toujours ni allouer ni lire un fichier.
    const std::string chemin = ecrireLongFichier("vsm-sans-alloc.wav", 25.0);
    auto charge = vsm::audio::io::loadAudioTrack(chemin, 48000.0);
    VSM_ASSERT(charge.success);
    // Vingt-cinq secondes : au-dessus du seuil, donc DIFFUSÉE. Si ce n'était
    // pas le cas, ce test mesurerait le chemin résident une seconde fois.
    VSM_ASSERT(charge.streamed);

    AudioClipSpan span;
    span.lengthFrames = charge.source->frames();
    charge.source->clips.push_back(span);

    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste;
    piste.kind = Track::Kind::Audio;
    projet.tracks.push_back(piste);
    graphe.setProject(projet);
    graphe.setTrackAudio(0, charge.source);
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);

    std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
    // ON LAISSE LE THREAD DE DIFFUSION PRENDRE DE L'AVANCE, puis on reste dans
    // ce qu'il a livré.
    //
    // POURQUOI LA MESURE EST COURTE, ET C'EST UNE PROPRIÉTÉ DU TEST ET NON UNE
    // FAIBLESSE DU MOTEUR : ici, deux cents blocs se rendent en une
    // milliseconde, alors qu'ils durent deux secondes à l'écoute. Le thread de
    // diffusion, qui se réveille toutes les dix millisecondes, n'a aucune
    // chance de suivre -- et il a raison de ne pas la suivre, puisque personne
    // ne joue mille fois plus vite que le temps réel. On mesure donc quarante
    // blocs, qui tiennent dans la fenêtre déjà lue : c'est le régime permanent
    // de la lecture, et c'est celui que l'invariant décrit.
    for (int i = 0; i < 20; ++i) {
        DiskStreamer::instance().pump();
        graphe.processBlock(gauche.data(), droite.data(), 512);
    }

    Compteur compteur;
    for (int i = 0; i < 40; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    // ZÉRO, et le thread de diffusion peut allouer autant qu'il veut pendant ce
    // temps : le compteur est propre au thread qui rend.
    VSM_ASSERT_EQ(compteur.total(), 0);

    // Et il a bien joué quelque chose : un silence n'aurait rien prouvé.
    float crete = 0.0f;
    for (float s : gauche) crete = std::max(crete, std::abs(s));
    VSM_ASSERT(crete > 0.0f);
    fs::remove(chemin);
}

VSM_TEST(process_block_allocates_nothing_when_looping_and_automated) {
    // LES DEUX CHEMINS QUI DÉCOUPENT LE BLOC : le rebouclage échantillon-exact
    // et les sous-segments d'automation. Ce sont ceux où l'on serait le plus
    // tenté d'allouer un tampon « juste pour ce morceau-là ».
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    for (size_t t = 0; t < 4; ++t) graphe.setTrackInstrument(t, "vsm.minimoog");
    graphe.setProject(projetAvecNotes(4));

    AutomationLane courbe;
    courbe.target = AutomationTarget::TrackVolume;
    courbe.targetTrackIndex = 0;
    courbe.addPoint(0, 0.2f);
    courbe.addPoint(480 * 8, 1.0f);
    graphe.setAutomationLanes({courbe});
    graphe.setLoopRegion(0.0, 0.37, true);   // frontière au milieu d'un bloc
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);

    std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
    for (int i = 0; i < 20; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);

    Compteur compteur;
    for (int i = 0; i < 200; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    VSM_ASSERT_EQ(compteur.total(), 0);
    // Le rebouclage a bien eu lieu : sans cela, on aurait mesuré le chemin
    // droit une troisième fois.
    VSM_ASSERT(graphe.loopWrapCount() > 0);
}
