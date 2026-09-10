#include "PreferencesWindow.h"
#include "UiScale.h"
#include "Langue.h"
#include "vsm/audio/engine/RenderThreadPool.h"

namespace vsm::app::ui {

namespace {
void titre(juce::Label& etiquette, const juce::String& texte) {
    etiquette.setText(texte, juce::dontSendNotification);
    etiquette.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    etiquette.setColour(juce::Label::textColourId, juce::Colours::skyblue);
}
void ligne(juce::Label& etiquette, const juce::String& texte) {
    etiquette.setText(texte, juce::dontSendNotification);
    etiquette.setFont(juce::Font(juce::FontOptions(15.0f)));
}
} // namespace

PreferencesWindow::PreferencesWindow() {
    titre(titreAffichage_, juce::String());
    titre(titreAudio_, juce::String());
    titre(titreChaine_, juce::String());
    titre(titreCommandes_, juce::String());
    titre(titreBibliotheque_, juce::String());
    for (auto* e : {&titreAffichage_, &titreAudio_, &titreChaine_, &titreCommandes_,
                     &titreBibliotheque_})
        addAndMakeVisible(*e);

    ligne(libelleEchelle_, juce::String());
    ligne(libelleThreads_, juce::String());
    ligne(libelleRetour_, juce::String());
    addAndMakeVisible(libelleRetour_);
    retourAuDepart_.onClick = [this] { if (onReturnToStartChanged) onReturnToStartChanged(retourAuDepart_.getToggleState()); };
    addAndMakeVisible(retourAuDepart_);
    // LE MÉTRONOME (D16.6). Le niveau est un GAIN LINÉAIRE, montré en pour
    // cent : un métronome ne se règle pas en décibels, on le veut plus fort ou
    // moins fort, et la crête suit le chiffre exactement.
    ligne(libelleClic_, juce::String());
    addAndMakeVisible(libelleClic_);
    niveauClic_.setRange(0.0, 1.0, 0.01);
    niveauClic_.setTextValueSuffix("");
    niveauClic_.setNumDecimalPlacesToDisplay(2);
    niveauClic_.onValueChange = [this] {
        if (onMetronomeLevelChanged) onMetronomeLevelChanged(static_cast<float>(niveauClic_.getValue()));
    };
    addAndMakeVisible(niveauClic_);
    ligne(libelleQuandClic_, juce::String());
    addAndMakeVisible(libelleQuandClic_);
    clicDecompteSeul_.onClick = [this] {
        if (onMetronomeCountInOnlyChanged) onMetronomeCountInOnlyChanged(clicDecompteSeul_.getToggleState());
    };
    clicEnregistrementSeul_.onClick = [this] {
        if (onMetronomeRecordOnlyChanged) onMetronomeRecordOnlyChanged(clicEnregistrementSeul_.getToggleState());
    };
    addAndMakeVisible(clicDecompteSeul_);
    addAndMakeVisible(clicEnregistrementSeul_);

    // D17.2 : le suivi de l'automation. Dans « Audio » plutôt qu'ailleurs :
    // c'est un réglage de montage, comme le retour au départ juste au-dessus.
    ligne(libelleSuiviAutomation_, juce::String());
    addAndMakeVisible(libelleSuiviAutomation_);
    automationSuit_.onClick = [this] {
        if (onAutomationFollowsClipsChanged) onAutomationFollowsClipsChanged(automationSuit_.getToggleState());
    };
    addAndMakeVisible(automationSuit_);

    ligne(libelleChaine_, juce::String());
    ligne(etatChaine_, "");
    for (auto* e : {&libelleEchelle_, &libelleThreads_, &libelleChaine_, &etatChaine_})
        addAndMakeVisible(*e);

    const auto& paliers = UiScale::steps();
    for (int i = 0; i < paliers.size(); ++i)
        echelle_.addItem(UiScale::label(paliers[i]), i + 1);
    echelle_.onChange = [this] {
        const int index = echelle_.getSelectedId() - 1;
        if (index >= 0 && index < UiScale::steps().size() && onUiScaleChanged)
            onUiScaleChanged(UiScale::steps()[index]);
    };
    addAndMakeVisible(echelle_);

    // « Automatique » d'abord : c'est le réglage juste sur presque toutes les
    // machines, et le seul qui suive celle sur laquelle on ouvre le projet.
    threads_.addItem(tr(u8"Automatique"), 1);
    const int maximum = std::min<int>(
        static_cast<int>(vsm::audio::engine::RenderThreadPool::kMaxWorkers),
        std::max(1, static_cast<int>(std::thread::hardware_concurrency())) - 1);
    for (int n = 0; n <= maximum; ++n)
        threads_.addItem(n == 0 ? tr(u8"Mono-cœur") : tr(u8"%1 thread(s)").replace("%1", juce::String(n)),
                          n + 2);
    threads_.onChange = [this] {
        if (onRenderThreadsChanged)
            onRenderThreadsChanged(threads_.getSelectedId() == 1 ? -1 : threads_.getSelectedId() - 2);
    };
    addAndMakeVisible(threads_);

    choisirChaine_.onClick = [this] { if (onChooseChainFolder) onChooseChainFolder(); };
    addAndMakeVisible(choisirChaine_);

    ligne(libelleBibliotheque_, juce::String());
    addAndMakeVisible(libelleBibliotheque_);
    choisirBibliotheque_.onClick = [this] { if (onChooseLibraryFolder) onChooseLibraryFolder(); };
    addAndMakeVisible(choisirBibliotheque_);

    raccourcis_.onClick = [this] { if (onOpenShortcuts) onOpenShortcuts(); };
    associations_.onClick = [this] { if (onOpenMidiLearn) onOpenMidiLearn(); };
    addAndMakeVisible(raccourcis_);
    addAndMakeVisible(associations_);
    retraduire();   // D85 : les textes fixes, en un seul endroit
}

void PreferencesWindow::retraduire() {
    // D85 : TOUT CE QUE LA FENÊTRE ÉCRIT UNE FOIS. Rien n'y passait par la table :
    // la capture en anglais et la capture en français étaient la même image.
    // Les textes qui dépendent d'un état (le nombre de threads, l'état de la
    // chaîne, les compteurs) sont refaits par `refresh`, que le client rappelle.
    titreAffichage_.setText(tr(u8"Affichage"), juce::dontSendNotification);
    titreAudio_.setText(tr(u8"Audio"), juce::dontSendNotification);
    titreChaine_.setText(tr(u8"Chaîne d'analyse"), juce::dontSendNotification);
    titreCommandes_.setText(tr(u8"Commandes"), juce::dontSendNotification);
    titreBibliotheque_.setText(tr(u8"Bibliothèque (navigateur)"), juce::dontSendNotification);
    libelleEchelle_.setText(tr(u8"Taille de l'interface"), juce::dontSendNotification);
    libelleThreads_.setText(tr(u8"Threads de rendu"), juce::dontSendNotification);
    libelleRetour_.setText(tr(u8"À l'arrêt"), juce::dontSendNotification);
    libelleClic_.setText(tr(u8"Niveau du clic"), juce::dontSendNotification);
    libelleQuandClic_.setText(tr(u8"Le clic bat"), juce::dontSendNotification);
    libelleSuiviAutomation_.setText(tr(u8"En déplaçant"), juce::dontSendNotification);
    libelleChaine_.setText(tr(u8"Dossier"), juce::dontSendNotification);
    libelleBibliotheque_.setText(tr(u8"Dossier"), juce::dontSendNotification);
    automationSuit_.setButtonText(tr(u8"L'automation suit les clips"));
    clicDecompteSeul_.setButtonText(tr(u8"Seulement au décompte"));
    clicEnregistrementSeul_.setButtonText(tr(u8"Seulement à l'enregistrement"));
    retourAuDepart_.setButtonText(tr(u8"Revenir au point de départ"));
    choisirChaine_.setButtonText(tr(u8"Choisir le dossier..."));
    choisirBibliotheque_.setButtonText(tr(u8"Choisir le dossier..."));
    // Les entrées « Mono-cœur » / « N thread(s) » : la sélection se lit AVANT de
    // renommer (JUCE rend 0 pour une entrée choisie renommée, D78).
    const int choisie = threads_.getSelectedId();
    for (int i = 1; i < threads_.getNumItems(); ++i) {
        const int n = threads_.getItemId(i) - 2;
        threads_.changeItemText(n + 2, n == 0 ? tr(u8"Mono-cœur") : tr(u8"%1 thread(s)").replace("%1", juce::String(n)));
    }
    if (choisie > 1) threads_.setSelectedId(choisie, juce::dontSendNotification);
    repaint();
}

void PreferencesWindow::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff23262b)); }

void PreferencesWindow::resized() {
    auto zone = getLocalBounds().reduced(14);
    auto rangee = [&zone](int hauteur) { return zone.removeFromTop(hauteur); };
    auto paire = [](juce::Rectangle<int> r, juce::Component& gauche, juce::Component& droite) {
        auto d = r;
        droite.setBounds(d.removeFromRight(240).reduced(0, 2));
        d.removeFromRight(10);
        gauche.setBounds(d);
    };

    titreAffichage_.setBounds(rangee(26));
    paire(rangee(30), libelleEchelle_, echelle_);
    rangee(12);
    titreAudio_.setBounds(rangee(26));
    paire(rangee(30), libelleThreads_, threads_);
    paire(rangee(30), libelleRetour_, retourAuDepart_);
    paire(rangee(30), libelleSuiviAutomation_, automationSuit_);
    paire(rangee(30), libelleClic_, niveauClic_);
    paire(rangee(28), libelleQuandClic_, clicDecompteSeul_);
    // La seconde case n'a pas de libellé à gauche : elle prolonge la première,
    // et répéter « Le clic bat » ferait croire à deux réglages sans rapport.
    {
        auto r = rangee(28);
        clicEnregistrementSeul_.setBounds(r.removeFromRight(240).reduced(0, 2));
    }
    rangee(12);
    titreChaine_.setBounds(rangee(26));
    paire(rangee(30), libelleChaine_, choisirChaine_);
    etatChaine_.setBounds(rangee(42));
    rangee(12);
    titreBibliotheque_.setBounds(rangee(26));
    paire(rangee(30), libelleBibliotheque_, choisirBibliotheque_);
    rangee(12);
    titreCommandes_.setBounds(rangee(26));
    auto boutons = rangee(32);
    raccourcis_.setBounds(boutons.removeFromLeft(boutons.getWidth() / 2).reduced(2));
    associations_.setBounds(boutons.reduced(2));
}

void PreferencesWindow::refresh(float uiScale, int renderThreads, int recommendedThreads,
                                 const vsm::interchange::ReconstructionChain& chain,
                                 const juce::String& designatedChainFolder,
                                 const juce::String& libraryFolder,
                                 int shortcutCount, int midiMappingCount, bool returnToStartOnStop,
                                 float metronomeLevel, bool metronomeCountInOnly,
                                 bool metronomeRecordOnly, bool automationFollowsClips) {
    automationSuit_.setToggleState(automationFollowsClips, juce::dontSendNotification);
    retourAuDepart_.setToggleState(returnToStartOnStop, juce::dontSendNotification);
    niveauClic_.setValue(metronomeLevel, juce::dontSendNotification);
    clicDecompteSeul_.setToggleState(metronomeCountInOnly, juce::dontSendNotification);
    clicEnregistrementSeul_.setToggleState(metronomeRecordOnly, juce::dontSendNotification);
    const auto& paliers = UiScale::steps();
    for (int i = 0; i < paliers.size(); ++i)
        if (std::abs(paliers[i] - uiScale) < 1.0e-3f)
            echelle_.setSelectedId(i + 1, juce::dontSendNotification);

    // « AUTOMATIQUE » DOIT DIRE CE QU'IL VAUT ICI. Le menu Fichier l'écrivait
    // déjà ; cette fenêtre recevait le chiffre et le jetait, si bien qu'on
    // pouvait choisir « Automatique » sans jamais apprendre à combien de
    // threads cela revient sur cette machine -- alors que c'est la seule
    // question qu'on se pose en regardant ce réglage.
    threads_.changeItemText(1, tr(u8"Automatique (%1 ici)").replace("%1", juce::String(recommendedThreads)));
    threads_.setSelectedId(renderThreads < 0 ? 1 : renderThreads + 2, juce::dontSendNotification);

    // LE DOSSIER ET SON ÉTAT SONT DEUX CHOSES DIFFÉRENTES : un chemin qui
    // existe et une chaîne qui marche ne se confondent pas, et c'est
    // précisément la distinction que D9.1 a coûté du code à établir.
    libelleChaine_.setText(designatedChainFolder.isEmpty()
                                ? tr(u8"Dossier (trouvé automatiquement)")
                                : designatedChainFolder,
                            juce::dontSendNotification);

    juce::String etat;
    if (chain.available) {
        // LE CHEMIN EST DÉJÀ SUR LA LIGNE DU DESSUS quand l'utilisateur l'a
        // désigné : le répéter en dessous ne dit rien de plus et fait croire à
        // deux dossiers. Quand il a été TROUVÉ, en revanche, il faut le
        // montrer -- c'est la seule façon de savoir lequel a été retenu.
        etat = designatedChainFolder.isNotEmpty()
                   ? tr(u8"Prête.")
                   : tr(u8"Prête — trouvée dans %1").replace("%1", juce::String::fromUTF8(chain.chainFolder.c_str()));
    } else {
        etat = juce::String::fromUTF8(chain.reason.c_str());
        if (!chain.remedy.empty())
            etat += juce::String("\n") + juce::String::fromUTF8(chain.remedy.c_str());
    }
    etatChaine_.setText(etat, juce::dontSendNotification);

    libelleBibliotheque_.setText(
        libraryFolder.isEmpty()
            ? tr(u8"Aucune — seul le projet ouvert est indexé")
            : libraryFolder,
        juce::dontSendNotification);

    raccourcis_.setButtonText(tr(u8"Raccourcis clavier (%1)...").replace("%1", juce::String(shortcutCount)));
    associations_.setButtonText(tr(u8"Associations MIDI (%1)...").replace("%1", juce::String(midiMappingCount)));
}

} // namespace vsm::app::ui
