#include "PianoRollRulerComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::midi;
using namespace vsm::ui;

PianoRollRulerComponent::PianoRollRulerComponent(PianoRollComponent& pianoRoll)
    : pianoRoll_(pianoRoll) {}

void PianoRollRulerComponent::setLoopRegion(Tick start, Tick end, bool active) {
    loopStart_ = start;
    loopEnd_ = end;
    loopActive_ = active;
    repaint();
}

void PianoRollRulerComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::panel);
    const auto bounds = getLocalBounds();

    if (loopActive_ && loopEnd_ > loopStart_) {
        const float x1 = pianoRoll_.tickToX(loopStart_);
        const float x2 = pianoRoll_.tickToX(loopEnd_);
        g.setColour(Palette::accentTeal.withAlpha(0.30f));
        g.fillRect(juce::Rectangle<float>(x1, 0.0f, x2 - x1, static_cast<float>(bounds.getHeight())));
    }

    // Graduations : une par mesure, avec son numéro ; les temps n'apparaissent
    // que si le zoom laisse la place de les distinguer.
    const Tick startTick = std::max<Tick>(0, pianoRoll_.xToTick(static_cast<float>(pianoRoll_.keyboardWidth())));
    const Tick endTick = pianoRoll_.xToTick(static_cast<float>(bounds.getWidth()));
    const Tick barTicks = pianoRoll_.ticksPerBarAt(startTick);
    const Tick beatTicks = pianoRoll_.ticksPerBeat();
    const double pxPerTick = pianoRoll_.pixelsPerTick();

    if (beatTicks * pxPerTick > 12.0) {
        g.setColour(Palette::gridLine);
        for (Tick t = (startTick / beatTicks) * beatTicks; t <= endTick; t += beatTicks) {
            if (t % barTicks == 0) continue;
            const float x = pianoRoll_.tickToX(t);
            g.drawLine(x, static_cast<float>(bounds.getHeight()) * 0.6f, x, static_cast<float>(bounds.getHeight()), 1.0f);
        }
    }

    g.setFont(11.0f);
    for (Tick t = (startTick / barTicks) * barTicks; t <= endTick; t += barTicks) {
        const float x = pianoRoll_.tickToX(t);
        g.setColour(Palette::gridLineStrong);
        g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.2f);
        g.setColour(Palette::textSecondary);
        g.drawText(juce::String(static_cast<int>(t / barTicks) + 1),
                    static_cast<int>(x) + 3, 1, 40, bounds.getHeight() - 2,
                    juce::Justification::centredLeft, false);
    }

    const float playX = pianoRoll_.tickToX(pianoRoll_.playheadTick());
    if (playX >= static_cast<float>(pianoRoll_.keyboardWidth())) {
        g.setColour(Palette::accentAmber);
        g.drawLine(playX, 0.0f, playX, static_cast<float>(bounds.getHeight()), 1.5f);
        // Petit triangle : la tête de lecture reste repérable même immobile
        // au milieu de graduations.
        juce::Path marker;
        marker.addTriangle(playX - 5.0f, 0.0f, playX + 5.0f, 0.0f, playX, 8.0f);
        g.fillPath(marker);
    }

    // LES REPÈRES DE LA LIGNE DE TEMPS. Dessinés APRÈS la tête de lecture pour
    // rester lisibles quand les deux se croisent, et avec leur nom : un repère
    // réduit à un trait obligerait à se souvenir de ce qu'il repérait.
    if (const auto* project = pianoRoll_.project()) {
        const auto& markers = project->markers;
        for (size_t i = 0; i < markers.size(); ++i) {
            const float x = pianoRoll_.tickToX(markers[i].tick);
            if (x < static_cast<float>(pianoRoll_.keyboardWidth()) || x > static_cast<float>(bounds.getWidth()))
                continue;

            g.setColour(Palette::accentTeal);
            g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.5f);
            juce::Path fanion;
            fanion.addTriangle(x, 0.0f, x + 9.0f, 3.5f, x, 7.0f);
            g.fillPath(fanion);

            // LA PLACE DISPONIBLE VA JUSQU'AU REPÈRE SUIVANT, et pas plus loin.
            // Sans cette limite, deux repères proches écrivaient leurs noms
            // l'un par-dessus l'autre (« Refrain » et « Pont » donnaient
            // « Refrain Pont » illisible), et le premier mangeait le numéro de
            // mesure du second. Quand il n'y a pas la place d'écrire, on
            // n'écrit pas : le fanion suffit à dire qu'il y a un repère, un nom
            // tronqué à deux lettres ne dit rien du tout.
            float limite = static_cast<float>(bounds.getWidth());
            if (i + 1 < markers.size()) limite = pianoRoll_.tickToX(markers[i + 1].tick) - 3.0f;
            const int place = static_cast<int>(std::min(180.0f, limite - x - 11.0f));
            if (place < 26) continue;

            // Un fond opaque derrière le nom : la règle porte déjà les numéros
            // de mesure, et deux textes superposés ne se lisent ni l'un ni
            // l'autre.
            const juce::Rectangle<int> cadre(static_cast<int>(x) + 10, 1, place,
                                              bounds.getHeight() - 3);
            g.setColour(Palette::panel.withAlpha(0.92f));
            g.fillRect(cadre);
            g.setColour(Palette::accentTeal);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::String(markers[i].name), cadre.reduced(3, 0),
                        juce::Justification::centredLeft, true);
        }
    }

    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(bounds.getHeight()) - 0.5f,
                static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight()) - 0.5f, 1.0f);
}

void PianoRollRulerComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.position.x < static_cast<float>(pianoRoll_.keyboardWidth())) return;
    const Tick tick = std::max<Tick>(0, pianoRoll_.xToTick(event.position.x));

    if (event.mods.isPopupMenu()) {
        // Le repère le plus proche du clic, à une dizaine de pixels près : on
        // vise un trait à la souris, pas un tick.
        int survole = -1;
        if (const auto* project = pianoRoll_.project())
            for (size_t i = 0; i < project->markers.size(); ++i)
                if (std::abs(pianoRoll_.tickToX(project->markers[i].tick) - event.position.x) < 10.0f)
                    survole = static_cast<int>(i);

        juce::PopupMenu menu;
        menu.addItem(1, "Poser un repere ici...");
        menu.addItem(2, "Retirer ce repere", survole >= 0);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, tick, survole](int choix) {
            if (choix == 1 && onMarkerRequested) onMarkerRequested(tick);
            if (choix == 2 && survole >= 0 && onMarkerRemoved) onMarkerRemoved(static_cast<size_t>(survole));
        });
        return;
    }

    if (event.mods.isShiftDown()) {
        loopDragAnchor_ = tick;
        loopStart_ = loopEnd_ = tick;
        loopActive_ = true;
        if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, loopActive_);
        repaint();
        return;
    }
    if (onPlayheadRequested) onPlayheadRequested(tick);
}

void PianoRollRulerComponent::mouseDrag(const juce::MouseEvent& event) {
    if (event.position.x < static_cast<float>(pianoRoll_.keyboardWidth())) return;
    const Tick tick = std::max<Tick>(0, pianoRoll_.xToTick(event.position.x));

    if (event.mods.isShiftDown()) {
        loopStart_ = std::min(loopDragAnchor_, tick);
        loopEnd_ = std::max(loopDragAnchor_, tick);
        if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, loopActive_);
        repaint();
        return;
    }
    if (onPlayheadRequested) onPlayheadRequested(tick);
}

void PianoRollRulerComponent::mouseDoubleClick(const juce::MouseEvent&) {
    loopActive_ = false;
    if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, false);
    repaint();
}
