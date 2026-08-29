#include "StepSequencerComponent.h"
#include "vsm/sequencer/NoteEdit.h" // noteNumberToName
#include <algorithm>

using namespace vsm::panels;
using namespace vsm::sequencer;

namespace {
/// std::string -> juce::String en UTF-8 EXPLICITE. Sans cela, les libellés
/// accentués des façades (« RÉGLAGES ») s'affichent en mojibake selon la
/// locale de compilation : le texte est déjà en UTF-8 dans le source, mais
/// JUCE ne le suppose pas.
juce::String toJuce(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }

juce::Colour colourFrom(const std::string& hex, juce::Colour fallback) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    return juce::Colour::fromString("ff" + juce::String(hex.substr(1)));
}
} // namespace

StepSequencerComponent::StepSequencerComponent() { setOpaque(false); }

void StepSequencerComponent::configure(const SequencerSpec& spec, Track* track,
                                        juce::Colour panelColour, juce::Colour textColour) {
    spec_ = spec;
    track_ = track;
    panelColour_ = panelColour;
    textColour_ = textColour;

    if (spec_.kind == SequencerKind::DrumGrid) {
        std::vector<std::pair<std::string, uint8_t>> pieces;
        for (const auto& [name, note] : spec_.lanes)
            pieces.emplace_back(name, static_cast<uint8_t>(note));
        pattern_ = makeDrumPattern(pieces, spec_.stepCount);
    } else {
        pattern_ = makeMonoPattern(spec_.defaultNote, spec_.stepCount);
    }
    reloadFromTrack();
    repaint();
}

void StepSequencerComponent::setPlayheadStep(int step) {
    if (step == playheadStep_) return;
    playheadStep_ = step;
    repaint();
}

void StepSequencerComponent::reloadFromTrack() {
    if (!track_) return;
    pattern_ = patternFromNotes(track_->notes, pattern_);
}

void StepSequencerComponent::commitToTrack() {
    if (!track_) return;
    // Le compteur d'identifiants repart des notes existantes : deux notes de
    // même identifiant casseraient silencieusement la sélection du piano roll.
    uint64_t idCounter = 0;
    for (const auto& note : track_->notes) idCounter = std::max(idCounter, note.id);
    writePatternToTrack(*track_, pattern_, idCounter);
    if (onPatternEdited) onPatternEdited();
    repaint();
}

juce::Rectangle<float> StepSequencerComponent::stepBounds(int lane, int step) const {
    const auto area = getLocalBounds().toFloat().reduced(4.0f).withTrimmedTop(16.0f);
    const int laneCount = std::max<int>(1, static_cast<int>(pattern_.lanes.size()));
    // Une ligne ne s'étire pas indéfiniment : un motif monophonique (TB-303)
    // n'a qu'une ligne, et l'étirer sur toute la hauteur donnerait seize
    // colonnes géantes qui ne ressemblent à aucun séquenceur.
    const float laneHeight = std::min(area.getHeight() / static_cast<float>(laneCount), 46.0f);
    const float gridX = area.getX() + static_cast<float>(kLaneLabelWidth);
    const float stepWidth = (area.getRight() - gridX) / static_cast<float>(std::max(1, pattern_.stepCount));
    return juce::Rectangle<float>(gridX + static_cast<float>(step) * stepWidth,
                                   area.getY() + static_cast<float>(lane) * laneHeight,
                                   stepWidth, laneHeight).reduced(2.0f);
}

StepSequencerComponent::Hit StepSequencerComponent::stepAt(juce::Point<float> position) const {
    for (int lane = 0; lane < static_cast<int>(pattern_.lanes.size()); ++lane)
        for (int step = 0; step < pattern_.stepCount; ++step)
            if (stepBounds(lane, step).contains(position)) return {lane, step};
    return {};
}

void StepSequencerComponent::mouseDown(const juce::MouseEvent& event) {
    const Hit hit = stepAt(event.position);
    if (hit.lane < 0 || hit.step < 0 || !track_) return;
    auto& cell = pattern_.lanes[static_cast<size_t>(hit.lane)].steps[static_cast<size_t>(hit.step)];

    if (event.mods.isShiftDown()) {
        // Accent : la seule nuance de ces machines. Accentuer un pas éteint
        // l'allume aussi -- personne n'accentue un silence.
        cell.accent = !cell.accent;
        if (cell.accent) cell.active = true;
    } else if (event.mods.isAltDown() && spec_.kind == SequencerKind::MonoPattern) {
        cell.slide = !cell.slide;
        if (cell.slide) cell.active = true;
    } else {
        cell.active = !cell.active;
        if (!cell.active) { cell.accent = false; cell.slide = false; }
        if (cell.active && cell.noteNumber == 0) cell.noteNumber = pattern_.lanes[static_cast<size_t>(hit.lane)].noteNumber;
    }
    commitToTrack();
}

void StepSequencerComponent::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel) {
    if (spec_.kind != SequencerKind::MonoPattern) return;
    const Hit hit = stepAt(event.position);
    if (hit.lane < 0 || hit.step < 0) return;

    auto& cell = pattern_.lanes[static_cast<size_t>(hit.lane)].steps[static_cast<size_t>(hit.step)];
    const int base = cell.noteNumber != 0 ? cell.noteNumber : pattern_.lanes[0].noteNumber;
    // Un demi-ton par cran ; l'octave d'un coup avec Maj, comme partout
    // ailleurs dans l'application.
    const int delta = (wheel.deltaY > 0 ? 1 : -1) * (event.mods.isShiftDown() ? 12 : 1);
    cell.noteNumber = static_cast<uint8_t>(juce::jlimit(0, 127, base + delta));
    cell.active = true;
    commitToTrack();
}

void StepSequencerComponent::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(panelColour_.darker(0.35f));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(textColour_.withAlpha(0.35f));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    g.setColour(textColour_.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText(toJuce(spec_.title), area.reduced(8.0f, 3.0f).removeFromTop(14.0f).toNearestInt(),
                juce::Justification::centredLeft, false);

    if (!track_) {
        g.setColour(textColour_.withAlpha(0.5f));
        g.setFont(11.0f);
        g.drawText(juce::String::fromUTF8(u8"Sélectionnez une piste pour éditer le motif"), area.toNearestInt(),
                    juce::Justification::centred, false);
        return;
    }

    for (int lane = 0; lane < static_cast<int>(pattern_.lanes.size()); ++lane) {
        const auto& laneData = pattern_.lanes[static_cast<size_t>(lane)];
        const auto first = stepBounds(lane, 0);
        g.setColour(textColour_.withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.drawText(laneData.name.empty() ? juce::String("PATTERN") : toJuce(laneData.name),
                    juce::Rectangle<float>(area.getX() + 6.0f, first.getY(),
                                            static_cast<float>(kLaneLabelWidth) - 10.0f, first.getHeight())
                        .toNearestInt(),
                    juce::Justification::centredLeft, false);

        for (int step = 0; step < pattern_.stepCount; ++step) {
            const auto bounds = stepBounds(lane, step);
            const auto& cell = laneData.steps[static_cast<size_t>(step)];

            // Couleur par groupe de quatre : c'est ce qui permet de compter
            // les temps sans les compter, comme sur la machine d'origine.
            const size_t groupCount = std::max<size_t>(1, spec_.stepGroupColours.size());
            const size_t group = static_cast<size_t>(step / 4) % groupCount;
            const juce::Colour groupColour =
                colourFrom(spec_.stepGroupColours[group], juce::Colours::orange);

            juce::Colour fill = groupColour.withAlpha(cell.active ? 0.95f : 0.18f);
            if (cell.active && cell.accent) fill = groupColour.brighter(0.4f);
            g.setColour(fill);
            g.fillRoundedRectangle(bounds, 2.5f);

            // Le pas en cours de lecture s'éclaire : sans ce repère, une
            // grille de seize cases ne dit pas où on en est.
            if (step == playheadStep_) {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawRoundedRectangle(bounds, 2.5f, 2.0f);
            } else {
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.drawRoundedRectangle(bounds, 2.5f, 1.0f);
            }

            if (cell.active && cell.accent) {
                // Marque d'accent : un point, lisible même sur un petit pas.
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.fillEllipse(bounds.withSizeKeepingCentre(4.0f, 4.0f));
            }
            if (cell.active && cell.slide) {
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.drawLine(bounds.getX() + 3.0f, bounds.getBottom() - 3.0f,
                            bounds.getRight() - 3.0f, bounds.getY() + 3.0f, 1.4f);
            }
            if (cell.active && spec_.kind == SequencerKind::MonoPattern && bounds.getHeight() > 22.0f) {
                g.setColour(juce::Colours::black.withAlpha(0.8f));
                g.setFont(juce::Font(juce::FontOptions(9.0f)));
                g.drawText(juce::String(vsm::sequencer::noteNumberToName(
                                cell.noteNumber != 0 ? cell.noteNumber : laneData.noteNumber)),
                            bounds.toNearestInt(), juce::Justification::centredBottom, false);
            }
        }
    }
}
