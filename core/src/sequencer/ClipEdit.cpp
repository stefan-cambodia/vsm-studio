#include "vsm/sequencer/ClipEdit.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsm::sequencer {

namespace {
bool selected(const ClipSelection& selection, const Clip& clip) {
    return selection.count(clip.id) > 0;
}
} // namespace

Tick clipPlayedLength(const Clip& clip, Tick materialEnd) {
    if (clip.length > 0) return clip.length;
    if (clip.sourceLength > 0) return clip.sourceLength;
    // Ni durée jouée ni fenêtre : le clip va jusqu'au bout du matériau.
    return std::max<Tick>(0, materialEnd - clip.sourceStart);
}

void moveClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick deltaTicks) {
    if (selection.empty() || deltaTicks == 0) return;

    // LE DÉCALAGE EST RÉDUIT POUR TOUS quand l'un des clips buterait sur zéro :
    // une sélection qui se déformerait parce qu'un seul de ses clips touche le
    // début du morceau ne serait plus la figure qu'on a saisie.
    Tick applique = deltaTicks;
    if (applique < 0) {
        Tick plusAGauche = -1;
        for (const auto& clip : clips)
            if (selected(selection, clip))
                plusAGauche = plusAGauche < 0 ? clip.startTick
                                              : std::min(plusAGauche, clip.startTick);
        if (plusAGauche >= 0) applique = std::max(applique, -plusAGauche);
    }
    if (applique == 0) return;

    for (auto& clip : clips)
        if (selected(selection, clip)) clip.startTick += applique;
}

void resizeClipsEnd(std::vector<Clip>& clips, const ClipSelection& selection,
                     Tick deltaTicks, Tick materialEnd) {
    if (selection.empty() || deltaTicks == 0) return;
    for (auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        const Tick actuelle = clipPlayedLength(clip, materialEnd);
        const Tick nouvelle = std::max<Tick>(1, actuelle + deltaTicks);
        clip.length = nouvelle;

        // LA FENÊTRE SUIT TANT QU'IL RESTE DU MATÉRIAU, et s'arrête au bout.
        // Au-delà, la durée jouée dépasse la fenêtre : le clip RÉPÈTE, sans
        // qu'une seule note soit copiée (voir `Clip::length`). C'est la
        // « boucle par étirement » de D5.2, et elle naît du même geste parce
        // que personne ne sait à l'avance où finit le matériau.
        const Tick disponible = std::max<Tick>(1, materialEnd - clip.sourceStart);
        clip.sourceLength = std::min(nouvelle, disponible);
    }
}

void resizeClipsStart(std::vector<Clip>& clips, const ClipSelection& selection,
                       Tick deltaTicks, Tick materialEnd,
                       const std::function<double(Tick)>& ticksToSeconds) {
    if (selection.empty() || deltaTicks == 0) return;
    for (auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        const Tick actuelle = clipPlayedLength(clip, materialEnd);
        // On ne rogne jamais au-delà du clip lui-même, ni avant le début du
        // morceau : le bord gauche s'arrête, il ne traverse pas.
        Tick applique = deltaTicks;
        applique = std::min(applique, actuelle - 1);
        applique = std::max(applique, -clip.startTick);
        if (applique == 0) continue;

        // UN CLIP AUDIO A SA FENÊTRE EN SECONDES (voir `Clip`) : sans cette
        // ligne, rogner le début d'une prise DÉCALERAIT le son au lieu de le
        // rogner -- le clip commencerait plus tard en jouant la même chose.
        // Un clip qui SUIT LE TEMPO (D12) lit la nouvelle position dans sa
        // carte, pas dans celle du tempo ; et ses marqueurs glissent avec lui.
        if (clipIsWarped(clip)) {
            const double nouveau = std::max(0.0, warpSourceSecondsAt(clip, applique));
            std::vector<WarpMarker> gardes;
            gardes.push_back({nouveau, 0});
            for (const auto& m : clip.warpMarkers)
                if (m.tick > applique) gardes.push_back({m.sourceSeconds, m.tick - applique});
            if (gardes.size() < 2) {
                // Tout rogné jusqu'au dernier marqueur : on prolonge le rapport.
                const double fin = warpSourceSecondsAt(clip, actuelle);
                gardes.push_back({fin, actuelle - applique});
            }
            clip.warpMarkers = std::move(gardes);
            clip.sourceStartSeconds = nouveau;
        } else if (ticksToSeconds) {
            const double avant = ticksToSeconds(clip.startTick);
            const double apres = ticksToSeconds(clip.startTick + applique);
            clip.sourceStartSeconds = std::max(0.0, clip.sourceStartSeconds + (apres - avant));
        }

        clip.startTick += applique;
        clip.sourceStart += applique;
        clip.length = actuelle - applique;
        clip.sourceLength = clip.length;
    }
}

void setClipFadeIn(std::vector<Clip>& clips, uint64_t clipId, Tick atTick, Tick materialEnd,
                    const std::function<double(Tick)>& ticksToSeconds) {
    if (!ticksToSeconds) return;
    for (auto& clip : clips) {
        if (clip.id != clipId) continue;
        const Tick longueur = clipPlayedLength(clip, materialEnd);
        // LE FONDU NE DÉPASSE JAMAIS LE CLIP : au-delà il mangerait ce qui
        // vient après, et ne s'entendrait plus comme un fondu.
        const Tick borne = std::clamp(atTick, clip.startTick, clip.startTick + longueur);
        clip.fadeInSeconds =
            std::max(0.0, ticksToSeconds(borne) - ticksToSeconds(clip.startTick));
        return;
    }
}

void setClipFadeOut(std::vector<Clip>& clips, uint64_t clipId, Tick atTick, Tick materialEnd,
                     const std::function<double(Tick)>& ticksToSeconds) {
    if (!ticksToSeconds) return;
    for (auto& clip : clips) {
        if (clip.id != clipId) continue;
        const Tick longueur = clipPlayedLength(clip, materialEnd);
        const Tick fin = clip.startTick + longueur;
        const Tick borne = std::clamp(atTick, clip.startTick, fin);
        clip.fadeOutSeconds = std::max(0.0, ticksToSeconds(fin) - ticksToSeconds(borne));
        return;
    }
}

void setClipGain(std::vector<Clip>& clips, const ClipSelection& selection, float gain) {
    if (selection.empty()) return;
    // JAMAIS NÉGATIF : une inversion de phase est un réglage à part, et la
    // confondre avec un gain négatif rendrait le bouton illisible -- on ne
    // saurait plus si un clip est faible ou inversé.
    const float valeur = std::max(0.0f, gain);
    for (auto& clip : clips)
        if (selected(selection, clip)) clip.gain = valeur;
}

void toggleClipPhase(std::vector<Clip>& clips, const ClipSelection& selection) {
    if (selection.empty()) return;
    // BASCULE PAR CLIP et non « tous à vrai » : inverser une sélection dont la
    // moitié l'est déjà doit rendre l'autre moitié, pas tout aligner.
    for (auto& clip : clips)
        if (selected(selection, clip)) clip.invertPhase = !clip.invertPhase;
}

bool clipSelectionBounds(const std::vector<Clip>& clips, const ClipSelection& selection,
                          Tick materialEnd, Tick& startTick, Tick& endTick) {
    bool trouve = false;
    for (const auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        const Tick fin = clip.startTick + clipPlayedLength(clip, materialEnd);
        if (!trouve) { startTick = clip.startTick; endTick = fin; trouve = true; }
        else { startTick = std::min(startTick, clip.startTick); endTick = std::max(endTick, fin); }
    }
    return trouve;
}

ClipTrackMove moveClipsAcrossTracks(std::vector<Track>& tracks, const ClipSelection& selection,
                                    int deltaTracks) {
    ClipTrackMove rapport;
    if (selection.empty() || deltaTracks == 0 || tracks.empty()) return rapport;

    // ON REPÈRE D'ABORD, ON DÉPLACE ENSUITE : déplacer en parcourant ferait
    // retrouver un clip déjà posé sur sa piste cible et le pousserait encore.
    struct Repere { size_t piste; uint64_t clip; };
    std::vector<Repere> reperes;
    int plusHaut = -1, plusBas = -1;
    for (size_t t = 0; t < tracks.size(); ++t)
        for (const auto& clip : tracks[t].clips)
            if (selected(selection, clip)) {
                reperes.push_back({t, clip.id});
                const int i = static_cast<int>(t);
                plusHaut = plusHaut < 0 ? i : std::min(plusHaut, i);
                plusBas = plusBas < 0 ? i : std::max(plusBas, i);
            }
    if (reperes.empty()) return rapport;

    // LA FIGURE GARDE SA FORME : le décalage est réduit pour tous.
    int applique = deltaTracks;
    if (applique < 0) applique = std::max(applique, -plusHaut);
    else applique = std::min(applique, static_cast<int>(tracks.size()) - 1 - plusBas);
    rapport.applied = applique;
    if (applique == 0) return rapport;

    for (const auto& repere : reperes) {
        auto& source = tracks[repere.piste];
        auto& cible = tracks[static_cast<size_t>(static_cast<int>(repere.piste) + applique)];
        auto it = std::find_if(source.clips.begin(), source.clips.end(),
                               [&](const Clip& c) { return c.id == repere.clip; });
        if (it == source.clips.end()) continue;

        // CE QUI EST REFUSÉ EST COMPTÉ : une piste de groupe, un genre qui ne
        // correspond pas, un fichier audio qui n'est pas le sien.
        const bool memeGenre = source.kind == cible.kind && cible.kind != Track::Kind::Group;
        const bool audioOk = cible.kind != Track::Kind::Audio || cible.audio.empty()
                             || cible.audio.path == source.audio.path;
        if (!memeGenre || !audioOk) { ++rapport.refused; continue; }
        if (cible.kind == Track::Kind::Audio && cible.audio.empty()) cible.audio = source.audio;

        // LES NOTES QUE LA FENÊTRE COUVRE SUIVENT LE CLIP, aux mêmes ticks.
        if (source.kind == Track::Kind::Midi) {
            const Tick debut = it->sourceStart;
            const bool jusquAuBout = it->sourceLength <= 0;
            const Tick fin = debut + it->sourceLength;
            std::vector<Note> restent;
            restent.reserve(source.notes.size());
            for (auto& note : source.notes) {
                const bool couverte = note.startTick >= debut && (jusquAuBout || note.startTick < fin);
                if (couverte) cible.notes.push_back(note);
                else restent.push_back(note);
            }
            source.notes.swap(restent);
            std::stable_sort(cible.notes.begin(), cible.notes.end(),
                              [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        }

        Clip deplace = *it;
        source.clips.erase(it);
        cible.clips.push_back(deplace);
        std::stable_sort(cible.clips.begin(), cible.clips.end(),
                          [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
        ++rapport.moved;
    }
    return rapport;
}

ClipCreation createClip(std::vector<Clip>& clips, Tick startTick, Tick length,
                        uint64_t& idCounter, Tick materialEnd) {
    ClipCreation faite;
    if (startTick < 0 || length <= 0) return faite;

    // LE DÉBUT EST-IL LIBRE ? On regarde la durée JOUÉE, pas la fenêtre : un
    // clip bouclé couvre la ligne de temps sur toute sa répétition, et créer
    // au milieu d'une boucle doublerait ce qu'elle répète.
    Tick suivant = -1;
    for (const auto& clip : clips) {
        const Tick longueur = clipPlayedLength(clip, materialEnd);
        if (startTick >= clip.startTick && startTick < clip.startTick + longueur) return faite;
        if (clip.startTick > startTick)
            suivant = suivant < 0 ? clip.startTick : std::min(suivant, clip.startTick);
    }

    Tick obtenue = length;
    if (suivant >= 0 && startTick + obtenue > suivant) {
        obtenue = suivant - startTick;
        faite.truncated = true;
    }
    // Le clip suivant colle au point visé : il n'y a pas la place d'un clip,
    // et un clip de zéro tick serait invisible et injouable.
    if (obtenue <= 0) return faite;

    Clip clip;
    clip.id = idCounter++;
    clip.sourceStart = startTick;
    clip.sourceLength = obtenue;
    clip.startTick = startTick;
    clip.length = obtenue;
    clips.push_back(clip);
    std::stable_sort(clips.begin(), clips.end(),
                      [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });

    faite.id = clip.id;
    faite.startTick = startTick;
    faite.length = obtenue;
    return faite;
}

ClipSelection duplicateClips(std::vector<Clip>& clips, const ClipSelection& selection,
                              Tick offsetTicks, uint64_t& idCounter) {
    ClipSelection creees;
    if (selection.empty()) return creees;

    // ON COLLECTE AVANT D'INSÉRER : ajouter dans le vecteur qu'on parcourt
    // invaliderait les itérateurs, et dupliquerait les copies.
    std::vector<Clip> copies;
    for (const auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        Clip copie = clip;
        copie.id = idCounter++;
        copie.startTick = std::max<Tick>(0, clip.startTick + offsetTicks);
        copies.push_back(copie);
        creees.insert(copie.id);
    }
    for (auto& copie : copies) clips.push_back(std::move(copie));
    std::stable_sort(clips.begin(), clips.end(),
                      [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
    return creees;
}

ClipJoin joinClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick materialEnd,
                    bool audioTrack, const std::function<double(Tick)>& ticksToSeconds) {
    ClipJoin bilan;
    if (selection.size() < 2) return bilan;

    // La fenêtre d'un clip, en ticks de matériau (zéro = jusqu'au bout).
    auto fenetreDe = [materialEnd](const Clip& c) {
        return c.sourceLength > 0 ? c.sourceLength : std::max<Tick>(0, materialEnd - c.sourceStart);
    };

    // On travaille sur les INDICES des clips choisis, rangés par position :
    // « le suivant » n'a de sens que sur la ligne de temps.
    std::vector<size_t> choisis;
    for (size_t i = 0; i < clips.size(); ++i)
        if (selected(selection, clips[i])) choisis.push_back(i);
    std::stable_sort(choisis.begin(), choisis.end(),
                      [&clips](size_t a, size_t b) { return clips[a].startTick < clips[b].startTick; });

    auto joignable = [&](const Clip& a, const Clip& b) {
        const Tick fa = fenetreDe(a), fb = fenetreDe(b);
        if (fa <= 0 || fb <= 0) return false;
        if (clipPlayedLength(a, materialEnd) != fa) return false;   // a boucle
        if (clipPlayedLength(b, materialEnd) != fb) return false;   // b boucle
        if (a.startTick + fa != b.startTick) return false;          // pas contigus
        if (a.sourceStart + fa != b.sourceStart) return false;      // la fenêtre ne se prolonge pas
        if (a.warpMode != WarpMode::Off || b.warpMode != WarpMode::Off) return false;
        if (a.muted != b.muted || a.reversed != b.reversed) return false;
        if (a.invertPhase != b.invertPhase || a.gain != b.gain) return false;
        // POUR L'AUDIO, la fenêtre du FICHIER doit se prolonger aussi : c'est
        // la même exigence, dans l'unité du matériau. Un demi-échantillon de
        // tolérance à 96 kHz, soit ce qu'un aller-retour en ticks peut coûter.
        if (audioTrack && ticksToSeconds) {
            const double attendue =
                a.sourceStartSeconds + (ticksToSeconds(a.sourceStart + fa) - ticksToSeconds(a.sourceStart));
            if (std::abs(b.sourceStartSeconds - attendue) > 0.5 / 96000.0) return false;
        }
        return true;
    };

    std::vector<bool> absorbe(clips.size(), false);
    size_t courant = 0;
    while (courant < choisis.size()) {
        Clip& tete = clips[choisis[courant]];
        size_t suivant = courant + 1;
        while (suivant < choisis.size()) {
            Clip& candidat = clips[choisis[suivant]];
            if (!joignable(tete, candidat)) { ++bilan.refused; break; }
            // La tête s'étend, et garde le fondu de SORTIE du dernier absorbé :
            // c'est le seul bord qui reste un bord.
            const Tick fa = fenetreDe(tete), fb = fenetreDe(candidat);
            tete.sourceLength = fa + fb;
            tete.length = tete.sourceLength;
            tete.fadeOutSeconds = candidat.fadeOutSeconds;
            absorbe[choisis[suivant]] = true;
            ++bilan.joined;
            ++suivant;
        }
        courant = suivant;
    }

    if (bilan.joined > 0) {
        std::vector<Clip> restants;
        restants.reserve(clips.size() - bilan.joined);
        for (size_t i = 0; i < clips.size(); ++i)
            if (!absorbe[i]) restants.push_back(clips[i]);
        clips = std::move(restants);
    }
    return bilan;
}

size_t splitClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick atTick,
                   Tick materialEnd, uint64_t& idCounter,
                   const std::function<double(Tick)>& ticksToSeconds) {
    if (selection.empty()) return 0;

    std::vector<Clip> ajoutes;
    size_t coupes = 0;
    for (auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        const Tick longueur = clipPlayedLength(clip, materialEnd);
        // STRICTEMENT À L'INTÉRIEUR : couper sur un bord ne produirait qu'un
        // clip vide et un clip identique, c'est-à-dire rien d'utile et une
        // annulation à faire.
        if (atTick <= clip.startTick || atTick >= clip.startTick + longueur) continue;

        const Tick avant = atTick - clip.startTick;

        Clip seconde = clip;
        seconde.id = idCounter++;
        seconde.startTick = atTick;
        seconde.sourceStart = clip.sourceStart + avant;
        seconde.length = longueur - avant;
        seconde.sourceLength = seconde.length;
        // LES DEUX MOITIÉS REJOUENT EXACTEMENT CE QUE JOUAIT L'ORIGINAL : la
        // seconde reprend la fenêtre là où la première l'a laissée, en secondes
        // pour un clip audio comme en ticks pour un clip MIDI. Un clip qui
        // suit le tempo (D12) coupe SA carte : chaque moitié garde les
        // marqueurs qui la concernent et reçoit un marqueur neuf au point de
        // coupe, à la position du fichier que la carte y mettait — si bien
        // que les deux cartes mises bout à bout sont l'ancienne, tick pour tick.
        if (clipIsWarped(clip)) {
            const double coupe = warpSourceSecondsAt(clip, avant);
            std::vector<WarpMarker> premiere, deuxieme;
            for (const auto& m : clip.warpMarkers) {
                if (m.tick < avant) premiere.push_back(m);
                else if (m.tick > avant) deuxieme.push_back({m.sourceSeconds, m.tick - avant});
            }
            premiere.push_back({coupe, avant});
            deuxieme.insert(deuxieme.begin(), {coupe, 0});
            if (deuxieme.size() < 2) deuxieme.push_back({warpSourceSecondsAt(clip, longueur), longueur - avant});
            clip.warpMarkers = std::move(premiere);
            seconde.warpMarkers = std::move(deuxieme);
            seconde.sourceStartSeconds = coupe;
        } else if (ticksToSeconds) {
            seconde.sourceStartSeconds =
                clip.sourceStartSeconds + (ticksToSeconds(atTick) - ticksToSeconds(clip.startTick));
        }
        // LES FONDUS NE SE DUPLIQUENT PAS : le fondu d'entrée appartient au
        // début de l'original, le fondu de sortie à sa fin. Les recopier sur
        // les deux moitiés ferait apparaître un trou au point de coupe.
        seconde.fadeInSeconds = 0.0;
        clip.fadeOutSeconds = 0.0;

        clip.length = avant;
        clip.sourceLength = avant;
        ajoutes.push_back(std::move(seconde));
        ++coupes;
    }
    for (auto& clip : ajoutes) clips.push_back(std::move(clip));
    std::stable_sort(clips.begin(), clips.end(),
                      [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
    return coupes;
}

// ---------------------------------------------------------------------------
// Le suivi de tempo (D12.4)
// ---------------------------------------------------------------------------

bool clipIsWarped(const Clip& clip) {
    return clip.warpMode != WarpMode::Off && clip.warpMarkers.size() >= 2;
}

namespace {

Clip* clipById(std::vector<Clip>& clips, uint64_t clipId) {
    for (auto& clip : clips) if (clip.id == clipId) return &clip;
    return nullptr;
}
double pente(const WarpMarker& a, const WarpMarker& b) {
    const Tick dt = std::max<Tick>(1, b.tick - a.tick);
    return (b.sourceSeconds - a.sourceSeconds) / static_cast<double>(dt);
}

} // namespace

double warpSourceSecondsAt(const Clip& clip, Tick relativeTick) {
    const auto& m = clip.warpMarkers;
    if (m.size() < 2) return clip.sourceStartSeconds;
    size_t i = 1;
    while (i + 1 < m.size() && m[i].tick <= relativeTick) ++i;
    return m[i - 1].sourceSeconds + pente(m[i - 1], m[i]) * static_cast<double>(relativeTick - m[i - 1].tick);
}

Tick warpTickAtSeconds(const Clip& clip, double sourceSeconds) {
    const auto& m = clip.warpMarkers;
    if (m.size() < 2) return 0;
    size_t i = 1;
    while (i + 1 < m.size() && m[i].sourceSeconds <= sourceSeconds) ++i;
    const double p = pente(m[i - 1], m[i]);
    if (p <= 0.0) return m[i - 1].tick;
    return m[i - 1].tick + static_cast<Tick>(std::llround((sourceSeconds - m[i - 1].sourceSeconds) / p));
}

void setClipWarpMode(std::vector<Clip>& clips, const ClipSelection& selection, WarpMode mode,
                     Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds) {
    for (auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        clip.warpMode = mode;
        if (mode == WarpMode::Off || clip.warpMarkers.size() >= 2 || !ticksToSeconds) continue;
        // LA PAIRE NEUTRE : le début et la fin, au rapport un. Le son ne
        // change pas d'un bit (court-circuit du moteur) tant qu'on ne bouge
        // rien ; le clip est simplement devenu calable.
        const Tick longueur = clipPlayedLength(clip, materialEnd);
        const double secondes = ticksToSeconds(clip.startTick + longueur) - ticksToSeconds(clip.startTick);
        clip.warpMarkers = {{clip.sourceStartSeconds, 0},
                            {clip.sourceStartSeconds + secondes, std::max<Tick>(1, longueur)}};
    }
}

double setClipBars(std::vector<Clip>& clips, uint64_t clipId, int bars, Tick ticksPerBar,
                   Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds) {
    Clip* clip = clipById(clips, clipId);
    if (!clip || bars <= 0 || ticksPerBar <= 0) return 0.0;
    const Tick longueur = clipPlayedLength(*clip, materialEnd);
    // Le matériau actuellement joué, en secondes de FICHIER : par la carte s'il
    // y en a une, par le tempo sinon.
    double secondes = 0.0;
    if (clipIsWarped(*clip)) secondes = warpSourceSecondsAt(*clip, longueur) - clip->sourceStartSeconds;
    else if (ticksToSeconds) secondes = ticksToSeconds(clip->startTick + longueur) - ticksToSeconds(clip->startTick);
    if (secondes <= 0.0) return 0.0;
    const Tick nouvelle = static_cast<Tick>(bars) * ticksPerBar;
    clip->warpMarkers = {{clip->sourceStartSeconds, 0}, {clip->sourceStartSeconds + secondes, nouvelle}};
    clip->length = nouvelle;
    clip->sourceLength = nouvelle;
    if (clip->warpMode == WarpMode::Off) clip->warpMode = WarpMode::KeepPitch;
    // Le tempo d'origine : N mesures de 4 temps en `secondes`. (Le nombre de
    // temps par mesure est celui que `ticksPerBar` encode ; on rend des
    // mesures par minute × 4, ce qui est le BPM d'une mesure à quatre temps.)
    return 60.0 * 4.0 * static_cast<double>(bars) / secondes;
}

int addWarpMarker(std::vector<Clip>& clips, uint64_t clipId, Tick relativeTick) {
    Clip* clip = clipById(clips, clipId);
    if (!clip || !clipIsWarped(*clip)) return -1;
    auto& m = clip->warpMarkers;
    if (relativeTick <= m.front().tick || relativeTick >= m.back().tick) return -1;
    for (const auto& existant : m) if (existant.tick == relativeTick) return -1;
    const WarpMarker neuf{warpSourceSecondsAt(*clip, relativeTick), relativeTick};
    // L'INDICE SE CALCULE AVANT L'INSERTION, et ce n'est pas du zèle : écrit
    // `m.insert(ou, neuf) - m.begin()`, les deux opérandes ne sont pas
    // séquencés, `m.begin()` peut être lu AVANT l'insertion, donc avant la
    // réallocation qui l'invalide -- la différence vaut alors n'importe quoi
    // (94 au lieu de 1, mesuré par le test qui a trouvé ce défaut).
    const auto indice = std::upper_bound(m.begin(), m.end(), relativeTick,
                                          [](Tick t, const WarpMarker& x) { return t < x.tick; })
                        - m.begin();
    m.insert(m.begin() + indice, neuf);
    return static_cast<int>(indice);
}

bool moveWarpMarker(std::vector<Clip>& clips, uint64_t clipId, size_t index, Tick relativeTick) {
    Clip* clip = clipById(clips, clipId);
    if (!clip || !clipIsWarped(*clip)) return false;
    auto& m = clip->warpMarkers;
    if (index == 0 || index >= m.size()) return false;
    const Tick bas = m[index - 1].tick + 1;
    const Tick haut = index + 1 < m.size() ? m[index + 1].tick - 1 : std::numeric_limits<Tick>::max();
    const Tick vise = std::clamp(relativeTick, bas, haut);
    if (vise == m[index].tick) return false;
    m[index].tick = vise;
    return true;
}

void toggleClipReverse(std::vector<Clip>& clips, const ClipSelection& selection) {
    for (auto& clip : clips)
        if (selected(selection, clip)) clip.reversed = !clip.reversed;
}

bool stretchClipsEnd(std::vector<Clip>& clips, const ClipSelection& selection, Tick deltaTicks,
                     Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds) {
    if (selection.empty() || deltaTicks == 0) return false;
    bool bouge = false;
    for (auto& clip : clips) {
        if (!selected(selection, clip)) continue;
        const Tick avant = clipPlayedLength(clip, materialEnd);
        const Tick apres = std::max<Tick>(1, avant + deltaTicks);
        if (apres == avant) continue;
        if (!clipIsWarped(clip)) {
            clip.warpMode = clip.warpMode == WarpMode::Off ? WarpMode::KeepPitch : clip.warpMode;
            if (clip.warpMarkers.size() < 2) {
                if (!ticksToSeconds) continue;
                const double secondes = ticksToSeconds(clip.startTick + avant) - ticksToSeconds(clip.startTick);
                clip.warpMarkers = {{clip.sourceStartSeconds, 0},
                                    {clip.sourceStartSeconds + secondes, std::max<Tick>(1, avant)}};
            }
        }
        // LES MARQUEURS GLISSENT EN PROPORTION : le calage relatif est gardé,
        // le dernier suit le bord. Deux marqueurs ne se confondent jamais.
        const double rapport = static_cast<double>(apres) / static_cast<double>(std::max<Tick>(1, clip.warpMarkers.back().tick));
        Tick precedent = -1;
        for (auto& m : clip.warpMarkers) {
            m.tick = std::max<Tick>(precedent + 1, static_cast<Tick>(std::llround(static_cast<double>(m.tick) * rapport)));
            precedent = m.tick;
        }
        clip.warpMarkers.back().tick = std::max<Tick>(clip.warpMarkers.back().tick, apres);
        clip.length = apres;
        clip.sourceLength = apres;
        bouge = true;
    }
    return bouge;
}

bool removeWarpMarker(std::vector<Clip>& clips, uint64_t clipId, size_t index) {
    Clip* clip = clipById(clips, clipId);
    if (!clip || !clipIsWarped(*clip)) return false;
    auto& m = clip->warpMarkers;
    if (index == 0 || index >= m.size() || m.size() <= 2) return false;
    m.erase(m.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

} // namespace vsm::sequencer
