#pragma once
#include "vsm/audio/dsp/SincResampler.h"
#include "vsm/audio/io/WavStreamReader.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

namespace vsm::audio::engine {

/// D'OÙ VIENNENT LES ÉCHANTILLONS D'UNE PISTE AUDIO (D8.2).
///
/// Jusqu'ici il n'y avait qu'une réponse : deux `std::vector<float>` chargés
/// entiers au moment d'ouvrir le projet. C'est le bon choix pour un
/// échantillon de sampler, et c'est ruineux pour une prise de neuf minutes --
/// 200 Mo décodés en flottants stéréo, pour un fichier qui en pèse 50 sur le
/// disque. Vingt pistes de ce genre demandaient quatre gigaoctets.
///
/// Cette interface est la couture, et elle est mince PAR CONSTRUCTION : le
/// graphe ne sait pas d'où vient un échantillon, il demande le n-ième. Tout ce
/// qui suit -- diffusion, cache, rééchantillonnage à la volée -- vit derrière.
///
/// LE CONTRAT DU THREAD AUDIO vaut pour toutes les implémentations :
/// `frameAt` n'alloue pas, ne fait aucune entrée-sortie et ne prend aucun
/// verrou. Une implémentation qui ne peut pas répondre répond FAUX -- elle ne
/// va pas chercher la donnée.
class SampleStore {
public:
    virtual ~SampleStore() = default;

    /// Longueur du matériau, à la fréquence de la SESSION.
    virtual int64_t frames() const = 0;

    /// Thread audio. Renvoie faux quand l'échantillon n'est pas disponible --
    /// hors du fichier, ou pas encore lu depuis le disque. L'appelant se tait
    /// alors, ce qui est la seule réponse honnête.
    virtual bool frameAt(int64_t index, float& left, float& right) const = 0;

    /// Thread audio, UNE FOIS PAR CLIP ET PAR BLOC, avant de lire.
    ///
    /// C'est ce qui remplace la voyance : le cache ne peut pas deviner qu'un
    /// saut de tête de lecture va l'envoyer trois minutes plus loin, mais le
    /// clip, lui, sait exactement quelles trames il va demander. Sans coût
    /// pour le matériau résident, qui n'en fait rien.
    virtual void requestRange(int64_t startFrame, int64_t count) const { (void)startFrame; (void)count; }

    /// Encadrent la lecture d'un bloc. C'est par là que le thread de diffusion
    /// sait qu'il ne doit pas réécrire une fenêtre en cours de lecture.
    virtual void beginRead() const {}
    virtual void endRead() const {}

    /// Ce que ce matériau occupe RÉELLEMENT en mémoire. C'est le chiffre du
    /// critère de D8.2, et il doit pouvoir se lire, pas se supposer.
    virtual size_t residentBytes() const = 0;

    /// Échantillons demandés que le cache n'avait pas. Doit rester à zéro en
    /// lecture normale ; toute autre valeur est un trou dans le son, et il n'y
    /// a aucune raison de le taire.
    virtual uint64_t cacheMisses() const { return 0; }

    /// RAII pour `beginRead`/`endRead`.
    class ReadGuard {
    public:
        explicit ReadGuard(const SampleStore* store) : store_(store) {
            if (store_) store_->beginRead();
        }
        ~ReadGuard() { if (store_) store_->endRead(); }
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;
    private:
        const SampleStore* store_;
    };
};

/// LE MATÉRIAU RÉSIDENT : tout le fichier, décodé, en mémoire.
///
/// Reste le bon choix pour ce qui est court -- une prise de batterie de huit
/// mesures, un échantillon. Aucun thread, aucun cache, aucune latence : le
/// n-ième échantillon est une indexation de tableau.
class MemorySampleStore final : public SampleStore {
public:
    MemorySampleStore(std::vector<float> left, std::vector<float> right)
        : left_(std::move(left)), right_(std::move(right)) {}

    int64_t frames() const override { return static_cast<int64_t>(left_.size()); }

    bool frameAt(int64_t index, float& left, float& right) const override {
        if (index < 0 || index >= static_cast<int64_t>(left_.size())) return false;
        const auto i = static_cast<size_t>(index);
        left = left_[i];
        right = i < right_.size() ? right_[i] : left_[i];
        return true;
    }

    size_t residentBytes() const override {
        return (left_.size() + right_.size()) * sizeof(float);
    }

    const std::vector<float>& leftChannel() const { return left_; }
    const std::vector<float>& rightChannel() const { return right_; }

private:
    std::vector<float> left_, right_;
};

/// LE MATÉRIAU DIFFUSÉ : quelques secondes autour de ce qu'on écoute, et le
/// reste sur le disque (D8.2).
///
/// LA MÉMOIRE OCCUPÉE NE DÉPEND PAS DE LA DURÉE DU FICHIER, et c'est toute la
/// démonstration du critère de la phase : quatre fenêtres de 32 768 trames
/// stéréo font un mégaoctet, qu'il s'agisse de trente secondes ou de neuf
/// minutes. Vingt pistes en occupent vingt.
///
/// QUATRE FENÊTRES, ET PAS DEUX. Une piste ne lit pas toujours un seul endroit
/// du fichier : deux clips superposés y puisent à deux endroits, et un bloc à
/// cheval sur une frontière en touche deux d'un coup. Deux fenêtres suffiraient
/// à la lecture linéaire et laisseraient un trou au premier montage un peu
/// serré -- un trou qu'on entendrait sans savoir d'où il vient.
class StreamedSampleStore final : public SampleStore {
public:
    /// COMMENT LE CACHE EST REMPLI, et c'est la seule différence entre écouter
    /// et exporter.
    ///
    /// `Realtime` : un thread de diffusion remplit en arrière-plan, et ce qui
    /// n'est pas encore là est silencieux -- le thread audio n'attend JAMAIS le
    /// disque.
    ///
    /// `Blocking` : la lecture va chercher elle-même ce qui manque. Interdit
    /// sur le thread audio, obligatoire pour le rendu hors ligne : un export
    /// dans lequel il manquerait ce que le disque n'a pas eu le temps de
    /// livrer ne serait pas un export, ce serait une loterie.
    enum class Mode { Realtime, Blocking };

    static std::shared_ptr<StreamedSampleStore> open(const std::string& path,
                                                      double sessionSampleRate, Mode mode,
                                                      std::string& error);

    ~StreamedSampleStore() override = default;

    int64_t frames() const override { return frames_; }
    bool frameAt(int64_t index, float& left, float& right) const override;
    void requestRange(int64_t startFrame, int64_t count) const override;
    void beginRead() const override;
    void endRead() const override;
    size_t residentBytes() const override;
    uint64_t cacheMisses() const override { return misses_.load(std::memory_order_relaxed); }

    double fileSampleRate() const { return fileSampleRate_; }
    bool resampled() const { return ratio_ != 1.0; }

    /// Un tour de remplissage. Appelée par le thread de diffusion, et
    /// directement par `requestRange` en mode `Blocking`.
    void refill();

    /// Trames par fenêtre. Puissance de deux : l'indice de fenêtre et le
    /// décalage dedans se calculent alors par décalage et masque, ce qui compte
    /// quand on le fait une fois par échantillon.
    static constexpr int64_t kWindowShift = 15;
    static constexpr int64_t kWindowFrames = int64_t{1} << kWindowShift;
    static constexpr size_t kWindows = 4;

private:
    StreamedSampleStore() = default;
    void loadWindow(size_t windowIndex, int64_t chunk);
    void publishRequest(int64_t chunk) const;

    struct Window {
        std::atomic<int64_t> chunk{-1};   ///< -1 = vide ou en cours d'écriture
        std::vector<float> left, right;
    };

    std::shared_ptr<vsm::audio::io::WavStreamReader> reader_;
    mutable std::array<Window, kWindows> windows_;
    Mode mode_ = Mode::Realtime;
    int64_t frames_ = 0;              ///< à la fréquence de la SESSION
    double ratio_ = 1.0;              ///< fréquence du fichier / fréquence de session
    double fileSampleRate_ = 48000.0;
    /// Le noyau de rééchantillonnage (D12.1), construit à l'ouverture pour le
    /// rapport du fichier ; le même que celui du chargeur résident.
    vsm::audio::dsp::SincResampler noyau_;

    /// Les dernières plages demandées par le thread audio. Un anneau plutôt
    /// qu'une valeur : plusieurs clips peuvent demander plusieurs endroits dans
    /// le même bloc, et écraser la demande du voisin le rendrait muet.
    ///
    /// AUTANT DE DEMANDES QUE DE FENÊTRES, ET PAS UNE DE PLUS. Le remplissage
    /// refuse de recycler une fenêtre encore réclamée -- c'est ce qui empêche
    /// deux besoins d'alterner en se chassant l'un l'autre. Si l'anneau pouvait
    /// retenir plus de demandes qu'il n'y a de fenêtres, des demandes PÉRIMÉES
    /// suffiraient à toutes les épingler, et une fenêtre réellement nécessaire
    /// ne se chargerait plus jamais : le silence, définitivement. Mesuré.
    static constexpr size_t kRequests = kWindows;
    mutable std::array<std::atomic<int64_t>, kRequests> requested_{};
    mutable std::atomic<size_t> requestCursor_{0};

    mutable std::atomic<int> readers_{0};
    mutable std::atomic<uint64_t> misses_{0};
    /// Mémorise la dernière fenêtre servie : la lecture est très majoritairement
    /// linéaire, donc la première essayée est presque toujours la bonne.
    mutable size_t lastWindow_ = 0;

    /// UN SEUL REMPLISSAGE À LA FOIS. Le thread de diffusion n'est pas seul à
    /// appeler `refill` -- le mode bloquant le fait depuis le lecteur, et les
    /// tests le poussent à la main. Deux remplissages simultanés écriraient
    /// dans la même fenêtre. Ce verrou n'est JAMAIS pris par le thread audio,
    /// qui ne fait que lire.
    std::mutex refillMutex_;

    /// Tampon de décodage du thread de diffusion. Jamais touché par le thread
    /// audio -- c'est pour cela qu'il peut être aussi grand qu'il faut.
    std::vector<float> sourceL_, sourceR_;
};

/// LE THREAD QUI VA CHERCHER LES OCTETS.
///
/// UN SEUL POUR TOUT LE PROGRAMME, et non un par piste : vingt threads qui se
/// réveillent toutes les dix millisecondes pour constater qu'il n'y a rien à
/// faire coûteraient plus que ce qu'ils servent. Il ne connaît les caches que
/// par des `weak_ptr` : une piste qu'on retire du projet disparaît d'elle-même,
/// sans que personne ait à penser à la désinscrire.
class DiskStreamer {
public:
    static DiskStreamer& instance();

    void add(const std::shared_ptr<StreamedSampleStore>& store);
    /// Un tour de remplissage tout de suite, sur le thread appelant. Sert aux
    /// tests, qui ne doivent pas dépendre du réveil d'un thread pour être
    /// reproductibles.
    void pump();
    size_t trackedStores() const;

private:
    DiskStreamer();
    ~DiskStreamer();
    void loop();

    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<StreamedSampleStore>> stores_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

} // namespace vsm::audio::engine
