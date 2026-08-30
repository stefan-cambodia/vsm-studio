#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <vector>

namespace vsm::audio::engine {

/// LE BANC DE THREADS DU RENDU (D8.1).
///
/// Il ne fait qu'une chose : exécuter `count` tâches indépendantes et ne rendre
/// la main que lorsque la dernière est finie. C'est exactement la forme dont le
/// graphe a besoin -- « rendez-moi ces trente-deux pistes » -- et rien de plus.
/// Pas de file de travaux, pas de vol de tâches entre rondes, pas de futur :
/// chacune de ces généralités coûterait des allocations et des indirections sur
/// le chemin le plus contraint du programme.
///
/// LE THREAD APPELANT TRAVAILLE AUSSI, et ce n'est pas un détail d'économie :
/// c'est ce qui rend le banc utilisable à zéro thread auxiliaire. Avec zéro
/// travailleur, `runParallel` est une simple boucle -- donc le chemin
/// mono-cœur reste EXACTEMENT ce qu'il était, sans branche « au cas où ».
///
/// POURQUOI UN SÉMAPHORE ET NON UN VERROU. Le thread audio ne doit jamais
/// ATTENDRE un verrou que détient un thread moins prioritaire (inversion de
/// priorité : le clic classique). Ici il ne fait que `release()` -- il donne des
/// jetons, il n'en prend pas -- et il attend la fin en tournant sur un entier
/// atomique. Aucun verrou n'est jamais pris par le thread audio.
///
/// CE QU'IL NE SAIT PAS FAIRE, et qui est la responsabilité de l'appelant :
/// vérifier que les tâches sont réellement indépendantes. Deux tâches qui
/// écrivent au même endroit produiront ici une course, silencieuse et rare.
class RenderThreadPool {
public:
    /// Une tâche. `index` est le numéro de la tâche dans la ronde, `workerId`
    /// celui du thread qui l'exécute -- 0 pour le thread appelant, 1..N pour les
    /// auxiliaires. Le second existe pour que l'appelant puisse donner à chaque
    /// thread son propre tampon de travail sans le chercher dans une table.
    using JobFn = void (*)(void* context, size_t index, size_t workerId);

    RenderThreadPool() = default;
    ~RenderThreadPool();
    RenderThreadPool(const RenderThreadPool&) = delete;
    RenderThreadPool& operator=(const RenderThreadPool&) = delete;

    /// Thread UI, JAMAIS le thread audio : cette fonction crée et détruit des
    /// threads. `workerCount` est le nombre de threads AUXILIAIRES ; zéro rend
    /// le banc transparent. Plafonné à `kMaxWorkers`.
    void resize(size_t workerCount);

    size_t workerCount() const { return workers_.size(); }
    /// Combien de threads travaillent réellement : les auxiliaires plus
    /// l'appelant. C'est ce nombre-là qui dimensionne les tampons par thread.
    size_t parallelism() const { return workers_.size() + 1; }

    /// Thread audio. Exécute `job(context, i, workerId)` pour i dans [0, count)
    /// et ne revient que lorsque toutes sont finies. N'alloue rien.
    void runParallel(JobFn job, void* context, size_t count);

    /// Ce qu'on prend par défaut : un thread auxiliaire par cœur, moins celui
    /// qui appelle, moins un pour laisser respirer l'interface et le système --
    /// et jamais plus que `kRecommendedCeiling`. Zéro si la machine ne déclare
    /// pas assez de cœurs pour que ça ait un sens.
    static size_t recommendedWorkerCount();

    /// CE QUE LE BANC DE MESURE A DIT, ET NON CE QUE LE BON SENS SUGGÉRAIT.
    ///
    /// « Un thread par cœur » est le réglage qu'on écrit spontanément ; sur la
    /// machine de développement (Core Ultra 7 155H, 22 cœurs logiques), il
    /// donne le MEILLEUR `min` de tout le tableau et un p99 deux fois pire que
    /// huit threads -- parce que la ronde ne finit qu'avec son dernier
    /// travailleur, et qu'un cœur E ou LP-E met deux à trois fois plus
    /// longtemps qu'un cœur P à faire la même piste. Or c'est le p99 qui
    /// décide s'il y a un clic, pas la moyenne.
    ///
    /// Mesures sur 32 pistes chargées (8 voix + 3 inserts chacune), gain sur le
    /// p99 : x1,47 à 1 thread, x1,85 à 2, x2,99 à 4, **x3,70 à 8**, puis x1,80
    /// à 12 et x1,72 à 16. Le sommet est net et le décrochage l'est autant.
    /// D'où ce plafond, qu'on relèvera le jour où une mesure le demandera.
    static constexpr size_t kRecommendedCeiling = 8;

    /// PLAFOND ABSOLU de ce que l'utilisateur peut demander à la main. Il est
    /// bien au-dessus du recommandé : rien n'interdit d'essayer plus sur une
    /// machine à cœurs homogènes, mais ce n'est plus le réglage par défaut.
    static constexpr size_t kMaxWorkers = 31;

private:
    void workerLoop(size_t workerId);

    std::vector<std::thread> workers_;
    std::atomic<bool> quitting_{false};

    // La ronde en cours. Écrite par le thread audio AVANT `release()`, lue par
    // les travailleurs APRÈS `acquire()` : le sémaphore fait la synchronisation.
    JobFn job_ = nullptr;
    void* context_ = nullptr;
    size_t count_ = 0;

    std::atomic<size_t> nextIndex_{0};   ///< prochaine tâche à prendre

    /// COMBIEN DE TRAVAILLEURS SONT ENCORE DANS LA RONDE -- et non « combien de
    /// tâches restent ». La nuance est ce qui rend le banc sûr : un travailleur
    /// qui vient de finir la DERNIÈRE tâche est encore dans sa boucle, et va
    /// tenter une prise de plus avant d'en sortir. Si l'appelant était déjà
    /// reparti préparer la ronde suivante, cette prise-là piocherait dans la
    /// NOUVELLE ronde et exécuterait sa première tâche deux fois. En attendant
    /// que chacun soit SORTI, on garantit qu'aucun travailleur ne touche encore
    /// `job_`, `count_` ni `nextIndex_` quand ils sont réécrits.
    std::atomic<size_t> active_{0};

    // Un jeton par travailleur et par ronde : exactement autant de `release`
    // que de `acquire`, donc jamais de jeton qui traîne d'une ronde à l'autre.
    std::counting_semaphore<static_cast<std::ptrdiff_t>(kMaxWorkers) + 1> start_{0};
};

} // namespace vsm::audio::engine
