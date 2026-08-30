#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/BrowserIndex.h"
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// LE NAVIGATEUR (D10.1) : « trouver un preset ne demande plus d'ouvrir un
/// dossier ».
///
/// Ce que l'application savait faire, c'était CHARGER un preset, un profil, un
/// échantillon — chacun par un sélecteur de fichiers, c'est-à-dire à condition
/// de savoir déjà où il était. Trente-quatre machines, autant de presets par
/// projet, des profils et des dossiers d'échantillons : la matière existait, et
/// le seul moyen d'y accéder était de s'en souvenir.
///
/// **IL S'OUVRE PLEIN, PAS VIDE.** Un navigateur qui demande une recherche
/// avant de montrer quoi que ce soit suppose qu'on sait ce qu'on cherche —
/// alors qu'on l'ouvre justement pour voir ce qu'il y a.
///
/// **DEUX GESTES, ET LE SECOND EXISTE PARCE QUE LE PREMIER MENT UN PEU.** Un
/// double-clic applique à la piste sélectionnée : c'est le geste court, et il
/// suppose qu'on a la bonne piste en tête. Un glisser dépose sur la piste qu'on
/// VOIT, ce qui ne suppose rien.
class BrowserComponent : public juce::Component,
                          public juce::DragAndDropContainer {
public:
    BrowserComponent();
    // Défini dans le .cpp : `Liste` y est complète, et `unique_ptr` a besoin
    // de sa taille pour la détruire.
    ~BrowserComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Remplace l'inventaire (au changement de projet, ou de bibliothèque).
    void setItems(std::vector<vsm::interchange::BrowserItem> items);
    /// Ce que la liste montre en ce moment, pour l'application.
    const std::vector<vsm::interchange::BrowserItem>& visibleItems() const { return filtres_; }

    /// L'utilisateur veut appliquer une entrée à la piste sélectionnée.
    std::function<void(const vsm::interchange::BrowserItem&)> onApply;

    /// La description d'une entrée pour le glisser-déposer. Publique parce que
    /// la liste des pistes doit la relire : deux écritures différentes du même
    /// message finiraient par se contredire.
    static juce::String dragDescriptionFor(const vsm::interchange::BrowserItem& item);
    /// L'inverse. Renvoie faux si la description ne vient pas d'ici.
    static bool parseDragDescription(const juce::String& description,
                                      vsm::interchange::BrowserItemKind& kind,
                                      juce::String& reference);

private:
    class Liste;
    void refilter();

    juce::TextEditor recherche_;
    juce::Label compte_;
    std::unique_ptr<Liste> liste_;
    juce::Viewport defilement_;
    std::vector<vsm::interchange::BrowserItem> tous_, filtres_;
};

} // namespace vsm::app::ui
