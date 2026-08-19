#include "AutomationComponent.h"
#include <algorithm>

using vsm::audio::engine::AutomationLane;
using vsm::audio::engine::AutomationPoint;
using vsm::audio::engine::Tick;
using namespace vsm::ui;

AutomationComponent::AutomationComponent() {
    trackLabel_.setText("Piste", juce::dontSendNotification);
    trackLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    trackLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(trackLabel_);
    addAndMakeVisible(trackBox_);
    trackBox_.onChange = [this] {
        selectedTrack_ = static_cast<size_t>(juce::jmax(0, trackBox_.getSelectedItemIndex()));
        rebuildParamBox();
    };

    paramLabel_.setText("Parametre", juce::dontSendNotification);
    paramLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    paramLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(paramLabel_);
    addAndMakeVisible(paramBox_);
    paramBox_.onChange = [this] {
        int idx = paramBox_.getSelectedItemIndex();
        if (idx >= 0 && idx < static_cast<int>(paramEntries_.size())) {
            const auto& e = paramEntries_[static_cast<size_t>(idx)];
            selectedParam_ = e.id;
            paramMin_ = e.min;
            paramMax_ = e.max;
            hasSelection_ = true;
            loadSelectedLane();
            repaint();
        }
    };

    hintLabel_.setText("Clic : ajouter  -  Glisser : deplacer  -  Clic droit : supprimer",
                       juce::dontSendNotification);
    hintLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    hintLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    hintLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLabel_);
}

void AutomationComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    // Longueur de la vue = fin de la note la plus tardive, au moins 4 mesures.
    maxTick_ = 1920 * 4;
    if (project_ != nullptr) {
        for (const auto& t : project_->tracks)
            for (const auto& n : t.notes)
                maxTick_ = std::max(maxTick_, n.endTick);
    }
    rebuildTrackBox();
}

void AutomationComponent::rebuildTrackBox() {
    trackBox_.clear(juce::dontSendNotification);
    if (project_ != nullptr) {
        for (size_t i = 0; i < project_->tracks.size(); ++i) {
            const auto& name = project_->tracks[i].name;
            trackBox_.addItem(name.empty() ? ("Piste " + juce::String(i + 1))
                                           : juce::String(name),
                              static_cast<int>(i) + 1);
        }
    }
    if (trackBox_.getNumItems() > 0) {
        trackBox_.setSelectedItemIndex(juce::jlimit(0, trackBox_.getNumItems() - 1,
                                                    static_cast<int>(selectedTrack_)),
                                       juce::dontSendNotification);
        selectedTrack_ = static_cast<size_t>(trackBox_.getSelectedItemIndex());
    }
    rebuildParamBox();
}

void AutomationComponent::rebuildParamBox() {
    paramBox_.clear(juce::dontSendNotification);
    paramEntries_.clear();
    hasSelection_ = false;

    vsm::audio::plugin::ISynthPlugin* inst =
        instrumentProvider ? instrumentProvider(selectedTrack_) : nullptr;
    if (inst != nullptr) {
        int itemId = 1;
        for (const auto& info : inst->parameterList()) {
            paramBox_.addItem(juce::String(info.name), itemId++);
            paramEntries_.push_back({info.id, info.minValue, info.maxValue});
        }
        if (paramBox_.getNumItems() > 0) {
            paramBox_.setSelectedItemIndex(0, juce::sendNotificationSync); // déclenche onChange -> sélection
        }
    }
    repaint();
}

void AutomationComponent::loadSelectedLane() {
    editPoints_.clear();
    for (const auto& lane : lanes_)
        if (lane.targetTrackIndex == selectedTrack_ && lane.targetParam == selectedParam_)
            editPoints_ = lane.points();
    dragIndex_ = -1;
}

void AutomationComponent::commit() {
    // Retire l'ancienne lane (track,param) puis rajoute si des points existent.
    lanes_.erase(std::remove_if(lanes_.begin(), lanes_.end(),
                                [this](const AutomationLane& l) {
                                    return l.targetTrackIndex == selectedTrack_ &&
                                           l.targetParam == selectedParam_;
                                }),
                 lanes_.end());
    if (!editPoints_.empty()) {
        AutomationLane lane;
        lane.targetTrackIndex = selectedTrack_;
        lane.targetParam = selectedParam_;
        for (const auto& p : editPoints_) lane.addPoint(p.tick, p.value, p.curveToNext);
        lanes_.push_back(std::move(lane));
    }
    if (onAutomationChanged) onAutomationChanged(lanes_);
}

// ------------------------------------------------------------- géométrie ---

juce::Rectangle<int> AutomationComponent::editorArea() const {
    return getLocalBounds().withTrimmedTop(30).reduced(8);
}

int AutomationComponent::tickToX(Tick tick) const {
    auto a = editorArea();
    const double ratio = maxTick_ > 0 ? static_cast<double>(tick) / static_cast<double>(maxTick_) : 0.0;
    return a.getX() + static_cast<int>(ratio * a.getWidth());
}

Tick AutomationComponent::xToTick(int x) const {
    auto a = editorArea();
    const double ratio = a.getWidth() > 0 ? static_cast<double>(x - a.getX()) / a.getWidth() : 0.0;
    return static_cast<Tick>(juce::jlimit(0.0, 1.0, ratio) * static_cast<double>(maxTick_));
}

int AutomationComponent::valueToY(float value) const {
    auto a = editorArea();
    const float range = (paramMax_ - paramMin_);
    const float norm = range > 0.0f ? (value - paramMin_) / range : 0.0f;
    return a.getBottom() - static_cast<int>(juce::jlimit(0.0f, 1.0f, norm) * a.getHeight());
}

float AutomationComponent::yToValue(int y) const {
    auto a = editorArea();
    const float norm = a.getHeight() > 0 ? static_cast<float>(a.getBottom() - y) / a.getHeight() : 0.0f;
    return paramMin_ + juce::jlimit(0.0f, 1.0f, norm) * (paramMax_ - paramMin_);
}

int AutomationComponent::findPointNear(juce::Point<int> p) const {
    for (size_t i = 0; i < editPoints_.size(); ++i) {
        juce::Point<int> pos(tickToX(editPoints_[i].tick), valueToY(editPoints_[i].value));
        if (pos.getDistanceFrom(p) <= kPointRadius + 4) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------- souris ---

void AutomationComponent::mouseDown(const juce::MouseEvent& e) {
    if (!hasSelection_ || !editorArea().contains(e.getPosition())) return;

    int hit = findPointNear(e.getPosition());
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (hit >= 0) {
            editPoints_.erase(editPoints_.begin() + hit);
            dragIndex_ = -1;
            commit();
            repaint();
        }
        return;
    }

    if (hit >= 0) {
        dragIndex_ = hit;
    } else {
        AutomationPoint np;
        np.tick = xToTick(e.x);
        np.value = yToValue(e.y);
        editPoints_.push_back(np);
        std::sort(editPoints_.begin(), editPoints_.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
        // Retrouve l'index du point inséré pour permettre un drag immédiat.
        for (size_t i = 0; i < editPoints_.size(); ++i)
            if (editPoints_[i].tick == np.tick) { dragIndex_ = static_cast<int>(i); break; }
        commit();
    }
    repaint();
}

void AutomationComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragIndex_ < 0 || dragIndex_ >= static_cast<int>(editPoints_.size())) return;

    // Contrainte : un point ne peut pas croiser ses voisins -> l'index reste
    // stable et la liste triée (pas de resort pendant le drag).
    Tick newTick = xToTick(e.x);
    const size_t i = static_cast<size_t>(dragIndex_);
    if (i > 0) newTick = std::max(newTick, editPoints_[i - 1].tick + 1);
    if (i + 1 < editPoints_.size()) newTick = std::min(newTick, editPoints_[i + 1].tick - 1);

    editPoints_[i].tick = newTick;
    editPoints_[i].value = yToValue(e.y);
    repaint();
}

void AutomationComponent::mouseUp(const juce::MouseEvent&) {
    if (dragIndex_ >= 0) { commit(); dragIndex_ = -1; }
}

// ----------------------------------------------------------------- paint ---

void AutomationComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
    auto a = editorArea();

    g.setColour(Palette::panel);
    g.fillRoundedRectangle(a.toFloat(), 4.0f);

    // Grille verticale par mesure (4 noires * ppq).
    if (project_ != nullptr && project_->ticksPerQuarterNote > 0) {
        const Tick ticksPerBar = static_cast<Tick>(project_->ticksPerQuarterNote) * 4;
        g.setColour(Palette::gridLine);
        for (Tick t = 0; t <= maxTick_; t += ticksPerBar) {
            int x = tickToX(t);
            g.drawVerticalLine(x, static_cast<float>(a.getY()), static_cast<float>(a.getBottom()));
        }
    }
    // Lignes horizontales min / milieu / max.
    g.setColour(Palette::gridLineStrong);
    for (float f : {0.0f, 0.5f, 1.0f}) {
        int y = a.getBottom() - static_cast<int>(f * a.getHeight());
        g.drawHorizontalLine(y, static_cast<float>(a.getX()), static_cast<float>(a.getRight()));
    }

    if (!hasSelection_) {
        g.setColour(Palette::textSecondary);
        g.drawText("Selectionnez une piste avec un instrument pour automatiser un parametre.",
                   a, juce::Justification::centred);
        return;
    }

    // Bornes de valeur.
    g.setColour(Palette::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(juce::String(paramMax_, 1), a.getX() + 2, a.getY(), 60, 14, juce::Justification::topLeft);
    g.drawText(juce::String(paramMin_, 1), a.getX() + 2, a.getBottom() - 14, 60, 14, juce::Justification::bottomLeft);

    if (editPoints_.empty()) {
        g.setColour(Palette::textSecondary.withAlpha(0.7f));
        g.drawText("Cliquez pour ajouter des points d'automation.", a, juce::Justification::centred);
        return;
    }

    // Segments (interpolation linéaire ; les pas seraient horizontaux -- non
    // exposés dans cette première version de l'éditeur, curve = Linear).
    juce::Path path;
    juce::Point<int> first(tickToX(editPoints_.front().tick), valueToY(editPoints_.front().value));
    // maintien avant le premier point
    path.startNewSubPath(static_cast<float>(a.getX()), static_cast<float>(first.y));
    path.lineTo(first.toFloat());
    for (size_t i = 1; i < editPoints_.size(); ++i)
        path.lineTo(static_cast<float>(tickToX(editPoints_[i].tick)),
                    static_cast<float>(valueToY(editPoints_[i].value)));
    // maintien après le dernier point
    juce::Point<int> last(tickToX(editPoints_.back().tick), valueToY(editPoints_.back().value));
    path.lineTo(static_cast<float>(a.getRight()), static_cast<float>(last.y));

    g.setColour(Palette::accentTeal);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    // Points.
    for (const auto& p : editPoints_) {
        float x = static_cast<float>(tickToX(p.tick));
        float y = static_cast<float>(valueToY(p.value));
        g.setColour(Palette::accentAmber);
        g.fillEllipse(x - kPointRadius, y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
    }
}

void AutomationComponent::resized() {
    auto top = getLocalBounds().removeFromTop(26).reduced(6, 2);
    trackLabel_.setBounds(top.removeFromLeft(38));
    trackBox_.setBounds(top.removeFromLeft(150));
    top.removeFromLeft(10);
    paramLabel_.setBounds(top.removeFromLeft(70));
    paramBox_.setBounds(top.removeFromLeft(170));
    hintLabel_.setBounds(top);
}
