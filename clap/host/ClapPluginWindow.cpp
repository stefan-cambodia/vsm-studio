#include "ClapPluginWindow.h"
#include "ClapHostInternals.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace vsm::clap {

namespace {

/// L'API DE FENÊTRAGE QU'ON DEMANDE, ET IL N'Y EN A QU'UNE ICI. CLAP en
/// nomme trois (`win32`, `cocoa`, `x11`) ; sous Linux, JUCE dessine dans X11 —
/// y compris sous Wayland, à travers XWayland. Demander autre chose serait
/// demander ce qu'on ne saurait pas recevoir.
constexpr const char* kApiFenetre = CLAP_WINDOW_API_X11;

const clap_plugin_gui* guiOf(const clap_plugin* plugin) {
    if (plugin == nullptr) return nullptr;
    return static_cast<const clap_plugin_gui*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
}

/// LA FAÇADE : un composant JUCE dont la fenêtre native accueille celle du
/// plugin.
///
/// L'ORDRE DES APPELS N'EST PAS NÉGOCIABLE, et c'est tout ce que cette classe
/// a de délicat. Le protocole de `clap/ext/gui.h` demande : `create`, puis
/// `get_size`, puis `set_parent`, puis `show`. Notamment, **`set_parent` exige
/// une fenêtre native qui existe déjà** — donc un composant JUCE qui a un
/// « peer ». Un composant qui n'est pas encore ajouté à une fenêtre n'en a pas,
/// et lui demander son identifiant X11 rendrait zéro : le plugin
/// s'incrusterait alors dans la racine de l'écran, c'est-à-dire nulle part et
/// partout. D'où l'incrustation dans `parentHierarchyChanged`, et non dans le
/// constructeur.
class ClapEditor final : public juce::Component, private juce::Timer {
public:
    ClapEditor(const clap_plugin* plugin, const clap_plugin_gui* gui, HostBridge* pont)
        : plugin_(plugin), gui_(gui), pont_(pont) {
        uint32_t largeur = 400, hauteur = 300;
        if (gui_->get_size && gui_->get_size(plugin_, &largeur, &hauteur))
            setSize(static_cast<int>(largeur), static_cast<int>(hauteur));
        else
            setSize(400, 300);

        redimensionnable_ = gui_->can_resize && gui_->can_resize(plugin_);

        if (pont_ != nullptr) {
            // CE QUE LE PLUGIN PEUT NOUS DEMANDER TANT QUE LA FENÊTRE EST LÀ.
            // Installés ici, retirés dans le destructeur : un plugin qui
            // demande à être redimensionné alors que plus rien ne le montre
            // demande dans le vide, et c'est la bonne réponse.
            pont_->onRequestResize = [this](uint32_t l, uint32_t h) {
                // Le plugin peut appeler depuis n'importe quel thread ; JUCE
                // exige le thread des messages pour toucher un composant.
                juce::MessageManager::callAsync([pointeur = juce::Component::SafePointer<ClapEditor>(this),
                                                  l, h] {
                    if (pointeur != nullptr)
                        pointeur->setSize(static_cast<int>(l), static_cast<int>(h));
                });
            };
            pont_->onClosed = [this] {
                juce::MessageManager::callAsync(
                    [pointeur = juce::Component::SafePointer<ClapEditor>(this)] {
                        if (pointeur != nullptr && pointeur->onPluginClosed)
                            pointeur->onPluginClosed();
                    });
            };
        }
        // LES MINUTERIES DU PLUGIN BATTENT ICI. Beaucoup d'interfaces ne
        // dessinent rien sans elles : elles s'attendent à être appelées pour
        // se rafraîchir, et un hôte muet donne une fenêtre figée qui ressemble
        // à un plugin cassé.
        minuteries_ = static_cast<const clap_plugin_timer_support*>(
            plugin_->get_extension(plugin_, CLAP_EXT_TIMER_SUPPORT));
        if (minuteries_ != nullptr) startTimer(16);
    }

    ~ClapEditor() override {
        stopTimer();
        if (pont_ != nullptr) { pont_->onRequestResize = nullptr; pont_->onClosed = nullptr; }
        if (gui_ != nullptr && plugin_ != nullptr) {
            if (gui_->hide) gui_->hide(plugin_);
            if (gui_->destroy) gui_->destroy(plugin_);
        }
    }

    bool isResizable() const { return redimensionnable_; }
    std::function<void()> onPluginClosed;

    void parentHierarchyChanged() override {
        if (incruste_) return;
        auto* peer = getPeer();
        if (peer == nullptr) return;
        void* natif = peer->getNativeHandle();
        if (natif == nullptr) return;

        clap_window fenetre{};
        fenetre.api = kApiFenetre;
        // SOUS X11, LE « HANDLE » DE JUCE EST L'IDENTIFIANT DE FENÊTRE lui-même,
        // rangé dans un pointeur. Le déréférencer donnerait n'importe quoi ; on
        // le reconvertit en entier, ce que CLAP attend.
        fenetre.x11 = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(natif));
        if (gui_->set_parent == nullptr || !gui_->set_parent(plugin_, &fenetre)) return;

        incruste_ = true;
        if (gui_->set_size && redimensionnable_)
            gui_->set_size(plugin_, static_cast<uint32_t>(getWidth()),
                            static_cast<uint32_t>(getHeight()));
        if (gui_->show) gui_->show(plugin_);
    }

    void resized() override {
        // ON DIT AU PLUGIN CE QU'ON LUI DONNE. Sans cet appel, une fenêtre
        // agrandie à la souris laisserait l'interface dans son coin, à sa
        // taille d'origine, sur un fond vide.
        if (incruste_ && redimensionnable_ && gui_->set_size)
            gui_->set_size(plugin_, static_cast<uint32_t>(std::max(1, getWidth())),
                            static_cast<uint32_t>(std::max(1, getHeight())));
    }

    void paint(juce::Graphics& g) override {
        // CE QU'ON DESSINE EST CE QU'ON VOIT QUAND LE PLUGIN NE DESSINE PAS :
        // un fond noir, et rien d'autre. Le plugin peint dans SA fenêtre, qui
        // est posée par-dessus celle-ci.
        g.fillAll(juce::Colours::black);
    }

private:
    void timerCallback() override {
        if (minuteries_ == nullptr || minuteries_->on_timer == nullptr || pont_ == nullptr) return;
        const uint32_t maintenant = static_cast<uint32_t>(juce::Time::getMillisecondCounter());
        // Une copie : `on_timer` peut faire enregistrer ou retirer une
        // minuterie, donc modifier la liste qu'on est en train de parcourir.
        const auto liste = pont_->timers;
        for (const auto& minuterie : liste) {
            auto& dernier = dernierAppel_[minuterie.id];
            if (maintenant - dernier < minuterie.periodMs) continue;
            dernier = maintenant;
            minuteries_->on_timer(plugin_, minuterie.id);
        }
    }

    const clap_plugin* plugin_ = nullptr;
    const clap_plugin_gui* gui_ = nullptr;
    HostBridge* pont_ = nullptr;
    const clap_plugin_timer_support* minuteries_ = nullptr;
    std::map<clap_id, uint32_t> dernierAppel_;
    bool incruste_ = false;
    bool redimensionnable_ = false;
};

/// Fabrique commune aux instruments et aux effets : le protocole est le même,
/// seule la façon d'atteindre le plugin change.
std::unique_ptr<juce::Component> fabriquer(const HostedPlugin& hote) {
    const clap_plugin_gui* gui = guiOf(hote.plugin);
    if (gui == nullptr || gui->is_api_supported == nullptr || gui->create == nullptr)
        return nullptr;
    // `false` = INCRUSTÉE. Une fenêtre flottante que l'application ne place
    // pas, ne redimensionne pas et ne ferme pas n'est pas une façade.
    if (!gui->is_api_supported(hote.plugin, kApiFenetre, false)) return nullptr;
    if (!gui->create(hote.plugin, kApiFenetre, false)) return nullptr;
    return std::make_unique<ClapEditor>(hote.plugin, gui, hote.bridge);
}

} // namespace

std::unique_ptr<juce::Component> createEditorFor(vsm::audio::plugin::ISynthPlugin& instrument) {
    return fabriquer(hostedPluginOf(instrument));
}
std::unique_ptr<juce::Component> createEditorFor(vsm::audio::effect::IAudioEffect& effect) {
    return fabriquer(hostedPluginOf(effect));
}

} // namespace vsm::clap
