#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ReconstructionChain.h"
#include <functional>

namespace vsm::app::ui {

/// LES PRÉFÉRENCES, RASSEMBLÉES (D10.3).
///
/// Elles existaient toutes, et elles étaient **éparpillées** : la taille de
/// l'interface dans *Affichage*, les threads de rendu et le dossier de la
/// chaîne d'analyse dans *Fichier*, les raccourcis et les associations MIDI
/// dans deux fenêtres qu'il fallait connaître. Un réglage qu'on ne retrouve
/// qu'en se souvenant du menu où il se cache est un réglage qu'on ne change
/// pas.
///
/// **CE PANNEAU NE DÉTIENT RIEN.** Chaque contrôle appelle l'application, qui
/// possède déjà le réglage et sait l'enregistrer — c'est la même règle que pour
/// le mixeur et la fenêtre des raccourcis. Dupliquer l'état ici créerait une
/// seconde vérité, et c'est toujours la seconde qui finit par mentir.
class PreferencesWindow : public juce::Component {
public:
    PreferencesWindow();

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Republie l'état affiché depuis l'application.
    /// LE PANNEAU REÇOIT L'ÉTAT, PAS LA PHRASE. Il recevait auparavant un
    /// texte déjà composé par l'application ; l'aperçu hors écran en composait
    /// donc un autre, et montrait une fenêtre qui n'existait pas -- le chemin
    /// de la chaîne y apparaissait deux fois là où l'application ne l'écrit
    /// qu'une. Une phrase construite à deux endroits finit toujours par
    /// diverger, et c'est le second qui ment.
    void refresh(float uiScale, int renderThreads, int recommendedThreads,
                  const vsm::interchange::ReconstructionChain& chain,
                  const juce::String& designatedChainFolder,
                  const juce::String& libraryFolder,
                  int shortcutCount, int midiMappingCount, bool returnToStartOnStop = false);

    std::function<void(float)> onUiScaleChanged;
    /// RETOUR AU DÉBUT À L'ARRÊT (D14.5) : la préférence de Cubase, le défaut
    /// de Live. Stop ramène la tête là où la lecture était partie.
    std::function<void(bool)> onReturnToStartChanged;
    /// -1 = automatique.
    std::function<void(int)> onRenderThreadsChanged;
    std::function<void()> onChooseChainFolder;
    /// La bibliothèque du navigateur (D10.1) : le dossier où l'utilisateur
    /// range ses presets, ses profils et ses échantillons.
    std::function<void()> onChooseLibraryFolder;
    std::function<void()> onOpenShortcuts;
    std::function<void()> onOpenMidiLearn;

private:
    juce::Label titreAffichage_, titreAudio_, titreChaine_, titreCommandes_;
    juce::Label libelleEchelle_, libelleThreads_, libelleChaine_, etatChaine_;
    juce::ComboBox echelle_, threads_;
    juce::Label libelleRetour_;
    juce::ToggleButton retourAuDepart_ { juce::String::fromUTF8(u8"Revenir au point de départ") };
    juce::TextButton choisirChaine_ { u8"Choisir le dossier..." };
    juce::Label titreBibliotheque_, libelleBibliotheque_;
    juce::TextButton choisirBibliotheque_ { u8"Choisir le dossier..." };
    juce::TextButton raccourcis_, associations_;
};

} // namespace vsm::app::ui
