#include "TestFramework.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/engine/SampleStore.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/io/WaveformPeaks.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace vsm::audio::engine;
using namespace vsm::audio::io;

// D8.2 de docs/ROADMAP-daw.md — LA DIFFUSION DEPUIS LE DISQUE.
//
// LE CRITÈRE EST UN CHIFFRE : « 20 pistes de 9 minutes n'occupent pas 1 Go ».
// Écrire vingt fichiers de neuf minutes pour le vérifier coûterait deux
// gigaoctets de disque et une minute à chaque exécution de la suite, pour
// mesurer une propriété qui se démontre exactement : la mémoire occupée par un
// matériau diffusé NE DÉPEND PAS de la durée du fichier. Ces tests vérifient
// donc cette propriété-là, sur des fichiers courts, puis font l'arithmétique
// que le critère demande. C'est plus fort qu'un essai à vingt pistes, qui ne
// dirait rien de la vingt-et-unième.

namespace {

/// UN FICHIER QUI DIT OÙ ON EST, ET LES DEUX CANAUX SONT NÉCESSAIRES.
///
/// Le canal gauche donne la position DANS la fenêtre de cache, le droit le
/// NUMÉRO de la fenêtre. Encoder la position dans un seul canal ne suffirait
/// pas : une fenêtre servie à la place d'une autre porte les mêmes décalages
/// internes, donc les mêmes valeurs, et le test passerait sur l'erreur exacte
/// qu'il est censé attraper.
///
/// Tout tient dans [-0,4 ; +0,4] : un WAV ne porte pas d'échantillon au-delà de
/// 1,0, et un motif qui déborde se ferait écrêter à l'écriture -- on
/// comparerait alors des plateaux.
float attenduGauche(int64_t trame) {
    const int64_t dansLaFenetre = trame & (StreamedSampleStore::kWindowFrames - 1);
    return static_cast<float>(dansLaFenetre)
               / static_cast<float>(StreamedSampleStore::kWindowFrames) * 0.8f - 0.4f;
}

float attenduDroite(int64_t trame) {
    const int64_t fenetre = (trame >> StreamedSampleStore::kWindowShift) % 64;
    return static_cast<float>(fenetre) / 64.0f * 0.8f - 0.4f;
}

std::string ecrireFichierReperable(const std::string& nom, int64_t trames, double frequence) {
    const auto chemin = std::filesystem::temp_directory_path() / nom;
    std::vector<float> gauche(static_cast<size_t>(trames)), droite(static_cast<size_t>(trames));
    for (int64_t i = 0; i < trames; ++i) {
        gauche[static_cast<size_t>(i)] = attenduGauche(i);
        droite[static_cast<size_t>(i)] = attenduDroite(i);
    }
    WavFileWriter::writeFile(gauche.data(), droite.data(), static_cast<size_t>(trames),
                              frequence, SampleFormat::Float32, chemin.string());
    return chemin.string();
}

std::shared_ptr<StreamedSampleStore> ouvrir(const std::string& chemin, double frequence,
                                             StreamedSampleStore::Mode mode) {
    std::string erreur;
    auto store = StreamedSampleStore::open(chemin, frequence, mode, erreur);
    if (!store) VSM_ASSERT(erreur.empty()); // fera échouer le test EN DISANT pourquoi
    return store;
}

} // namespace

VSM_TEST(streamed_store_serves_the_same_samples_as_the_file) {
    const std::string chemin = ecrireFichierReperable("vsm-d82-lecture.wav", 200000, 48000.0);
    auto store = ouvrir(chemin, 48000.0, StreamedSampleStore::Mode::Blocking);
    VSM_ASSERT_EQ(store->frames(), int64_t{200000});

    // DES POSITIONS ÉPARPILLÉES, ET DANS LE DÉSORDRE : c'est là qu'un cache
    // qui se croit linéaire sert la fenêtre précédente sans le dire.
    const int64_t positions[] = {0, 199999, 32768, 32767, 100000, 5, 165000, 65536, 1};
    for (int64_t p : positions) {
        store->requestRange(p, 1);
        float g = 0.0f, d = 0.0f;
        const SampleStore::ReadGuard garde(store.get());
        VSM_ASSERT(store->frameAt(p, g, d));
        VSM_ASSERT_NEAR(g, attenduGauche(p), 1e-6f);
        VSM_ASSERT_NEAR(d, attenduDroite(p), 1e-6f);
    }
    // Hors du fichier : faux, et non un échantillon inventé.
    float g = 0.0f, d = 0.0f;
    const SampleStore::ReadGuard garde(store.get());
    VSM_ASSERT(!store->frameAt(200000, g, d));
    VSM_ASSERT(!store->frameAt(-1, g, d));
    std::filesystem::remove(chemin);
}

VSM_TEST(streamed_store_memory_does_not_depend_on_file_length) {
    // LE CŒUR DU CRITÈRE DE D8.2, et il tient en une comparaison.
    const std::string court = ecrireFichierReperable("vsm-d82-court.wav", 48000, 48000.0);
    const std::string long_ = ecrireFichierReperable("vsm-d82-long.wav", 48000 * 40, 48000.0);

    auto petit = ouvrir(court, 48000.0, StreamedSampleStore::Mode::Blocking);
    auto grand = ouvrir(long_, 48000.0, StreamedSampleStore::Mode::Blocking);
    VSM_ASSERT_EQ(petit->residentBytes(), grand->residentBytes());

    // Et l'arithmétique que le critère demande : vingt pistes de neuf minutes.
    // Le chiffre ne dépend pas des neuf minutes -- c'est justement le point --
    // donc vingt fois la même chose suffit à le conclure.
    const size_t vingtPistes = 20 * grand->residentBytes();
    VSM_ASSERT(vingtPistes < 1024ull * 1024ull * 1024ull);
    // Et de très loin : on veut savoir qu'on n'est pas à 999 Mo.
    VSM_ASSERT(vingtPistes < 128ull * 1024ull * 1024ull);

    std::filesystem::remove(court);
    std::filesystem::remove(long_);
}

VSM_TEST(streamed_and_resident_material_render_the_same_thing) {
    // Deux façons de tenir le même fichier, un seul son. Sans cette égalité,
    // la diffusion serait une seconde façon de sonner, et un projet changerait
    // de son selon la durée de ses prises.
    const std::string chemin = ecrireFichierReperable("vsm-d82-compare.wav", 150000, 48000.0);

    auto residentCharge = loadAudioTrack(chemin, 48000.0, AudioLoadPolicy::ForceResident);
    VSM_ASSERT(residentCharge.success);
    VSM_ASSERT(!residentCharge.streamed);

    std::string erreur;
    auto diffuse = std::make_shared<AudioTrackSource>();
    diffuse->samples = StreamedSampleStore::open(chemin, 48000.0,
                                                  StreamedSampleStore::Mode::Blocking, erreur);
    VSM_ASSERT(diffuse->samples != nullptr);

    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = 150000;
    span.sourceStartFrame = 0;
    residentCharge.source->clips.push_back(span);
    diffuse->clips.push_back(span);

    // Sur plusieurs blocs, dont un qui commence au milieu d'une fenêtre de
    // cache : c'est là que les deux chemins divergeraient s'ils devaient.
    for (int64_t depart : {int64_t{0}, int64_t{32700}, int64_t{65536}, int64_t{99999}}) {
        std::vector<float> ag(512, 0.0f), ad(512, 0.0f), bg(512, 0.0f), bd(512, 0.0f);
        residentCharge.source->mixInto(ag.data(), ad.data(), depart, 512);
        diffuse->mixInto(bg.data(), bd.data(), depart, 512);
        for (size_t i = 0; i < 512; ++i) {
            VSM_ASSERT_NEAR(ag[i], bg[i], 1e-6f);
            VSM_ASSERT_NEAR(ad[i], bd[i], 1e-6f);
        }
    }
    std::filesystem::remove(chemin);
}

VSM_TEST(the_loader_streams_what_is_long_and_keeps_what_is_short) {
    // LE SEUIL EST UNE DÉCISION, PAS UN HASARD : ce qui est court est lu cent
    // fois et doit répondre à l'échantillon près ; ce qui est long est lu une
    // fois d'un bout à l'autre. Ce test fixe la frontière pour qu'on ne la
    // déplace pas par inadvertance.
    const std::string court = ecrireFichierReperable(
        "vsm-d82-seuil-court.wav", static_cast<int64_t>(48000.0 * 5.0), 48000.0);
    const std::string long_ = ecrireFichierReperable(
        "vsm-d82-seuil-long.wav", static_cast<int64_t>(48000.0 * (kStreamAboveSeconds + 5.0)),
        48000.0);

    const auto petit = loadAudioTrack(court, 48000.0);
    VSM_ASSERT(petit.success);
    VSM_ASSERT(!petit.streamed);

    const auto grand = loadAudioTrack(long_, 48000.0);
    VSM_ASSERT(grand.success);
    VSM_ASSERT(grand.streamed);
    // Et le long occupe MOINS que le court une fois chargé, alors qu'il est
    // cinq fois plus long : c'est toute la phase, en une assertion.
    VSM_ASSERT(grand.residentBytes < static_cast<size_t>(48000.0 * (kStreamAboveSeconds + 5.0)) * 8);

    std::filesystem::remove(court);
    std::filesystem::remove(long_);
}

VSM_TEST(streamed_store_resamples_like_the_resident_path) {
    // Un fichier à 44,1 kHz dans une session à 48 kHz : le cas le plus courant
    // qui soit. Les deux chemins passent par le MÊME noyau fenêtré (D12.1),
    // et doivent donc tomber sur les mêmes valeurs -- sinon la même prise
    // sonnerait autrement selon sa durée, ce qui serait absurde.
    const std::string chemin = ecrireFichierReperable("vsm-d82-4410.wav", 120000, 44100.0);

    auto resident = loadAudioTrack(chemin, 48000.0, AudioLoadPolicy::ForceResident);
    VSM_ASSERT(resident.success);
    VSM_ASSERT(resident.resampled);

    std::string erreur;
    auto diffuse = StreamedSampleStore::open(chemin, 48000.0,
                                              StreamedSampleStore::Mode::Blocking, erreur);
    VSM_ASSERT(diffuse != nullptr);
    VSM_ASSERT(diffuse->resampled());

    // Les longueurs se rejoignent à une trame près (arrondi de la conversion).
    VSM_ASSERT(std::abs(diffuse->frames() - resident.source->frames()) <= 1);

    const SampleStore::ReadGuard garde(diffuse.get());
    for (int64_t position : {int64_t{0}, int64_t{1000}, int64_t{40000}, int64_t{80000}}) {
        diffuse->requestRange(position, 1);
        float g1 = 0.0f, d1 = 0.0f, g2 = 0.0f, d2 = 0.0f;
        VSM_ASSERT(resident.source->samples->frameAt(position, g1, d1));
        VSM_ASSERT(diffuse->frameAt(position, g2, d2));
        VSM_ASSERT_NEAR(g1, g2, 1e-5f);
        VSM_ASSERT_NEAR(d1, d2, 1e-5f);
    }
    std::filesystem::remove(chemin);
}

VSM_TEST(waveform_peaks_read_from_the_file_match_the_resident_ones) {
    // L'APERÇU NE DOIT PAS RECHARGER CE QUE LA LECTURE A RENONCÉ À CHARGER :
    // sinon la diffusion n'aurait rien économisé, elle aurait juste déplacé la
    // dépense. Les deux calculs doivent donner le même dessin.
    const std::string chemin = ecrireFichierReperable("vsm-d82-apercu.wav", 130000, 48000.0);

    auto resident = loadAudioTrack(chemin, 48000.0, AudioLoadPolicy::ForceResident);
    VSM_ASSERT(resident.success);
    const auto* memoire =
        dynamic_cast<const MemorySampleStore*>(resident.source->samples.get());
    VSM_ASSERT(memoire != nullptr);
    const auto attendu = computePeaks(memoire->leftChannel().data(),
                                       memoire->rightChannel().data(), resident.source->frames());

    auto lecteur = WavStreamReader::open(chemin);
    VSM_ASSERT(lecteur.reader != nullptr);
    const auto obtenu = computePeaksFromFile(*lecteur.reader, 48000.0);

    VSM_ASSERT_EQ(attendu.size(), obtenu.size());
    for (size_t i = 0; i < attendu.size(); ++i) {
        VSM_ASSERT_NEAR(attendu[i].minimum, obtenu[i].minimum, 1e-5f);
        VSM_ASSERT_NEAR(attendu[i].maximum, obtenu[i].maximum, 1e-5f);
    }
    std::filesystem::remove(chemin);
}

VSM_TEST(the_streaming_thread_never_tears_a_window_under_a_reader) {
    // LE SEUL VRAI DANGER DE CETTE PHASE, et il est silencieux : le thread de
    // diffusion réécrit une fenêtre pendant que le thread audio la lit, et le
    // son contient une demi-seconde d'un autre endroit du morceau. Ça ne
    // plante pas, ça ne se voit pas dans un test qui lit tranquillement -- ça
    // s'entend une fois par heure.
    //
    // On force donc la situation : un lecteur qui saute sans arrêt d'un bout à
    // l'autre du fichier (donc qui fait recycler les fenêtres en permanence) et
    // qui vérifie CHAQUE échantillon servi. Une fenêtre déchirée donnerait une
    // valeur qui n'est pas celle de sa position.
    const std::string chemin = ecrireFichierReperable("vsm-d82-course.wav", 400000, 48000.0);
    auto store = ouvrir(chemin, 48000.0, StreamedSampleStore::Mode::Realtime);

    std::atomic<bool> fini{false};
    std::atomic<int> faux{0};
    std::atomic<int64_t> servis{0};

    std::thread lecteur([&] {
        int64_t position = 0;
        while (!fini.load(std::memory_order_acquire)) {
            // Un saut à chaque tour : c'est ce qui fait travailler le recyclage.
            position = (position + 33331) % 350000;
            store->requestRange(position, 256);
            const SampleStore::ReadGuard garde(store.get());
            for (int64_t i = 0; i < 256; ++i) {
                float g = 0.0f, d = 0.0f;
                if (!store->frameAt(position + i, g, d)) continue;   // pas encore lu : normal
                if (std::abs(g - attenduGauche(position + i)) > 1e-5f
                    || std::abs(d - attenduDroite(position + i)) > 1e-5f)
                    faux.fetch_add(1);
                servis.fetch_add(1);
            }
        }
    });

    // Le thread de diffusion tourne en fond ; on le pousse aussi à la main pour
    // que le test ne dépende pas de son rythme de réveil.
    const auto debut = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - debut < std::chrono::milliseconds(400))
        DiskStreamer::instance().pump();
    fini.store(true, std::memory_order_release);
    lecteur.join();

    VSM_ASSERT_EQ(faux.load(), 0);
    // Et le cache a bel et bien servi quelque chose : zéro échantillon servi
    // ferait passer le test sans rien prouver.
    VSM_ASSERT(servis.load() > 10000);
    std::filesystem::remove(chemin);
}

VSM_TEST(what_the_disk_has_not_delivered_is_counted_not_hidden) {
    // LA RÈGLE DE LA MAISON : ce qui n'a pas pu être joué se compte. Un trou de
    // diffusion ne se distingue pas, à l'oreille, d'un passage silencieux ; le
    // compteur est la seule chose qui permette de dire lequel des deux on
    // vient d'entendre.
    const std::string chemin = ecrireFichierReperable("vsm-d82-compte.wav", 300000, 48000.0);
    auto store = ouvrir(chemin, 48000.0, StreamedSampleStore::Mode::Realtime);
    VSM_ASSERT_EQ(store->cacheMisses(), uint64_t{0});

    // Rien n'a été demandé, donc rien n'a été lu : la lecture se tait, et le
    // dit.
    {
        const SampleStore::ReadGuard garde(store.get());
        float g = 0.0f, d = 0.0f;
        for (int i = 0; i < 100; ++i) VSM_ASSERT(!store->frameAt(1000 + i, g, d));
    }
    VSM_ASSERT_EQ(store->cacheMisses(), uint64_t{100});

    // Une fois la fenêtre demandée ET livrée, plus aucun trou.
    store->requestRange(1000, 100);
    DiskStreamer::instance().pump();
    const uint64_t avant = store->cacheMisses();
    {
        const SampleStore::ReadGuard garde(store.get());
        float g = 0.0f, d = 0.0f;
        for (int i = 0; i < 100; ++i) VSM_ASSERT(store->frameAt(1000 + i, g, d));
    }
    VSM_ASSERT_EQ(store->cacheMisses(), avant);

    // ET LA POSITION HORS FICHIER N'EST PAS UN TROU DE DIFFUSION : il n'y a
    // rien à livrer là, jamais. La confondre avec un retard du disque ferait
    // sonner l'alarme sur chaque clip qui dépasse la fin de sa prise.
    {
        const SampleStore::ReadGuard garde(store.get());
        float g = 0.0f, d = 0.0f;
        VSM_ASSERT(!store->frameAt(999999999, g, d));
    }
    VSM_ASSERT_EQ(store->cacheMisses(), avant);
    std::filesystem::remove(chemin);
}
