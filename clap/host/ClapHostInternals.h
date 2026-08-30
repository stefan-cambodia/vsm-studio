#pragma once
// PRIVÉ À `clap/host/`. Cet en-tête inclut le SDK CLAP ; il ne doit être
// inclus que par les `.cpp` de ce dossier, jamais par le reste du projet
// (règle du § 0 de ROADMAP-interop.md, tenue depuis P6 : `ClapPluginHost.h`
// n'inclut aucun en-tête CLAP).

#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <clap/clap.h>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace vsm::clap {

/// L'HÔTE VU PAR UN PLUGIN, ET IL EN FAUT UN PAR PLUGIN (D7.4, façade CLAP).
///
/// Jusqu'ici l'hôte était une structure STATIQUE partagée, dont
/// `get_extension` répondait toujours `nullptr` et dont `host_data` était nul.
/// C'était suffisant pour faire jouer un plugin : le son ne demande rien à
/// l'hôte. Une interface, si — et elle le demande *en retour*, ce qui change
/// tout : `request_resize`, `closed`, `register_timer` sont des appels du
/// plugin VERS l'hôte, et l'hôte doit pouvoir retrouver DE QUEL plugin il
/// s'agit. Un `host_data` nul rendait la question sans réponse.
///
/// D'où ce pont : un `clap_host` par instance, dont `host_data` pointe ici.
struct HostBridge {
    clap_host host{};
    const clap_plugin* plugin = nullptr;
    /// Le thread depuis lequel le pont a été construit, c'est-à-dire celui de
    /// l'interface. Sert à répondre à `clap_host_thread_check`, que beaucoup
    /// de plugins interrogent avant de faire quoi que ce soit.
    std::thread::id mainThread = std::this_thread::get_id();

    // --- Ce que la fenêtre installe quand elle s'ouvre, et retire en se
    // fermant. Vides le reste du temps : un plugin qui demande à être
    // redimensionné alors qu'aucune fenêtre ne le montre demande dans le vide,
    // et c'est la bonne réponse.
    std::function<void(uint32_t width, uint32_t height)> onRequestResize;
    std::function<void()> onClosed;

    /// LES MINUTERIES QUE LE PLUGIN DEMANDE. On les ENREGISTRE ici et c'est la
    /// fenêtre qui les fait battre : une minuterie n'a de sens que sur le
    /// thread de l'interface, et ce fichier ne connaît pas JUCE.
    struct Timer {
        clap_id id = 0;
        uint32_t periodMs = 0;
    };
    std::vector<Timer> timers;
    clap_id nextTimerId = 1;

    static HostBridge* from(const clap_host* h) {
        return h ? static_cast<HostBridge*>(h->host_data) : nullptr;
    }

    /// Remplit `host` : identité, rappels de base, et les extensions qu'une
    /// interface exige. À appeler AVANT `create_plugin`.
    void install();
};

/// Le pont d'un instrument ou d'un effet CLAP déjà chargé, ou nullptr si
/// l'objet n'en est pas un. Défini dans `ClapPluginHost.cpp` ; employé par
/// `ClapPluginWindow.cpp`, qui a besoin du plugin brut et du pont.
struct HostedPlugin {
    const clap_plugin* plugin = nullptr;
    HostBridge* bridge = nullptr;
};
HostedPlugin hostedPluginOf(vsm::audio::plugin::ISynthPlugin& instrument);
HostedPlugin hostedPluginOf(vsm::audio::effect::IAudioEffect& effect);

} // namespace vsm::clap
