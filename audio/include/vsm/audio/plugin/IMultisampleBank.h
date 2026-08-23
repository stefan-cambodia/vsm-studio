#pragma once
#include "vsm/audio/io/WavFileReader.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace vsm::audio::plugin {

/// Une ZONE : quel échantillon joue, pour quelles notes et quelles vélocités.
///
/// C'est la brique qui sépare `vsm.multisample` de `vsm.sampler`. Le sampler a
/// SEIZE EMPLACEMENTS déclenchés par note fixe, sans transposition : c'est ce
/// qu'attend un kit de batterie. Un instrument mélodique acoustique demande
/// l'inverse -- un même son couvre une poignée de demi-tons en étant repitché,
/// et change de nature selon la force du jeu. Une zone déclare donc les deux
/// étendues, celle des notes et celle des vélocités.
///
/// PAS DE JSON ICI, ET C'EST VOULU. L'invariant du projet interdit à `audio/`
/// de connaître un format d'échange (feuille de route § 8). Le fichier de
/// profil est lu par `interchange/`, qui remplit ces structures et les remet à
/// la machine. Le DSP ne sait donc rien du format, et le format peut évoluer
/// sans recompiler le moteur.
struct MultisampleZoneSpec {
    /// Programme auquel la zone appartient. Un profil peut en contenir
    /// plusieurs (l'orchestre General MIDI en a cent vingt-huit) ; un profil
    /// de piano n'en a qu'un, le programme 0.
    int program = 0;

    int lowNote = 0;        ///< étendue de notes, bornes INCLUSES
    int highNote = 127;
    int lowVelocity = 1;    ///< étendue de vélocités, bornes INCLUSES
    int highVelocity = 127;

    /// Note à laquelle l'échantillon a été enregistré. C'est elle qui fixe le
    /// facteur de repitch : jouer `rootNote` relit le fichier à sa vitesse.
    int rootNote = 60;
    /// Accord fin de la zone, en cents. Sert à rattraper un échantillon qui
    /// n'est pas exactement à la hauteur qu'il annonce -- fréquent sur les
    /// banques enregistrées instrument par instrument.
    float tuneCents = 0.0f;
    /// Niveau propre à la zone. Sert à égaliser des couches de vélocité
    /// enregistrées à des distances de micro différentes.
    float level = 1.0f;

    /// Boucle de tenue, en TRAMES DU FICHIER. `loopEnd` est exclu.
    bool loopEnabled = false;
    uint64_t loopStart = 0;
    uint64_t loopEnd = 0;

    /// Chemin ABSOLU, déjà résolu par l'appelant à partir du chemin relatif du
    /// profil. La machine lit ce fichier ; elle ne résout rien elle-même,
    /// parce que la règle « chemin relatif obligatoire » est une règle de
    /// FORMAT, et que le format vit dans `interchange/`.
    std::string samplePath;
    /// Le chemin tel qu'il est écrit dans le profil, conservé pour les
    /// messages d'erreur et pour l'état sauvegardé.
    std::string relativePath;
};

/// Un profil complet, prêt à être chargé.
struct MultisampleProfileSpec {
    std::string name;
    /// Licence et crédit de la banque. OBLIGATOIRE (§ 28 d'ARCHITECTURE.md) :
    /// un profil sans attribution est refusé, pas chargé « en attendant ».
    std::string attribution;
    /// Chemin du fichier de profil lui-même, pour le réenregistrer dans un
    /// projet et pour l'afficher.
    std::string sourcePath;
    /// Nom lisible de chaque programme, indexé par numéro de programme.
    std::vector<std::string> programNames;
    /// L'ORDRE FAIT FOI : la première zone qui contient (programme, note,
    /// vélocité) est celle qui joue. C'est déterministe et cela se lit dans le
    /// fichier ; un départage par « meilleure » zone serait plus malin et
    /// beaucoup moins prévisible.
    std::vector<MultisampleZoneSpec> zones;
};

/// Cache d'échantillons DÉCODÉS, partagé entre instances de machine.
///
/// POURQUOI IL EXISTE, ET LE CHIFFRE QUI L'A EXIGÉ. Le service de rendu crée
/// une instance neuve à chaque requête -- c'est ce qui garantit que deux rendus
/// identiques donnent le même son, et il ne faut pas y toucher. Mais un profil
/// de piano, c'est cent vingt fichiers et deux cent trente mégaoctets : les
/// redécoder à chaque rendu coûtait **124 ms contre 4 ms pour un soustractif**,
/// soit trente fois le prix d'une évaluation. Une machine trente fois plus
/// chère que les autres ne se cherche pas, elle se subit.
///
/// Ce qui est partagé est UNIQUEMENT la donnée décodée, jamais l'état de la
/// machine : chaque instance reconstruit ses zones et sa polyphonie. Le rendu
/// reste donc identique au bit près, cache ou pas -- un test le vérifie.
class MultisampleSampleCache {
public:
    /// Rend l'échantillon décodé pour ce chemin, en le lisant si besoin.
    /// `outError` non vide = échec, et le résultat est nul.
    vsm::audio::io::SampleBufferPtr get(const std::string& path, std::string& outError);

    void clear();
    size_t bytes() const;

    /// Au-delà de ce total, le cache est VIDÉ avant d'accueillir un nouvel
    /// échantillon. Politique volontairement brutale plutôt que fine : dans
    /// l'usage réel, un processus emploie un profil et un seul, et une
    /// éviction élaborée coûterait plus de code qu'elle ne rapporterait.
    static constexpr size_t kBudgetBytes = 512u * 1024u * 1024u;

private:
    mutable std::mutex mutex_;
    std::map<std::string, vsm::audio::io::SampleBufferPtr> entries_;
    size_t bytes_ = 0;
};

/// Charger un PROFIL multi-échantillons dans une machine.
///
/// Même raison d'être qu'`ISampleLoader`, et mêmes règles : `ISynthPlugin` ne
/// transporte que des flottants, un profil n'entre pas dans ce moule, et ces
/// méthodes s'appellent depuis le thread UI ou un thread de chargement --
/// JAMAIS depuis le thread audio, puisqu'elles lisent des fichiers. Le profil
/// est publié par échange atomique, comme les échantillons du sampler.
///
/// Interface DISTINCTE d'`ISampleLoader` plutôt qu'une extension : les deux
/// familles n'ont pas le même modèle (emplacements numérotés contre zones
/// déclarées), et fondre les deux obligerait le sampler à répondre à des
/// questions qui n'ont pas de sens pour lui.
class IMultisampleBank {
public:
    virtual ~IMultisampleBank() = default;

    /// Charge le profil : lit chaque fichier déclaré, vérifie le budget
    /// mémoire, et ne publie qu'une fois TOUT chargé. En cas d'échec, le
    /// profil précédent reste en place et `outError` dit ce qui manque -- un
    /// profil à moitié chargé jouerait juste sur une partie du clavier et
    /// faux sur le reste, ce qui est pire que rien.
    /// `cache` peut être nul : la machine lit alors chaque fichier elle-même.
    /// Un appelant qui charge le même profil plusieurs fois -- le service de
    /// rendu, qui crée une instance par requête -- doit en fournir un.
    virtual bool loadProfile(const MultisampleProfileSpec& spec, std::string& outError,
                             MultisampleSampleCache* cache) = 0;

    /// Vide le profil. La machine devient silencieuse ; elle ne repart pas sur
    /// un son de synthèse de repli.
    virtual void clearProfile() = 0;

    virtual std::string profileName() const = 0;
    virtual std::string profileSourcePath() const = 0;
    virtual std::string profileAttribution() const = 0;
    virtual int zoneCount() const = 0;
    virtual int programCount() const = 0;
    /// Empreinte mémoire réelle des échantillons publiés, en octets.
    virtual size_t profileMemoryBytes() const = 0;
};

/// Budget mémoire d'un profil (§ 3 du cahier des charges). Au-delà, le
/// chargement est REFUSÉ avec un message : un profil qui fait déborder la
/// mémoire ne se manifeste pas par une erreur claire mais par un système qui
/// rame, et personne ne fait le lien avec la banque installée la veille.
inline constexpr size_t kMultisampleMemoryBudgetBytes = 256u * 1024u * 1024u;

} // namespace vsm::audio::plugin
