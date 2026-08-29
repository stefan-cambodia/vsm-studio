#pragma once
#include "ISynthPlugin.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsm::audio::plugin {

using SynthPluginFactory = std::function<SynthPluginPtr()>;
/// Fabrique une machine d'après un identifiant qu'elle est seule à savoir lire.
using SynthPluginFactoryById = std::function<SynthPluginPtr(const std::string&)>;

/// Registre global des machines disponibles. Chaque plugin s'enregistre
/// lui-même via VSM_REGISTER_SYNTH_PLUGIN (voir plus bas) : ajouter le
/// Jupiter-8 en Phase 3+ consistera à créer un nouveau fichier
/// `plugins/jupiter8/...` qui s'auto-enregistre, SANS toucher à ce fichier
/// ni à AudioEngine (garantie de la section 22).
class PluginRegistry {
public:
    static PluginRegistry& instance() {
        static PluginRegistry registry;
        return registry;
    }

    void registerPlugin(const std::string& id, const std::string& displayName, SynthPluginFactory factory) {
        entries_[id] = Entry{displayName, std::move(factory)};
    }

    /// LES MACHINES QU'ON N'A PAS ÉCRITES (D7.1). Un plugin tiers ne peut pas
    /// s'auto-enregistrer avant `main` : il vit dans un fichier que
    /// l'utilisateur désigne en cours de route. Le registre accepte donc UN
    /// résolveur, appelé quand l'identifiant demandé n'est pas connu.
    ///
    /// POURQUOI UN CROCHET PLUTÔT QU'UNE DÉPENDANCE. `vsm_audio` ne doit rien
    /// savoir de CLAP -- c'est `clap/` qui dépend de `audio/`, jamais
    /// l'inverse, et le SDK CLAP est facultatif. Le crochet inverse la
    /// dépendance : la couche CLAP se pose elle-même ici quand elle est
    /// présente, et tout le reste du moteur continue de ne parler que
    /// d'identifiants.
    ///
    /// CONSÉQUENCE VOULUE : `ProcessGraph`, le rendu hors ligne et le format de
    /// projet n'ont pas une ligne à changer pour accepter des instruments
    /// tiers.
    void setExternalResolver(SynthPluginFactoryById resolver) {
        externalResolver_ = std::move(resolver);
    }

    /// LE RÉSOLVEUR EN PLACE, pour qu'une seconde famille de plugins puisse se
    /// poser SANS effacer la première : elle enchaîne le sien devant celui-ci
    /// (voir `installClapResolver` et `installVst3Resolver`). Un unique
    /// résolveur qu'on écrase ferait disparaître CLAP ou VST3 selon l'ordre des
    /// appels -- le genre de règle d'ordre que personne ne se rappelle et que
    /// rien ne signale.
    const SynthPluginFactoryById& externalResolver() const { return externalResolver_; }

    SynthPluginPtr create(const std::string& id) const {
        auto it = entries_.find(id);
        if (it != entries_.end()) return it->second.factory();
        if (externalResolver_) return externalResolver_(id);
        return nullptr;
    }

    /// Vrai pour une machine du parc. UN PLUGIN TIERS RÉPOND FAUX ICI, et
    /// c'est délibéré : savoir s'il est là demande d'ouvrir un fichier, ce que
    /// cette question -- posée partout, y compris dans des boucles d'interface
    /// -- ne doit pas faire. Ce qui décide reste `create()`, dont l'échec est
    /// déjà signalé et jamais substitué.
    bool isRegistered(const std::string& id) const { return entries_.count(id) > 0; }

    std::vector<std::pair<std::string, std::string>> listAvailable() const {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(entries_.size());
        for (const auto& [id, entry] : entries_)
            result.emplace_back(id, entry.displayName);
        return result;
    }

private:
    struct Entry { std::string displayName; SynthPluginFactory factory; };
    std::unordered_map<std::string, Entry> entries_;
    SynthPluginFactoryById externalResolver_;
};

} // namespace vsm::audio::plugin

// ---------------------------------------------------------------------------
// Auto-enregistrement. ATTENTION (piège classique du C++, documenté ici
// pour ne pas le redécouvrir douloureusement en Phase 3) :
//
// 1) ClassName DOIT être un identifiant NON qualifié (ex: TestToneSynth, PAS
//    vsm::plugins::testtone::TestToneSynth) : la macro fait du token-pasting
//    (ClassName##Registrar), qui ne fonctionne que sur un seul token. Placez
//    donc l'appel de cette macro À L'INTÉRIEUR du namespace du plugin, en
//    dernière ligne, avec le nom simple.
//
// 2) Ce pattern s'appuie sur l'initialisation statique AVANT main(). Si le
//    fichier .cpp du plugin ne se retrouve référencé par AUCUN symbole
//    ailleurs dans le programme, l'éditeur de liens est en droit d'éliminer
//    toute la traduction unit d'une bibliothèque statique -- et donc le
//    registrar avec elle (le plugin "disparaît" silencieusement, sans
//    erreur de compilation). C'est vrai MÊME si le plugin est compilé
//    directement comme source de vsm_audio plutôt que dans une bibliothèque
//    "plugins" séparée : vsm_audio reste une bibliothèque statique du point
//    de vue de tout exécutable qui la lie. C'est pourquoi CHAQUE plugin
//    intégré doit AUSSI être référencé explicitement dans
//    vsm::audio::plugin::registerBuiltInPlugins() (voir BuiltInPlugins.h/
//    .cpp) -- c'est le seul autre endroit à toucher pour ajouter une
//    machine ; AudioEngine/ProcessGraph n'ont eux jamais besoin de changer.
#define VSM_REGISTER_SYNTH_PLUGIN(id, displayName, ClassName)                          \
    namespace {                                                                        \
    struct ClassName##Registrar {                                                      \
        ClassName##Registrar() {                                                       \
            vsm::audio::plugin::PluginRegistry::instance().registerPlugin(             \
                id, displayName, [] { return std::make_shared<ClassName>(); });        \
        }                                                                               \
    };                                                                                  \
    static ClassName##Registrar g_##ClassName##Registrar;                              \
    } // namespace
