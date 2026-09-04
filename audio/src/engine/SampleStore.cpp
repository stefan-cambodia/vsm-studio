#include "vsm/audio/engine/SampleStore.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::engine {

// ---------------------------------------------------------------------------
// StreamedSampleStore
// ---------------------------------------------------------------------------

std::shared_ptr<StreamedSampleStore> StreamedSampleStore::open(const std::string& path,
                                                                double sessionSampleRate,
                                                                Mode mode, std::string& error) {
    if (sessionSampleRate <= 0.0) { error = "fréquence de session invalide"; return nullptr; }

    auto ouverture = vsm::audio::io::WavStreamReader::open(path);
    if (!ouverture.reader) { error = ouverture.error; return nullptr; }

    auto store = std::shared_ptr<StreamedSampleStore>(new StreamedSampleStore());
    store->reader_ = ouverture.reader;
    store->mode_ = mode;
    store->fileSampleRate_ = ouverture.reader->sampleRate();
    store->ratio_ = store->fileSampleRate_ / sessionSampleRate;
    if (std::abs(store->ratio_ - 1.0) < 1e-12) store->ratio_ = 1.0;
    // La longueur est celle du matériau UNE FOIS RÉÉCHANTILLONNÉ : tout ce qui
    // sort d'ici parle en trames de la session, comme le reste du graphe.
    store->frames_ = static_cast<int64_t>(
        std::llround(static_cast<double>(ouverture.reader->frames()) / store->ratio_));

    for (auto& fenetre : store->windows_) {
        fenetre.left.assign(static_cast<size_t>(kWindowFrames), 0.0f);
        fenetre.right.assign(static_cast<size_t>(kWindowFrames), 0.0f);
        fenetre.chunk.store(-1, std::memory_order_relaxed);
    }
    for (auto& demande : store->requested_) demande.store(-1, std::memory_order_relaxed);

    // LE TAMPON DE DÉCODAGE : une fenêtre de session vaut `ratio` trames de
    // fichier, plus le noyau de part et d'autre (D12.1 : un sinc fenêtré
    // regarde `taps/2` échantillons avant et après chaque position).
    if (store->ratio_ != 1.0) store->noyau_.prepare(store->ratio_);
    const int64_t sourceFrames =
        static_cast<int64_t>(std::ceil(static_cast<double>(kWindowFrames) * store->ratio_))
        + store->noyau_.taps() + 2;
    store->sourceL_.assign(static_cast<size_t>(sourceFrames), 0.0f);
    store->sourceR_.assign(static_cast<size_t>(sourceFrames), 0.0f);

    if (mode == Mode::Realtime) DiskStreamer::instance().add(store);
    return store;
}

void StreamedSampleStore::beginRead() const {
    // SÉQUENTIELLEMENT COHÉRENT DES DEUX CÔTÉS, et c'est indispensable : ce
    // compteur et le numéro de fenêtre forment un protocole de Dekker avec le
    // thread de diffusion (« si tu ne me vois pas, je te vois »). Un ordre plus
    // faible autoriserait le processeur à retarder cette écriture derrière la
    // lecture du numéro de fenêtre, et les deux côtés se croiseraient.
    readers_.fetch_add(1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void StreamedSampleStore::endRead() const {
    readers_.fetch_sub(1, std::memory_order_seq_cst);
}

bool StreamedSampleStore::frameAt(int64_t index, float& left, float& right) const {
    if (index < 0 || index >= frames_) return false;
    const int64_t chunk = index >> kWindowShift;
    const size_t decalage = static_cast<size_t>(index & (kWindowFrames - 1));

    // LA DERNIÈRE FENÊTRE SERVIE D'ABORD : la lecture est linéaire dans
    // l'immense majorité des cas, donc la première essayée est la bonne.
    for (size_t essai = 0; essai < kWindows; ++essai) {
        const size_t w = (lastWindow_ + essai) % kWindows;
        if (windows_[w].chunk.load(std::memory_order_acquire) != chunk) continue;
        lastWindow_ = w;
        left = windows_[w].left[decalage];
        right = windows_[w].right[decalage];
        return true;
    }
    misses_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void StreamedSampleStore::publishRequest(int64_t chunk) const {
    if (chunk < 0) return;
    const size_t slot = requestCursor_.fetch_add(1, std::memory_order_relaxed) % kRequests;
    requested_[slot].store(chunk, std::memory_order_release);
}

void StreamedSampleStore::requestRange(int64_t startFrame, int64_t count) const {
    if (count <= 0 || frames_ <= 0) return;
    const int64_t premier = std::max<int64_t>(0, startFrame) >> kWindowShift;
    const int64_t dernier = std::min(frames_ - 1, startFrame + count - 1) >> kWindowShift;
    for (int64_t c = premier; c <= dernier; ++c) publishRequest(c);
    // ET LA SUIVANTE. Sans cette ligne, le cache ne commencerait à lire la
    // fenêtre d'après qu'au moment où on en a besoin, c'est-à-dire trop tard :
    // la lecture avancerait par à-coups d'un demi-seconde.
    publishRequest(dernier + 1);

    // EN MODE BLOQUANT, ON VA LE CHERCHER TOUT DE SUITE. Personne n'attend
    // derrière : ce mode n'existe que pour le rendu hors ligne, où il vaut
    // infiniment mieux attendre le disque que rendre un silence.
    if (mode_ == Mode::Blocking) const_cast<StreamedSampleStore*>(this)->refill();
}

size_t StreamedSampleStore::residentBytes() const {
    size_t total = 0;
    for (const auto& fenetre : windows_)
        total += (fenetre.left.size() + fenetre.right.size()) * sizeof(float);
    total += (sourceL_.size() + sourceR_.size()) * sizeof(float);
    return total;
}

void StreamedSampleStore::loadWindow(size_t windowIndex, int64_t chunk) {
    Window& fenetre = windows_[windowIndex];

    // ON FERME LA FENÊTRE, PUIS ON ATTEND QUE PERSONNE N'Y SOIT. L'inverse
    // -- attendre puis fermer -- laisserait un lecteur entrer entre les deux et
    // lire des octets à moitié réécrits. C'est le seul endroit de tout le
    // moteur où un thread ATTEND le thread audio, et c'est le bon sens de
    // l'attente : celui qui n'a pas d'échéance attend celui qui en a une.
    fenetre.chunk.store(-1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    // EN MODE BLOQUANT, PERSONNE N'EST EN FACE : c'est le lecteur lui-même qui
    // vient chercher ce qui lui manque, depuis l'intérieur de sa propre garde
    // de lecture. Attendre qu'elle se referme serait s'attendre soi-même --
    // mesuré, et exactement aussi long que ça en a l'air.
    if (mode_ == Mode::Realtime)
        while (readers_.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();

    const int64_t premiereTrame = chunk << kWindowShift;
    if (ratio_ == 1.0) {
        reader_->readFrames(premiereTrame, kWindowFrames, fenetre.left.data(), fenetre.right.data());
    } else {
        // RÉÉCHANTILLONNAGE À LA VOLÉE, et il est SANS ÉTAT : la position dans
        // le fichier se déduit de la position de session, pas d'un compteur
        // qu'on ferait avancer. C'est ce qui rend une fenêtre lisible sans
        // avoir lu les précédentes -- donc un saut de tête de lecture exact.
        // Le tampon commence un demi-noyau AVANT la première position, pour
        // que le noyau ait ses échantillons des deux côtés ; avant le
        // fichier, `readFrames` livre du silence, comme le chargeur résident
        // voit du silence hors de son vecteur : mêmes valeurs des deux côtés.
        const double debutSource = static_cast<double>(premiereTrame) * ratio_;
        const int64_t marge = noyau_.taps() / 2;
        const int64_t sourceDebut = static_cast<int64_t>(std::floor(debutSource)) - marge;
        const int64_t sourceCount = static_cast<int64_t>(sourceL_.size());
        reader_->readFrames(sourceDebut, sourceCount, sourceL_.data(), sourceR_.data());
        // Au-delà de la fin du fichier, le noyau doit voir du silence, pas les
        // zéros du tampon confondus avec du signal : `at` s'arrête à `count`.
        const int64_t utiles = std::min(sourceCount, reader_->frames() - sourceDebut);
        for (int64_t i = 0; i < kWindowFrames; ++i) {
            const double position = debutSource + static_cast<double>(i) * ratio_
                                    - static_cast<double>(sourceDebut);
            fenetre.left[static_cast<size_t>(i)] = noyau_.at(sourceL_.data(), utiles, position);
            fenetre.right[static_cast<size_t>(i)] = noyau_.at(sourceR_.data(), utiles, position);
        }
    }

    fenetre.chunk.store(chunk, std::memory_order_release);
}

void StreamedSampleStore::refill() {
    std::lock_guard<std::mutex> verrou(refillMutex_);

    // Les fenêtres demandées, sans doublon. Huit demandes au plus, quatre
    // fenêtres : la boucle tient dans un tableau sur la pile.
    std::array<int64_t, kRequests> besoins{};
    size_t nombre = 0;
    for (const auto& demande : requested_) {
        const int64_t chunk = demande.load(std::memory_order_acquire);
        if (chunk < 0 || (chunk << kWindowShift) >= frames_) continue;
        bool deja = false;
        for (size_t i = 0; i < nombre; ++i) if (besoins[i] == chunk) deja = true;
        if (!deja) besoins[nombre++] = chunk;
    }
    if (nombre == 0) return;

    for (size_t i = 0; i < nombre; ++i) {
        const int64_t chunk = besoins[i];
        bool presente = false;
        for (auto& fenetre : windows_)
            if (fenetre.chunk.load(std::memory_order_acquire) == chunk) presente = true;
        if (presente) continue;

        // UNE FENÊTRE QU'AUCUNE DEMANDE NE RÉCLAME : c'est celle qu'on recycle.
        // S'il n'y en a aucune, on ne recycle RIEN -- perdre une fenêtre encore
        // utile pour en charger une autre ferait alterner deux trous.
        size_t choisie = kWindows;
        for (size_t w = 0; w < kWindows && choisie == kWindows; ++w) {
            const int64_t tenue = windows_[w].chunk.load(std::memory_order_acquire);
            if (tenue < 0) { choisie = w; break; }
            bool reclamee = false;
            for (size_t k = 0; k < nombre; ++k) if (besoins[k] == tenue) reclamee = true;
            if (!reclamee) choisie = w;
        }
        if (choisie == kWindows) break;
        loadWindow(choisie, chunk);
    }
}

// ---------------------------------------------------------------------------
// DiskStreamer
// ---------------------------------------------------------------------------

DiskStreamer& DiskStreamer::instance() {
    static DiskStreamer unique;
    return unique;
}

DiskStreamer::DiskStreamer() {
    thread_ = std::thread([this] { loop(); });
}

DiskStreamer::~DiskStreamer() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void DiskStreamer::add(const std::shared_ptr<StreamedSampleStore>& store) {
    if (!store) return;
    std::lock_guard<std::mutex> verrou(mutex_);
    // Au passage, on oublie ceux qui ont disparu : une piste retirée du projet
    // n'a personne pour venir se désinscrire.
    stores_.erase(std::remove_if(stores_.begin(), stores_.end(),
                                  [](const std::weak_ptr<StreamedSampleStore>& w) {
                                      return w.expired();
                                  }),
                   stores_.end());
    stores_.push_back(store);
}

size_t DiskStreamer::trackedStores() const {
    std::lock_guard<std::mutex> verrou(mutex_);
    size_t vivants = 0;
    for (const auto& faible : stores_) if (!faible.expired()) ++vivants;
    return vivants;
}

void DiskStreamer::pump() {
    std::vector<std::shared_ptr<StreamedSampleStore>> vivants;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        vivants.reserve(stores_.size());
        for (const auto& faible : stores_)
            if (auto fort = faible.lock()) vivants.push_back(std::move(fort));
    }
    // LE VERROU EST RENDU AVANT DE LIRE LE DISQUE. Le garder ferait attendre
    // l'ouverture d'un projet -- qui appelle `add` -- pendant une lecture de
    // fichier, pour rien.
    for (const auto& store : vivants) store->refill();
}

void DiskStreamer::loop() {
    using namespace std::chrono_literals;
    while (running_.load(std::memory_order_acquire)) {
        pump();
        // DIX MILLISECONDES, soit à peu près la durée d'un bloc audio. Une
        // fenêtre couvre deux tiers de seconde : le cache a donc soixante tours
        // d'avance sur ce qu'on écoute, ce qui laisse de la marge à un disque
        // qui hésite, sans réveiller un thread pour rien cent fois par seconde.
        std::this_thread::sleep_for(10ms);
    }
}

} // namespace vsm::audio::engine
