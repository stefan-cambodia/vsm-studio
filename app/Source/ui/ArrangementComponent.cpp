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
    // D11.3 : PAR PAGES, pas centré en continu — un fond qui glisse à chaque
    // image fatigue et empêche de lire les positions (même règle qu'au piano
    // roll). La page tourne quand la tête sort de la zone des clips.
    if (followPlayhead_) {
        const float x = tickToX(tick);
        const float droite = static_cast<float>(getWidth()) - 40.0f;
        if (x > droite || x < static_cast<float>(kHeaderWidth)) {
            const double visibles = static_cast<double>(getWidth() - kHeaderWidth) / std::max(1.0e-6, pixelsPerTick_);
            scrollTick_ = std::max<vsm::midi::Tick>(0, tick - static_cast<vsm::midi::Tick>(visibles * 0.15));
        }
    }
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
    const vsm::midi::Tick pas = snapStep(tick);
    if (pas <= 0) return tick;
    return ((tick + pas / 2) / pas) * pas;
}

vsm::midi::Tick ArrangementComponent::snapStep(vsm::midi::Tick tick) const {
    if (project_ == nullptr) return 0;
    // AIMANTATION À LA MESURE PAR DÉFAUT, et non à la double croche : on arrange
    // par mesures, et la grille du piano roll n'a pas de sens à cette échelle.
    // Mais elle reste disponible -- c'est ce qu'il faut pour poser un clip sur
    // un contretemps, et c'est le « mêmes gestes que le piano roll » du critère.
    return
        aimanteALaMesure_
            ? project_->timeSignatureMap.ticksPerBar(std::max<vsm::midi::Tick>(0, tick),
                                                      project_->ticksPerQuarterNote)
            : vsm::sequencer::gridResolutionToTicks(
                  gridProvider ? gridProvider()
                               : vsm::sequencer::GridResolution{vsm::sequencer::NoteValue::Quarter,
                                                                 false, false},
                  project_->ticksPerQuarterNote);
}

void ArrangementComponent::nudgeSelection(vsm::midi::Tick delta) {
    if (project_ == nullptr || selection_.empty() || delta == 0) return;
    // D16.5 : ce qu'une piste VERROUILLÉE a refusé se compte AVANT de prendre
    // l'instantané -- un déplacement entièrement refusé ne doit rien laisser
    // dans l'historique, et il doit se dire.
    const size_t verrouilles =
        vsm::sequencer::lockedClipsInSelection(project_->tracks, selection_);
    size_t deplaces = 0;
    if (verrouilles < selection_.size()) {
        if (onEditStarted) onEditStarted(u8"Déplacer des clips");
        for (auto& track : project_->tracks)
            deplaces += vsm::sequencer::moveClips(track, selection_, delta);
        notifyChanged();
        repaint();
    }
    (void)deplaces;
    if (verrouilles > 0 && onLockRefused) onLockRefused(verrouilles);
}

void ArrangementComponent::moveSelectionAcrossTracks(int deltaTracks) {
    if (project_ == nullptr || selection_.empty() || deltaTracks == 0) return;
    if (onEditStarted) onEditStarted(u8"Déplacer des clips de piste");
    const auto rapport = vsm::sequencer::moveClipsAcrossTracks(project_->tracks, selection_, deltaTracks);
    if (rapport.moved > 0) {
        const int cible = static_cast<int>(pisteCourante_) + rapport.applied;
        if (cible >= 0 && static_cast<size_t>(cible) < project_->tracks.size()) {
            pisteCourante_ = static_cast<size_t>(cible);
            if (onTrackSelected) onTrackSelected(pisteCourante_);
        }
    }
    if (rapport.refused > 0 && onClipsRefused) onClipsRefused(rapport.refused);
    notifyChanged();
    repaint();
}

AutomationCurve* ArrangementComponent::curveShownOn(size_t trackIndex) {
    if (project_ == nullptr || trackIndex >= project_->tracks.size()) return nullptr;
    auto& courbes = project_->tracks[trackIndex].automation;
    if (courbes.empty()) return nullptr;
    const auto it = courbeMontree_.find(trackIndex);
    // PAR DÉFAUT LA PREMIÈRE : montrer « aucune » alors que la piste en a une
    // obligerait à la choisir avant de la voir, c'est-à-dire à savoir qu'elle
    // existe.
    const int index = it == courbeMontree_.end() ? 0 : it->second;
    if (index < 0 || index >= static_cast<int>(courbes.size())) return nullptr;
    return &courbes[static_cast<size_t>(index)];
}

void ArrangementComponent::toggleAutomation() {
    automationVisible_ = !automationVisible_;
    repaint();
}

void ArrangementComponent::showAutomationCurve(size_t trackIndex, int curveIndex) {
    courbeMontree_[trackIndex] = curveIndex;
    repaint();
}

float ArrangementComponent::valueToY(float valeur, float minimum, float maximum,
                                      int haut, int hauteur) const {
    if (maximum <= minimum) return static_cast<float>(haut + hauteur / 2);
    const float t = juce::jlimit(0.0f, 1.0f, (valeur - minimum) / (maximum - minimum));
    return static_cast<float>(haut + hauteur) - t * static_cast<float>(hauteur);
}

float ArrangementComponent::yToValue(float y, float minimum, float maximum,
                                      int haut, int hauteur) const {
    if (hauteur <= 0 || maximum <= minimum) return minimum;
    const float t = juce::jlimit(0.0f, 1.0f,
                                  (static_cast<float>(haut + hauteur) - y) / static_cast<float>(hauteur));
    return minimum + t * (maximum - minimum);
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
        const auto copies = duplicateClips(track, selection_, decalage, compteur);
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
        // LES COINS DU HAUT TIRENT LES FONDUS (D5.6), les bords redimensionnent.
        // Le haut et le bas d'un même bord font deux choses : c'est la
        // convention de tous les séquenceurs, et elle tient parce qu'un fondu
        // se dessine justement depuis le haut du clip.
        const float hautClip = static_cast<float>(trackTop(trackIndex) + 3);
        const bool enHaut = point.y <= hautClip + 8.0f;
        if (enHaut && point.x - x1 <= kBordSensible * 2.0f && x2 - x1 > 4 * kBordSensible)
            bord = Geste::FonduEntree;
        else if (enHaut && x2 - point.x <= kBordSensible * 2.0f && x2 - x1 > 4 * kBordSensible)
            bord = Geste::FonduSortie;
        else if (point.x - x1 <= kBordSensible && x2 - x1 > 3 * kBordSensible) bord = Geste::BordGauche;
        else if (x2 - point.x <= kBordSensible && x2 - x1 > 3 * kBordSensible) bord = Geste::BordDroit;
        return &(*it);
    }
    return nullptr;
}

void ArrangementComponent::mouseDown(const juce::MouseEvent& event) {
    if (project_ == nullptr) return;
    const auto point = event.position;

    // La règle : on y pose la tête de lecture, on n'y saisit pas de clip ;
    // le clic droit y pose, renomme ou retire un repère (D16.4).
    if (point.y < kRulerHeight) {
        if (point.x < kHeaderWidth) return;
        if (event.mods.isPopupMenu()) {
            const vsm::midi::Tick tick = std::max<vsm::midi::Tick>(0, xToTick(point.x));
            const int survole = markerAt(point.x);
            juce::PopupMenu menu;
            menu.addItem(1, u8"Poser un repère ici…");
            menu.addItem(2, u8"Renommer ce repère…", survole >= 0);
            menu.addItem(3, u8"Retirer ce repère", survole >= 0);
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, tick, survole](int choix) {
                if (choix == 1 && onMarkerRequested) onMarkerRequested(tick);
                if (choix == 2 && survole >= 0 && onMarkerRenameRequested)
                    onMarkerRenameRequested(static_cast<size_t>(survole));
                if (choix == 3 && survole >= 0 && onMarkerRemoved) onMarkerRemoved(static_cast<size_t>(survole));
            });
            return;
        }
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

    // LES COURBES PASSENT AVANT LES CLIPS quand elles sont montrées : elles
    // sont dessinées par-dessus, donc c'est ce qu'on vise en cliquant. Les
    // clips restent saisissables partout où il n'y a pas de point.
    if (automationVisible_) {
        const int pisteSousLeCurseur = trackAtY(point.y);
        if (pisteSousLeCurseur >= 0) {
            const size_t index = static_cast<size_t>(pisteSousLeCurseur);
            if (auto* courbe = curveShownOn(index)) {
                const auto& track = project_->tracks[index];
                const int y = trackTop(index);
                const int bande = trackHeight(track) - 8;
                float mini = 0.0f, maxi = 1.0f;
                const bool connue = automationRange
                                    && automationRange(index, courbe->parameter, mini, maxi);
                const vsm::midi::Tick t = xToTick(point.x);
                const vsm::midi::Tick tolerance =
                    static_cast<vsm::midi::Tick>(6.0 / std::max(1.0e-6, pixelsPerTick_));

                if (event.mods.isRightButtonDown() || event.mods.isCtrlDown()) {
                    // CTRL OU CLIC DROIT RETIRE un point. Le même geste que
                    // partout, et rien à choisir avant.
                    if (onEditStarted) onEditStarted(u8"Retirer un point d'automation");
                    if (vsm::sequencer::removeAutomationPointNear(*courbe, t, tolerance)) {
                        notifyChanged();
                        repaint();
                    }
                    return;
                }
                const size_t existant =
                    vsm::sequencer::automationPointNear(*courbe, t, tolerance);
                if (existant < courbe->points.size()) {
                    if (onEditStarted) onEditStarted(u8"Déplacer un point d'automation");
                    geste_ = Geste::Point;
                    pisteCourbeSaisie_ = pisteSousLeCurseur;
                    pointSaisi_ = existant;
                    return;
                }
                // POSER un point demande de savoir sur quelle échelle : sans
                // bornes connues, la courbe reste visible mais non modifiable
                // -- plutôt que modifiable sur une échelle inventée.
                if (connue && point.y >= static_cast<float>(y + 4)
                    && point.y <= static_cast<float>(y + 4 + bande)) {
                    if (onEditStarted) onEditStarted(u8"Poser un point d'automation");
                    const size_t pose = vsm::sequencer::setAutomationPoint(
                        *courbe, snapTick(t), yToValue(point.y, mini, maxi, y + 4, bande));
                    geste_ = Geste::Point;
                    pisteCourbeSaisie_ = pisteSousLeCurseur;
                    pointSaisi_ = pose;
                    notifyChanged();
                    repaint();
                    return;
                }
            }
        }
    }

    size_t piste = 0;
    Geste bord = Geste::Aucun;
    Clip* clip = clipAt(point, piste, bord);
    if (clip == nullptr) {
        if (!event.mods.isShiftDown()) selection_.clear();
        // LE LASSO PART DU VIDE : un rectangle tiré sur rien sélectionne les
        // clips qu'il touche (D11.2). Maj l'AJOUTE à la sélection en cours.
        if (event.mods.isLeftButtonDown() && point.x >= kHeaderWidth) {
            geste_ = Geste::Lasso;
            lassoOrigine_ = point;
            lasso_ = juce::Rectangle<float>(point, point);
        }
        repaint();
        return;
    }
    pisteCourante_ = piste;
    pisteDerniere_ = static_cast<int>(piste);
    refusesPendantLeGeste_ = 0;
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

    // LE MENU DU CLIP (D11.4) : renommer, colorer, reprendre la couleur de la
    // piste, rendre muet. Le clic droit a d'abord choisi le clip, ci-dessus.
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, u8"Renommer\u2026");
        menu.addItem(2, u8"Couleur\u2026");
        menu.addItem(3, u8"Couleur de la piste");
        menu.addItem(4, clip->muted ? u8"R\u00e9activer" : u8"Rendre muet");
        menu.addSeparator();
        menu.addItem(5, u8"Couper \u00e0 la t\u00eate de lecture (Ctrl+E)", !selection_.empty());
        menu.addItem(6, u8"Joindre les clips choisis (Ctrl+J)", selection_.size() > 1);
        // LE SUIVI DE TEMPO (D12.6) N'EST PROPOSÉ QUE SUR UNE PISTE AUDIO :
        // un clip MIDI suit déjà le tempo par nature, et lui offrir le choix
        // laisserait croire qu'il pourrait ne pas le suivre.
        clicTick_ = xToTick(point.x);
        if (project_->tracks[piste].kind == Track::Kind::Audio) {
            using vsm::sequencer::WarpMode;
            menu.addSeparator();
            juce::PopupMenu suivi;
            suivi.addItem(10, u8"Non", true, clip->warpMode == WarpMode::Off);
            suivi.addItem(11, u8"Hauteur conserv\u00e9e", true, clip->warpMode == WarpMode::KeepPitch);
            suivi.addItem(12, u8"R\u00e9\u00e9chantillonn\u00e9", true, clip->warpMode == WarpMode::Repitch);
            suivi.addItem(16, u8"Hauteur conserv\u00e9e (WSOLA, t\u00e9moin)", true,
                          clip->warpMode == WarpMode::KeepPitchWsola);
            menu.addSubMenu(u8"Suivre le tempo", suivi);
            menu.addItem(13, u8"Le clip fait N mesures\u2026");
            const int surMarqueur = marqueurAt(*clip, point.x);
            menu.addItem(14, u8"Ajouter un marqueur ici", vsm::sequencer::clipIsWarped(*clip)
                                                          && surMarqueur < 0);
            menu.addItem(15, u8"Retirer ce marqueur", surMarqueur > 0);
            menu.addItem(17, u8"\u00c0 l'envers", true, clip->reversed);
            // D17.1 : LA FORME DES FONDUS. Sur une piste audio seulement --
            // un clip MIDI n'a pas de fondu à donner une forme.
            {
                using vsm::sequencer::FadeShape;
                juce::PopupMenu formes;
                formes.addItem(30, u8"Droite (mat\u00e9riau corr\u00e9l\u00e9)", true,
                                clip->fadeShape == FadeShape::Linear);
                formes.addItem(31, u8"\u00c9gale puissance (mat\u00e9riau d\u00e9corr\u00e9l\u00e9)", true,
                                clip->fadeShape == FadeShape::EqualPower);
                formes.addItem(32, u8"Lente au d\u00e9part", true, clip->fadeShape == FadeShape::Slow);
                formes.addItem(33, u8"Rapide au d\u00e9part", true, clip->fadeShape == FadeShape::Fast);
                menu.addSubMenu(u8"Forme des fondus", formes);
            }
            menu.addItem(18, u8"Normaliser (gain = 1 / cr\u00eate)", waveformProvider != nullptr);
            marqueurGeste_ = surMarqueur;
        }
        // LE ZOOM (D14.2), pour tout clip : tout voir, ou la sélection.
        menu.addSeparator();
        menu.addItem(20, u8"Zoom : tout voir");
        menu.addItem(21, u8"Zoom : la s\u00e9lection");
        const uint64_t id = clip->id;
        const size_t p = piste;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                               juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                           [this, p, id](int choix) { clipMenuAction(p, id, choix); });
        repaint();
        return;
    }

    // ALT COUPE, plutôt qu'un OUTIL qu'on choisit et qu'on oublie de quitter.
    // Un mode se laisse allumé, et le geste suivant fait autre chose que ce
    // qu'on croit ; un modificateur ne dure que le temps où on le tient. C'est
    // la même raison qui fait dessiner la région de boucle avec Maj sur la
    // règle du piano roll plutôt qu'avec un outil « boucle ».
    if (event.mods.isAltDown()) {
        const vsm::midi::Tick ou = snapTick(xToTick(point.x));
        auto& track = project_->tracks[piste];
        const uint64_t depart = project_->peekNextClipId();
        uint64_t compteur = depart;
        if (onEditStarted) onEditStarted(u8"Couper un clip");
        const size_t coupes = splitClips(track, selection_, ou, materialEnd(track), compteur,
                                          [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); });
        project_->ensureClipIdAbove(compteur - 1);
        // Les moitiés restent choisies, comme au Ctrl+E : couper à la souris
        // et couper au clavier sont le même geste, et ne doivent pas laisser
        // deux sélections différentes.
        for (uint64_t id = depart; id < compteur; ++id) selection_.insert(id);
        if (coupes > 0) notifyChanged();
        repaint();
        return;
    }

    // UN MARQUEUR DE TEMPO SE SAISIT AVANT LE RESTE (D12.6) : il est dans le
    // corps du clip, là où un clic déplacerait, et c'est le geste le plus
    // précis des deux -- quatre pixels contre toute la largeur.
    if (const int marqueur = marqueurAt(*clip, point.x); marqueur > 0) {
        geste_ = Geste::MarqueurWarp;
        marqueurGeste_ = marqueur;
        clipFondu_ = clip->id;
        gesteOrigine_ = gesteDernier_ = xToTick(point.x);
        if (onEditStarted) onEditStarted(u8"Caler un marqueur de tempo");
        repaint();
        return;
    }

    geste_ = bord;
    // CTRL SUR LE BORD DROIT D'UN CLIP AUDIO ÉTIRE (D13.2) : le geste de Live
    // (Alt + bord) et de Cubase (« le redimensionnement étire »). Un
    // modificateur, pas un outil -- la même raison qu'Alt pour couper.
    if (bord == Geste::BordDroit && event.mods.isCtrlDown()
        && project_->tracks[piste].kind == Track::Kind::Audio)
        geste_ = Geste::Etirer;
    gesteOrigine_ = xToTick(point.x);
    gesteDernier_ = gesteOrigine_;
    clipFondu_ = clip->id;
    if (onEditStarted)
        onEditStarted(geste_ == Geste::Etirer ? juce::String(u8"Étirer un clip")
                      : bord == Geste::FonduEntree || bord == Geste::FonduSortie
                          ? juce::String(u8"Fondu d'un clip")
                      : bord == Geste::Deplacer ? juce::String(u8"Déplacer un clip")
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
    if (geste_ == Geste::Point && pisteCourbeSaisie_ >= 0) {
        const size_t index = static_cast<size_t>(pisteCourbeSaisie_);
        auto* courbe = curveShownOn(index);
        if (courbe == nullptr || pointSaisi_ >= courbe->points.size()) return;
        const auto& track = project_->tracks[index];
        const int y = trackTop(index);
        const int bande = trackHeight(track) - 8;
        float mini = 0.0f, maxi = 1.0f;
        if (!automationRange || !automationRange(index, courbe->parameter, mini, maxi)) return;

        // ON DÉPLACE LE POINT EN VALEUR ET EN TEMPS, puis on RETRIE : traîner
        // un point par-dessus son voisin est un geste normal, et la courbe doit
        // rester lisible ensuite.
        const float valeur = yToValue(event.position.y, mini, maxi, y + 4, bande);
        const vsm::midi::Tick t = std::max<vsm::midi::Tick>(0, snapTick(xToTick(event.position.x)));
        const bool palier = courbe->points[pointSaisi_].step;
        courbe->points.erase(courbe->points.begin() + static_cast<std::ptrdiff_t>(pointSaisi_));
        pointSaisi_ = vsm::sequencer::setAutomationPoint(*courbe, t, valeur, palier);
        notifyChanged();
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
    if (geste_ == Geste::Lasso) {
        lasso_ = juce::Rectangle<float>(lassoOrigine_, event.position);
        selectClipsInLasso(event.mods.isShiftDown());
        repaint();
        return;
    }
    if (selection_.empty()) return;
    const int piste = trackAtY(event.position.y);

    // LE CLIP CHANGE DE PISTE (D11.1) : le décalage de pistes est relatif au
    // dernier pas, comme celui du temps, et il emporte les notes que la
    // fenêtre couvre (voir `moveClipsAcrossTracks`). Ce qui est refusé est
    // compté et dit au relâchement.
    if (geste_ == Geste::Deplacer && piste >= 0 && pisteDerniere_ >= 0 && piste != pisteDerniere_) {
        const auto rapport = vsm::sequencer::moveClipsAcrossTracks(
            project_->tracks, selection_, piste - pisteDerniere_);
        pisteDerniere_ += rapport.applied;
        refusesPendantLeGeste_ += rapport.refused;
        if (rapport.moved > 0) {
            pisteCourante_ = static_cast<size_t>(pisteDerniere_);
            if (onTrackSelected) onTrackSelected(pisteCourante_);
        }
    }

    // LE MARQUEUR SE TIRE EN ABSOLU : la poignée suit le pointeur. Ce qui
    // bouge est sa position MUSICALE ; sa position dans le fichier ne change
    // pas -- c'est exactement le geste de calage (D12.6).
    if (geste_ == Geste::MarqueurWarp && marqueurGeste_ > 0) {
        const vsm::midi::Tick ou = snapTick(xToTick(event.position.x));
        for (auto& track : project_->tracks)
            for (const auto& c : track.clips)
                if (c.id == clipFondu_)
                    moveWarpMarker(track.clips, clipFondu_, static_cast<size_t>(marqueurGeste_),
                                    ou - c.startTick);
        notifyChanged();
        repaint();
        return;
    }

    // LES FONDUS SE TIRENT EN ABSOLU, pas en relatif : le coin suit le
    // pointeur, comme on l'attend d'une poignée qu'on tient.
    if (geste_ == Geste::FonduEntree || geste_ == Geste::FonduSortie) {
        const vsm::midi::Tick ou = xToTick(event.position.x);
        auto conversion = [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); };
        for (auto& track : project_->tracks) {
            const vsm::midi::Tick fin = materialEnd(track);
            if (geste_ == Geste::FonduEntree)
                setClipFadeIn(track.clips, clipFondu_, ou, fin, conversion);
            else
                setClipFadeOut(track.clips, clipFondu_, ou, fin, conversion);
        }
        notifyChanged();
        repaint();
        return;
    }

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
            case Geste::Deplacer:   moveClips(track, selection_, delta); break;
            case Geste::BordDroit:  resizeClipsEnd(track, selection_, delta, fin); break;
            case Geste::Etirer:     stretchClipsEnd(track, selection_, delta, fin, conversion); break;
            case Geste::BordGauche: resizeClipsStart(track, selection_, delta, fin, conversion); break;
            // Les autres gestes ont été traités plus haut et n'atteignent jamais
            // cette boucle ; les nommer garde le compilateur du côté du lecteur
            // le jour où l'on en ajoutera un.
            case Geste::Hauteur:
            case Geste::Reordonner:
            case Geste::Point:
            case Geste::FonduEntree:
            case Geste::FonduSortie:
            case Geste::Lasso:
            case Geste::MarqueurWarp:
            case Geste::Aucun: break;
        }
    }
    notifyChanged();
    repaint();
}

void ArrangementComponent::mouseUp(const juce::MouseEvent&) {
    if (geste_ == Geste::Hauteur || geste_ == Geste::Reordonner) notifyChanged();
    if (geste_ == Geste::Lasso) { lasso_ = {}; repaint(); }
    if (refusesPendantLeGeste_ > 0 && onClipsRefused) onClipsRefused(refusesPendantLeGeste_);
    refusesPendantLeGeste_ = 0;
    pisteDerniere_ = -1;
    geste_ = Geste::Aucun;
    pisteSaisie_ = -1;
    pisteCourbeSaisie_ = -1;
    reordonnancementOuvert_ = false;
}

void ArrangementComponent::joinSelection() {
    if (project_ == nullptr || selection_.size() < 2) return;
    size_t joints = 0, refuses = 0;
    // L'INSTANTANÉ N'EST PRIS QUE SI QUELQUE CHOSE VA CHANGER : une jonction
    // toute refusée qui laisserait « Joindre des clips » dans l'historique
    // ferait annuler du vide. On mesure donc d'abord sur une copie.
    std::vector<std::pair<size_t, std::vector<Clip>>> resultats;
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        auto& track = project_->tracks[i];
        auto essai = track.clips;
        const auto bilan =
            track.locked
                ? vsm::sequencer::ClipJoin{}
                : vsm::sequencer::joinClips(essai, selection_, materialEnd(track),
                                             track.kind == Track::Kind::Audio,
                                             [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); });
        joints += bilan.joined;
        refuses += bilan.refused;
        if (bilan.joined > 0) resultats.emplace_back(i, std::move(essai));
    }
    if (joints > 0) {
        if (onEditStarted) onEditStarted(u8"Joindre des clips");
        for (auto& [i, clips] : resultats) project_->tracks[i].clips = std::move(clips);
        // Les clips absorbés ont disparu : leur identifiant ne désigne plus
        // rien, et une sélection qui les garderait ferait porter le geste
        // suivant sur du vide.
        ClipSelection restants;
        for (const auto& track : project_->tracks)
            for (const auto& c : track.clips)
                if (selection_.count(c.id) > 0) restants.insert(c.id);
        selection_ = std::move(restants);
        notifyChanged();
        repaint();
    }
    if (refuses > 0 && onJoinRefused) onJoinRefused(refuses);
}

void ArrangementComponent::splitSelectionAtPlayhead() {
    if (project_ == nullptr || selection_.empty()) return;
    size_t coupes = 0;
    const uint64_t depart = project_->peekNextClipId();
    uint64_t compteur = depart;
    // Même précaution : on compte avant de prendre l'instantané.
    std::vector<std::pair<size_t, std::vector<Clip>>> resultats;
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        auto& track = project_->tracks[i];
        auto essai = track.clips;
        const size_t faites =
            track.locked
                ? 0u
                : splitClips(essai, selection_, playhead_, materialEnd(track), compteur,
                              [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); });
        coupes += faites;
        if (faites > 0) resultats.emplace_back(i, std::move(essai));
    }
    if (coupes == 0) return;
    if (onEditStarted) onEditStarted(u8"Couper à la tête de lecture");
    for (auto& [i, clips] : resultats) project_->tracks[i].clips = std::move(clips);
    project_->ensureClipIdAbove(compteur - 1);
    // LES DEUX MOITIÉS RESTENT CHOISIES, comme une duplication rend la
    // sélection des copies : le geste suivant porte sur ce qu'on vient de
    // faire. Sans cela, Ctrl+J juste après Ctrl+E ne trouvait qu'une moitié
    // sur deux et ne recollait rien -- une paire de raccourcis inverses qui
    // ne s'annulent pas est une paire cassée. Les identifiants neufs sont
    // exactement ceux que le compteur a distribués.
    for (uint64_t id = depart; id < compteur; ++id) selection_.insert(id);
    notifyChanged();
    repaint();
}

int ArrangementComponent::markerAt(float x) const {
    if (project_ == nullptr) return -1;
    int trouve = -1;
    for (size_t i = 0; i < project_->markers.size(); ++i)
        if (std::abs(tickToX(project_->markers[i].tick) - x) < 10.0f) trouve = static_cast<int>(i);
    return trouve;
}

void ArrangementComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (project_ == nullptr) return;
    // Sur la règle : un double-clic sur un repère le renomme, ailleurs il en
    // pose un (le double-clic de Cubase sur la piste de marqueurs).
    if (event.position.y < kRulerHeight) {
        if (event.position.x < kHeaderWidth) return;
        const int survole = markerAt(event.position.x);
        if (survole >= 0) {
            if (onMarkerRenameRequested) onMarkerRenameRequested(static_cast<size_t>(survole));
        } else if (onMarkerRequested) {
            onMarkerRequested(std::max<vsm::midi::Tick>(0, xToTick(event.position.x)));
        }
        return;
    }
    size_t piste = 0;
    Geste bord = Geste::Aucun;
    if (auto* clip = clipAt(event.position, piste, bord)) {
        if (onClipRenameRequested) onClipRenameRequested(piste, clip->id);
        return;
    }
    // SUR LE VIDE D'UNE PISTE : créer un clip d'une mesure (D16.1). C'est le
    // double-clic de Live sur une piste et le crayon de Cubase entre les
    // locateurs. Le point est ramené SUR LA GRILLE VERS L'ARRIÈRE -- on vise
    // une mesure, pas un tick, et un clip qui commencerait trois pixels après
    // la barre de mesure ne s'alignerait avec rien.
    const int surPiste = trackAtY(event.position.y);
    if (surPiste < 0 || event.position.x < static_cast<float>(kHeaderWidth)) return;
    if (!onClipCreationRequested) return;
    const vsm::midi::Tick brut = std::max<vsm::midi::Tick>(0, xToTick(event.position.x));
    const vsm::midi::Tick pas = std::max<vsm::midi::Tick>(1, snapStep(brut));
    const vsm::midi::Tick tick = snap_ ? (brut / pas) * pas : brut;
    onClipCreationRequested(static_cast<size_t>(surPiste), tick);
}

bool ArrangementComponent::selectionBounds(vsm::midi::Tick& debut, vsm::midi::Tick& fin) const {
    if (project_ == nullptr || selection_.empty()) return false;
    bool trouve = false;
    vsm::midi::Tick d = 0, f = 0;
    for (const auto& track : project_->tracks) {
        vsm::midi::Tick td = 0, tf = 0;
        if (!clipSelectionBounds(track.clips, selection_, materialEnd(track), td, tf)) continue;
        if (!trouve) { d = td; f = tf; trouve = true; }
        else { d = std::min(d, td); f = std::max(f, tf); }
    }
    if (trouve) { debut = d; fin = f; }
    return trouve;
}

void ArrangementComponent::zoomToFit() {
    if (project_ == nullptr) return;
    const vsm::midi::Tick fin = std::max<vsm::midi::Tick>(1, project_->lastUsedTick());
    const int largeur = std::max(1, getWidth() - kHeaderWidth);
    pixelsPerTick_ = juce::jlimit(0.0005, 8.0, static_cast<double>(largeur) * 0.96 / static_cast<double>(fin));
    scrollTick_ = 0;
    repaint();
}

void ArrangementComponent::zoomToSelection() {
    vsm::midi::Tick debut = 0, fin = 0;
    if (!selectionBounds(debut, fin)) return;
    const vsm::midi::Tick etendue = std::max<vsm::midi::Tick>(1, fin - debut);
    const int largeur = std::max(1, getWidth() - kHeaderWidth);
    pixelsPerTick_ = juce::jlimit(0.0005, 8.0, static_cast<double>(largeur) * 0.9 / static_cast<double>(etendue));
    scrollTick_ = std::max<vsm::midi::Tick>(0, debut - etendue / 20);
    repaint();
}

int ArrangementComponent::marqueurAt(const Clip& clip, float x) const {
    if (!vsm::sequencer::clipIsWarped(clip)) return -1;
    // LE PREMIER MARQUEUR NE SE SAISIT PAS : il est le début du clip, et le
    // déplacer voudrait dire rogner -- ce que le bord gauche fait déjà.
    for (size_t i = 1; i < clip.warpMarkers.size(); ++i) {
        const float mx = tickToX(clip.startTick + clip.warpMarkers[i].tick);
        if (std::abs(mx - x) <= 4.0f) return static_cast<int>(i);
    }
    return -1;
}

void ArrangementComponent::clipMenuAction(size_t piste, uint64_t clipId, int choix) {
    if (project_ == nullptr || choix == 0 || piste >= project_->tracks.size()) return;
    auto& track = project_->tracks[piste];
    auto it = std::find_if(track.clips.begin(), track.clips.end(),
                           [clipId](const Clip& c) { return c.id == clipId; });
    if (it == track.clips.end()) return;
    switch (choix) {
        case 1: if (onClipRenameRequested) onClipRenameRequested(piste, clipId); return;
        case 2: if (onClipColourRequested) onClipColourRequested(piste, clipId); return;
        case 3:
            if (onEditStarted) onEditStarted(u8"Couleur d'un clip");
            it->colorRgba = track.colorRgba;
            break;
        case 4: {
            // Sur TOUTE la sélection : rendre muets six clips choisis au lasso
            // est un geste, pas six.
            if (onEditStarted) onEditStarted(u8"Muet sur des clips");
            const bool muet = !it->muted;
            for (auto& t : project_->tracks)
                for (auto& c : t.clips)
                    if (selection_.count(c.id) > 0 || c.id == clipId) c.muted = muet;
            break;
        }
        case 30: case 31: case 32: case 33: {
            using vsm::sequencer::FadeShape;
            const FadeShape forme = choix == 31 ? FadeShape::EqualPower
                                  : choix == 32 ? FadeShape::Slow
                                  : choix == 33 ? FadeShape::Fast : FadeShape::Linear;
            if (it->fadeShape == forme) return;
            if (onEditStarted) onEditStarted(u8"Forme des fondus");
            // Sur TOUTE la sélection, comme le muet : régler la forme de six
            // raccords choisis au lasso est un geste, pas six.
            for (auto& t : project_->tracks)
                for (auto& c : t.clips)
                    if (selection_.count(c.id) > 0 || c.id == clipId) c.fadeShape = forme;
            break;
        }
        case 5: splitSelectionAtPlayhead(); return;
        case 6: joinSelection(); return;
        case 10: case 11: case 12: case 16: {
            using vsm::sequencer::WarpMode;
            const WarpMode mode = choix == 11 ? WarpMode::KeepPitch
                                : choix == 12 ? WarpMode::Repitch
                                : choix == 16 ? WarpMode::KeepPitchWsola : WarpMode::Off;
            if (it->warpMode == mode) return;
            if (onEditStarted) onEditStarted(u8"Suivre le tempo");
            // ALLUMER EST NEUTRE : la paire de marqueurs posée vaut le rapport
            // un, et le moteur court-circuite alors l'étireur -- le son ne
            // change pas d'un bit tant qu'on n'a rien calé (§ 0 du CDC).
            setClipWarpMode(track.clips, {clipId}, mode, materialEnd(track),
                             [this](vsm::midi::Tick t) { return project_->ticksToSeconds(t); });
            break;
        }
        case 13: if (onClipBarsRequested) onClipBarsRequested(piste, clipId); return;
        case 20: zoomToFit(); return;
        case 21: zoomToSelection(); return;
        case 18: {
            // NORMALISER (D13.6) : le gain devient 1 / crête du matériau JOUÉ.
            // La crête vient du cache d'aperçu, qui garde les extrêmes de
            // chaque tranche de 256 trames : c'est exactement ce qu'il faut, et
            // il est déjà là -- pas besoin de relire le fichier.
            if (!waveformProvider) return;
            auto cache = waveformProvider(piste);
            if (!cache) return;
            const double sr = sampleRateProvider ? sampleRateProvider() : 48000.0;
            const auto jouee = clipPlayedLength(*it, materialEnd(track));
            const auto depart = static_cast<int64_t>(it->sourceStartSeconds * sr);
            const double duree = project_->ticksToSeconds(it->startTick + jouee) - project_->ticksToSeconds(it->startTick);
            const auto arrivee = depart + static_cast<int64_t>(duree * sr);
            const auto tranches = vsm::audio::io::peaksForRange(*cache, depart, std::max(arrivee, depart + 1), 1024);
            float crete = 0.0f;
            for (const auto& t : tranches) crete = std::max({crete, std::abs(t.minimum), std::abs(t.maximum)});
            if (crete < 1e-6f) return;   // du silence ne se normalise pas
            if (onEditStarted) onEditStarted(u8"Normaliser un clip");
            it->gain = 1.0f / crete;
            break;
        }
        case 17: {
            // Sur toute la sélection, chacun le sien -- comme la phase.
            if (onEditStarted) onEditStarted(u8"Clip \u00e0 l'envers");
            ClipSelection cibles = selection_;
            cibles.insert(clipId);
            toggleClipReverse(track.clips, cibles);
            break;
        }
        case 14: {
            if (onEditStarted) onEditStarted(u8"Ajouter un marqueur de tempo");
            if (addWarpMarker(track.clips, clipId, clicTick_ - it->startTick) < 0) return;
            break;
        }
        case 15: {
            if (marqueurGeste_ <= 0) return;
            if (onEditStarted) onEditStarted(u8"Retirer un marqueur de tempo");
            if (!removeWarpMarker(track.clips, clipId, static_cast<size_t>(marqueurGeste_))) return;
            break;
        }
        default: return;
    }
    notifyChanged();
    repaint();
}

void ArrangementComponent::mouseMove(const juce::MouseEvent& event) {
    size_t piste = 0;
    Geste bord = Geste::Aucun;
    const Geste avant = survol_;
    const Clip* sous = project_ != nullptr ? clipAt(event.position, piste, bord) : nullptr;
    survol_ = sous != nullptr ? bord : Geste::Aucun;
    // UN MARQUEUR SOUS LE POINTEUR L'EMPORTE sur le geste du clip : c'est ce
    // qui se produira au clic, et le curseur doit le dire d'avance.
    if (sous != nullptr && marqueurAt(*sous, event.position.x) > 0) survol_ = Geste::MarqueurWarp;
    // CTRL SUR LE BORD DROIT D'UN CLIP AUDIO : le curseur dit d'avance qu'il
    // étirera, pas qu'il redimensionnera.
    if (sous != nullptr && bord == Geste::BordDroit && event.mods.isCtrlDown()
        && project_->tracks[piste].kind == Track::Kind::Audio)
        survol_ = Geste::Etirer;
    if (survol_ != avant) updateMouseCursor();
}

juce::MouseCursor ArrangementComponent::getMouseCursor() {
    if (survol_ == Geste::MarqueurWarp) return juce::MouseCursor::LeftRightResizeCursor;
    if (survol_ == Geste::Etirer) return juce::MouseCursor::UpDownLeftRightResizeCursor;
    if (survol_ == Geste::BordGauche || survol_ == Geste::BordDroit)
        return juce::MouseCursor::LeftRightResizeCursor;
    // Le coin de fondu a son propre curseur : sans cela, rien ne distinguerait
    // les huit pixels qui tirent un fondu de ceux qui redimensionnent, et on
    // découvrirait la différence en la subissant.
    if (survol_ == Geste::FonduEntree || survol_ == Geste::FonduSortie)
        return juce::MouseCursor::TopLeftCornerResizeCursor;
    return juce::MouseCursor::NormalCursor;
}

void ArrangementComponent::selectAll() {
    if (project_ == nullptr) return;
    selection_.clear();
    for (const auto& track : project_->tracks)
        for (const auto& clip : track.clips) selection_.insert(clip.id);
    repaint();
}

void ArrangementComponent::selectClipsInLasso(bool etendre) {
    if (project_ == nullptr) return;
    if (!etendre) selection_.clear();
    const auto zone = lasso_;
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        const auto& track = project_->tracks[i];
        const int y = trackTop(i);
        const int h = trackHeight(track);
        const vsm::midi::Tick fin = materialEnd(track);
        for (const auto& clip : track.clips) {
            const float x1 = tickToX(clip.startTick);
            const float x2 = tickToX(clip.startTick + clipPlayedLength(clip, fin));
            const juce::Rectangle<float> r(x1, static_cast<float>(y + 3), std::max(2.0f, x2 - x1),
                                            static_cast<float>(h - 7));
            if (zone.intersects(r)) selection_.insert(clip.id);
        }
    }
}

bool ArrangementComponent::selectionTickRange(vsm::midi::Tick& debut,
                                               vsm::midi::Tick& fin) const {
    if (project_ == nullptr || selection_.empty()) return false;
    bool trouve = false;
    for (const auto& track : project_->tracks) {
        vsm::midi::Tick a = 0, b = 0;
        if (!vsm::sequencer::clipSelectionBounds(track.clips, selection_, materialEnd(track), a, b))
            continue;
        debut = trouve ? std::min(debut, a) : a;
        fin = trouve ? std::max(fin, b) : b;
        trouve = true;
    }
    return trouve;
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
            case 'a': case 'A': selectAll(); return true;
            case 'c': case 'C': copySelection(); return true;
            case 'v': case 'V': paste(); return true;
            case 'd': case 'D': duplicateSelection(); return true;
            case 'x': case 'X': copySelection(); deleteSelection(); return true;
            // D16.3 : Ctrl+J et Ctrl+E, les mêmes lettres que la table des
            // raccourcis et que le piano roll. Elles sont écrites ici en
            // clair, comme les cinq au-dessus : faire consulter la table à
            // l'arrangement est un autre chantier, et il devra déplacer les
            // sept d'un coup plutôt que d'en laisser cinq en dur et deux non.
            case 'j': case 'J': joinSelection(); return true;
            case 'e': case 'E': splitSelectionAtPlayhead(); return true;
            default: break;
        }
    }
    // `S` coupe l'aimantation, `G` bascule entre la MESURE et la grille fine du
    // piano roll -- les deux réglages qu'on change en arrangeant, et les seuls.
    // `A` MONTRE ET CACHE LES COURBES : « plus une lane isolée dans un onglet »,
    // mais pas non plus des courbes en permanence par-dessus les clips quand on
    // arrange.
    if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A') {
        toggleAutomation();
        return true;
    }
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F') {
        setFollowPlayhead(!followPlayhead_);
        return true;
    }
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
    // LES FLÈCHES, COMME AU PIANO ROLL (D15.2) : avec une sélection, ←/→ la
    // déplacent d'un pas d'aimantation (Maj : quatre pas) et ↑/↓ la font
    // changer de piste ; sans sélection, ←/→ font défiler. Leur sens EST leur
    // direction : elles ne sont pas dans la table des raccourcis, la page les
    // liste comme fixes.
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        const auto mods = key.getModifiers();
        vsm::midi::Tick pas = snapStep(scrollTick_);
        if (pas <= 0 && project_ != nullptr) pas = project_->ticksPerQuarterNote;
        if (mods.isShiftDown()) pas *= 4;
        const vsm::midi::Tick delta = key == juce::KeyPress::leftKey ? -pas : pas;
        if (hasSelection()) nudgeSelection(delta);
        else { scrollTick_ = std::max<vsm::midi::Tick>(0, scrollTick_ + delta); repaint(); }
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
        if (!hasSelection()) return false;
        moveSelectionAcrossTracks(key == juce::KeyPress::upKey ? -1 : 1);
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

    // --- Repères (D16.4) : un trait sur toute la hauteur, le fanion et le
    // nom dans la règle. Même dessin que la règle du piano roll, mêmes
    // règles : le nom n'a que la place jusqu'au repère suivant, et quand il
    // n'y a pas la place on n'écrit pas -- le fanion suffit.
    {
        const auto& markers = project_->markers;
        for (size_t i = 0; i < markers.size(); ++i) {
            const float x = tickToX(markers[i].tick);
            if (x < static_cast<float>(kHeaderWidth) || x > static_cast<float>(bounds.getWidth())) continue;
            g.setColour(Palette::accentTeal.withAlpha(0.45f));
            g.drawLine(x, static_cast<float>(kRulerHeight), x, static_cast<float>(bounds.getHeight()), 1.0f);
            g.setColour(Palette::accentTeal);
            g.drawLine(x, 0.0f, x, static_cast<float>(kRulerHeight), 1.5f);
            juce::Path fanion;
            fanion.addTriangle(x, 0.0f, x + 9.0f, 3.5f, x, 7.0f);
            g.fillPath(fanion);
            float limite = static_cast<float>(bounds.getWidth());
            if (i + 1 < markers.size()) limite = tickToX(markers[i + 1].tick) - 3.0f;
            const int place = static_cast<int>(std::min(180.0f, limite - x - 11.0f));
            if (place < 26) continue;
            const juce::Rectangle<int> cadre(static_cast<int>(x) + 10, 1, place, kRulerHeight - 3);
            g.setColour(Palette::panel.withAlpha(0.92f));
            g.fillRect(cadre);
            g.setColour(Palette::accentTeal);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::String(markers[i].name), cadre.reduced(3, 0), juce::Justification::centredLeft, true);
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
            // UNE PISTE GELÉE LE DIT (D5.5). Sans cela, on éditerait ses notes
            // en se demandant pourquoi rien ne change : son instrument ne
            // tourne plus, c'est son gel qu'on entend. Le mot plutôt qu'une
            // icône, parce qu'il n'y a rien à deviner.
            juce::String nature = track.kind == Track::Kind::Audio  ? "audio"
                                : track.kind == Track::Kind::Group ? "groupe"
                                                                   : "midi";
            if (track.frozen) nature += u8" · gelé";
            // UNE PISTE VERROUILLÉE LE DIT AUSSI (D16.5), et pour la même
            // raison : sans cela on tirerait un clip en se demandant pourquoi
            // il ne bouge pas. Le mot, pas une icône -- il n'y a rien à
            // deviner, et un cadenas de dix pixels ne se lit pas.
            if (track.locked) nature += u8" · verrouillé";
            g.setColour(track.locked ? Palette::accentAmber
                                     : track.frozen ? Palette::accentTeal : Palette::textSecondary);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(nature, 24, y + 22, kHeaderWidth - 30, 14, juce::Justification::centredLeft);
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
            // LES CLIPS D'UNE PISTE GELÉE SONT ESTOMPÉS : ils décrivent encore
            // le morceau, mais ce n'est plus eux qu'on entend. Les montrer
            // pleins laisserait croire qu'on les édite.
            // Un clip de piste verrouillée est GRISÉ comme un clip de piste
            // gelée : ce qu'on ne peut pas saisir doit se voir avant qu'on
            // essaie de le saisir (D16.5).
            const float opacite = (track.frozen || track.locked) ? 0.35f
                                                                  : (clip.muted ? 0.25f : 0.75f);
            // UN CLIP MUET SE VOIT SANS DISPARAÎTRE : hachuré plutôt qu'effacé,
            // comme une note muette dans le piano roll.
            g.setColour(juce::Colour(clip.colorRgba).withAlpha(opacite));
            g.fillRoundedRectangle(r, 3.0f);
            // LA FORME D'ONDE (D5.7), dessinée DANS le clip. Un rectangle de
            // couleur dit où est le son ; la forme dit ce qu'il est -- où
            // frappe la caisse claire, où la voix respire, où la prise est
            // muette. C'est ce qu'on cherche en arrangeant.
            if (track.kind == Track::Kind::Audio && waveformProvider && r.getWidth() > 4.0f
                && r.getHeight() > 8.0f) {
                if (auto cache = waveformProvider(i)) {
                    const double sr = sampleRateProvider ? sampleRateProvider() : 48000.0;
                    // LA FENÊTRE DU CLIP DANS LE FICHIER, en trames. Elle
                    // commence à `sourceStartSeconds` : sans cela, un clip
                    // rogné dessinerait le début du fichier au lieu de ce qu'il
                    // joue -- une forme d'onde qui ne correspond pas au son est
                    // pire que pas de forme du tout.
                    const int64_t depart = static_cast<int64_t>(clip.sourceStartSeconds * sr);
                    const double dureeSecondes =
                        project_->ticksToSeconds(clip.startTick + clipPlayedLength(clip, fin))
                        - project_->ticksToSeconds(clip.startTick);
                    const int64_t arrivee = depart + static_cast<int64_t>(dureeSecondes * sr);
                    const int colonnes = static_cast<int>(r.getWidth());

                    // LA FORME D'ONDE D'UN CLIP ÉTIRÉ SE DESSINE DANS LE TEMPS
                    // ÉTIRÉ (D12.6) : chaque colonne demande au clip OÙ elle
                    // est dans le fichier. Sans cela, un clip calé montrerait
                    // ses temps ailleurs qu'où il les joue -- et le calage se
                    // ferait à l'oreille, alors qu'il se fait à l'œil.
                    std::vector<vsm::audio::io::PeakBin> tracé;
                    if (vsm::sequencer::clipIsWarped(clip)) {
                        const auto jouee = clipPlayedLength(clip, fin);
                        tracé.resize(static_cast<size_t>(std::max(0, colonnes)));
                        for (int c = 0; c < colonnes; ++c) {
                            const auto t0 = static_cast<vsm::midi::Tick>(
                                static_cast<double>(jouee) * c / std::max(1, colonnes));
                            const auto t1 = static_cast<vsm::midi::Tick>(
                                static_cast<double>(jouee) * (c + 1) / std::max(1, colonnes));
                            const auto f0 = static_cast<int64_t>(warpSourceSecondsAt(clip, t0) * sr);
                            const auto f1 = static_cast<int64_t>(warpSourceSecondsAt(clip, t1) * sr);
                            const auto une = vsm::audio::io::peaksForRange(*cache, f0,
                                                                            std::max(f1, f0 + 1), 1);
                            if (!une.empty()) tracé[static_cast<size_t>(c)] = une[0];
                        }
                    } else {
                        tracé = vsm::audio::io::peaksForRange(*cache, depart, arrivee, colonnes);
                    }
                    // À L'ENVERS, LA FORME SE DESSINE À L'ENVERS (D13.4) : ce
                    // qu'on voit à droite est ce qu'on entend en dernier.
                    if (clip.reversed) std::reverse(tracé.begin(), tracé.end());
                    const float milieu = r.getCentreY();
                    const float demi = r.getHeight() * 0.45f;
                    g.setColour(Palette::background.withAlpha(0.72f));
                    for (size_t c = 0; c < tracé.size(); ++c) {
                        const float x = r.getX() + static_cast<float>(c);
                        const float haut = milieu - tracé[c].maximum * demi * clip.gain;
                        const float bas = milieu - tracé[c].minimum * demi * clip.gain;
                        g.drawLine(x, std::min(haut, bas), x, std::max(haut, bas) + 0.5f, 1.0f);
                    }
                }
            }

            // LES FONDUS SE VOIENT (D5.6) : deux triangles sombres aux coins.
            // Un fondu réglé qui ne se dessinerait pas obligerait à écouter
            // pour savoir s'il existe, et à deviner sa longueur.
            const double dureeClip = project_->ticksToSeconds(clip.startTick
                                                               + clipPlayedLength(clip, fin))
                                    - project_->ticksToSeconds(clip.startTick);
            if (dureeClip > 0.0) {
                const float largeur = r.getWidth();
                // LE DESSIN SUIT LA FORME (D17.1), et par la MÊME formule que
                // le moteur (`sequencer::fadeShapeGain`) : le triangle en dur
                // dessinait une droite quelle que soit la forme jouée, et l'on
                // aurait vu une droite en entendant un quart de sinusoïde.
                // Douze segments suffisent à la lire, et c'est un masque, pas
                // une courbe de précision.
                auto masqueDeFondu = [&](float xDebut, float w, bool entree) {
                    if (w <= 0.0f) return;
                    juce::Path coin;
                    coin.startNewSubPath(xDebut, r.getBottom());
                    for (int i = 0; i <= 12; ++i) {
                        const float t = static_cast<float>(i) / 12.0f;
                        const float gain = vsm::sequencer::fadeShapeGain(clip.fadeShape, t);
                        coin.lineTo(xDebut + (entree ? w * t : -w * t),
                                     r.getBottom() - gain * r.getHeight());
                    }
                    coin.lineTo(xDebut + (entree ? w : -w), r.getY());
                    coin.lineTo(xDebut, r.getY());
                    coin.closeSubPath();
                    g.setColour(Palette::background.withAlpha(0.72f));
                    g.fillPath(coin);
                };
                if (clip.fadeInSeconds > 0.0)
                    masqueDeFondu(r.getX(),
                                   static_cast<float>(clip.fadeInSeconds / dureeClip) * largeur, true);
                if (clip.fadeOutSeconds > 0.0)
                    masqueDeFondu(r.getRight(),
                                   static_cast<float>(clip.fadeOutSeconds / dureeClip) * largeur, false);
            }
            // LE FONDU ENCHAÎNÉ SE VOIT (D13.1) : la zone où ce clip en
            // chevauche un autre de la piste est hachurée. C'est là que l'un
            // s'éteint et que l'autre monte, et un chevauchement invisible
            // s'entendrait sans se comprendre.
            if (track.kind == Track::Kind::Audio) {
                const auto finClip = clip.startTick + clipPlayedLength(clip, fin);
                for (const auto& autre : track.clips) {
                    if (autre.id == clip.id || autre.muted) continue;
                    const auto finAutre = autre.startTick + clipPlayedLength(autre, fin);
                    const auto debut = std::max(clip.startTick, autre.startTick);
                    const auto arret = std::min(finClip, finAutre);
                    if (arret <= debut) continue;
                    const float x0 = std::max(r.getX(), tickToX(debut));
                    const float x1 = std::min(r.getRight(), tickToX(arret));
                    if (x1 <= x0) continue;
                    g.setColour(Palette::background.withAlpha(0.35f));
                    g.fillRect(x0, r.getY(), x1 - x0, r.getHeight());
                    g.setColour(Palette::textPrimary.withAlpha(0.35f));
                    for (float x = x0 - r.getHeight(); x < x1; x += 7.0f)
                        g.drawLine(std::max(x0, x), r.getBottom(), std::min(x1, x + r.getHeight()),
                                   r.getBottom() - std::min(r.getHeight(), std::min(x1, x + r.getHeight()) - std::max(x0, x)), 1.0f);
                }
            }

            // LES MARQUEURS DE TEMPO SE VOIENT (D12.6) : un trait vertical et
            // une pointe en haut, là où on les saisit. Un marqueur qu'on ne
            // verrait pas se déplacerait par surprise, en croyant déplacer le
            // clip -- c'est pourquoi le trait est dessiné AVANT tout autre
            // décor du clip et sur toute sa hauteur.
            if (vsm::sequencer::clipIsWarped(clip)) {
                for (size_t m = 1; m < clip.warpMarkers.size(); ++m) {
                    const float mx = tickToX(clip.startTick + clip.warpMarkers[m].tick);
                    if (mx < r.getX() || mx > r.getRight()) continue;
                    g.setColour(Palette::accentAmber.withAlpha(0.9f));
                    g.fillRect(mx - 0.5f, r.getY(), 1.5f, r.getHeight());
                    juce::Path pointe;
                    pointe.addTriangle(mx - 3.5f, r.getY(), mx + 3.5f, r.getY(), mx, r.getY() + 5.0f);
                    g.fillPath(pointe);
                }
            }

            // LA PHASE INVERSÉE SE VOIT AUSSI : un liséré en tirets. Deux clips
            // identiques dont l'un est inversé s'annulent en s'additionnant, et
            // rien d'autre ne le dirait.
            if (clip.invertPhase) {
                g.setColour(Palette::accentRed.withAlpha(0.9f));
                for (float x = r.getX() + 2.0f; x < r.getRight() - 2.0f; x += 6.0f)
                    g.fillRect(x, r.getBottom() - 3.0f, 3.0f, 2.0f);
            }

            // UN CLIP QUI BOUCLE DOIT SE VOIR BOUCLER. Étiré au-delà de son
            // matériau, il répète sa fenêtre (D5.2) -- et dessiné comme un
            // simple rectangle plus long, il mentirait sur ce qu'il joue. Un
            // trait fin à chaque tour, et on lit d'un coup d'œil combien de
            // fois le motif revient.
            const vsm::midi::Tick jouee = clipPlayedLength(clip, fin);
            const vsm::midi::Tick fenetre = clip.sourceLength > 0 ? clip.sourceLength : jouee;
            if (fenetre > 0 && jouee > fenetre) {
                g.setColour(Palette::background.withAlpha(0.72f));
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

    // --- Courbes d'automation, PAR-DESSUS les clips (D5.4) --------------------
    //
    // Par-dessus et non à côté : une courbe se lit par rapport à ce qu'elle
    // pilote, et une bande séparée obligerait à faire l'aller-retour des yeux
    // entre le fondu et le clip qu'il éteint. Semi-transparente, pour que le
    // clip reste lisible dessous.
    if (automationVisible_) {
        for (size_t i = 0; i < project_->tracks.size(); ++i) {
            auto* courbe = curveShownOn(i);
            if (courbe == nullptr || courbe->points.empty()) continue;
            const auto& track = project_->tracks[i];
            const int y = trackTop(i);
            const int h = trackHeight(track);
            if (y > bounds.getHeight() || h < 20) continue;

            float mini = 0.0f, maxi = 1.0f;
            if (automationRange) automationRange(i, courbe->parameter, mini, maxi);
            const int bande = h - 8;

            juce::Path trace;
            bool commence = false;
            for (int x = kHeaderWidth; x < bounds.getWidth(); x += 2) {
                const vsm::midi::Tick t = xToTick(static_cast<float>(x));
                const float v = vsm::sequencer::automationValueAt(*courbe, t);
                const float yy = valueToY(v, mini, maxi, y + 4, bande);
                if (!commence) { trace.startNewSubPath(static_cast<float>(x), yy); commence = true; }
                else trace.lineTo(static_cast<float>(x), yy);
            }
            g.setColour(Palette::accentAmber.withAlpha(0.9f));
            g.strokePath(trace, juce::PathStrokeType(1.5f));

            for (const auto& point : courbe->points) {
                const float x = tickToX(point.tick);
                if (x < kHeaderWidth || x > bounds.getWidth()) continue;
                const float yy = valueToY(point.value, mini, maxi, y + 4, bande);
                g.setColour(Palette::accentAmber);
                g.fillRect(x - 3.0f, yy - 3.0f, 6.0f, 6.0f);
                // UN PALIER SE VOIT : carré plein contre carré évidé. Sans
                // cela, deux points identiques à l'œil se comporteraient
                // différemment, et rien ne dirait pourquoi.
                if (!point.step) {
                    g.setColour(Palette::background);
                    g.fillRect(x - 1.5f, yy - 1.5f, 3.0f, 3.0f);
                }
            }

            // LE NOM DE CE QUI EST PILOTÉ VA DANS L'EN-TÊTE, pas sur les clips :
            // écrit par-dessus la piste, il se superposait au nom du clip et les
            // deux devenaient illisibles. Il appartient de toute façon à la
            // piste -- c'est elle qui décide quelle courbe elle montre.
            if (h >= 40) {
                g.setColour(Palette::accentAmber.withAlpha(0.85f));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(juce::String(courbe->parameter), 24, y + h - 16, kHeaderWidth - 30, 13,
                            juce::Justification::centredLeft, true);
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
    g.drawText((followPlayhead_ ? juce::String(u8"suit \u00b7 ") : juce::String())
                   + juce::String(snap_ ? (aimanteALaMesure_ ? "aimant : mesure" : "aimant : grille")
                                        : "aimant : libre")
                   + (automationVisible_ ? "  |  auto" : ""),
                4, 2, kHeaderWidth - 8, kRulerHeight - 4, juce::Justification::centredRight);

    if (project_->tracks.empty()) {
        g.setColour(Palette::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(u8"Aucune piste — Piste ▸ Ajouter une piste",
                    bounds.withTrimmedLeft(kHeaderWidth), juce::Justification::centred);
    }

    // LA CIBLE D'UN GLISSER (D10.1) : la piste survolée, et le trait vertical
    // qui dit à quelle mesure ça tombera. Sans ce trait, on lâche à
    // l'aveugle -- et pour un échantillon, « à l'aveugle » veut dire à la
    // mauvaise mesure.
    if (dropTrack_ >= 0 && project_ != nullptr
        && static_cast<size_t>(dropTrack_) < project_->tracks.size()) {
        const int haut = trackTop(static_cast<size_t>(dropTrack_));
        const int hauteur = trackHeight(project_->tracks[static_cast<size_t>(dropTrack_)]);
        g.setColour(juce::Colours::gold.withAlpha(0.18f));
        g.fillRect(juce::Rectangle<int>(0, haut, getWidth(), hauteur));
        const float x = tickToX(dropTick_);
        g.setColour(juce::Colours::gold);
        g.fillRect(juce::Rectangle<float>(x - 1.0f, static_cast<float>(haut), 2.0f,
                                           static_cast<float>(hauteur)));
    }

    // LE LASSO (D11.2) : le rectangle qu'on tire, par-dessus tout.
    if (geste_ == Geste::Lasso && !lasso_.isEmpty()) {
        g.setColour(Palette::accentTeal.withAlpha(0.15f));
        g.fillRect(lasso_);
        g.setColour(Palette::accentTeal);
        g.drawRect(lasso_, 1.0f);
    }
}

// --- D10.1 : recevoir un échantillon, à une piste ET à une mesure -----------

bool ArrangementComponent::isInterestedInDragSource(const SourceDetails& details) {
    return details.description.toString().startsWith("vsm-browser:");
}

void ArrangementComponent::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void ArrangementComponent::itemDragMove(const SourceDetails& details) {
    const int piste = trackAtY(static_cast<float>(details.localPosition.y));
    // AIMANTÉ, COMME TOUT LE RESTE DE L'ARRANGEMENT. Poser un échantillon à
    // trois millisecondes du premier temps est le genre de décalage qu'on ne
    // voit pas et qu'on entend.
    const vsm::midi::Tick tick = snapTick(xToTick(static_cast<float>(details.localPosition.x)));
    if (piste == dropTrack_ && tick == dropTick_) return;
    dropTrack_ = piste;
    dropTick_ = tick;
    repaint();
}

void ArrangementComponent::itemDragExit(const SourceDetails&) {
    dropTrack_ = -1;
    repaint();
}

void ArrangementComponent::itemDropped(const SourceDetails& details) {
    const int piste = dropTrack_;
    const vsm::midi::Tick tick = dropTick_;
    dropTrack_ = -1;
    repaint();
    if (piste < 0 || project_ == nullptr || static_cast<size_t>(piste) >= project_->tracks.size())
        return;
    if (onBrowserItemDropped)
        onBrowserItemDropped(static_cast<size_t>(piste), tick, details.description.toString());
}
