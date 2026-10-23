#include "MoniteurMidi.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::ui;

namespace vsm::app::ui {

MoniteurMidi::MoniteurMidi(std::function<bool(Evenement&)> tirer) : tirer_(std::move(tirer)) {
    liste_.setModel(this);
    liste_.setRowHeight(24);
    liste_.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    addAndMakeVisible(liste_);
    effacer_.setName("moniteur.effacer");   // le nom par lequel le banc le désigne (cliquer:)
    effacer_.onClick = [this] { effacer(); };
    addAndMakeVisible(effacer_);
    explication_.setColour(juce::Label::textColourId, Palette::textSecondary);
    explication_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(explication_);
    retraduire();
}

void MoniteurMidi::retraduire() {
    effacer_.setButtonText(tr(u8"Effacer"));
    explication_.setText(tr(u8"Les événements MIDI que le moteur reçoit, les plus récents en bas (500 au plus). "
                            u8"Recueillis tant que cette fenêtre est ouverte."),
                         juce::dontSendNotification);
    liste_.repaint();
}

void MoniteurMidi::demarrer() {
    // CE QUI TRAÎNAIT DANS LA FILE date d'avant l'ouverture : jeté, pas montré.
    Evenement e;
    while (tirer_ && tirer_(e)) {}
    startTimerHz(20);
}

void MoniteurMidi::arreter() {
    stopTimer();
    tirerTout();
}

void MoniteurMidi::effacer() {
    lignes_.clear();
    liste_.updateContent();
    liste_.repaint();
    if (onCountChanged) onCountChanged(0);
}

void MoniteurMidi::tirerTout() {
    if (!tirer_) return;
    Evenement e;
    bool neuf = false;
    while (tirer_(e)) {
        lignes_.push_back(decrire(e));
        neuf = true;
    }
    if (!neuf) return;
    while (lignes_.size() > kLignesMax) lignes_.pop_front();
    liste_.updateContent();
    liste_.scrollToEnsureRowIsOnscreen(static_cast<int>(lignes_.size()) - 1);
    liste_.repaint();
    if (onCountChanged) onCountChanged(lignes_.size());
}

juce::String MoniteurMidi::releverPourCapture() {
    tirerTout();
    juce::StringArray morceaux;
    for (const auto& l : lignes_) morceaux.add(l);
    return morceaux.joinIntoString("\n");
}

juce::String MoniteurMidi::decrire(const Evenement& e) {
    const juce::String heure = juce::Time(e.millis).formatted("%H:%M:%S") + "."
                               + juce::String(e.millis % 1000).paddedLeft('0', 3);
    const juce::String source = e.source[0] != '\0' ? juce::String::fromUTF8(e.source) : tr(u8"(interne)");
    juce::String canal = juce::String::fromUTF8(u8"—"), quoi;
    if (e.size == 0) {
        quoi = tr(u8"SysEx de %1 octet(s)").replace("%1", juce::String(static_cast<int>(e.sysexBytes)));
    } else {
        const int statut = e.bytes[0], d1 = e.bytes[1], d2 = e.bytes[2];
        const auto nom = [](int n) { return juce::MidiMessage::getMidiNoteName(n, true, true, 4) + " (" + juce::String(n) + ")"; };
        if (statut < 0xF0) canal = tr(u8"canal %1").replace("%1", juce::String((statut & 0x0F) + 1));
        switch (statut & 0xF0) {
            case 0x80: quoi = tr(u8"Note relâchée %1").replace("%1", nom(d1)); break;
            case 0x90: quoi = d2 == 0 ? tr(u8"Note relâchée %1").replace("%1", nom(d1))
                                      : tr(u8"Note enfoncée %1, vélocité %2").replace("%1", nom(d1)).replace("%2", juce::String(d2));
                       break;
            case 0xA0: quoi = tr(u8"Pression polyphonique %1 = %2").replace("%1", nom(d1)).replace("%2", juce::String(d2)); break;
            case 0xB0: quoi = "CC " + juce::String(d1) + " = " + juce::String(d2); break;
            case 0xC0: quoi = tr(u8"Programme %1").replace("%1", juce::String(d1)); break;
            case 0xD0: quoi = tr(u8"Pression %1").replace("%1", juce::String(d1)); break;
            case 0xE0: {
                const int valeur = ((d2 << 7) | d1) - 8192;
                quoi = "Pitch bend " + (valeur > 0 ? "+" + juce::String(valeur) : juce::String(valeur));
                break;
            }
            default:   quoi = tr(u8"Message système %1").replace("%1", "0x" + juce::String::toHexString(statut).toUpperCase()); break;
        }
    }
    return heure + juce::String::fromUTF8(u8" · ") + source + juce::String::fromUTF8(u8" · ") + canal
           + juce::String::fromUTF8(u8" · ") + quoi;
}

int MoniteurMidi::getNumRows() { return static_cast<int>(lignes_.size()); }

void MoniteurMidi::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || static_cast<size_t>(row) >= lignes_.size()) return;
    if (selected) {
        g.setColour(Palette::accentTeal.withAlpha(0.25f));
        g.fillRect(0, 0, width, height);
    }
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    g.setColour(Palette::textPrimary);
    g.drawText(lignes_[static_cast<size_t>(row)], 8, 0, width - 16, height, juce::Justification::centredLeft);
}

void MoniteurMidi::paint(juce::Graphics& g) { g.fillAll(Palette::background); }

void MoniteurMidi::resized() {
    auto zone = getLocalBounds().reduced(8);
    auto bas = zone.removeFromBottom(44);
    effacer_.setBounds(bas.removeFromRight(110).reduced(0, 8));
    explication_.setBounds(bas);
    liste_.setBounds(zone);
}

} // namespace vsm::app::ui
