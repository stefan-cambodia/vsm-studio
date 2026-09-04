#include "TestFramework.h"
#include <cstdio>
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <cmath>
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/SampleStore.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/sequencer/Project.h"
#include <atomic>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <pthread.h>
#include <unistd.h>
#include <filesystem>
#include <new>
#include <vector>

// INVARIANT N° 2 DE `ROADMAP-daw.md` § 6 — MESURÉ, ET VOICI EXACTEMENT QUOI.
//
// « `process()` reste sans allocation, sans verrou, sans I/O — y compris quand
// une piste audio lit 47 Mo depuis le disque. »
//
// CE FICHIER N'EN MESURAIT QU'UN TIERS, ET IL CITAIT POURTANT LA PHRASE
// ENTIÈRE : seules les allocations étaient comptées, les verrous et les
// entrées-sorties étaient tenus par la relecture — le reproche même que ce
// fichier adressait à D2.2 en naissant. Un verrou pris dans `process()` est
// la cause classique du décrochage qu'on ne reproduit jamais : il ne coûte
// rien mille fois, puis il attend le thread qui le détient.
//
// LE CONTRAT DE CE QUE LES COMPTEURS VOIENT, ÉCRIT POUR NE PAS ÊTRE SURVENDU.
// Sont interposés : `operator new` (toutes formes), `pthread_mutex_lock` —
// donc `std::mutex`, ce que le moteur emploie — et `read`/`write`. NE SONT PAS
// VUS : `pthread_rwlock_*` (`std::shared_mutex`), `sem_wait`, les attentes de
// variables de condition, `open`, `pread`/`pwrite`, `mmap`, et les lectures
// `FILE*` dont glibc appelle `__read` en interne sans passer par la PLT. Le
// moteur d'aujourd'hui n'utilise que les primitives couvertes ; si le chemin
// de rendu adopte l'une des autres, ce test restera VERT À TORT — élargir
// l'interposition ICI, en même temps.
//
// COMMENT ON COMPTE LES DEUX AUTRES. Même technique que pour `operator new`,
// par interposition de symbole : une définition forte dans le binaire de tests
// l'emporte sur celle de la bibliothèque C, et l'implémentation vraie se
// retrouve par `dlsym(RTLD_NEXT, ...)`. La résolution est forcée AVANT que le
// comptage ne commence, sans quoi `dlsym` -- qui alloue et verrouille au
// premier appel -- se compterait lui-même. D2.2 en faisait déjà un critère
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
thread_local int g_verrous = 0;
thread_local int g_es = 0;
thread_local bool g_counting = false;
} // namespace

// --- Interposition des verrous et des entrées-sorties ----------------------
// `extern "C"` et hors namespace : ce sont les symboles de la bibliothèque C
// qu'on remplace, pas des homonymes.
extern "C" {
using FnVerrou = int (*)(pthread_mutex_t*);
using FnLire  = ssize_t (*)(int, void*, size_t);
using FnEcrire = ssize_t (*)(int, const void*, size_t);

static FnVerrou vraiLock() {
    static FnVerrou f = reinterpret_cast<FnVerrou>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
    return f;
}
static FnLire vraiRead() {
    static FnLire f = reinterpret_cast<FnLire>(dlsym(RTLD_NEXT, "read"));
    return f;
}
static FnEcrire vraiWrite() {
    static FnEcrire f = reinterpret_cast<FnEcrire>(dlsym(RTLD_NEXT, "write"));
    return f;
}

// `pthread_mutex_trylock` N'EST PAS INTERPOSÉ, ET C'EST UNE DÉCISION :
// `try_lock` ne bloque pas. Un chemin temps réel a le droit d'essayer un
// verrou et de renoncer ; ce que l'invariant interdit, c'est d'attendre.
int pthread_mutex_lock(pthread_mutex_t* m) {
    if (g_counting) ++g_verrous;
    return vraiLock()(m);
}
ssize_t read(int fd, void* buf, size_t n) {
    if (g_counting) ++g_es;
    return vraiRead()(fd, buf, n);
}
ssize_t write(int fd, const void* buf, size_t n) {
    if (g_counting) ++g_es;
    return vraiWrite()(fd, buf, n);
}
} // extern "C"

namespace {

/// Compte, puis rend la main. Rien d'autre : un `operator new` qui ferait quoi
/// que ce soit d'autre changerait ce qu'il mesure.
struct Compteur {
    Compteur() {
        // Résolution FORCÉE avant le comptage : `dlsym` alloue et verrouille à
        // son premier appel, et se compterait lui-même.
        (void) vraiLock(); (void) vraiRead(); (void) vraiWrite();
        g_allocations = 0; g_verrous = 0; g_es = 0; g_counting = true;
    }
    ~Compteur() { g_counting = false; }
    int total() const { return g_allocations; }
    int verrous() const { return g_verrous; }
    int entreesSorties() const { return g_es; }
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

/// Le verdict entier de l'invariant, en un endroit : si une quatrième clause
/// s'ajoute un jour, elle s'ajoute ici et vaut pour les quatre scénarios.
#define VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur)      \
    do {                                                    \
        VSM_ASSERT_EQ((compteur).total(), 0);               \
        VSM_ASSERT_EQ((compteur).verrous(), 0);             \
        VSM_ASSERT_EQ((compteur).entreesSorties(), 0);      \
    } while (0)

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

VSM_TEST(les_compteurs_de_verrou_et_d_es_comptent_vraiment_quelque_chose) {
    // GARDE-FOU DU GARDE-FOU, deuxième édition. Un compteur qui rend toujours
    // zéro donne exactement le même verdict qu'un chemin temps réel propre :
    // « aucun verrou, aucune I/O » ne veut rien dire tant qu'on n'a pas montré
    // que ces compteurs savent voir un verrou et une lecture quand il y en a.
    std::mutex m;
    const auto chemin = fs::temp_directory_path() / "vsm-garde-fou.bin";
    { std::ofstream f(chemin, std::ios::binary); f.put('x'); }

    int verrous = 0, es = 0;
    {
        Compteur compteur;
        m.lock();
        m.unlock();
        const int fd = ::open(chemin.string().c_str(), O_RDONLY);
        char octet = 0;
        if (fd >= 0) { (void) ::read(fd, &octet, 1); ::close(fd); }
        verrous = compteur.verrous();
        es = compteur.entreesSorties();
    }
    fs::remove(chemin);
    VSM_ASSERT(verrous > 0);   // l'interposition de pthread_mutex_lock fonctionne
    VSM_ASSERT(es > 0);        // celle de read aussi
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
    VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
}

VSM_TEST(process_block_allocates_nothing_for_ANY_machine_of_the_parc) {
    // L'INVARIANT N° 2 VAUT POUR LE PARC ENTIER, ET IL N'ÉTAIT VÉRIFIÉ QUE
    // SUR UNE MACHINE. Le test ci-dessus monte huit pistes de `vsm.minimoog`
    // et ne dit donc rien des trente-huit autres : une machine qui
    // allouerait dans `process()` — un `std::vector` redimensionné à la
    // volée, une chaîne construite pour un nom — passerait la suite entière
    // sans être vue. C'est exactement la forme de garde-fou que le § 6 de
    // `ROADMAP-daw.md` dit qu'on perd sans s'en apercevoir : il gardait un
    // trente-neuvième du parc.
    //
    // Ici, CHAQUE machine enregistrée joue à son tour, seule, et son bloc de
    // régime permanent doit être propre. Le rodage reste hors comptage, pour
    // la raison écrite plus haut.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& [identifiant, nom] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        ProcessGraph graphe;
        graphe.prepare(48000.0, 512);
        graphe.setTrackInstrument(0, identifiant);
        graphe.setProject(projetAvecNotes(1));
        graphe.seekSeconds(0.0);
        graphe.setPlaying(true);

        std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
        for (int i = 0; i < 20; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);

        Compteur compteur;
        for (int i = 0; i < 60; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
        // La machine fautive est NOMMÉE : un échec anonyme sur trente-neuf
        // machines ne dit rien de ce qu'il faut réparer.
        if (compteur.verrous() != 0 || compteur.entreesSorties() != 0)
            std::printf("      [%s] verrous %d, E/S %d\n",
                        identifiant.c_str(), compteur.verrous(), compteur.entreesSorties());
        VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
    }
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
    VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
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
    VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);

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
    VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
    // Le rebouclage a bien eu lieu : sans cela, on aurait mesuré le chemin
    // droit une troisième fois.
    VSM_ASSERT(graphe.loopWrapCount() > 0);
}

// ---------------------------------------------------------------------------
// § 6 DE ROADMAP-daw, REVÉRIFIÉ APRÈS D12, D13 ET D14 : ce qui s'est ajouté
// dans `process()` -- les seize effets d'insert, et les clips qui suivent le
// tempo (vocodeur, WSOLA, rééchantillonné) ou jouent à l'envers -- n'alloue
// pas davantage que le reste. Le garde-fou ne montait ni effet ni clip étiré.
// ---------------------------------------------------------------------------

VSM_TEST(process_block_allocates_nothing_with_every_factory_effect_inserted) {
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.minimoog");
    auto chaine = std::make_shared<ProcessGraph::EffectChain>();
    for (const auto& info : vsm::audio::effect::EffectFactory::available()) {
        auto effet = vsm::audio::effect::EffectFactory::create(info.id);
        VSM_ASSERT(effet != nullptr);
        effet->prepare(48000.0, 512);
        // Un réglage qui fait travailler l'effet, sans quoi certains chemins
        // (pitch shift à zéro, trémolo à zéro) resteraient inertes.
        for (const auto& p : effet->parameterList())
            effet->setParameter(p.id, p.minValue + 0.6f * (p.maxValue - p.minValue));
        chaine->push_back(std::move(effet));
    }
    graphe.setTrackEffectChain(0, chaine);
    graphe.setProject(projetAvecNotes(1));
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);
    std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
    for (int i = 0; i < 20; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    Compteur compteur;
    for (int i = 0; i < 200; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
    VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
}

VSM_TEST(process_block_allocates_nothing_with_warped_and_reversed_audio_clips) {
    for (int variante = 0; variante < 4; ++variante) {
        ProcessGraph graphe;
        graphe.prepare(48000.0, 512);
        auto source = std::make_shared<AudioTrackSource>();
        std::vector<float> l(48000 * 4), r(48000 * 4);
        for (size_t i = 0; i < l.size(); ++i) {
            l[i] = 0.3f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / 48000.0));
            r[i] = l[i];
        }
        source->setMemorySamples(std::move(l), std::move(r));

        Project projet;
        projet.ticksPerQuarterNote = 480;
        Track piste;
        piste.kind = Track::Kind::Audio;
        piste.audio.path = "audio/prise.wav";
        piste.audio.sampleRate = 48000.0;
        piste.audio.frames = 48000 * 4;
        Clip clip;
        clip.id = 1; clip.length = 3840; clip.sourceLength = 3840;   // quatre secondes... étirées sur 4 s de ticks
        clip.warpMode = variante == 0 ? WarpMode::KeepPitch
                      : variante == 1 ? WarpMode::KeepPitchWsola
                      : variante == 2 ? WarpMode::Repitch : WarpMode::Off;
        if (variante < 3) clip.warpMarkers = {{0.0, 0}, {3.0, 3840}};   // 3 s de matériau sur 4 s
        clip.reversed = variante == 3;
        piste.clips.push_back(clip);
        projet.tracks.push_back(piste);
        source->clips = spansFromTrack(piste, 48000.0, [&](int64_t t) { return projet.ticksToSeconds(t); });
        prepareWarpedSpans(*source);   // hors thread audio : ici, et c'est le seul endroit qui alloue
        graphe.setProject(projet);
        graphe.setTrackAudio(0, source);
        graphe.seekSeconds(0.0);
        graphe.setPlaying(true);
        std::vector<float> gauche(512, 0.0f), droite(512, 0.0f);
        for (int i = 0; i < 10; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
        Compteur compteur;
        for (int i = 0; i < 200; ++i) graphe.processBlock(gauche.data(), droite.data(), 512);
        VSM_ASSERT_CHEMIN_TEMPS_REEL_PROPRE(compteur);
    }
}
