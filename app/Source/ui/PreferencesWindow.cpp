#include "PreferencesWindow.h"
#include "UiScale.h"
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
    titre(titreAffichage_, juce::String::fromUTF8(u8"Affichage"));
    titre(titreAudio_, juce::String::fromUTF8(u8"Audio"));
    titre(titreChaine_, juce::String::fromUTF8(u8"Chaîne d'analyse"));
    titre(titreCommandes_, juce::String::fromUTF8(u8"Commandes"));
    titre(titreBibliotheque_, juce::String::fromUTF8(u8"Bibliothèque (navigateur)"));
    for (auto* e : {&titreAffichage_, &titreAudio_, &titreChaine_, &titreCommandes_,
                     &titreBibliotheque_})
        addAndMakeVisible(*e);

    ligne(libelleEchelle_, juce::String::fromUTF8(u8"Taille de l'interface"));
    ligne(libelleThreads_, juce::String::fromUTF8(u8"Threads de rendu"));
    ligne(libelleRetour_, juce::String::fromUTF8(u8"À l'arrêt"));
    addAndMakeVisible(libelleRetour_);
    retourAuDepart_.onClick = [this] { if (onReturnToStartChanged) onReturnToStartChanged(retourAuDepart_.getToggleState()); };
    addAndMakeVisible(retourAuDepart_);
    // LE MÉTRONOME (D16.6). Le niveau est un GAIN LINÉAIRE, montré en pour
    // cent : un métronome ne se règle pas en décibels, on le veut plus fort ou
    // moins fort, et la crête suit le chiffre exactement.
    ligne(libelleClic_, juce::String::fromUTF8(u8"Niveau du clic"));
    addAndMakeVisible(libelleClic_);
    niveauClic_.setRange(0.0, 1.0, 0.01);
    niveauClic_.setTextValueSuffix("");
    niveauClic_.setNumDecimalPlacesToDisplay(2);
    niveauClic_.onValueChange = [this] {
        if (onMetronomeLevelChanged) onMetronomeLevelChanged(static_cast<float>(niveauClic_.getValue()));
    };
    addAndMakeVisible(niveauClic_);
    ligne(libelleQuandClic_, juce::String::fromUTF8(u8"Le clic bat"));
    addAndMakeVisible(libelleQuandClic_);
    clicDecompteSeul_.onClick = [this] {
        if (onMetronomeCountInOnlyChanged) onMetronomeCountInOnlyChanged(clicDecompteSeul_.getToggleState());
    };
    clicEnregistrementSeul_.onClick = [this] {
        if (onMetronomeRecordOnlyChanged) onMetronomeRecordOnlyChanged(clicEnregistrementSeul_.getToggleState());
    };
    addAndMakeVisible(clicDecompteSeul_);
    addAndMakeVisible(clicEnregistrementSeul_);

    ligne(libelleChaine_, juce::String::fromUTF8(u8"Dossier"));
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
    threads_.addItem(juce::String::fromUTF8(u8"Automatique"), 1);
    const int maximum = std::min<int>(
        static_cast<int>(vsm::audio::engine::RenderThreadPool::kMaxWorkers),
        std::max(1, static_cast<int>(std::thread::hardware_concurrency())) - 1);
    for (int n = 0; n <= maximum; ++n)
        threads_.addItem(n == 0 ? juce::String::fromUTF8(u8"Mono-cœur")
                                 : juce::String(n) + juce::String::fromUTF8(u8" thread(s)"),
                          n + 2);
    threads_.onChange = [this] {
        if (onRenderThreadsChanged)
            onRenderThreadsChanged(threads_.getSelectedId() == 1 ? -1 : threads_.getSelectedId() - 2);
    };
    addAndMakeVisible(threads_);

    choisirChaine_.onClick = [this] { if (onChooseChainFolder) onChooseChainFolder(); };
    addAndMakeVisible(choisirChaine_);

    ligne(libelleBibliotheque_, juce::String::fromUTF8(u8"Dossier"));
    addAndMakeVisible(libelleBibliotheque_);
    choisirBibliotheque_.onClick = [this] { if (onChooseLibraryFolder) onChooseLibraryFolder(); };
    addAndMakeVisible(choisirBibliotheque_);

    raccourcis_.onClick = [this] { if (onOpenShortcuts) onOpenShortcuts(); };
    associations_.onClick = [this] { if (onOpenMidiLearn) onOpenMidiLearn(); };
    addAndMakeVisible(raccourcis_);
    addAndMakeVisible(associations_);
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
                                 bool metronomeRecordOnly) {
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
    threads_.changeItemText(1, juce::String::fromUTF8(u8"Automatique (")
                                   + juce::String(recommendedThreads)
                                   + juce::String::fromUTF8(u8" ici)"));
    threads_.setSelectedId(renderThreads < 0 ? 1 : renderThreads + 2, juce::dontSendNotification);

    // LE DOSSIER ET SON ÉTAT SONT DEUX CHOSES DIFFÉRENTES : un chemin qui
    // existe et une chaîne qui marche ne se confondent pas, et c'est
    // précisément la distinction que D9.1 a coûté du code à établir.
    libelleChaine_.setText(designatedChainFolder.isEmpty()
                                ? juce::String::fromUTF8(u8"Dossier (trouvé automatiquement)")
                                : designatedChainFolder,
                            juce::dontSendNotification);

    juce::String etat;
    if (chain.available) {
        // LE CHEMIN EST DÉJÀ SUR LA LIGNE DU DESSUS quand l'utilisateur l'a
        // désigné : le répéter en dessous ne dit rien de plus et fait croire à
        // deux dossiers. Quand il a été TROUVÉ, en revanche, il faut le
        // montrer -- c'est la seule façon de savoir lequel a été retenu.
        etat = designatedChainFolder.isNotEmpty()
                   ? juce::String::fromUTF8(u8"Prête.")
                   : juce::String::fromUTF8(u8"Prête — trouvée dans ")
                         + juce::String::fromUTF8(chain.chainFolder.c_str());
    } else {
        etat = juce::String::fromUTF8(chain.reason.c_str());
        if (!chain.remedy.empty())
            etat += juce::String("\n") + juce::String::fromUTF8(chain.remedy.c_str());
    }
    etatChaine_.setText(etat, juce::dontSendNotification);

    libelleBibliotheque_.setText(
        libraryFolder.isEmpty()
            ? juce::String::fromUTF8(u8"Aucune — seul le projet ouvert est indexé")
            : libraryFolder,
        juce::dontSendNotification);

    raccourcis_.setButtonText(juce::String::fromUTF8(u8"Raccourcis clavier (")
                                  + juce::String(shortcutCount) + ")...");
    associations_.setButtonText(juce::String::fromUTF8(u8"Associations MIDI (")
                                    + juce::String(midiMappingCount) + ")...");
}

} // namespace vsm::app::ui
