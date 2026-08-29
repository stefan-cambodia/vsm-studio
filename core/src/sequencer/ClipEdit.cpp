#include "vsm/sequencer/ClipEdit.h"
#include <algorithm>

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
        if (ticksToSeconds) {
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
        // pour un clip audio comme en ticks pour un clip MIDI.
        if (ticksToSeconds)
            seconde.sourceStartSeconds =
                clip.sourceStartSeconds + (ticksToSeconds(atTick) - ticksToSeconds(clip.startTick));
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

} // namespace vsm::sequencer
