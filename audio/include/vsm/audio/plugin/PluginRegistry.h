#pragma once
#include "ISynthPlugin.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsm::audio::plugin {

using SynthPluginFactory = std::function<SynthPluginPtr()>;

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

    SynthPluginPtr create(const std::string& id) const {
        auto it = entries_.find(id);
        if (it == entries_.end()) return nullptr;
        return it->second.factory();
    }

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
