#include "ImportReportComponent.h"

namespace vsm::app::ui {
namespace P = vsm::ui::Palette;

namespace {

// Généreux à dessein : ce texte se LIT, il ne se survole pas. L'échelle
// d'interface (ui/UiScale.h) multiplie ensuite tout par 1,5 par défaut.
constexpr int kHauteurDeLigne = 22;
constexpr int kCorpsDeLigne = 15;
constexpr int kMarge = 18;
constexpr int kHauteurEnTete = 62;
constexpr int kHauteurPied = 46;

juce::Font policeDeLigne(bool titre = false) {
    return juce::Font(juce::FontOptions(float(kCorpsDeLigne + (titre ? 2 : 0))));
}

// LA COULEUR DIT L'IMPORTANCE, et c'est la seule mise en forme du panneau.
// Dans une liste uniforme, la ligne qui compte se noie au milieu des lignes de
// comptage -- or c'est précisément elle qu'on écrit le rapport pour faire lire.
// Les mots repérés ici sont ceux que les lecteurs emploient déjà (voir
// interchange/src/DawImport.cpp) : « ATTENTION » pour ce qui est deviné plutôt
// que lu, « AUCUN instrument » et « NON importée » pour ce qui manque à
// l'arrivée.
juce::Colour couleurDeLaLigne(const juce::String& texte) {
    if (texte.contains("ATTENTION")) return P::accentRed;
    // LE TOTAL D'ABORD, avant la règle sur « ignorée » : la ligne « Total : 2
    // piste(s) MIDI, 7 note(s), 1 piste(s) audio ignorée(s) » contient le mot
    // et se peignait en ambre, ce qui la faisait lire comme un avertissement
    // alors qu'elle est le récapitulatif. Une capture d'écran l'a montrée.
    if (texte.startsWith("Total")) return P::textPrimary;
    if (texte.contains("AUCUN instrument") || texte.contains("NON import")
        || texte.contains("ignor"))
        return P::accentAmber;
    return P::textSecondary;
}

} // namespace

// --- La liste qui défile ----------------------------------------------------

void ImportReportComponent::Liste::paint(juce::Graphics& g) {
    for (int i = 0; i < lignes_.size(); ++i) {
        const auto& ligne = lignes_.getReference(i);
        if (ligne.texte.isEmpty()) continue;
        g.setColour(ligne.couleur);
        g.setFont(policeDeLigne(ligne.titre));
        g.drawText(ligne.texte, 0, i * kHauteurDeLigne, getWidth(), kHauteurDeLigne,
                   juce::Justification::centredLeft, false);
    }
}

// --- Le panneau -------------------------------------------------------------

ImportReportComponent::ImportReportComponent() {
    addAndMakeVisible(vue_);
    vue_.setViewedComponent(&liste_, false);
    vue_.setScrollBarsShown(true, false);
    // La barre de défilement dans la PALETTE : sans cela elle arrive au bleu
    // par défaut de JUCE, la seule tache froide d'une interface entièrement
    // chaude -- et l'aperçu hors écran, qui n'installe pas le LookAndFeel de
    // l'application, l'a montrée sans détour.
    vue_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                          P::border.brighter(0.35f));
    vue_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, P::panel);

    fermer_.setButtonText(juce::String::fromUTF8("Fermer"));
    addAndMakeVisible(fermer_);
    fermer_.onClick = [this] {
        setVisible(false);
        if (onClose) onClose();
    };

    // COPIER : un rapport d'import sert souvent à être montré à quelqu'un
    // d'autre -- sur un forum, dans un message -- quand l'import ne donne pas
    // ce qu'on attendait. Le retaper depuis une capture d'écran serait absurde.
    copier_.setButtonText(juce::String::fromUTF8("Copier"));
    addAndMakeVisible(copier_);
    copier_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(reportText()); };

    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
    setVisible(false);
}

void ImportReportComponent::showReport(const vsm::interchange::DawImportReport& rapport) {
    titre_ = juce::String::fromUTF8("Rapport d'import");
    // LE FORMAT ET LA VERSION, sans les redire deux fois : Live annonce
    // « Ableton Live 11.3.4 », qui contient déjà le nom du format, et
    // « Ableton Live — Ableton Live 11.3.4 » ne renseignait personne.
    const auto format = juce::String::fromUTF8(rapport.sourceFormat.c_str());
    const auto version = juce::String::fromUTF8(rapport.sourceVersion.c_str());
    if (version.isEmpty())          sousTitre_ = format;
    else if (version.contains(format)) sousTitre_ = version;
    else                            sousTitre_ = format + juce::String::fromUTF8(" \xe2\x80\x94 ") + version;

    source_.clear();

    // LE RÉSUMÉ D'ABORD. Sans lui il faudrait compter les lignes pour savoir si
    // l'import a rapporté quelque chose ; c'est la première question qu'on se
    // pose, elle mérite la première ligne.
    juce::String resume;
    // « lu(s) » pour les clips, et le mot compte : `clipsSeen` dénombre ce que
    // le FICHIER contenait, pas ce que le projet a reçu. Les trois nombres
    // alignés sans distinction se liraient comme trois résultats, et un clip vu
    // mais non repris passerait pour un clip repris.
    resume << rapport.midiTracksImported << juce::String::fromUTF8(" piste(s) reprise(s), ")
           << rapport.notesImported << juce::String::fromUTF8(" note(s), ")
           << rapport.clipsSeen << juce::String::fromUTF8(" clip(s) lu(s)");
    source_.add({resume, P::textPrimary, true});

    if (rapport.audioTracksSeen > 0) {
        juce::String audio;
        audio << rapport.audioTracksSeen
              << juce::String::fromUTF8(" piste(s) audio vue(s), NON import\xc3\xa9""e(s)");
        source_.add({audio, P::accentAmber, false});
    }
    if (rapport.tracksWithoutInstrument > 0) {
        juce::String sansInstrument;
        sansInstrument << rapport.tracksWithoutInstrument
                       << juce::String::fromUTF8(" piste(s) sans instrument assign\xc3\xa9");
        source_.add({sansInstrument, P::accentAmber, false});
    }
    if (rapport.eventsRead > 0) {
        // LE GARDE-FOU DES FORMATS BINAIRES (§ 3 bis du CDC), remonté à
        // l'écran. Le sens des identifiants d'un `.flp` est reconstitué, pas
        // garanti : ce « compris sur lus » est ce qui permet au musicien de
        // voir si la lecture a mordu, sans avoir à nous croire sur parole.
        juce::String compte;
        compte << rapport.eventsUnderstood
               << juce::String::fromUTF8(" \xc3\xa9v\xc3\xa9nement(s) compris sur ")
               << rapport.eventsRead << juce::String::fromUTF8(" lus");
        source_.add({compte, P::textSecondary, false});
    }
    source_.add({{}, P::textSecondary, false});

    for (const auto& ligne : rapport.lines) {
        const auto texte = juce::String::fromUTF8(ligne.c_str());
        source_.add({texte, couleurDeLaLigne(texte), false});
    }

    // `resized()` et non `reconstruireLaListe()` : la hauteur du cadre suit le
    // nombre de lignes, et ce nombre vient de changer.
    resized();
    setVisible(true);
    toFront(false);
    // LE CLAVIER, pour qu'Échap ferme vraiment : sans cette prise de focus, la
    // touche part au panneau qui l'avait avant, et le rapport ne se fermerait
    // qu'à la souris.
    grabKeyboardFocus();
}

void ImportReportComponent::showFailure(const juce::String& titre, const juce::String& message) {
    titre_ = titre;
    sousTitre_ = {};
    source_.clear();
    // Le message est repris EN ENTIER : celui d'un `.cpr` nomme les deux
    // chemins praticables (Track Archive XML, MIDI Type 1), et c'est tout son
    // intérêt. Le repli à la largeur du cadre s'occupe de le rendre lisible.
    source_.add({message, P::accentRed, false});

    // `resized()` et non `reconstruireLaListe()` : la hauteur du cadre suit le
    // nombre de lignes, et ce nombre vient de changer.
    resized();
    setVisible(true);
    toFront(false);
    // LE CLAVIER, pour qu'Échap ferme vraiment : sans cette prise de focus, la
    // touche part au panneau qui l'avait avant, et le rapport ne se fermerait
    // qu'à la souris.
    grabKeyboardFocus();
}

void ImportReportComponent::reopen() {
    if (source_.isEmpty()) return;
    // `resized()` et non `reconstruireLaListe()` : la hauteur du cadre suit le
    // nombre de lignes, et ce nombre vient de changer.
    resized();
    setVisible(true);
    toFront(false);
    // LE CLAVIER, pour qu'Échap ferme vraiment : sans cette prise de focus, la
    // touche part au panneau qui l'avait avant, et le rapport ne se fermerait
    // qu'à la souris.
    grabKeyboardFocus();
}

juce::String ImportReportComponent::reportText() const {
    juce::String texte;
    texte << titre_ << "\n";
    if (sousTitre_.isNotEmpty()) texte << sousTitre_ << "\n";
    texte << "\n";
    for (const auto& ligne : source_) texte << ligne.texte << "\n";
    return texte;
}

juce::Rectangle<int> ImportReportComponent::cadre() const {
    // Un cadre centré, large mais pas plein écran : le voile qui l'entoure dit
    // que le reste de la fenêtre attend, sans le cacher.
    //
    // LA HAUTEUR SUIT LE CONTENU, et c'est ce que la première capture a
    // réclamé : un message de trois lignes dans un cadre de six cents pixels
    // laissait quatre cents pixels de vide sous le texte, ce qui donne à lire
    // « il manque quelque chose » là où il ne manque rien. La largeur, elle,
    // ne dépend PAS du contenu -- c'est elle qui décide du repli des lignes,
    // donc de leur nombre : la faire dépendre du nombre de lignes bouclerait.
    auto zone = getLocalBounds();
    const int largeur = juce::jmin(juce::jmax(240, zone.getWidth() - 40), 880);
    const int hauteurMax = juce::jmin(juce::jmax(160, zone.getHeight() - 40), 620);
    // Le compte doit être JUSTE, pas approché : huit pixels de moins et le
    // Viewport se croit trop petit, sort une barre de défilement pour un
    // contenu qui tient, et le panneau a l'air de cacher quelque chose. Les
    // deux termes qui suivent la hauteur des lignes sont exactement ce que
    // `disposer()` retranche (le `reduced(kMarge, 4)` du Viewport) plus une
    // ligne d'air.
    const int voulue = kHauteurEnTete + kHauteurPied + 8 + kHauteurDeLigne / 2
                     + juce::jmax(1, lignes_.size()) * kHauteurDeLigne;
    const int hauteur = juce::jlimit(juce::jmin(160, hauteurMax), hauteurMax, voulue);
    return juce::Rectangle<int>(largeur, hauteur).withCentre(zone.getCentre());
}

void ImportReportComponent::disposer() {
    auto zone = cadre();
    zone.removeFromTop(kHauteurEnTete);
    auto pied = zone.removeFromBottom(kHauteurPied).reduced(kMarge, 8);
    fermer_.setBounds(pied.removeFromRight(110));
    pied.removeFromRight(8);
    copier_.setBounds(pied.removeFromRight(110));
    vue_.setBounds(zone.reduced(kMarge, 4));
}

void ImportReportComponent::envelopper(const Ligne& source, int largeurMax) {
    if (source.texte.isEmpty()) {
        lignes_.add({{}, source.couleur, false});
        return;
    }
    const auto police = policeDeLigne(source.titre);
    // Le RETRAIT des suites : sans lui, une explication repliée sur trois
    // lignes se lit comme trois faits distincts.
    const juce::String retrait = juce::String::fromUTF8("    ");

    juce::StringArray mots;
    mots.addTokens(source.texte, " ", "");
    juce::String courante;
    bool premiere = true;
    for (const auto& mot : mots) {
        const juce::String essai =
            courante.isEmpty() ? (premiere ? mot : retrait + mot) : courante + " " + mot;
        if (!courante.isEmpty()
            && juce::GlyphArrangement::getStringWidth(police, essai) > float(largeurMax)) {
            lignes_.add({courante, source.couleur, source.titre && premiere});
            premiere = false;
            courante = retrait + mot;
        } else {
            courante = essai;
        }
    }
    if (courante.isNotEmpty()) lignes_.add({courante, source.couleur, source.titre && premiere});
}

void ImportReportComponent::reconstruireLaListe() {
    // La largeur de repli est celle qu'on a VRAIMENT, barre de défilement
    // déduite -- pas une largeur devinée qui laisserait la fin des phrases
    // hors du cadre.
    const int largeurMax = juce::jmax(200, vue_.getMaximumVisibleWidth() - 8);

    lignes_.clear();
    for (const auto& ligne : source_) envelopper(ligne, largeurMax);

    liste_.setSize(largeurMax, juce::jmax(1, lignes_.size()) * kHauteurDeLigne);
    liste_.repaint();
    repaint();
}

void ImportReportComponent::paint(juce::Graphics& g) {
    // Le voile : le rapport arrête le regard sans arrêter l'application. Rien
    // n'est modal ici -- un DAW dont la boucle de messages s'arrête est un DAW
    // dont l'audio hoquette.
    g.fillAll(juce::Colour(0xc0000000));

    auto zone = cadre();
    g.setColour(P::panel);
    g.fillRoundedRectangle(zone.toFloat(), 6.0f);
    g.setColour(P::border);
    g.drawRoundedRectangle(zone.toFloat().reduced(0.5f), 6.0f, 1.5f);

    auto enTete = zone.removeFromTop(kHauteurEnTete).reduced(kMarge, 8);
    g.setColour(P::accentAmber);
    g.setFont(juce::Font(juce::FontOptions(19.0f)));
    g.drawText(titre_, enTete.removeFromTop(24), juce::Justification::centredLeft, false);
    if (sousTitre_.isNotEmpty()) {
        g.setColour(P::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(sousTitre_, enTete, juce::Justification::centredLeft, false);
    }
    g.setColour(P::border);
    g.fillRect(zone.getX() + kMarge, zone.getY(), zone.getWidth() - 2 * kMarge, 1);
}

void ImportReportComponent::resized() {
    // DEUX PASSES, et elles ne bouclent pas : la première donne au Viewport sa
    // largeur, le repli en déduit le nombre de lignes, la seconde ajuste la
    // hauteur du cadre à ce nombre. La largeur ne dépendant pas du contenu, la
    // seconde passe ne peut pas changer le repli.
    disposer();
    reconstruireLaListe();
    disposer();
}

bool ImportReportComponent::keyPressed(const juce::KeyPress& touche) {
    if (touche == juce::KeyPress::escapeKey) {
        setVisible(false);
        if (onClose) onClose();
        return true;
    }
    return false;
}

} // namespace vsm::app::ui
