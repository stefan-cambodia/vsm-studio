#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/Project.h"
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Ligne représentant une piste. Volontairement "bête" : elle lit/écrit
// directement les champs de vsm::sequencer::Track qu'on lui passe, et
// notifie le parent des changements via des callbacks — aucune logique de
// mixage réelle ici (ça viendra avec le Mixer / AudioEngine en Phase 2).
class TrackRowComponent : public juce::Component {
public:
    /// `groupes` donne, pour chaque piste de groupe du projet, son index et son
    /// nom : c'est ce que le sélecteur de sortie propose. Passé de l'extérieur
    /// parce qu'une ligne ne connaît que SA piste -- lui donner le projet
    /// entier pour lire la liste des groupes serait lui donner de quoi tout
    /// modifier.
    /// `sourceName` : le nom de la piste dont celle-ci publie une sortie
    /// (D18.7b), vide sinon. Passé plutôt que déduit, pour la même raison que
    /// `groupes` -- la rangée n'a pas besoin du projet entier pour dire ce
    /// qu'elle porte.
    TrackRowComponent(vsm::sequencer::Track& track, size_t trackIndex,
                       const std::vector<std::pair<int, std::string>>& groupes,
                       const juce::String& sourceName = {});

    void paint(juce::Graphics&) override;
    /// D30.2 : le voile de la piste désactivée, PAR-DESSUS ses enfants.
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    /// D38.1 : LES MODIFICATEURS SONT TRANSMIS, ils étaient jetés. La rangée
    /// ne décide pas ce qu'ils veulent dire -- elle ne connaît pas ses
    /// voisines, et « étendre depuis la piste active » demande de les connaître
    /// toutes. Elle dit ce qui a été cliqué et avec quoi ; la liste tranche.
    void mouseDown(const juce::MouseEvent& e) override {
        if (onSelectedWithMods) onSelectedWithMods(index_, e.mods);
        else if (onSelected) onSelected(index_);
    }

    std::function<void(size_t)> onSelected;
    /// D38.1 : cliqué, et avec quels modificateurs (Ctrl : ajouter/retirer,
    /// Maj : étendre).
    std::function<void(size_t, juce::ModifierKeys)> onSelectedWithMods;
    /// D36.1 : UN GESTE VA COMMENCER À MODIFIER LA PISTE.
    ///
    /// Émis AVANT l'écriture, jamais après : `SnapshotHistory` mémorise l'état
    /// d'AVANT, et un signal émis après ferait photographier la modification
    /// elle-même -- l'annulation rendrait alors le projet déjà modifié, ce qui
    /// ne se distingue pas, à l'écran, d'une annulation qui ne marche pas.
    ///
    /// La ligne ne connaît toujours ni l'historique ni le projet (c'est la
    /// décision de son en-tête) : elle dit qu'elle va écrire, et `MainComponent`
    /// décide ce que cela veut dire. Le libellé est celui du menu Édition.
    std::function<void(const juce::String& label)> onEditStarted;
    /// D37.1 : LA PISTE VIENT D'ÊTRE RENOMMÉE. Séparé de `onChanged`, qui part
    /// aussi à chaque pixel d'un glissé de fader : le nom s'affiche dans sept
    /// panneaux dont trois le rangent dans un widget, et les reconstruire
    /// trois cents fois pour un mouvement de curseur serait insupportable.
    std::function<void()> onRenamed;
    std::function<void()> onChanged; // mute/solo/volume/pan modifiés -> reconstruire le scheduler
    /// L'armement a changé. SÉPARÉ de `onChanged` : armer ne touche ni au
    /// planning de lecture ni au mixage, et republier le projet au moteur pour
    /// un bouton d'armement couperait le son à chaque clic.
    std::function<void()> onArmChanged;
    /// La sortie de la piste a changé (master ou groupe) : le moteur doit
    /// republier le projet pour que le routage prenne effet.
    std::function<void()> onOutputChanged;
    std::function<void(size_t, const std::string&)> onInstrumentChanged; // trackIndex, pluginId ("" = aucun)

    /// D38.2 : LA RANGÉE NE TAIT PLUS ELLE-MÊME. Elle dit qu'on a cliqué son
    /// M ou son S ; c'est la LISTE qui applique le geste, parce qu'elle seule
    /// connaît la sélection. La rangée reste « bête », comme son en-tête
    /// l'annonce depuis toujours.
    std::function<void(size_t)> onGesteMuet;
    std::function<void(size_t)> onGesteSolo;
    /// D38.3 — CE QUI NE SE MULTIPLIE PAS, ET POURQUOI. Quatre gestes de cette
    /// rangée restent sur SA piste, et n'ont donc pas de jumeau ci-dessus :
    ///
    ///  - **renommer** : six pistes du même nom ne se distinguent plus, et le
    ///    nom est justement ce qui les distingue. Cubase numérote les copies ;
    ///    inventer des numéros à la place de l'utilisateur serait décider pour
    ///    lui de ce qu'il allait taper.
    ///  - **le canal MIDI** : mettre six pistes sur le même canal les fait
    ///    jouer l'une par-dessus l'autre sur le même instrument matériel. Le
    ///    geste a l'air d'un réglage et fait une fusion.
    ///  - **la machine** : le rack, la chaîne d'effets et l'automation
    ///    n'éditent qu'UNE piste (c'est la décision qui a rendu D38 petite) ;
    ///    en changer six laisserait cinq machines réglées que rien ne montre.
    ///  - **l'armement** : D3.3 l'interdit déjà -- une seule piste audio armée
    ///    à la fois, sans quoi une prise s'écrirait dans plusieurs fichiers.
    ///
    /// La règle générale : un geste se multiplie quand il pose la MÊME valeur
    /// sur toutes les pistes sans les rendre indistinctes. Un nom, un canal et
    /// une machine ne remplissent pas cette condition.
    /// Pose l'état muet de CETTE piste (sans historique : la liste l'a déjà
    /// ouvert pour tout le lot) et met son bouton d'accord.
    void poserMuet(bool muet);
    void poserSolo(bool solo);
    /// D37.1 : renomme la piste par le chemin du champ de nom (le libellé
    /// change, donc `onTextChange` part, donc le pas d'historique aussi).
    void renommer(const juce::String& nom);
    /// D42.3 : choisit une machine PAR LE CHEMIN DU SÉLECTEUR (le même
    /// `onChange` que le clic), pour que ce qu'on photographie soit ce que
    /// l'utilisateur obtient. Rend false si l'identifiant n'est pas au parc.
    bool choisirMachine(const juce::String& pluginId);
    /// D37.2 : règle le volume par le chemin du curseur.
    void reglerVolume(float valeur);
    /// D110 : armer la piste comme le bouton R le fait -- son état basculé, puis
    /// son `onClick`, TOUT DE SUITE. `triggerClick()` passe par la file des
    /// messages : le geste agissait APRÈS la touche F9 qui le suivait, et le
    /// bouton Rec était encore gris (premier témoin de D110, 0 boîte sur 4).
    void armerPourCapture() {
        armButton_.setToggleState(!armButton_.getToggleState(), juce::dontSendNotification);
        if (armButton_.onClick) armButton_.onClick();
    }

    void setSelected(bool selected) { selected_ = selected; repaint(); }
    /// Réaffiche le fichier de la piste (sans effet sur une piste MIDI).
    /// Appelée après une prise audio, qui vient de lui en donner un.
    void refreshAudioSource();
    /// D51 : LA FRÉQUENCE DU FICHIER, quand elle n'est pas celle de la
    /// session. Le chargeur mesure le rééchantillonnage depuis D2 et le porte
    /// dans son résultat ; personne, dans l'application, ne le lisait -- seul
    /// le rendu hors ligne en faisait un avertissement. Un fichier
    /// rééchantillonné n'est plus le fichier qu'on a posé, et c'est une
    /// propriété PERMANENTE de cette piste à cette fréquence de session : elle
    /// s'écrit donc sur la ligne, à côté du nom de fichier qu'elle concerne,
    /// et non dans une boîte à fermer (la règle de D43).
    /// `fileRate <= 0` ou `fileRate == sessionRate` efface la mention.
    ///
    /// `streamed` dit que le matériau est DIFFUSÉ depuis le disque au lieu
    /// d'être résident (D8.2) -- l'autre champ que le chargeur remplissait et
    /// que personne ne lisait. Ce n'est pas un détail d'implémentation pour qui
    /// se demande pourquoi son projet tient en mémoire, ni pour qui vient de
    /// débrancher le disque où vit le fichier.
    void setAudioSourceRate(double fileRate, double sessionRate, bool streamed = false,
                             size_t residentBytes = 0);
    /// D24.5 : relit le nom de la piste (une ligne créée avant qu'on la nomme).
    void refreshName();
    void retraduire();   // D83
    /// D36.7 : relit le muet et le solo. Le muet et le solo s'affichent à DEUX
    /// endroits -- ici et dans la tranche du mélangeur --, et chacun posait son
    /// bouton une seule fois, à sa construction : rendre une piste muette dans
    /// l'un laissait l'autre montrer le contraire, indéfiniment.
    void refreshMuteSolo();
    /// D37 : relit le volume et le panoramique (l'autre sens de l'accord avec
    /// la tranche du mélangeur). Le nom a son propre chemin : il se relit par
    /// `refreshName`, qui existait déjà.
    void refreshMix();

private:
    /// Dit qu'un geste va écrire dans la piste. Un seul chemin, pour qu'un
    /// geste ajouté plus tard n'ait pas à se souvenir de deux choses.
    void debutEdition(const juce::String& libelle);
    /// D94 : les infobulles et les mentions que la ligne écrit une fois, dans
    /// la langue courante -- à la construction, puis à chaque bascule.
    void poserTextes();

    vsm::sequencer::Track& track_;
    size_t index_;
    juce::String sourceName_;   ///< D94 : gardé pour refaire « sortie n° … de … »
    bool selected_ = false;
    /// Figé à la construction : la nature d'une piste ne change pas en cours de
    /// route, et la ligne est reconstruite si le projet change.
    const bool audio_;

    juce::Label nameLabel_;
    juce::Label channelLabel_;
    juce::ComboBox instrumentBox_; // rempli depuis PluginRegistry::listAvailable()
    /// D103 : (identifiant, nom enregistré) des entrées de `instrumentBox_`, dans
    /// leur ordre -- de quoi reposer leurs noms au changement de langue.
    std::vector<std::pair<std::string, std::string>> instruments_;
    juce::Label audioSourceLabel_; // à sa place, sur une piste audio
    /// D51 : fréquence du FICHIER et fréquence de la SESSION, telles que le
    /// chargeur les a mesurées. Zéro = rien à dire.
    double fileSampleRate_ = 0.0;
    double sessionSampleRate_ = 0.0;
    bool audioStreamed_ = false;
    size_t audioResidentBytes_ = 0;
    juce::ComboBox outputBox_;     // master ou groupe (D4.2)
    juce::TextButton muteButton_ { "M" };
    juce::TextButton soloButton_ { "S" };
    /// D19.4 : le repli d'un DOSSIER. Présent sur les seules pistes dossier,
    /// et c'est lui qui range ou déploie tout ce qu'elles contiennent.
    juce::TextButton folderButton_ { "" };
    juce::TextButton armButton_  { "R" };
    juce::Slider volumeSlider_;
    juce::Slider panSlider_;
    /// Un glissé est EN COURS : ses `onValueChange` suivants ne rouvrent pas
    /// de pas d'historique. Un seul drapeau pour la ligne : on ne glisse
    /// qu'un curseur à la fois.
    bool glisseEnCours_ = false;
};

/// Liste verticale de pistes (Track Editor, section 4). Reconstruit ses
/// lignes à partir du Project quand loadProject() est appelé (ex : après
/// un import MIDI).
class TrackListComponent : public juce::Component,
                            public juce::DragAndDropTarget {
public:
    TrackListComponent();

    void loadProject(vsm::sequencer::Project& project);
    void resized() override;
    /// D73 : repose les libellés écrits une fois à la construction, après un
    /// changement de langue.
    void retraduire();

    void paint(juce::Graphics&) override;

    std::function<void(size_t)> onTrackSelected;
    /// D36.1 : une ligne va modifier sa piste (voir
    /// `TrackRowComponent::onEditStarted`). La liste ne fait que transmettre :
    /// elle ne sait pas plus que la ligne ce qu'est un historique.
    std::function<void(const juce::String& label)> onEditStarted;
    std::function<void()> onTracksChanged;
    /// D37.1 : une piste a été renommée (voir `TrackRowComponent::onRenamed`).
    std::function<void()> onRenamed;
    /// D39.3 : LA SÉLECTION A CHANGÉ. Les autres panneaux qui dessinent des
    /// pistes en ont besoin pour la montrer -- la liste n'était pas seule à
    /// devoir la connaître, elle était seule à la connaître.
    std::function<void()> onSelectionChanged;
    std::function<void(size_t, const std::string&)> onInstrumentChanged;
    /// L'armement d'une piste a changé (voir TrackRowComponent::onArmChanged).
    std::function<void()> onArmChanged;
    /// La sortie d'une piste a changé (voir TrackRowComponent::onOutputChanged).
    std::function<void()> onOutputChanged;
    std::function<void()> onAddTrack;          // bouton "+ Ajouter une piste"
    std::function<void(size_t)> onRemoveTrack; // bouton "Supprimer" (piste sélectionnée)

    /// D10.1 : QUELQUE CHOSE A ÉTÉ LÂCHÉ SUR UNE PISTE. La description vient du
    /// navigateur (`BrowserComponent`) ; la liste ne l'interprète pas, elle dit
    /// seulement SUR QUELLE PISTE. Lui faire charger un preset la rendrait
    /// dépendante de l'interop, et une liste de pistes n'a pas à savoir ce
    /// qu'est un `*.synth.json`.
    std::function<void(size_t, const juce::String&)> onBrowserItemDropped;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    /// LA PISTE ACTIVE : celle qu'éditent le piano roll, le rack, la chaîne
    /// d'effets et l'onglet MIDI CC. Son sens n'a pas changé en D38, et c'est
    /// la décision qui a rendu cette phase petite : soixante-deux appels
    /// restent justes mot pour mot.
    size_t selectedTrackIndex() const { return selectedIndex_; }
    /// D38.1 : LA SÉLECTION, qui contient toujours la piste active. Seuls les
    /// gestes qui ont un sens sur plusieurs pistes la consultent -- taire,
    /// colorer, masquer, supprimer. Éditer des notes n'en a pas.
    const std::set<size_t>& selectedTracks() const { return selection_; }
    /// D38.1 : pose la sélection (l'index actif y est toujours ajouté).
    void setSelectedTracks(std::set<size_t> tracks, size_t active);
    /// D39.2 : ÉTEND la sélection d'une piste vers le bas (+1) ou le haut (-1),
    /// depuis l'ancre -- exactement comme le Maj+clic de D38.1, et pour la même
    /// raison : étendre depuis le résultat de l'extension précédente ferait
    /// grandir la sélection à chaque touche au lieu de la redessiner.
    /// Les pistes MASQUÉES sont sautées, comme elles le sont pour la navigation
    /// simple (D17.4) : on ne choisit pas ce qu'on ne voit pas.
    void etendreSelection(int delta);
    /// D39.2 : toutes les pistes visibles.
    void choisirToutesLesPistes();
    /// D39.3 : un clic sur une piste, modificateurs compris -- d'où qu'il
    /// vienne. La liste des pistes l'appelle depuis ses rangées, l'arrangement
    /// depuis ses en-têtes : un seul calcul de sélection, deux endroits d'où
    /// l'on clique.
    void cliquerSurLaPiste(size_t index, juce::ModifierKeys mods) { cliqueSurLaLigne(index, mods); }
    /// D38.4 : appelée AVANT un geste multipliable venu de la ligne `index`.
    /// Si cette piste n'est pas dans la sélection, la sélection devient elle
    /// seule -- agir sur des pistes qu'on ne regarde pas est le pire des
    /// défauts de ce genre de fonction. Rend la sélection à employer.
    const std::set<size_t>& selectionPourUnGesteSur(size_t index);
    /// Bascule le muet de la piste `index` PAR LE CHEMIN DU BOUTON M : la
    /// sélection est consultée (D38.4), un seul pas d'historique est ouvert
    /// pour le lot, et toutes les pistes choisies prennent le même état --
    /// celui de la piste cliquée, renversé. « Toutes prennent le même » plutôt
    /// que « chacune se renverse » : sur six pistes dont deux muettes, se
    /// renverser chacune en laisserait quatre muettes et deux non, ce qui ne
    /// ressemble à aucune intention.
    void basculerMuet(size_t index);
    void basculerSolo(size_t index);
    /// D37 : les deux autres gestes qu'aucun menu ne porte.
    void renommer(size_t index, const juce::String& nom);
    bool choisirMachine(size_t index, const juce::String& pluginId);
    /// D110 : le bouton R de la ligne `index`, par son clic (geste de banc « armer »).
    void armer(size_t index);
    void reglerVolume(size_t index, float valeur);
    /// D36.7 : relit le muet et le solo de toutes les lignes.
    void refreshMuteSolo();
    /// D37 : toutes les lignes relisent nom, volume et panoramique.
    void refreshFromTracks();
    /// D37.2 : toutes les lignes relisent le volume et le panoramique SEULS.
    /// Séparé de `refreshFromTracks` parce qu'un glissé de fader l'appelle à
    /// chaque pixel : relire aussi les noms y serait du travail pour rien.
    void refreshMix();

    /// Sélectionne une piste par index (met à jour l'état visuel et notifie
    /// via onTrackSelected). Sans effet si l'index est hors bornes.
    void selectTrackIndex(size_t idx);
    /// Fait défiler la liste juste assez pour montrer la piste `idx` entière.
    void faireVoirLaPiste(size_t idx);
    int aMontrer_ = -1;   ///< piste à faire voir dès que la liste aura une hauteur

    /// Réaffiche une seule ligne, sans reconstruire la liste -- reconstruire
    /// remettrait la sélection et le défilement à zéro. Sert après une prise
    /// audio, qui vient de donner un fichier à sa piste.
    void refreshTrackRow(size_t idx);
    /// D51 : pose sur la ligne `idx` la fréquence du fichier et celle de la
    /// session. Sans effet si la ligne n'existe pas ou n'est pas audio.
    void setAudioSourceRate(size_t idx, double fileRate, double sessionRate,
                             bool streamed = false, size_t residentBytes = 0);

private:
    vsm::sequencer::Project* project_ = nullptr;
    juce::OwnedArray<TrackRowComponent> rows_;
    juce::Viewport viewport_;
    juce::Component rowContainer_;
    juce::TextButton addButton_ { "+ Ajouter une piste" };
    juce::TextButton removeButton_ { "Supprimer" };
    /// D19.2 : LE FILTRE DE LA LISTE. Un état de SÉANCE et non du morceau —
    /// il n'est écrit nulle part, et rouvrir un projet ne cache jamais une
    /// piste. C'est précisément ce qui le distingue de `Track::hidden`
    /// (D17.4), lequel appartient au morceau et se sauvegarde.
    juce::TextEditor filterBox_;
    /// D19.2 : PANNE MUETTE INTERDITE, jusque dans une liste vide. Un filtre
    /// qui ne trouve rien laisse un panneau vierge, et un panneau vierge
    /// ressemble à des pistes supprimées. Il dit donc pourquoi il est vide.
    juce::Label emptyLabel_;
    /// Vrai quand le filtre est posé et que le nom de la piste ne lui répond
    /// pas. N'a AUCUN effet sur le son : la piste continue de jouer, elle
    /// n'est simplement plus dans la liste — masquer une piste et la taire
    /// sont deux gestes différents, et les confondre ferait disparaître un
    /// instrument d'un mélange pour avoir cherché son voisin.
    bool masqueeParLeFiltre(size_t index) const;
public:
    /// D19.2 : pose le filtre sans souris, pour que la capture d'écran puisse
    /// le MONTRER À L'ŒUVRE et pas seulement montrer un champ vide. Même
    /// raison d'être que `VSM_VUE` : sous Wayland, une interface qu'on ne peut
    /// pas piloter sans souris est une interface qu'on ne peut pas juger.
    void setFilterText(const juce::String& texte) {
        filterBox_.setText(texte, juce::dontSendNotification);
        resized();
        repaint();
    }
private:
    size_t selectedIndex_ = 0;
    /// D38.1 : la sélection. Contient TOUJOURS `selectedIndex_` -- une
    /// sélection vide et une piste active seraient deux vérités sur la même
    /// chose, et c'est toujours la seconde qui ment.
    std::set<size_t> selection_ { 0 };
    /// D38.1 : d'où Maj+clic étend. C'est la dernière piste désignée par un
    /// clic SIMPLE, et non la piste active : étendre depuis le résultat de
    /// l'extension précédente ferait grandir la sélection à chaque Maj+clic.
    size_t ancreSelection_ = 0;
    /// La piste survolée pendant un glisser, ou -1. Sans ce retour, on lâche à
    /// l'aveugle et on découvre après coup sur laquelle.
    int dropRow_ = -1;
    /// L'index de piste sous un point de la liste, ou -1.
    int trackIndexAt(juce::Point<int> position) const;
    /// D38.1 : un clic sur une ligne, modificateurs compris.
    void cliqueSurLaLigne(size_t index, juce::ModifierKeys mods);
    /// D38.1 : chaque ligne se dessine choisie ou non, d'après `selection_`.
    void rafraichirDessinDeLaSelection();

    static constexpr int kRowHeight = 88;
    static constexpr int kToolbarHeight = 36;
    /// D19.2 : la ligne du filtre, sous la barre d'outils.
    static constexpr int kFilterHeight = 30;
};
