#include "vsm/audio/engine/RenderThreadPool.h"
#include <algorithm>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#include <sched.h>
#endif

namespace vsm::audio::engine {

namespace {

/// Fait ce qu'on peut pour que le thread de rendu ne soit pas préempté par
/// n'importe quoi. Un travailleur qui dort pendant que le thread audio
/// l'attend, c'est un bloc en retard -- et un clic.
///
/// « CE QU'ON PEUT » EST LITTÉRAL : l'ordonnancement temps réel demande des
/// droits que l'application n'a pas toujours (sous Linux, `RLIMIT_RTPRIO` ou
/// `CAP_SYS_NICE`). L'échec est donc NORMAL et silencieux : le banc reste
/// parfaitement fonctionnel avec des threads ordinaires, simplement plus
/// exposé aux caprices de l'ordonnanceur. Ce n'est pas une condition d'erreur,
/// c'est une qualité de service qu'on demande sans l'exiger.
void tryRaisePriority() {
#if defined(__linux__) || defined(__APPLE__)
    sched_param param{};
    const int policy = SCHED_FIFO;
    const int minPrio = sched_get_priority_min(policy);
    const int maxPrio = sched_get_priority_max(policy);
    if (minPrio < 0 || maxPrio < minPrio) return;
    // Un cran SOUS le sommet : le thread audio du pilote, lui, doit rester
    // au-dessus de nous. Un travailleur plus prioritaire que le rappel audio
    // retarderait ce qu'il est censé servir.
    param.sched_priority = std::max(minPrio, maxPrio - 2);
    (void)pthread_setschedparam(pthread_self(), policy, &param);
#endif
}

} // namespace

RenderThreadPool::~RenderThreadPool() { resize(0); }

size_t RenderThreadPool::recommendedWorkerCount() {
    const unsigned int cores = std::thread::hardware_concurrency();
    // Deux cœurs, c'est un thread auxiliaire : l'appelant en occupe déjà un, et
    // il ne reste rien à donner. En dessous, le banc ne peut que coûter.
    if (cores < 3) return 0;
    return std::min<size_t>(kRecommendedCeiling, static_cast<size_t>(cores) - 2);
}

void RenderThreadPool::resize(size_t workerCount) {
    workerCount = std::min(workerCount, kMaxWorkers);
    if (workerCount == workers_.size()) return;

    if (!workers_.empty()) {
        quitting_.store(true, std::memory_order_release);
        start_.release(static_cast<std::ptrdiff_t>(workers_.size()));
        for (auto& t : workers_)
            if (t.joinable()) t.join();
        workers_.clear();
        quitting_.store(false, std::memory_order_release);
    }

    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i)
        workers_.emplace_back([this, i] { workerLoop(i + 1); });
}

void RenderThreadPool::workerLoop(size_t workerId) {
    tryRaisePriority();
    for (;;) {
        start_.acquire();
        if (quitting_.load(std::memory_order_acquire)) return;

        // TOUT LE MONDE PREND LA TÂCHE SUIVANTE DISPONIBLE, plutôt qu'une part
        // fixe attribuée d'avance. Les pistes n'ont pas le même coût -- un DX7
        // à huit voix contre une piste audio -- et un découpage statique ferait
        // attendre tout le monde sur la plus lourde.
        for (;;) {
            const size_t index = nextIndex_.fetch_add(1, std::memory_order_relaxed);
            if (index >= count_) break;
            job_(context_, index, workerId);
        }
        active_.fetch_sub(1, std::memory_order_release);
    }
}

void RenderThreadPool::runParallel(JobFn job, void* context, size_t count) {
    if (count == 0 || job == nullptr) return;

    // AUCUN THREAD AUXILIAIRE : la boucle telle qu'elle a toujours été. Pas de
    // sémaphore, pas d'atomique, pas de réveil -- rien à payer pour une
    // parallélisation qu'on n'a pas demandée.
    if (workers_.empty()) {
        for (size_t i = 0; i < count; ++i) job(context, i, 0);
        return;
    }

    job_ = job;
    context_ = context;
    count_ = count;
    nextIndex_.store(0, std::memory_order_relaxed);
    active_.store(workers_.size(), std::memory_order_release);

    start_.release(static_cast<std::ptrdiff_t>(workers_.size()));

    // L'appelant est le travailleur 0 et prend sa part comme les autres.
    for (;;) {
        const size_t index = nextIndex_.fetch_add(1, std::memory_order_relaxed);
        if (index >= count) break;
        job(context, index, 0);
    }

    // ATTENTE ACTIVE, et c'est le bon choix ICI et nulle part ailleurs : à ce
    // point toutes les tâches sont distribuées, il ne reste qu'à laisser
    // finir les dernières. Se rendormir sur une condition coûterait deux
    // changements de contexte pour économiser quelques microsecondes d'attente.
    // Le `yield` après quelques tours protège du cas pathologique -- un
    // travailleur préempté juste avant de finir -- où tourner à vide
    // empêcherait justement l'ordonnanceur de le remettre en marche.
    int tours = 0;
    while (active_.load(std::memory_order_acquire) != 0) {
        if (++tours < 512) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        } else {
            std::this_thread::yield();
        }
    }
}

} // namespace vsm::audio::engine
