#include "ReconstructionWindow.h"
#include "Langue.h"
#include "UiScale.h"

namespace vsm::app::ui {

ReconstructionWindow::ReconstructionWindow() {
    titre_.setText(tr(u8"Reconstruction"), juce::dontSendNotification);
    titre_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    addAndMakeVisible(titre_);

    etape_.setText(tr(u8"Démarrage..."), juce::dontSendNotification);
    bouton_.setButtonText(tr(u8"Annuler"));   // D87 : l'en-tête l'initialisait en français
    etape_.setFont(juce::Font(juce::FontOptions(15.0f)));
    addAndMakeVisible(etape_);

    // LE JOURNAL EST EN LECTURE SEULE ET SÉLECTIONNABLE : quand la chaîne
    // échoue, la ligne qui l'explique doit pouvoir être COPIÉE, pas recopiée à
    // la main dans un rapport de bug.
    journal_.setMultiLine(true, false);
    journal_.setReadOnly(true);
    journal_.setScrollbarsShown(true);
    journal_.setCaretVisible(false);
    journal_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                   13.0f, juce::Font::plain)));
    addAndMakeVisible(journal_);

    bouton_.onClick = [this] {
        if (termine_) { if (onClose) onClose(); }
        else { if (onCancel) onCancel(); }
    };
    addAndMakeVisible(bouton_);
}

void ReconstructionWindow::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff23262b));
}

void ReconstructionWindow::resized() {
    auto zone = getLocalBounds().reduced(14);
    titre_.setBounds(zone.removeFromTop(28));
    etape_.setBounds(zone.removeFromTop(26));
    zone.removeFromTop(8);
    auto bas = zone.removeFromBottom(34);
    bouton_.setBounds(bas.removeFromRight(140).reduced(0, 2));
    zone.removeFromBottom(8);
    journal_.setBounds(zone);
}

void ReconstructionWindow::setSource(const juce::String& nomDuFichier) {
    titre_.setText(tr(u8"Reconstruction de %1").replace("%1", nomDuFichier),
                    juce::dontSendNotification);
}

void ReconstructionWindow::setProgress(const vsm::app::ReconstructionRunner::Progress& avancement) {
    if (termine_) return;
    if (avancement.stepCount > 0) {
        // « Étape 2 sur 5 — Séparation en stems (htdemucs) », c'est-à-dire ce
        // que la chaîne vient de dire, et rien d'autre.
        etape_.setText(tr(u8"Étape %1 sur %2 — %3").replace("%1", juce::String(avancement.step))
                            .replace("%2", juce::String(avancement.stepCount)).replace("%3", avancement.stepLabel),
                        juce::dontSendNotification);
    }
    const juce::String texte = avancement.recentLines.joinIntoString("\n");
    if (texte != journal_.getText()) {
        journal_.setText(texte, false);
        journal_.moveCaretToEnd();
        journal_.scrollEditorToPositionCaret(0, journal_.getHeight());
    }
}

void ReconstructionWindow::setFinished(bool succes, const juce::String& message) {
    termine_ = true;
    etape_.setText(message, juce::dontSendNotification);
    etape_.setColour(juce::Label::textColourId,
                      succes ? juce::Colours::lightgreen : juce::Colours::orangered);
    bouton_.setButtonText(tr("Fermer"));
}

} // namespace vsm::app::ui
