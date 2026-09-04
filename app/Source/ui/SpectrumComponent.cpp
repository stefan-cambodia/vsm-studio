#include "SpectrumComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <cmath>

namespace Palette = vsm::ui::Palette;

namespace vsm::app::ui {

namespace {
constexpr float kBasDb = -96.0f;   // le bas de l'écran ; le plancher de l'analyse est plus bas
constexpr double kBasHz = 20.0;
}

SpectrumComponent::SpectrumComponent()
    : analyseur_(std::make_unique<Analyseur>()), fenetre_(Analyseur::kSize, 0.0f) {
    courbe_.fill(Analyseur::kFloorDb);
    tenue_.fill(Analyseur::kFloorDb);
    setOpaque(true);
    startTimerHz(25);
}

SpectrumComponent::~SpectrumComponent() { stopTimer(); }

void SpectrumComponent::timerCallback() {
    if (tap_ == nullptr || !isShowing()) return;
    if (sampleRateProvider) sampleRate_ = std::max(8000.0, sampleRateProvider());
    tap_->readLatest(fenetre_.data(), fenetre_.size());
    analyseur_->analyze(fenetre_.data());
    const auto& db = analyseur_->magnitudesDb();
    aDuSignal_ = false;
    for (size_t k = 0; k < Analyseur::kBins; ++k) {
        courbe_[k] = db[k];
        // La courbe tenue redescend d'un demi-décibel par image : deux
        // secondes pour perdre 25 dB, le temps de lire une crête.
        tenue_[k] = std::max(db[k], tenue_[k] - 0.5f);
        aDuSignal_ = aDuSignal_ || db[k] > kBasDb;
    }
    creteDb_ = analyseur_->peakDb();
    creteHz_ = analyseur_->peakFrequency(sampleRate_);
    repaint();
}

float SpectrumComponent::xPour(double hz, juce::Rectangle<float> zone) const {
    const double haut = std::max(kBasHz * 2.0, sampleRate_ / 2.0);
    const double t = (std::log(std::max(hz, kBasHz)) - std::log(kBasHz)) / (std::log(haut) - std::log(kBasHz));
    return zone.getX() + static_cast<float>(t) * zone.getWidth();
}

float SpectrumComponent::yPour(float db, juce::Rectangle<float> zone) const {
    const float t = (juce::jlimit(kBasDb, 0.0f, db) - kBasDb) / (0.0f - kBasDb);
    return zone.getBottom() - t * zone.getHeight();
}

void SpectrumComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
    auto tout = getLocalBounds().toFloat().reduced(8.0f);
    auto entete = tout.removeFromTop(22.0f);
    auto zone = tout;
    zone.removeFromLeft(46.0f);     // les étiquettes de niveau
    zone.removeFromBottom(22.0f);   // les étiquettes de fréquence
    const auto police = juce::Font(juce::FontOptions(13.0f));

    // L'en-tête : la crête nommée. « Master » parce que c'est le bus final,
    // après la tranche master, ce qui sort réellement.
    g.setColour(Palette::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    juce::String titre = juce::String::fromUTF8(u8"Master — ");
    if (!aDuSignal_ || creteDb_ <= kBasDb) titre += juce::String::fromUTF8(u8"silence");
    else titre += juce::String::fromUTF8(u8"crête ") + juce::String(creteHz_, 1) + " Hz, "
                  + juce::String(creteDb_, 1) + " dB";
    g.drawText(titre, entete, juce::Justification::centredLeft);

    // La grille : fréquences en décades et en 1-2-5, niveaux par 12 dB.
    g.setFont(police);
    const double haut = sampleRate_ / 2.0;
    for (double hz : {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0}) {
        if (hz > haut) break;
        const float x = xPour(hz, zone);
        const int entier = static_cast<int>(hz);
        g.setColour(Palette::textSecondary.withAlpha(entier == 100 || entier == 1000 || entier == 10000 ? 0.45f : 0.2f));
        g.drawVerticalLine(static_cast<int>(x), zone.getY(), zone.getBottom());
        g.setColour(Palette::textSecondary);
        const juce::String etiquette = hz >= 1000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                                                    : juce::String(static_cast<int>(hz));
        g.drawText(etiquette, juce::Rectangle<float>(x - 20.0f, zone.getBottom() + 2.0f, 40.0f, 18.0f),
                   juce::Justification::centred);
    }
    for (int dbEntier = 0; dbEntier >= static_cast<int>(kBasDb); dbEntier -= 12) {
        const float db = static_cast<float>(dbEntier);
        const float y = yPour(db, zone);
        g.setColour(Palette::textSecondary.withAlpha(dbEntier == 0 ? 0.45f : 0.2f));
        g.drawHorizontalLine(static_cast<int>(y), zone.getX(), zone.getRight());
        g.setColour(Palette::textSecondary);
        g.drawText(juce::String(dbEntier), juce::Rectangle<float>(tout.getX(), y - 9.0f, 40.0f, 18.0f),
                   juce::Justification::centredRight);
    }

    // Les deux courbes : la tenue, fine et claire ; la vive, remplie.
    auto tracer = [&](const std::array<float, Analyseur::kBins>& valeurs) {
        juce::Path chemin;
        bool premier = true;
        for (size_t k = 1; k < Analyseur::kBins; ++k) {
            const double hz = Analyseur::binFrequency(k, sampleRate_);
            if (hz < kBasHz) continue;
            const float x = xPour(hz, zone), y = yPour(valeurs[k], zone);
            if (premier) { chemin.startNewSubPath(x, y); premier = false; }
            else chemin.lineTo(x, y);
        }
        return chemin;
    };
    if (aDuSignal_) {
        g.setColour(Palette::textSecondary.withAlpha(0.8f));
        g.strokePath(tracer(tenue_), juce::PathStrokeType(1.0f));
        auto vive = tracer(courbe_);
        auto remplie = vive;
        remplie.lineTo(zone.getRight(), zone.getBottom());
        remplie.lineTo(xPour(kBasHz, zone), zone.getBottom());
        remplie.closeSubPath();
        g.setColour(Palette::accentTeal.withAlpha(0.22f));
        g.fillPath(remplie);
        g.setColour(Palette::accentTeal);
        g.strokePath(vive, juce::PathStrokeType(1.5f));
    }
    g.setColour(Palette::textSecondary.withAlpha(0.5f));
    g.drawRect(zone, 1.0f);
}

} // namespace vsm::app::ui
