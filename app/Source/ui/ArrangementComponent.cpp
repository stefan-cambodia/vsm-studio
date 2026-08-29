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
    // AIMANTATION À LA MESURE PAR DÉFAUT, et non à la double croche : on arrange
    // par mesures, et la grille du piano roll n'a pas de sens à cette échelle.
    // Mais elle reste disponible -- c'est ce qu'il faut pour poser un clip sur
    // un contretemps, et c'est le « mêmes gestes que le piano roll » du critère.
    const vsm::midi::Tick pas =
        aimanteALaMesure_
            ? project_->timeSignatureMap.ticksPerBar(std::max<vsm::midi::Tick>(0, tick),
                                                      project_->ticksPerQuarterNote)
            : vsm::sequencer::gridResolutionToTicks(
                  gridProvider ? gridProvider()
                               : vsm::sequencer::GridResolution{vsm::sequencer::NoteValue::Quarter,
                                                                 false, false},
                  project_->ticksPerQuarterNote);
    if (pas <= 0) return tick;
    return ((tick + pas / 2) / pas) * pas;
}

void ArrangementComponent::copySelection() {
    if (project_ == nullptr || selection_.empty()) return;
    presse_papiers_.clear();
    for (size_t p = 0; p < project_->tracks.size(); ++p)
        for (const auto& clip : project_->tracks[p].clips)
            if (selection_.count(clip.id) > 0) {
                presse_papiers_.push_back(clip);
                pistePressePapiers_ = p;
            }
}

void ArrangementComponent::paste() {
    if (project_ == nullptr || presse_papiers_.empty()) return;
    // ON COLLE À LA TÊTE DE LECTURE, comme le piano roll : c'est le seul point
    // que l'utilisateur regarde en collant, et il n'a pas à viser à la souris.
    vsm::midi::Tick plusTot = presse_papiers_.front().startTick;
    for (const auto& clip : presse_papiers_) plusTot = std::min(plusTot, clip.startTick);
    const vsm::midi::Tick decalage = playhead_ - plusTot;

    // LA PISTE COURANTE, pas celle d'origine : coller sur la piste qu'on
    // regarde est ce qu'on attend, et c'est aussi ce qui permet de recopier un
    // motif d'une piste à l'autre.
    const size_t cible = pisteCourante_ < project_->tracks.size() ? pisteCourante_
                                                                   : pistePressePapiers_;
    if (cible >= project_->tracks.size()) return;

    if (onEditStarted) onEditStarted(u8"Coller des clips");
    selection_.clear();
    for (const auto& source : presse_papiers_) {
        Clip copie = source;
        copie.id = project_->nextClipId();
        copie.startTick = std::max<vsm::midi::Tick>(0, source.startTick + decalage);
        selection_.insert(copie.id);
        project_->tracks[cible].clips.push_back(std::move(copie));
    }
    std::stable_sort(project_->tracks[cible].clips.begin(), project_->tracks[cible].clips.end(),
                      [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
    notifyChanged();
    repaint();
}

void ArrangementComponent::duplicateSelection() {
    if (project_ == nullptr || selection_.empty()) return;

    // LE DÉCALAGE EST LA LONGUEUR DE LA SÉLECTION, arrondie à la grille :
    // dupliquer une mesure doit tomber pile sur la suivante. C'est exactement
    // la règle du piano roll, et elle vaut ici pour la même raison.
    vsm::midi::Tick debut = 0, fin = 0;
    bool trouve = false;
    for (const auto& track : project_->tracks)
        if (clipSelectionBounds(track.clips, selection_, materialEnd(track), debut, fin))
            trouve = true;
    if (!trouve) return;

    vsm::midi::Tick decalage = fin - debut;
    const vsm::midi::Tick pas =
        aimanteALaMesure_
            ? project_->timeSignatureMap.ticksPerBar(debut, project_->ticksPerQuarterNote)
            : vsm::sequencer::gridResolutionToTicks(
                  gridProvider ? gridProvider()
                               : vsm::sequencer::GridResolution{vsm::sequencer::NoteValue::Quarter,
                                                                 false, false},
                  project_->ticksPerQuarterNote);
    if (pas > 0) decalage = std::max(pas, ((decalage + pas - 1) / pas) * pas);

    if (onEditStarted) onEditStarted(u8"Dupliquer des clips");
    uint64_t compteur = project_->peekNextClipId();
    ClipSelection creees;
    for (auto& track : project_->tracks) {
        const auto copies = duplicateClips(track.clips, selection_, decalage, compteur);
        creees.insert(copies.begin(), copies.end());
    }
    project_->ensureClipIdAbove(compteur - 1);
    if (!creees.empty()) {
        selection_ = std::move(creees);
        notifyChanged();
    }
    repaint();
}

int ArrangementComponent::trackHeight(const Track& track) const {
    return track.folded ? kFoldedHeight
                        : juce::jlimit(kMinHeight, kMaxHeight, track.arrangementHeight);
}

int ArrangementComponent::trackTop(size_t index) const {
    int y = kRulerHeight;
    for (size_t i = 0; i < index && i < project_->tracks.size(); ++i)
        y += trackHeight(project_->tracks[i]);
    return y;
}

int ArrangementComponent::trackAtY(float y) const {
    if (project_ == nullptr || y < kRulerHeight) return -1;
    // LES HAUTEURS SONT VARIABLES (D5.3) : on parcourt, on ne divise pas. Une
    // division supposerait que toutes les pistes ont la même taille, ce qui
    // n'est plus vrai dès qu'on en plie une.
    int haut = kRulerHeight;
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        const int bas = haut + trackHeight(project_->tracks[i]);
        if (y >= haut && y < bas) return static_cast<int>(i);
        haut = bas;
    }
    return -1;
}

juce::Rectangle<float> ArrangementComponent::foldZone(size_t index) const {
    if (project_ == nullptr || index >= project_->tracks.size()) return {};
    return juce::Rectangle<float>(6.0f, static_cast<float>(trackTop(index)) + 3.0f, 14.0f, 14.0f);
}

juce::Rectangle<float> ArrangementComponent::colourZone(size_t index) const {
    if (project_ == nullptr || index >= project_->tracks.size()) return {};
    return juce::Rectangle<float>(0.0f, static_cast<float>(trackTop(index)), 5.0f,
                                   static_cast<float>(trackHeight(project_->tracks[index])));
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
        if (piste < 0) return;
        const size_t index = static_cast<size_t>(piste);
        pisteCourante_ = index;
        if (onTrackSelected) onTrackSelected(index);
        auto& track = project_->tracks[index];

        // LE TRIANGLE PLIE ET DÉPLIE. Plier n'écrase pas la hauteur réglée : on
        // la retrouve en dépliant, sinon le travail de mise en page serait
        // perdu au premier pli.
        if (foldZone(index).contains(point)) {
            if (onEditStarted) onEditStarted(track.folded ? u8"Déplier une piste"
                                                           : u8"Plier une piste");
            track.folded = !track.folded;
            notifyChanged();
            repaint();
            return;
        }
        // LE BANDEAU DE COULEUR OUVRE LE CHOIX DE COULEUR. C'est le seul
        // endroit qui en montre déjà une : y cliquer pour en changer n'a pas
        // besoin d'être expliqué.
        if (colourZone(index).contains(point)) {
            if (onColourRequested) onColourRequested(index);
            return;
        }
        // LE BAS DE L'EN-TÊTE RÈGLE LA HAUTEUR, le reste RÉORDONNE. Deux gestes
        // dans la même bande, séparés par l'endroit où l'on saisit -- comme le
        // bord d'un clip le distingue de son milieu.
        const int bas = trackTop(index) + trackHeight(track);
        if (!track.folded && point.y > static_cast<float>(bas) - 5.0f) {
            geste_ = Geste::Hauteur;
            pisteSaisie_ = piste;
            hauteurOrigine_ = trackHeight(track);
            ySaisie_ = point.y;
            if (onEditStarted) onEditStarted(u8"Hauteur d'une piste");
            return;
        }
        geste_ = Geste::Reordonner;
        pisteSaisie_ = piste;
        ySaisie_ = point.y;
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
    pisteCourante_ = piste;
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
    if (project_ == nullptr || geste_ == Geste::Aucun) return;

    if (geste_ == Geste::Hauteur && pisteSaisie_ >= 0) {
        auto& track = project_->tracks[static_cast<size_t>(pisteSaisie_)];
        track.arrangementHeight = juce::jlimit(
            kMinHeight, kMaxHeight,
            hauteurOrigine_ + static_cast<int>(event.position.y - ySaisie_));
        repaint();
        return;
    }
    if (geste_ == Geste::Reordonner && pisteSaisie_ >= 0) {
        const int sous = trackAtY(event.position.y);
        if (sous >= 0 && sous != pisteSaisie_) {
            // UN PAS À LA FOIS, et l'instantané d'annulation n'est pris qu'au
            // premier : traverser six pistes est UN geste, pas six.
            if (onEditStarted && !reordonnancementOuvert_) {
                reordonnancementOuvert_ = true;
                onEditStarted(u8"Réordonner les pistes");
            }
            vsm::sequencer::moveTrack(*project_, static_cast<size_t>(pisteSaisie_),
                                       static_cast<size_t>(sous));
            pisteSaisie_ = sous;
            pisteCourante_ = static_cast<size_t>(sous);
            notifyChanged();
            repaint();
        }
        return;
    }
    if (selection_.empty()) return;
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
    if (geste_ == Geste::Hauteur || geste_ == Geste::Reordonner) notifyChanged();
    geste_ = Geste::Aucun;
    pisteSaisie_ = -1;
    reordonnancementOuvert_ = false;
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
    // LES MÊMES RACCOURCIS QUE LE PIANO ROLL, à la lettre : Ctrl+C, Ctrl+V,
    // Ctrl+D. Deux vues du même morceau qui demanderaient deux gestes
    // différents pour la même chose seraient deux logiciels.
    if (key.getModifiers().isCommandDown()) {
        switch (key.getTextCharacter()) {
            case 'c': case 'C': copySelection(); return true;
            case 'v': case 'V': paste(); return true;
            case 'd': case 'D': duplicateSelection(); return true;
            case 'x': case 'X': copySelection(); deleteSelection(); return true;
            default: break;
        }
    }
    // `S` coupe l'aimantation, `G` bascule entre la MESURE et la grille fine du
    // piano roll -- les deux réglages qu'on change en arrangeant, et les seuls.
    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S') {
        snap_ = !snap_;
        repaint();
        return true;
    }
    if (key.getTextCharacter() == 'g' || key.getTextCharacter() == 'G') {
        aimanteALaMesure_ = !aimanteALaMesure_;
        repaint();
        return true;
    }
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
        const int y = trackTop(i);
        const int h = trackHeight(track);
        if (y > bounds.getHeight()) break;

        g.setColour(i % 2 == 0 ? Palette::panel : Palette::panelRaised);
        g.fillRect(0, y, kHeaderWidth, h);
        g.setColour(juce::Colour(track.colorRgba));
        g.fillRect(0, y, 5, h);

        // LE TRIANGLE DE PLIAGE, tourné vers le bas quand la piste est ouverte
        // et vers la droite quand elle est pliée -- la convention de tous les
        // arbres, et la seule qu'on n'ait pas à expliquer.
        {
            juce::Path triangle;
            const auto z = foldZone(i).reduced(3.0f);
            if (track.folded)
                triangle.addTriangle(z.getX(), z.getY(), z.getX(), z.getBottom(),
                                      z.getRight(), z.getCentreY());
            else
                triangle.addTriangle(z.getX(), z.getY(), z.getRight(), z.getY(),
                                      z.getCentreX(), z.getBottom());
            g.setColour(Palette::textSecondary);
            g.fillPath(triangle);
        }

        g.setColour(Palette::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(juce::String(track.name), 24, y + 2, kHeaderWidth - 30,
                    std::min(18, h - 2), juce::Justification::centredLeft);
        // LA NATURE DE LA PISTE NE S'AFFICHE QUE SI LA PLACE EXISTE : sur une
        // piste pliée, le nom seul est ce qu'on est venu chercher.
        if (h >= 40) {
            g.setColour(Palette::textSecondary);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(track.kind == Track::Kind::Audio  ? "audio"
                        : track.kind == Track::Kind::Group ? "groupe"
                                                           : "midi",
                        24, y + 22, kHeaderWidth - 30, 14, juce::Justification::centredLeft);
        }

        g.setColour(Palette::border);
        g.drawLine(0.0f, static_cast<float>(y + h),
                    static_cast<float>(bounds.getWidth()), static_cast<float>(y + h), 1.0f);

        const vsm::midi::Tick fin = materialEnd(track);
        for (const auto& clip : track.clips) {
            const float x1 = std::max(static_cast<float>(kHeaderWidth), tickToX(clip.startTick));
            const float x2 = tickToX(clip.startTick + clipPlayedLength(clip, fin));
            if (x2 <= kHeaderWidth || x1 >= bounds.getWidth()) continue;

            juce::Rectangle<float> r(x1, static_cast<float>(y + 3),
                                      std::max(2.0f, x2 - x1), static_cast<float>(h - 7));
            const bool choisi = selection_.count(clip.id) > 0;
            // UN CLIP MUET SE VOIT SANS DISPARAÎTRE : hachuré plutôt qu'effacé,
            // comme une note muette dans le piano roll.
            g.setColour(juce::Colour(clip.colorRgba).withAlpha(clip.muted ? 0.25f : 0.75f));
            g.fillRoundedRectangle(r, 3.0f);
            // UN CLIP QUI BOUCLE DOIT SE VOIR BOUCLER. Étiré au-delà de son
            // matériau, il répète sa fenêtre (D5.2) -- et dessiné comme un
            // simple rectangle plus long, il mentirait sur ce qu'il joue. Un
            // trait fin à chaque tour, et on lit d'un coup d'œil combien de
            // fois le motif revient.
            const vsm::midi::Tick jouee = clipPlayedLength(clip, fin);
            const vsm::midi::Tick fenetre = clip.sourceLength > 0 ? clip.sourceLength : jouee;
            if (fenetre > 0 && jouee > fenetre) {
                g.setColour(Palette::background.withAlpha(0.55f));
                for (vsm::midi::Tick t = fenetre; t < jouee; t += fenetre) {
                    const float x = tickToX(clip.startTick + t);
                    if (x > r.getX() && x < r.getRight())
                        g.drawLine(x, r.getY() + 1.0f, x, r.getBottom() - 1.0f, 1.0f);
                }
            }

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

    // L'ÉTAT D'AIMANTATION EST ÉCRIT, en petit, dans le coin de la règle. Un
    // réglage qu'on bascule au clavier et qui ne se voit nulle part se retourne
    // contre celui qui l'a basculé sans s'en souvenir.
    g.setColour(Palette::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(snap_ ? (aimanteALaMesure_ ? "aimant : mesure" : "aimant : grille") : "aimant : libre",
                4, 2, kHeaderWidth - 8, kRulerHeight - 4, juce::Justification::centredRight);

    if (project_->tracks.empty()) {
        g.setColour(Palette::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(u8"Aucune piste — Piste ▸ Ajouter une piste",
                    bounds.withTrimmedLeft(kHeaderWidth), juce::Justification::centred);
    }
}
