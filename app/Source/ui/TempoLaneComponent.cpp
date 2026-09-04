#include "TempoLaneComponent.h"
#include <algorithm>
#include <cmath>

using vsm::midi::Tick;
using namespace vsm::ui;

namespace {
uint32_t microsecondsFromBpm(double bpm) {
    return static_cast<uint32_t>(std::llround(60000000.0 / std::max(1.0, bpm)));
}
double bpmFromMicroseconds(uint32_t uspq) {
    return uspq > 0 ? 60000000.0 / static_cast<double>(uspq) : 120.0;
}
} // namespace

TempoLaneComponent::TempoLaneComponent() {
    titleLabel_.setText("Tempo", juce::dontSendNotification);
    titleLabel_.setColour(juce::Label::textColourId, Palette::textPrimary);
    titleLabel_.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    addAndMakeVisible(titleLabel_);

    hintLabel_.setText(u8"Clic : ajouter un changement  -  Glisser : déplacer  -  Clic droit : supprimer  -  Ctrl+clic ou double-clic : rampe jusqu'au suivant  -  "
                       u8"le tempo de départ (tick 0) ne bouge qu'en valeur",
                       juce::dontSendNotification);
    hintLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    hintLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLabel_);
}

void TempoLaneComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    refresh();
}

void TempoLaneComponent::refresh() {
    maxTick_ = 1920 * 4;
    if (project_ != nullptr) {
        for (const auto& t : project_->tracks)
            for (const auto& n : t.notes) maxTick_ = std::max(maxTick_, n.endTick);
        for (const auto& c : project_->tempoMap.changes()) maxTick_ = std::max(maxTick_, c.tick);
    }
    loadPoints();
    repaint();
}

void TempoLaneComponent::loadPoints() {
    points_.clear();
    dragIndex_ = -1;
    if (project_ == nullptr) return;
    for (const auto& c : project_->tempoMap.changes())
        points_.push_back({c.tick, bpmFromMicroseconds(c.microsecondsPerQuarterNote), c.rampToNext});
    std::stable_sort(points_.begin(), points_.end(),
                     [](const Point& a, const Point& b) { return a.tick < b.tick; });
    if (points_.empty() || points_.front().tick != 0)
        points_.insert(points_.begin(), {0, project_->tempoMap.bpmAt(0)});
}

void TempoLaneComponent::commit(const juce::String& label) {
    if (project_ == nullptr) return;
    if (history_ != nullptr) history_->beginEdit(*project_, label.toStdString());
    // La carte est RECONSTRUITE d'un bloc : `clearTempoChanges()` remet 120 au
    // tick 0, puis chaque point la remplace ou s'y ajoute.
    project_->tempoMap.clearTempoChanges();
    for (const auto& p : points_)
        project_->tempoMap.addTempoChange(p.tick, microsecondsFromBpm(
            juce::jlimit(kBpmMin, kBpmMax, p.bpm)), p.ramp);
    if (onTempoEdited) onTempoEdited();
    loadPoints();
    repaint();
}

// ------------------------------------------------------------- géométrie ---

juce::Rectangle<int> TempoLaneComponent::editorArea() const {
    return getLocalBounds().withTrimmedTop(30).reduced(8);
}

int TempoLaneComponent::tickToX(Tick tick) const {
    auto a = editorArea();
    const double ratio = maxTick_ > 0 ? static_cast<double>(tick) / static_cast<double>(maxTick_) : 0.0;
    return a.getX() + static_cast<int>(ratio * a.getWidth());
}

Tick TempoLaneComponent::xToTick(int x) const {
    auto a = editorArea();
    const double ratio = a.getWidth() > 0 ? static_cast<double>(x - a.getX()) / a.getWidth() : 0.0;
    Tick tick = static_cast<Tick>(juce::jlimit(0.0, 1.0, ratio) * static_cast<double>(maxTick_));
    // AIMANTÉ AU TEMPS : un changement de tempo se pose sur un temps, comme
    // dans une partition ; entre deux temps il n'y a rien à ralentir.
    if (project_ != nullptr && project_->ticksPerQuarterNote > 0) {
        const Tick pas = std::max<Tick>(1, static_cast<Tick>(project_->ticksPerQuarterNote));
        tick = ((tick + pas / 2) / pas) * pas;
    }
    return tick;
}

int TempoLaneComponent::bpmToY(double bpm) const {
    auto a = editorArea();
    const double norm = (juce::jlimit(kBpmMin, kBpmMax, bpm) - kBpmMin) / (kBpmMax - kBpmMin);
    return a.getBottom() - static_cast<int>(norm * a.getHeight());
}

double TempoLaneComponent::yToBpm(int y) const {
    auto a = editorArea();
    const double norm = a.getHeight() > 0 ? static_cast<double>(a.getBottom() - y) / a.getHeight() : 0.0;
    // Au demi-BPM près : plus fin ne se lit pas, plus grossier ne se règle pas.
    return std::round((kBpmMin + juce::jlimit(0.0, 1.0, norm) * (kBpmMax - kBpmMin)) * 2.0) / 2.0;
}

int TempoLaneComponent::findPointNear(juce::Point<int> p) const {
    for (size_t i = 0; i < points_.size(); ++i) {
        juce::Point<int> pos(tickToX(points_[i].tick), bpmToY(points_[i].bpm));
        if (pos.getDistanceFrom(p) <= kPointRadius + 4) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------- souris ---

void TempoLaneComponent::mouseDown(const juce::MouseEvent& e) {
    if (project_ == nullptr || !editorArea().contains(e.getPosition())) return;
    const int hit = findPointNear(e.getPosition());
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        // Le point au tick 0 est le tempo de départ : on ne le supprime pas,
        // un morceau a toujours un tempo.
        if (hit > 0) {
            points_.erase(points_.begin() + hit);
            dragIndex_ = -1;
            commit("Supprimer un changement de tempo");
        }
        return;
    }
    // D15.5 : Ctrl+clic sur un point bascule sa RAMPE vers le suivant.
    if (hit >= 0 && e.mods.isCommandDown()) { toggleRamp(static_cast<size_t>(hit)); return; }
    dragged_ = false;
    if (hit >= 0) { dragIndex_ = hit; return; }
    const Tick tick = xToTick(e.x);
    const double bpm = yToBpm(e.y);
    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [tick](const Point& p) { return p.tick == tick; }),
                  points_.end());
    points_.push_back({tick, bpm});
    std::sort(points_.begin(), points_.end(), [](const Point& a, const Point& b) { return a.tick < b.tick; });
    for (size_t i = 0; i < points_.size(); ++i)
        if (points_[i].tick == tick) { dragIndex_ = static_cast<int>(i); break; }
    commit("Ajouter un changement de tempo");
}

void TempoLaneComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragIndex_ < 0 || dragIndex_ >= static_cast<int>(points_.size())) return;
    const size_t i = static_cast<size_t>(dragIndex_);
    if (i > 0) {
        Tick newTick = xToTick(e.x);
        newTick = std::max(newTick, points_[i - 1].tick + 1);
        if (i + 1 < points_.size()) newTick = std::min(newTick, points_[i + 1].tick - 1);
        points_[i].tick = newTick;
    }
    points_[i].bpm = yToBpm(e.y);
    dragged_ = true;
    repaint();
}

void TempoLaneComponent::mouseUp(const juce::MouseEvent&) {
    if (dragIndex_ >= 0 && dragged_) commit(u8"Déplacer un changement de tempo");
    dragIndex_ = -1;
    dragged_ = false;
}

void TempoLaneComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (project_ == nullptr) return;
    const int hit = findPointNear(e.getPosition());
    if (hit >= 0) toggleRamp(static_cast<size_t>(hit));
}

void TempoLaneComponent::toggleRamp(size_t index) {
    if (index + 1 >= points_.size()) return;   // rien vers quoi glisser après le dernier point
    points_[index].ramp = !points_[index].ramp;
    dragIndex_ = -1;
    commit(points_[index].ramp ? u8"Rampe de tempo" : u8"Palier de tempo");
}

// ----------------------------------------------------------------- paint ---

void TempoLaneComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
    auto a = editorArea();
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(a.toFloat(), 4.0f);

    if (project_ != nullptr && project_->ticksPerQuarterNote > 0) {
        const Tick ticksPerBar = static_cast<Tick>(project_->ticksPerQuarterNote) * 4;
        g.setColour(Palette::gridLine);
        for (Tick t = 0; t <= maxTick_; t += ticksPerBar)
            g.drawVerticalLine(tickToX(t), static_cast<float>(a.getY()), static_cast<float>(a.getBottom()));
    }
    g.setColour(Palette::gridLineStrong);
    for (double bpm : {60.0, 120.0, 180.0})
        g.drawHorizontalLine(bpmToY(bpm), static_cast<float>(a.getX()), static_cast<float>(a.getRight()));
    g.setColour(Palette::textSecondary);
    // L'échelle à DROITE : le point du tick 0 vit sur le bord gauche, et son
    // étiquette y recouvrait celle de l'échelle.
    for (double bpm : {kBpmMin, 60.0, 120.0, 180.0, kBpmMax})
        g.drawText(juce::String(static_cast<int>(bpm)), a.getRight() - 42, bpmToY(bpm) - 7, 40, 14,
                   juce::Justification::centredRight);

    if (project_ == nullptr) {
        g.drawText(u8"Aucun projet.", a, juce::Justification::centred);
        return;
    }

    // EN PALIERS : un tempo vaut jusqu'au suivant -- ou EN RAMPE (D15.5) : il
    // glisse jusqu'au suivant, et la courbe le montre en pente.
    juce::Path path;
    for (size_t i = 0; i < points_.size(); ++i) {
        const float x = static_cast<float>(tickToX(points_[i].tick));
        const float y = static_cast<float>(bpmToY(points_[i].bpm));
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);
        const bool rampe = points_[i].ramp && i + 1 < points_.size();
        const float xFin = i + 1 < points_.size() ? static_cast<float>(tickToX(points_[i + 1].tick))
                                                  : static_cast<float>(a.getRight());
        const float yFin = rampe ? static_cast<float>(bpmToY(points_[i + 1].bpm)) : y;
        path.lineTo(xFin, yFin);
    }
    g.setColour(Palette::accentAmber);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    for (const auto& p : points_) {
        const float x = static_cast<float>(tickToX(p.tick));
        const float y = static_cast<float>(bpmToY(p.bpm));
        g.setColour(Palette::accentTeal);
        g.fillEllipse(x - kPointRadius, y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
        g.setColour(Palette::textPrimary);
        g.drawText(juce::String(p.bpm, 1) + " BPM", static_cast<int>(x) + 8, static_cast<int>(y) - 16, 80, 14,
                   juce::Justification::centredLeft);
    }
    if (points_.size() <= 1) {
        g.setColour(Palette::textSecondary.withAlpha(0.7f));
        g.drawText(u8"Un seul tempo pour tout le morceau — cliquez pour poser un changement.",
                   a.withTrimmedTop(20), juce::Justification::centredTop);
    }
}

void TempoLaneComponent::resized() {
    auto top = getLocalBounds().removeFromTop(26).reduced(6, 2);
    titleLabel_.setBounds(top.removeFromLeft(80));
    hintLabel_.setBounds(top);
}
