// UN INSTRUMENT CLAP MINIMAL QUI A UNE VRAIE INTERFACE X11 (D7.4).
//
// POURQUOI IL EXISTE, ET POURQUOI IL EST INDISPENSABLE. La façade CLAP avait
// été DIFFÉRÉE avec ce motif, écrit dans `ROADMAP-daw.md` : « livrer cent
// cinquante lignes d'incrustation de fenêtre que personne n'a jamais vues
// tourner, en les déclarant faites, est exactement ce que ce projet refuse
// ailleurs. » Écrire l'hôte ne suffit donc pas : il faut quelque chose à
// ouvrir. Or aucun plugin CLAP tiers à interface n'est installé sur la machine
// de développement, et en exiger un rendrait la vérification dépendante de
// l'ordinateur — ce que le plugin d'essai de D7.3 avait déjà refusé pour les
// effets.
//
// CE QU'IL FAIT, ET RIEN DE PLUS :
//   - il déclare l'extension `clap.gui` en X11, INCRUSTABLE (non flottante) ;
//   - il crée une vraie fenêtre X11, se laisse reparenter, se montre, se
//     redimensionne et se détruit ;
//   - il PEINT quelque chose qui bouge, piloté par une minuterie qu'il demande
//     à l'hôte : une fenêtre qui ne bouge pas ne prouve pas que les minuteries
//     arrivent, et une interface figée est le symptôme le plus courant d'un
//     hôte qui ne les fournit pas ;
//   - il demande un redimensionnement au bout de quelques secondes, ce qui
//     exerce `clap_host_gui->request_resize`.
//
// Il n'est PAS une machine du parc : il ne s'enregistre nulle part et ne sort
// pas de ce dossier.

#include <clap/clap.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginId = "com.vsmstudio.test.gui";

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor kDescriptor = {
    CLAP_VERSION,
    kPluginId,
    "VSM Test GUI (CLAP)",
    "VSM Studio",
    "", "", "",
    "0.1.0",
    "Instrument d'essai : une interface X11 incrustable, qui bouge",
    kFeatures,
};

struct Instance {
    clap_plugin plugin{};
    const clap_host* host = nullptr;

    // --- l'interface -------------------------------------------------------
    Display* display = nullptr;
    Window fenetre = 0;
    GC contexte = nullptr;
    uint32_t largeur = 360;
    uint32_t hauteur = 220;
    bool visible = false;
    int tic = 0;
    clap_id minuterie = 0;
    bool minuterieDemandee = false;
    /// Vrai une fois qu'on a demandé un agrandissement à l'hôte. Une seule
    /// fois : le but est d'exercer `request_resize`, pas de faire clignoter
    /// une fenêtre.
    bool redimensionDemande = false;
};

Instance* self(const clap_plugin* p) { return static_cast<Instance*>(p->plugin_data); }

// --- audio : le strict minimum pour être un instrument valide -------------

uint32_t audioPortsCount(const clap_plugin*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin*, uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (isInput || index != 0 || info == nullptr) return false;
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", "out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports kAudioPorts = {audioPortsCount, audioPortsGet};

uint32_t notePortsCount(const clap_plugin*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin*, uint32_t index, bool isInput, clap_note_port_info* info) {
    if (!isInput || index != 0 || info == nullptr) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "in");
    return true;
}

const clap_plugin_note_ports kNotePorts = {notePortsCount, notePortsGet};

// --- l'interface X11 ------------------------------------------------------

void peindre(Instance* moi) {
    if (moi->display == nullptr || moi->fenetre == 0 || !moi->visible) return;
    // FOND, PUIS UNE BARRE QUI SE DÉPLACE. Le fond seul prouverait
    // l'incrustation ; la barre prouve en plus que les minuteries arrivent,
    // ce qui est l'autre moitié de ce que l'hôte doit fournir.
    XSetForeground(moi->display, moi->contexte, 0x00202830);
    XFillRectangle(moi->display, moi->fenetre, moi->contexte, 0, 0,
                    static_cast<unsigned>(moi->largeur), static_cast<unsigned>(moi->hauteur));

    const int largeurBarre = 40;
    const int course = static_cast<int>(moi->largeur) - largeurBarre;
    const int x = course > 0 ? (moi->tic * 7) % course : 0;
    XSetForeground(moi->display, moi->contexte, 0x00e0a030);
    XFillRectangle(moi->display, moi->fenetre, moi->contexte, x,
                    static_cast<int>(moi->hauteur) / 2 - 12,
                    static_cast<unsigned>(largeurBarre), 24u);

    XSetForeground(moi->display, moi->contexte, 0x00ffffff);
    const std::string texte = "VSM Test GUI (CLAP) " + std::to_string(moi->largeur) + "x"
                              + std::to_string(moi->hauteur);
    XDrawString(moi->display, moi->fenetre, moi->contexte, 14, 28,
                 texte.c_str(), static_cast<int>(texte.size()));
    XFlush(moi->display);
}

bool guiIsApiSupported(const clap_plugin*, const char* api, bool isFloating) {
    // INCRUSTABLE SEULEMENT : c'est ce que l'hôte demande, et prétendre savoir
    // flotter obligerait à gérer une fenêtre que personne ne place.
    return !isFloating && api != nullptr && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}

bool guiGetPreferredApi(const clap_plugin*, const char** api, bool* isFloating) {
    if (api == nullptr || isFloating == nullptr) return false;
    *api = CLAP_WINDOW_API_X11;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin* p, const char* api, bool isFloating) {
    if (!guiIsApiSupported(p, api, isFloating)) return false;
    Instance* moi = self(p);
    if (moi->display != nullptr) return true;
    moi->display = XOpenDisplay(nullptr);
    if (moi->display == nullptr) return false;

    const int ecran = DefaultScreen(moi->display);
    // CRÉÉE SOUS LA RACINE, PUIS REPARENTÉE par `set_parent`. C'est l'ordre que
    // le protocole impose : la fenêtre doit exister avant qu'on sache où la
    // mettre.
    moi->fenetre = XCreateSimpleWindow(moi->display, RootWindow(moi->display, ecran), 0, 0,
                                        moi->largeur, moi->hauteur, 0,
                                        BlackPixel(moi->display, ecran),
                                        BlackPixel(moi->display, ecran));
    if (moi->fenetre == 0) { XCloseDisplay(moi->display); moi->display = nullptr; return false; }
    moi->contexte = XCreateGC(moi->display, moi->fenetre, 0, nullptr);
    XFlush(moi->display);

    // LA MINUTERIE EST DEMANDÉE À L'HÔTE, jamais fabriquée ici : un plugin qui
    // ouvrirait son propre fil pour se rafraîchir contournerait exactement ce
    // que cet essai doit éprouver.
    if (moi->host != nullptr && !moi->minuterieDemandee) {
        const auto* timers = static_cast<const clap_host_timer_support*>(
            moi->host->get_extension(moi->host, CLAP_EXT_TIMER_SUPPORT));
        if (timers != nullptr && timers->register_timer != nullptr)
            moi->minuterieDemandee = timers->register_timer(moi->host, 33, &moi->minuterie);
    }
    return true;
}

void guiDestroy(const clap_plugin* p) {
    Instance* moi = self(p);
    if (moi->display == nullptr) return;
    if (moi->minuterieDemandee && moi->host != nullptr) {
        const auto* timers = static_cast<const clap_host_timer_support*>(
            moi->host->get_extension(moi->host, CLAP_EXT_TIMER_SUPPORT));
        if (timers != nullptr && timers->unregister_timer != nullptr)
            timers->unregister_timer(moi->host, moi->minuterie);
        moi->minuterieDemandee = false;
    }
    if (moi->contexte != nullptr) { XFreeGC(moi->display, moi->contexte); moi->contexte = nullptr; }
    if (moi->fenetre != 0) { XDestroyWindow(moi->display, moi->fenetre); moi->fenetre = 0; }
    XCloseDisplay(moi->display);
    moi->display = nullptr;
    moi->visible = false;
}

bool guiSetScale(const clap_plugin*, double) { return false; }

bool guiGetSize(const clap_plugin* p, uint32_t* w, uint32_t* h) {
    if (w == nullptr || h == nullptr) return false;
    *w = self(p)->largeur;
    *h = self(p)->hauteur;
    return true;
}

bool guiCanResize(const clap_plugin*) { return true; }

bool guiGetResizeHints(const clap_plugin*, clap_gui_resize_hints* hints) {
    if (hints == nullptr) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;
    return true;
}

bool guiAdjustSize(const clap_plugin*, uint32_t* w, uint32_t* h) {
    if (w == nullptr || h == nullptr) return false;
    if (*w < 160) *w = 160;
    if (*h < 100) *h = 100;
    return true;
}

bool guiSetSize(const clap_plugin* p, uint32_t w, uint32_t h) {
    Instance* moi = self(p);
    moi->largeur = w;
    moi->hauteur = h;
    if (moi->display != nullptr && moi->fenetre != 0) {
        XResizeWindow(moi->display, moi->fenetre, w, h);
        peindre(moi);
    }
    return true;
}

bool guiSetParent(const clap_plugin* p, const clap_window* fenetre) {
    Instance* moi = self(p);
    if (moi->display == nullptr || moi->fenetre == 0 || fenetre == nullptr) return false;
    if (fenetre->api == nullptr || std::strcmp(fenetre->api, CLAP_WINDOW_API_X11) != 0) return false;
    XReparentWindow(moi->display, moi->fenetre, static_cast<Window>(fenetre->x11), 0, 0);
    XFlush(moi->display);
    return true;
}

bool guiSetTransient(const clap_plugin*, const clap_window*) { return false; }
void guiSuggestTitle(const clap_plugin*, const char*) {}

bool guiShow(const clap_plugin* p) {
    Instance* moi = self(p);
    if (moi->display == nullptr || moi->fenetre == 0) return false;
    XMapWindow(moi->display, moi->fenetre);
    XFlush(moi->display);
    moi->visible = true;
    peindre(moi);
    return true;
}

bool guiHide(const clap_plugin* p) {
    Instance* moi = self(p);
    if (moi->display == nullptr || moi->fenetre == 0) return false;
    XUnmapWindow(moi->display, moi->fenetre);
    XFlush(moi->display);
    moi->visible = false;
    return true;
}

const clap_plugin_gui kGui = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate,      guiDestroy,
    guiSetScale,       guiGetSize,         guiCanResize,   guiGetResizeHints,
    guiAdjustSize,     guiSetSize,         guiSetParent,   guiSetTransient,
    guiSuggestTitle,   guiShow,            guiHide,
};

// --- la minuterie ---------------------------------------------------------

void onTimer(const clap_plugin* p, clap_id id) {
    Instance* moi = self(p);
    if (id != moi->minuterie) return;
    ++moi->tic;
    peindre(moi);

    // AU BOUT DE DEUX SECONDES, ON DEMANDE À GRANDIR. C'est le seul moyen
    // d'exercer `request_resize` sans qu'un humain tire sur un coin.
    if (!moi->redimensionDemande && moi->tic == 60 && moi->host != nullptr) {
        const auto* gui = static_cast<const clap_host_gui*>(
            moi->host->get_extension(moi->host, CLAP_EXT_GUI));
        if (gui != nullptr && gui->request_resize != nullptr) {
            moi->redimensionDemande = true;
            gui->request_resize(moi->host, 480, 260);
        }
    }
}

const clap_plugin_timer_support kTimerSupport = {onTimer};

// --- le plugin ------------------------------------------------------------

bool pluginInit(const clap_plugin*) { return true; }

void pluginDestroy(const clap_plugin* p) {
    guiDestroy(p);
    delete self(p);
}

bool pluginActivate(const clap_plugin*, double, uint32_t, uint32_t) { return true; }
void pluginDeactivate(const clap_plugin*) {}
bool pluginStartProcessing(const clap_plugin*) { return true; }
void pluginStopProcessing(const clap_plugin*) {}
void pluginReset(const clap_plugin*) {}

clap_process_status pluginProcess(const clap_plugin*, const clap_process* process) {
    // DU SILENCE, ET C'EST VOLONTAIRE : cet essai porte sur l'interface, pas
    // sur le son. Rendre du bruit brouillerait un test d'écoute voisin sans
    // rien prouver de plus.
    if (process != nullptr && process->audio_outputs_count > 0
        && process->audio_outputs[0].data32 != nullptr) {
        for (uint32_t c = 0; c < process->audio_outputs[0].channel_count; ++c)
            if (process->audio_outputs[0].data32[c] != nullptr)
                std::memset(process->audio_outputs[0].data32[c], 0,
                             sizeof(float) * process->frames_count);
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin*, const char* id) {
    if (id == nullptr) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGui;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerSupport;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin*) {}

uint32_t factoryCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor* factoryGetDescriptor(const clap_plugin_factory*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin* factoryCreate(const clap_plugin_factory*, const clap_host* host, const char* id) {
    if (id == nullptr || std::strcmp(id, kPluginId) != 0) return nullptr;
    auto* instance = new Instance();
    instance->host = host;
    instance->plugin.desc = &kDescriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

const clap_plugin_factory kFactory = {factoryCount, factoryGetDescriptor, factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    CLAP_VERSION, entryInit, entryDeinit, entryGetFactory,
};
