#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <memory>
#include <vector>

// Éditeur de chaîne d'effets d'insert (section 5 : TRACK -> SYNTH -> EFFECTS
// -> MIX). Dernière pièce UI de la Phase 2. Pour la piste active :
//  - "Ajouter" liste les effets de EffectFactory ;
//  - la chaîne courante s'affiche (sélection, monter/descendre, supprimer) ;
//  - les paramètres de l'effet sélectionné sont éditables (un knob par
//    entrée de parameterList(), comme le Synth Rack -- zéro code par effet).
//
// Les effets sont prepare()és sur le thread UI AVANT publication ; la chaîne
// est ensuite publiée atomiquement au moteur via onChainChanged (RT-safe).
//
// LA PISTE EST LA SOURCE DE VÉRITÉ, PAS CE COMPOSANT. Chaque geste écrit la
// DESCRIPTION de la chaîne dans `Track::effects` (type + valeurs), et les
// instances vivantes ci-dessous n'en sont que le reflet, reconstruit à la
// demande. Trois défauts disparaissent du même coup :
//
//  1. les chaînes vivaient dans une `std::map<int, Chain>` interne à ce
//     composant, donc n'étaient JAMAIS sauvegardées ;
//  2. cette table était indexée par numéro de piste : supprimer une piste
//     décalait les suivantes et réaffectait les effets aux mauvaises pistes,
//     en silence ;
//  3. rien ne les reposait sur le graphe d'export, qui rendait donc un fichier
//     sans les effets qu'on venait d'entendre.
//
// La règle qui les tient tous les trois : ce composant ne conserve aucun état
// que le projet ne porte pas.
class EffectChainComponent : public juce::Component {
public:
    using EffectPtr = std::shared_ptr<vsm::audio::effect::IAudioEffect>;
    using Chain = std::vector<EffectPtr>;

    EffectChainComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Le projet dont les pistes portent les chaînes. Sans lui, le composant
    /// est inerte : il n'a plus d'état à lui.
    void setProject(vsm::sequencer::Project* project);

    /// La configuration audio RÉELLE du périphérique. Les effets sont
    /// re-prepare()és et republiés si elle change -- c'est ce qui manquait
    /// quand 48 kHz était écrit en dur : sur une carte à 44,1 kHz, tous les
    /// temps de delay et de reverb étaient faux de 8,8 %.
    void setAudioConfig(double sampleRate, int blockSize);

    /// Refabrique toutes les instances vivantes à partir des descriptions du
    /// projet, et les publie. À appeler après un chargement, un ajout ou une
    /// suppression de piste.
    void rebuildFromProject();

    void setActiveTrack(int trackIndex);

    /// Prévenu AVANT chaque geste qui modifie la chaîne : c'est là que
    /// l'application prend son instantané d'annulation. Le libellé nomme le
    /// geste dans le menu Édition.
    std::function<void(const juce::String& label)> onEditStarted;

    /// Publie la chaîne (immuable) de la piste au moteur.
    std::function<void(size_t trackIndex, std::shared_ptr<const Chain>)> onChainChanged;

    /// D7.3 : LAISSE L'UTILISATEUR DÉSIGNER UN EFFET QU'ON N'A PAS ÉCRIT, et
    /// rappelle avec son identifiant (`clap:...` ou `vst3:...`).
    ///
    /// UN RAPPEL PLUTÔT QU'UN APPEL DIRECT, pour deux raisons. Le choix d'un
    /// fichier est ASYNCHRONE dans JUCE, donc il ne peut pas rendre une valeur.
    /// Et cette vue ne doit rien savoir de CLAP ni de VST3 : elle demande « un
    /// identifiant d'effet », l'application sait où les trouver. Non renseigné
    /// quand aucune couche d'hébergement n'est compilée -- l'entrée de menu
    /// n'apparaît alors pas, plutôt que d'apparaître et de ne rien faire.
    ///
    /// UN ACCESSEUR ET NON UN CHAMP PUBLIC : le menu est construit une fois, et
    /// il doit apprendre l'entrée supplémentaire AU MOMENT où le rappel arrive
    /// -- c'est-à-dire après le constructeur. Un champ nu se serait posé trop
    /// tard, et l'entrée n'aurait jamais paru.
    void setPluginEffectChooser(std::function<void(std::function<void(std::string)>)> chooser);

    /// Ajoute un effet par son identifiant de fabrique, interne ou tiers.
    /// Rend faux si la fabrique ne sait pas le construire -- l'appelant le DIT,
    /// il ne le remplace jamais par un autre effet.
    bool addEffectById(const std::string& effectId);

private:
    void rebuildEffectList();
    void rebuildParamControls();
    void publishChain(size_t trackIndex);
    void publishActiveChain();
    Chain* activeChain();
    /// La description de la piste active, dans le projet. C'est elle qu'on
    /// modifie ; la chaîne vivante suit.
    std::vector<vsm::sequencer::TrackEffect>* activeDescription();
    /// Fabrique une chaîne vivante à partir d'une description, prête à publier.
    Chain buildChain(const std::vector<vsm::sequencer::TrackEffect>& described) const;

    std::function<void(std::function<void(std::string)>)> pluginEffectChooser_;
    /// L'identifiant de menu de l'entrée « un plugin », ou 0 tant qu'elle
    /// n'existe pas.
    int idMenuPlugin_ = 0;
    /// Le prochain identifiant de menu libre, calculé au constructeur.
    int prochainIdMenu_ = 1;

    vsm::sequencer::Project* project_ = nullptr;
    int activeTrack_ = -1;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int selectedEffect_ = -1;

    /// Instances vivantes, alignées piste par piste sur `project_->tracks`.
    /// Reconstruites en bloc à chaque changement de structure : jamais
    /// d'entrée survivant à la piste qui l'a créée.
    std::vector<Chain> chains_;

    juce::Label titleLabel_;
    juce::Label addLabel_;
    juce::ComboBox addBox_;

    // Rangées d'effets (nom + monter/descendre/supprimer).
    struct EffectRow {
        std::unique_ptr<juce::TextButton> select;
        std::unique_ptr<juce::TextButton> up;
        std::unique_ptr<juce::TextButton> down;
        std::unique_ptr<juce::TextButton> remove;
    };
    std::vector<EffectRow> rows_;

    // Contrôles de paramètres de l'effet sélectionné.
    struct ParamControl {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<ParamControl> params_;
    juce::Label paramHeader_;
};
