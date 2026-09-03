#include "MidiCcComponent.h"
#include <algorithm>

using vsm::midi::Tick;
using vsm::sequencer::Track;
using namespace vsm::ui;

namespace {
// Les contrôleurs qu'un musicien nomme sans regarder la table ; les autres
// gardent leur numéro. Proposés en tête de liste, puis tous les autres.
struct NamedCc { int id; const char* name; };
constexpr NamedCc kNamed[] = {
    {1, "modulation"}, {2, "souffle"}, {4, "pédale"}, {5, "portamento"},
    {7, "volume"}, {10, "panoramique"}, {11, "expression"},
    {64, "sustain"}, {65, "portamento on/off"}, {66, "sostenuto"}, {67, "sourdine"},
    {71, "résonance"}, {72, "relâchement"}, {73, "attaque"}, {74, "coupure"},
    {91, "réverbération"}, {93, "chorus"},
};
} // namespace

juce::String MidiCcComponent::controllerName(int controller) {
    for (const auto& n : kNamed)
        if (n.id == controller)
            return juce::String(controller) + juce::String::fromUTF8(" · ") + juce::String::fromUTF8(n.name);
    return "CC " + juce::String(controller);
}

MidiCcComponent::MidiCcComponent() {
    trackLabel_.setText("Piste", juce::dontSendNotification);
    trackLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addAndMakeVisible(trackLabel_);
    addAndMakeVisible(trackBox_);
    trackBox_.onChange = [this] {
        selectedTrack_ = static_cast<size_t>(juce::jmax(0, trackBox_.getSelectedItemIndex()));
        rebuildControllerBox();
    };

    controllerLabel_.setText(u8"Contrôleur", juce::dontSendNotification);
    controllerLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addAndMakeVisible(controllerLabel_);
    addAndMakeVisible(controllerBox_);
    controllerBox_.onChange = [this] {
        const int idx = controllerBox_.getSelectedItemIndex();
        if (idx >= 0 && idx < static_cast<int>(controllerIds_.size())) {
            selectedController_ = controllerIds_[static_cast<size_t>(idx)];
            loadPoints();
            repaint();
        }
    };

    hintLabel_.setText(u8"Clic : ajouter  -  Glisser : déplacer  -  Clic droit : supprimer  -  "
                       u8"un CC vaut jusqu'au suivant",
                       juce::dontSendNotification);
    hintLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    hintLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLabel_);
}

Track* MidiCcComponent::activeTrack() const {
    if (project_ == nullptr || selectedTrack_ >= project_->tracks.size()) return nullptr;
    return &project_->tracks[selectedTrack_];
}

void MidiCcComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    maxTick_ = 1920 * 4;
    if (project_ != nullptr) {
        for (const auto& t : project_->tracks) {
            for (const auto& n : t.notes) maxTick_ = std::max(maxTick_, n.endTick);
            for (const auto& c : t.controlChanges) maxTick_ = std::max(maxTick_, c.tick);
        }
    }
    rebuildTrackBox();
}

void MidiCcComponent::setActiveTrackIndex(size_t trackIndex) {
    if (project_ == nullptr || trackIndex >= project_->tracks.size()) return;
    if (trackIndex == selectedTrack_) return;
    selectedTrack_ = trackIndex;
    trackBox_.setSelectedItemIndex(static_cast<int>(trackIndex), juce::dontSendNotification);
    rebuildControllerBox();
}

void MidiCcComponent::rebuildTrackBox() {
    trackBox_.clear(juce::dontSendNotification);
    if (project_ != nullptr) {
        for (size_t i = 0; i < project_->tracks.size(); ++i) {
            const auto& name = project_->tracks[i].name;
            trackBox_.addItem(name.empty() ? ("Piste " + juce::String(i + 1))
                                           : juce::String::fromUTF8(name.c_str()),
                              static_cast<int>(i) + 1);
        }
    }
    if (trackBox_.getNumItems() > 0) {
        trackBox_.setSelectedItemIndex(juce::jlimit(0, trackBox_.getNumItems() - 1,
                                                    static_cast<int>(selectedTrack_)),
                                       juce::dontSendNotification);
        selectedTrack_ = static_cast<size_t>(trackBox_.getSelectedItemIndex());
    }
    rebuildControllerBox();
}

void MidiCcComponent::rebuildControllerBox() {
    // Les contrôleurs DÉJÀ PRÉSENTS sur la piste d'abord, avec leur compte de
    // points : c'est ce qu'on vient regarder. Puis les nommés, puis le reste.
    controllerBox_.clear(juce::dontSendNotification);
    controllerIds_.clear();
    std::vector<int> presents;
    std::vector<int> comptes(128, 0);
    if (const Track* track = activeTrack()) {
        for (const auto& cc : track->controlChanges)
            if (cc.controller < 128) ++comptes[cc.controller];
        for (int c = 0; c < 128; ++c)
            if (comptes[static_cast<size_t>(c)] > 0) presents.push_back(c);
    }
    int itemId = 1;
    auto ajouter = [&](int c) {
        if (std::find(controllerIds_.begin(), controllerIds_.end(), c) != controllerIds_.end()) return;
        juce::String texte = controllerName(c);
        if (comptes[static_cast<size_t>(c)] > 0)
            texte << "  (" << comptes[static_cast<size_t>(c)] << " point(s))";
        controllerBox_.addItem(texte, itemId++);
        controllerIds_.push_back(c);
    };
    for (int c : presents) ajouter(c);
    for (const auto& n : kNamed) ajouter(n.id);
    for (int c = 0; c < 128; ++c) ajouter(c);

    // Garder le contrôleur choisi s'il existe ; sinon le premier présent.
    int choix = 0;
    if (!presents.empty()
        && std::find(presents.begin(), presents.end(), selectedController_) == presents.end())
        selectedController_ = presents.front();
    for (size_t i = 0; i < controllerIds_.size(); ++i)
        if (controllerIds_[i] == selectedController_) { choix = static_cast<int>(i); break; }
    controllerBox_.setSelectedItemIndex(choix, juce::dontSendNotification);
    selectedController_ = controllerIds_.empty() ? 74 : controllerIds_[static_cast<size_t>(choix)];
    loadPoints();
    repaint();
}

void MidiCcComponent::loadPoints() {
    points_.clear();
    dragIndex_ = -1;
    if (const Track* track = activeTrack()) {
        for (const auto& cc : track->controlChanges)
            if (cc.controller == selectedController_)
                points_.push_back({cc.tick, static_cast<int>(cc.value)});
        std::stable_sort(points_.begin(), points_.end(),
                         [](const Point& a, const Point& b) { return a.tick < b.tick; });
    }
}

void MidiCcComponent::commit(const juce::String& label) {
    Track* track = activeTrack();
    if (track == nullptr) return;
    if (history_ != nullptr && project_ != nullptr) history_->beginEdit(*project_, label.toStdString());
    // Les points de CE contrôleur sont remplacés ; les autres contrôleurs et
    // les autres événements de la piste ne bougent pas.
    auto& liste = track->controlChanges;
    liste.erase(std::remove_if(liste.begin(), liste.end(),
                               [this](const vsm::sequencer::CcPoint& cc) {
                                   return cc.controller == selectedController_;
                               }),
                liste.end());
    for (const auto& p : points_)
        liste.push_back({p.tick, track->channel, static_cast<uint8_t>(selectedController_),
                         static_cast<uint8_t>(juce::jlimit(0, 127, p.value))});
    track->sortEvents();
    if (onCcEdited) onCcEdited();
    rebuildControllerBox();   // le compte de points change
}

// ------------------------------------------------------------- géométrie ---

juce::Rectangle<int> MidiCcComponent::editorArea() const {
    return getLocalBounds().withTrimmedTop(30).reduced(8);
}

int MidiCcComponent::tickToX(Tick tick) const {
    auto a = editorArea();
    const double ratio = maxTick_ > 0 ? static_cast<double>(tick) / static_cast<double>(maxTick_) : 0.0;
    return a.getX() + static_cast<int>(ratio * a.getWidth());
}

Tick MidiCcComponent::xToTick(int x) const {
    auto a = editorArea();
    const double ratio = a.getWidth() > 0 ? static_cast<double>(x - a.getX()) / a.getWidth() : 0.0;
    Tick tick = static_cast<Tick>(juce::jlimit(0.0, 1.0, ratio) * static_cast<double>(maxTick_));
    // AIMANTÉ À LA DOUBLE-CROCHE : un CC posé à la souris se place sur la
    // grille, comme une note ; entre deux cases, il n'y a pas de musique.
    if (project_ != nullptr && project_->ticksPerQuarterNote > 0) {
        const Tick pas = std::max<Tick>(1, static_cast<Tick>(project_->ticksPerQuarterNote) / 4);
        tick = ((tick + pas / 2) / pas) * pas;
    }
    return tick;
}

int MidiCcComponent::valueToY(int value) const {
    auto a = editorArea();
    return a.getBottom() - static_cast<int>(juce::jlimit(0, 127, value) / 127.0 * a.getHeight());
}

int MidiCcComponent::yToValue(int y) const {
    auto a = editorArea();
    const double norm = a.getHeight() > 0 ? static_cast<double>(a.getBottom() - y) / a.getHeight() : 0.0;
    return static_cast<int>(std::lround(juce::jlimit(0.0, 1.0, norm) * 127.0));
}

int MidiCcComponent::findPointNear(juce::Point<int> p) const {
    for (size_t i = 0; i < points_.size(); ++i) {
        juce::Point<int> pos(tickToX(points_[i].tick), valueToY(points_[i].value));
        if (pos.getDistanceFrom(p) <= kPointRadius + 4) return static_cast<int>(i);
    }
    return -1;
}

// ---------------------------------------------------------------- souris ---

void MidiCcComponent::mouseDown(const juce::MouseEvent& e) {
    if (activeTrack() == nullptr || !editorArea().contains(e.getPosition())) return;
    const int hit = findPointNear(e.getPosition());
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (hit >= 0) {
            points_.erase(points_.begin() + hit);
            dragIndex_ = -1;
            commit("Supprimer un CC");
        }
        return;
    }
    dragged_ = false;
    if (hit >= 0) {
        dragIndex_ = hit;
        return;
    }
    const Tick tick = xToTick(e.x);
    const int value = yToValue(e.y);
    // Un point existant au même tick est remplacé : deux valeurs au même
    // instant n'ont pas de sens pour un CC.
    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [tick](const Point& p) { return p.tick == tick; }),
                  points_.end());
    points_.push_back({tick, value});
    std::sort(points_.begin(), points_.end(), [](const Point& a, const Point& b) { return a.tick < b.tick; });
    for (size_t i = 0; i < points_.size(); ++i)
        if (points_[i].tick == tick) { dragIndex_ = static_cast<int>(i); break; }
    commit("Ajouter un CC");
}

void MidiCcComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragIndex_ < 0 || dragIndex_ >= static_cast<int>(points_.size())) return;
    Tick newTick = xToTick(e.x);
    const size_t i = static_cast<size_t>(dragIndex_);
    if (i > 0) newTick = std::max(newTick, points_[i - 1].tick + 1);
    if (i + 1 < points_.size()) newTick = std::min(newTick, points_[i + 1].tick - 1);
    points_[i].tick = newTick;
    points_[i].value = yToValue(e.y);
    dragged_ = true;
    repaint();
}

void MidiCcComponent::mouseUp(const juce::MouseEvent&) {
    if (dragIndex_ >= 0 && dragged_) commit(u8"Déplacer un CC");
    dragIndex_ = -1;
    dragged_ = false;
}

// ----------------------------------------------------------------- paint ---

void MidiCcComponent::paint(juce::Graphics& g) {
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
    for (int v : {0, 64, 127})
        g.drawHorizontalLine(valueToY(v), static_cast<float>(a.getX()), static_cast<float>(a.getRight()));

    const Track* track = activeTrack();
    if (track == nullptr) {
        g.setColour(Palette::textSecondary);
        g.drawText(u8"Sélectionnez une piste.", a, juce::Justification::centred);
        return;
    }

    g.setColour(Palette::textSecondary);
    g.drawText("127", a.getX() + 2, a.getY(), 40, 14, juce::Justification::topLeft);
    g.drawText("64", a.getX() + 2, valueToY(64) - 7, 40, 14, juce::Justification::centredLeft);
    g.drawText("0", a.getX() + 2, a.getBottom() - 14, 40, 14, juce::Justification::bottomLeft);

    if (points_.empty()) {
        g.setColour(Palette::textSecondary.withAlpha(0.7f));
        g.drawText(controllerName(selectedController_)
                   + juce::String::fromUTF8(" : aucun point sur cette piste — cliquez pour en poser un."),
                   a, juce::Justification::centred);
        return;
    }

    // EN PALIERS : un CC vaut jusqu'au suivant. Avant le premier point, la
    // valeur est inconnue -- on ne dessine rien, plutôt qu'un maintien inventé.
    juce::Path path;
    for (size_t i = 0; i < points_.size(); ++i) {
        const float x = static_cast<float>(tickToX(points_[i].tick));
        const float y = static_cast<float>(valueToY(points_[i].value));
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);                       // le palier précédent arrive au nouveau tick
        const float xFin = i + 1 < points_.size() ? static_cast<float>(tickToX(points_[i + 1].tick))
                                                  : static_cast<float>(a.getRight());
        path.lineTo(xFin, y);
    }
    g.setColour(Palette::accentTeal);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    for (const auto& p : points_) {
        const float x = static_cast<float>(tickToX(p.tick));
        const float y = static_cast<float>(valueToY(p.value));
        g.setColour(Palette::accentAmber);
        g.fillEllipse(x - kPointRadius, y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
    }
}

void MidiCcComponent::resized() {
    auto top = getLocalBounds().removeFromTop(26).reduced(6, 2);
    trackLabel_.setBounds(top.removeFromLeft(44));
    trackBox_.setBounds(top.removeFromLeft(170));
    top.removeFromLeft(10);
    controllerLabel_.setBounds(top.removeFromLeft(80));
    controllerBox_.setBounds(top.removeFromLeft(220));
    hintLabel_.setBounds(top);
}
