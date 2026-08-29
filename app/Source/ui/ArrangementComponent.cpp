#include "ArrangementComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <algorithm>

using namespace vsm::sequencer;
using namespace vsm::ui;

namespace {
/// Largeur, en pixels, de la zone sensible d'un bord de clip. Assez large pour
/// qu'on l'attrape sans viser, assez étroite pour qu'un clip court reste
/// déplaçable par son milieu.
constexpr float kBordSensible = 6.0f;
}

ArrangementComponent::ArrangementComponent() {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ArrangementComponent::setProject(Project* project) {
    project_ = project;
    selection_.clear();
    if (project_ != nullptr) project_->assignClipIds();
    repaint();
}

void ArrangementComponent::setPlayheadTick(vsm::midi::Tick tick) {
    if (tick == playhead_) return;
    playhead_ = tick;
    repaint();
}

float ArrangementComponent::tickToX(vsm::midi::Tick tick) const {
    return static_cast<float>(kHeaderWidth)
         + static_cast<float>((tick - scrollTick_) * pixelsPerTick_);
}

vsm::midi::Tick ArrangementComponent::xToTick(float x) const {
    return scrollTick_
         + static_cast<vsm::midi::Tick>((x - kHeaderWidth) / pixelsPerTick_);
}

vsm::midi::Tick ArrangementComponent::snapTick(vsm::midi::Tick tick) const {
    if (!snap_ || project_ == nullptr) return tick;
    // AIMANTATION À LA MESURE, et non à la noire : on arrange par mesures, et
    // un clip qui tomberait sur un temps quelconque ne serait presque jamais ce
    // qu'on voulait.
    const vsm::midi::Tick parMesure =
        project_->timeSignatureMap.ticksPerBar(std::max<vsm::midi::Tick>(0, tick),
                                                project_->ticksPerQuarterNote);
    if (parMesure <= 0) return tick;
    return ((tick + parMesure / 2) / parMesure) * parMesure;
}

int ArrangementComponent::trackAtY(float y) const {
    if (project_ == nullptr || y < kRulerHeight) return -1;
    const int index = static_cast<int>((y - kRulerHeight) / kTrackHeight);
    return index >= 0 && index < static_cast<int>(project_->tracks.size()) ? index : -1;
}

vsm::midi::Tick ArrangementComponent::materialEnd(const Track& track) const {
    vsm::midi::Tick fin = 0;
    for (const auto& note : track.notes) fin = std::max(fin, note.endTick);
    if (track.kind == Track::Kind::Audio && project_ != nullptr && track.audio.sampleRate > 0.0)
        fin = std::max(fin, project_->secondsToTicks(track.audio.durationSeconds()));
    return fin;
}

Clip* ArrangementComponent::clipAt(juce::Point<float> point, size_t& trackIndex, Geste& bord) {
    bord = Geste::Deplacer;
    const int piste = trackAtY(point.y);
    if (piste < 0 || project_ == nullptr) return nullptr;
    auto& track = project_->tracks[static_cast<size_t>(piste)];
    const vsm::midi::Tick fin = materialEnd(track);

    // D'ARRIÈRE EN AVANT : quand deux clips se chevauchent, c'est celui du
    // dessus -- le dernier dessiné -- qu'on attrape, comme partout ailleurs.
    for (auto it = track.clips.rbegin(); it != track.clips.rend(); ++it) {
        const float x1 = tickToX(it->startTick);
        const float x2 = tickToX(it->startTick + clipPlayedLength(*it, fin));
        if (point.x < x1 || point.x > x2) continue;
        trackIndex = static_cast<size_t>(piste);
        if (point.x - x1 <= kBordSensible && x2 - x1 > 3 * kBordSensible) bord = Geste::BordGauche;
        else if (x2 - point.x <= kBordSensible && x2 - x1 > 3 * kBordSensible) bord = Geste::BordDroit;
        return &(*it);
    }
    return nullptr;
}

void ArrangementComponent::mouseDown(const juce::MouseEvent& event) {
    if (project_ == nullptr) return;
    const auto point = event.position;

    // La règle : on y pose la tête de lecture, on n'y saisit pas de clip.
    if (point.y < kRulerHeight) {
        if (onPlayheadRequested) onPlayheadRequested(std::max<vsm::midi::Tick>(0, xToTick(point.x)));
        return;
    }
    if (point.x < kHeaderWidth) {
        const int piste = trackAtY(point.y);
        if (piste >= 0 && onTrackSelected) onTrackSelected(static_cast<size_t>(piste));
        return;
    }

    size_t piste = 0;
    Geste bord = Geste::Aucun;
    Clip* clip = clipAt(point, piste, bord);
    if (clip == nullptr) {
        if (!event.mods.isShiftDown()) selection_.clear();
        repaint();
        return;
    }
    if (onTrackSelected) onTrackSelected(piste);

    // MAJ ÉTEND la sélection, comme partout. Sans Maj, cliquer un clip déjà
    // sélectionné GARDE la sélection : sinon, saisir un groupe de clips par
    // l'un d'eux le réduirait à celui-là juste avant de le déplacer.
    if (event.mods.isShiftDown()) {
        if (selection_.count(clip->id)) selection_.erase(clip->id);
        else selection_.insert(clip->id);
    } else if (selection_.count(clip->id) == 0) {
        selection_.clear();
        selection_.insert(clip->id);
    }

    // ALT COUPE, plutôt qu'un OUTIL qu'on choisit et qu'on oublie de quitter.
    // Un mode se laisse allumé, et le geste suivant fait autre chose que ce
    // qu'on croit ; un modificateur ne dure que le temps où on le tient. C'est
    // la même raison qui fait dessiner la région de boucle avec Maj sur la
    // règle du piano roll plutôt qu'avec un outil « boucle ».
    if (event.mods.isAltDown()) {
        const vsm::midi::Tick ou = snapTick(xToTick(point.x));
        auto& track = project_->tracks[piste];
        uint64_t compteur = project_->peekNextClipId();
        if (onEditStarted) onEditStarted(u8"Couper un clip");
        const size_t coupes = splitClips(track.clips, selection_, ou, materialEnd(track), compteur,
                                          [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); });
        project_->ensureClipIdAbove(compteur - 1);
        if (coupes > 0) notifyChanged();
        repaint();
        return;
    }

    geste_ = bord;
    gesteOrigine_ = xToTick(point.x);
    gesteDernier_ = gesteOrigine_;
    if (onEditStarted)
        onEditStarted(bord == Geste::Deplacer ? juce::String(u8"Déplacer un clip")
                                               : juce::String(u8"Redimensionner un clip"));
    repaint();
}

void ArrangementComponent::mouseDrag(const juce::MouseEvent& event) {
    if (project_ == nullptr || geste_ == Geste::Aucun || selection_.empty()) return;
    const int piste = trackAtY(event.position.y);
    juce::ignoreUnused(piste);

    // ON APPLIQUE LE DÉLTA DEPUIS LA DERNIÈRE POSITION, pas depuis l'origine :
    // les opérations de `ClipEdit` sont RELATIVES, et rejouer le geste entier à
    // chaque mouvement le doublerait.
    const vsm::midi::Tick maintenant = snapTick(xToTick(event.position.x));
    const vsm::midi::Tick delta = maintenant - gesteDernier_;
    if (delta == 0) return;
    gesteDernier_ = maintenant;

    for (auto& track : project_->tracks) {
        const vsm::midi::Tick fin = materialEnd(track);
        auto conversion = [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); };
        switch (geste_) {
            case Geste::Deplacer:   moveClips(track.clips, selection_, delta); break;
            case Geste::BordDroit:  resizeClipsEnd(track.clips, selection_, delta, fin); break;
            case Geste::BordGauche: resizeClipsStart(track.clips, selection_, delta, fin, conversion); break;
            case Geste::Aucun: break;
        }
    }
    notifyChanged();
    repaint();
}

void ArrangementComponent::mouseUp(const juce::MouseEvent&) {
    geste_ = Geste::Aucun;
}

void ArrangementComponent::mouseMove(const juce::MouseEvent& event) {
    size_t piste = 0;
    Geste bord = Geste::Aucun;
    const Geste avant = survol_;
    survol_ = (project_ != nullptr && clipAt(event.position, piste, bord) != nullptr)
                  ? bord : Geste::Aucun;
    if (survol_ != avant) updateMouseCursor();
}

juce::MouseCursor ArrangementComponent::getMouseCursor() {
    if (survol_ == Geste::BordGauche || survol_ == Geste::BordDroit)
        return juce::MouseCursor::LeftRightResizeCursor;
    return juce::MouseCursor::NormalCursor;
}

void ArrangementComponent::deleteSelection() {
    if (project_ == nullptr || selection_.empty()) return;
    if (onEditStarted) onEditStarted(u8"Supprimer des clips");
    for (auto& track : project_->tracks)
        track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(),
                                          [this](const Clip& c) { return selection_.count(c.id) > 0; }),
                           track.clips.end());
    selection_.clear();
    notifyChanged();
    repaint();
}

bool ArrangementComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (!hasSelection()) return false;
        deleteSelection();
        return true;
    }
    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=') {
        zoomHorizontally(1.25f);
        return true;
    }
    if (key.getTextCharacter() == '-') {
        zoomHorizontally(0.8f);
        return true;
    }
    return false;
}

void ArrangementComponent::zoomHorizontally(float facteur) {
    pixelsPerTick_ = juce::jlimit(0.004, 2.0, pixelsPerTick_ * static_cast<double>(facteur));
    repaint();
}

void ArrangementComponent::notifyChanged() {
    if (onClipsChanged) onClipsChanged();
}

void ArrangementComponent::resized() {}

void ArrangementComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
    if (project_ == nullptr) return;
    const auto bounds = getLocalBounds();

    // --- Règle : une graduation par mesure -----------------------------------
    g.setColour(Palette::panel);
    g.fillRect(0, 0, bounds.getWidth(), kRulerHeight);
    const vsm::midi::Tick parMesure =
        project_->timeSignatureMap.ticksPerBar(0, project_->ticksPerQuarterNote);
    if (parMesure > 0) {
        const vsm::midi::Tick premier = (scrollTick_ / parMesure) * parMesure;
        for (vsm::midi::Tick t = premier; tickToX(t) < bounds.getWidth(); t += parMesure) {
            const float x = tickToX(t);
            if (x < kHeaderWidth) continue;
            g.setColour(Palette::border);
            g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.0f);
            g.setColour(Palette::textSecondary);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(juce::String(static_cast<int>(t / parMesure) + 1),
                        static_cast<int>(x) + 3, 2, 40, kRulerHeight - 4,
                        juce::Justification::centredLeft);
        }
    }

    // --- Pistes et clips ------------------------------------------------------
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        const auto& track = project_->tracks[i];
        const int y = kRulerHeight + static_cast<int>(i) * kTrackHeight;
        if (y > bounds.getHeight()) break;

        g.setColour(i % 2 == 0 ? Palette::panel : Palette::panelRaised);
        g.fillRect(0, y, kHeaderWidth, kTrackHeight);
        g.setColour(juce::Colour(track.colorRgba));
        g.fillRect(0, y, 5, kTrackHeight);
        g.setColour(Palette::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(juce::String(track.name), 10, y + 4, kHeaderWidth - 16, 18,
                    juce::Justification::centredLeft);
        g.setColour(Palette::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(track.kind == Track::Kind::Audio  ? "audio"
                    : track.kind == Track::Kind::Group ? "groupe"
                                                       : "midi",
                    10, y + 22, kHeaderWidth - 16, 14, juce::Justification::centredLeft);

        g.setColour(Palette::border);
        g.drawLine(0.0f, static_cast<float>(y + kTrackHeight),
                    static_cast<float>(bounds.getWidth()), static_cast<float>(y + kTrackHeight), 1.0f);

        const vsm::midi::Tick fin = materialEnd(track);
        for (const auto& clip : track.clips) {
            const float x1 = std::max(static_cast<float>(kHeaderWidth), tickToX(clip.startTick));
            const float x2 = tickToX(clip.startTick + clipPlayedLength(clip, fin));
            if (x2 <= kHeaderWidth || x1 >= bounds.getWidth()) continue;

            juce::Rectangle<float> r(x1, static_cast<float>(y + 3),
                                      std::max(2.0f, x2 - x1), static_cast<float>(kTrackHeight - 7));
            const bool choisi = selection_.count(clip.id) > 0;
            // UN CLIP MUET SE VOIT SANS DISPARAÎTRE : hachuré plutôt qu'effacé,
            // comme une note muette dans le piano roll.
            g.setColour(juce::Colour(clip.colorRgba).withAlpha(clip.muted ? 0.25f : 0.75f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(choisi ? Palette::textPrimary : Palette::border);
            g.drawRoundedRectangle(r, 3.0f, choisi ? 2.0f : 1.0f);
            if (!clip.name.empty() && r.getWidth() > 30.0f) {
                g.setColour(Palette::textPrimary);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(juce::String(clip.name), r.reduced(4.0f, 2.0f),
                            juce::Justification::topLeft, true);
            }
        }
    }

    // --- En-tête et tête de lecture ------------------------------------------
    g.setColour(Palette::border);
    g.drawLine(static_cast<float>(kHeaderWidth), 0.0f,
                static_cast<float>(kHeaderWidth), static_cast<float>(bounds.getHeight()), 1.0f);
    const float xTete = tickToX(playhead_);
    if (xTete >= kHeaderWidth) {
        g.setColour(Palette::accentAmber);
        g.drawLine(xTete, 0.0f, xTete, static_cast<float>(bounds.getHeight()), 1.5f);
    }

    if (project_->tracks.empty()) {
        g.setColour(Palette::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(u8"Aucune piste — Piste ▸ Ajouter une piste",
                    bounds.withTrimmedLeft(kHeaderWidth), juce::Justification::centred);
    }
}
