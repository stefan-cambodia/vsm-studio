#include "vsm/sequencer/PlaybackScheduler.h"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace vsm::sequencer {

using namespace vsm::midi;

namespace {

/// Une RÉPÉTITION d'un clip : la portion du matériau qu'elle lit, et de
/// combien elle la décale sur la ligne de temps.
///
/// Un clip non bouclé en produit une seule ; un clip plus long que sa fenêtre
/// en produit autant qu'il contient de répétitions. Aucune note n'est copiée :
/// c'est le décalage qui est répété, pas le matériau.
struct Passage {
    Tick sourceFrom = 0;                                   ///< inclus
    Tick sourceTo = std::numeric_limits<Tick>::max();      ///< exclu
    Tick shift = 0;                                        ///< à ajouter au tick source
    Tick outLimit = std::numeric_limits<Tick>::max();      ///< fin dure sur la ligne de temps
};

/// Les passages d'une piste.
///
/// UNE PISTE SANS CLIP DONNE UN PASSAGE IDENTITÉ, et c'est ce qui rend
/// l'absence de régression DÉMONTRABLE plutôt que promise : il n'y a pas un
/// « chemin historique » à côté du chemin des clips, qui pourrait diverger de
/// lui à la première correction. Il y a un seul chemin, et le cas sans découpe
/// est la fenêtre qui ne coupe rien.
std::vector<Passage> passagesOf(const Track& track, Tick materialEnd) {
    std::vector<Passage> passages;
    if (track.clips.empty()) {
        passages.push_back(Passage{});
        return passages;
    }

    for (const auto& clip : track.clips) {
        if (clip.muted) continue;
        const Tick fenetre = clip.sourceLength > 0 ? clip.sourceLength
                                                   : std::max<Tick>(0, materialEnd - clip.sourceStart);
        if (fenetre <= 0) continue;   // fenêtre vide : rien à lire, et pas de boucle infinie
        const Tick jouee = clip.length > 0 ? clip.length : fenetre;

        for (Tick depart = 0; depart < jouee; depart += fenetre) {
            Passage passage;
            passage.sourceFrom = clip.sourceStart;
            passage.sourceTo = clip.sourceStart + fenetre;
            passage.shift = clip.startTick + depart - clip.sourceStart;
            passage.outLimit = clip.startTick + jouee;
            passages.push_back(passage);
        }
    }
    return passages;
}

/// LE DERNIER TICK DE SORTIE, STRICTEMENT AVANT `limite`, où cet événement du
/// matériau est joué -- tous passages confondus. -1 s'il n'est jamais joué
/// avant.
///
/// « Tous passages confondus » n'est pas un détail : un clip bouclé rejoue la
/// même valeur source à chaque répétition, et c'est la DERNIÈRE qui est passée
/// sous la tête de lecture, pas celle de la ligne de temps du matériau. Chasser
/// sur les ticks sources rendrait la valeur d'un passage qui n'a peut-être
/// jamais été joué.
Tick lastOutBefore(const std::vector<Passage>& passages, Tick source, Tick limit) {
    Tick meilleur = -1;
    for (const auto& passage : passages) {
        if (source < passage.sourceFrom || source >= passage.sourceTo) continue;
        const Tick out = source + passage.shift;
        if (out >= passage.outLimit || out >= limit) continue;
        meilleur = std::max(meilleur, out);
    }
    return meilleur;
}

} // namespace

std::vector<ScheduledEvent> PlaybackScheduler::chaseAt(const Project& project, Tick startTick) {
    std::vector<ScheduledEvent> resultat;
    if (startTick <= 0) return resultat;

    const bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                      [](const Track& t) { return t.solo; });
    const Tick materialEnd = project.lastUsedTick();

    for (size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex) {
        const Track& track = project.tracks[trackIndex];
        if (anySolo ? !track.solo : track.muted) continue;
        const std::vector<Passage> passages = passagesOf(track, materialEnd);

    // ------------------------------------------------------------------
    // LA CHASSE AUX CONTRÔLEURS (D16.2) — « Chase Events » de Cubase.
    //
    // Un événement continu n'est émis que si son tick tombe dans la
    // fenêtre demandée : démarrer la lecture au refrain perdait donc la
    // pédale forte posée au couplet, le balayage de filtre en cours et le
    // programme choisi à la première mesure. Le morceau ne sonnait pas
    // comme lui-même, et rien ne le disait -- on croyait entendre le
    // refrain, on entendait le refrain sans sa pédale.
    //
    // STRICTEMENT AVANT `startTick`, et non « jusqu'à » : un événement
    // posé exactement là est déjà émis par la boucle ci-dessous
    // (`inRange` commence à `startTick`), et le chasser aussi le
    // dédoublerait.
    //
    // CE QUI EST CHASSÉ, ET CE QUI NE L'EST PAS. Les contrôleurs continus
    // (CC), le pitch bend, la pression de CANAL et le programme : ce sont
    // des états du canal, qui valent tant qu'on ne les change pas, et
    // qu'un instrument appliquera aux notes à venir. La pression
    // POLYPHONIQUE, non : elle s'adresse à une note nommée, et aucune
    // note d'avant le point de départ ne sonne encore -- la rendre
    // enverrait une pression pour une note qui n'existe pas.
    //
    // Le programme part EN PREMIER : sur beaucoup d'instruments il
    // remplace le son, et les contrôleurs rendus avant lui seraient
    // effacés par lui.
    if (startTick > 0) {
        const double quand = project.ticksToSeconds(startTick);
        std::map<uint8_t, std::pair<Tick, uint8_t>> programmes;
        std::map<uint16_t, std::pair<Tick, uint8_t>> controleurs;
        std::map<uint8_t, std::pair<Tick, int16_t>> bends;
        std::map<uint8_t, std::pair<Tick, uint8_t>> pressions;
        auto retenir = [](auto& carte, auto cle, Tick out, auto valeur) {
            auto it = carte.find(cle);
            if (it == carte.end() || it->second.first < out) carte[cle] = {out, valeur};
        };

        for (const auto& pc : track.programChanges) {
            const Tick out = lastOutBefore(passages, pc.tick, startTick);
            if (out >= 0) retenir(programmes, pc.channel, out, pc.program);
        }
        for (const auto& cc : track.controlChanges) {
            const Tick out = lastOutBefore(passages, cc.tick, startTick);
            if (out >= 0)
                retenir(controleurs,
                        static_cast<uint16_t>(cc.channel * 256 + cc.controller), out, cc.value);
        }
        for (const auto& pb : track.pitchBends) {
            const Tick out = lastOutBefore(passages, pb.tick, startTick);
            if (out >= 0) retenir(bends, pb.channel, out, pb.value);
        }
        for (const auto& cp : track.channelPressure) {
            const Tick out = lastOutBefore(passages, cp.tick, startTick);
            if (out >= 0) retenir(pressions, cp.channel, out, cp.pressure);
        }

        for (const auto& [canal, v] : programmes)
            resultat.push_back({quand, trackIndex, ProgramChangeEvent{canal, v.second}});
        for (const auto& [cle, v] : controleurs)
            resultat.push_back({quand, trackIndex,
                               ControlChangeEvent{static_cast<uint8_t>(cle / 256),
                                                   static_cast<uint8_t>(cle % 256), v.second}});
        for (const auto& [canal, v] : bends)
            resultat.push_back({quand, trackIndex, PitchBendEvent{canal, v.second}});
        for (const auto& [canal, v] : pressions)
            resultat.push_back({quand, trackIndex, ChannelPressureEvent{canal, v.second}});
    }
    }
    return resultat;
}

std::vector<ScheduledEvent> PlaybackScheduler::build(const Project& project,
                                                       Tick startTick, Tick endTick) {
    std::vector<ScheduledEvent> result;
    // LA CHASSE AUX CONTRÔLEURS (D16.2) est calculée à part pour être placée
    // EN TÊTE : le tri final est stable, et une pédale rendue après la note
    // qu'elle devait tenir ne la tient pas.
    std::vector<ScheduledEvent> chasse =
        startTick < endTick ? chaseAt(project, startTick) : std::vector<ScheduledEvent>{};

    bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                [](const Track& t) { return t.solo; });
    const Tick materialEnd = project.lastUsedTick();

    for (size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex) {
        const Track& track = project.tracks[trackIndex];
        bool audible = anySolo ? track.solo : !track.muted;
        if (!audible) continue;

        const std::vector<Passage> passages = passagesOf(track, materialEnd);

        for (const auto& passage : passages) {
            // Le tick source appartient-il à ce passage, et où sort-il ?
            auto lu = [&passage](Tick source, Tick& out) {
                if (source < passage.sourceFrom || source >= passage.sourceTo) return false;
                out = source + passage.shift;
                return out < passage.outLimit;
            };
            auto inRange = [&](Tick t) { return t >= startTick && t < endTick; };

            for (const auto& note : track.notes) {
                if (note.muted) continue; // note rendue muette dans l'éditeur (Note::muted)
                Tick debut = 0;
                if (!lu(note.startTick, debut)) continue;
                if (inRange(debut))
                    result.push_back({project.ticksToSeconds(debut), trackIndex,
                                       NoteOnEvent{note.channel, note.number, note.velocity}});

                // LA FIN EST COUPÉE À LA FIN DU CLIP, jamais laissée pendre :
                // une note dont le NoteOff tomberait au-delà resterait tenue
                // pour toujours. C'est la règle de tout éditeur de régions, et
                // sans clip elle ne s'applique jamais (la limite est infinie).
                const Tick fin = std::min(note.endTick + passage.shift, passage.outLimit);
                if (inRange(fin))
                    result.push_back({project.ticksToSeconds(fin), trackIndex,
                                       NoteOffEvent{note.channel, note.number, note.releaseVelocity}});
            }

            Tick t = 0;
            for (const auto& cc : track.controlChanges)
                if (lu(cc.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ControlChangeEvent{cc.channel, cc.controller, cc.value}});
            for (const auto& pb : track.pitchBends)
                if (lu(pb.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       PitchBendEvent{pb.channel, pb.value}});
            for (const auto& pa : track.polyAftertouch)
                if (lu(pa.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       PolyPressureEvent{pa.channel, pa.note, pa.pressure}});
            for (const auto& cp : track.channelPressure)
                if (lu(cp.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ChannelPressureEvent{cp.channel, cp.pressure}});
            for (const auto& pc : track.programChanges)
                if (lu(pc.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ProgramChangeEvent{pc.channel, pc.program}});
        }
    }

    // LE DÉCALAGE DE PISTE (D16.7) s'applique EN SECONDES, à la toute fin, et
    // avant le tri : il ne suit pas le tempo (voir `Track::delayMs`), et un
    // événement décalé doit être trié à l'endroit où il sonne, pas à celui
    // où il était écrit. La chasse est décalée aussi -- une pédale rendue à
    // la position du transport doit arriver avec la piste qu'elle règle.
    for (auto* liste : {&chasse, &result})
        for (auto& ev : *liste) {
            if (ev.trackIndex >= project.tracks.size()) continue;
            const double ms = project.tracks[ev.trackIndex].delayMs;
            if (ms != 0.0) ev.timeSeconds += ms / 1000.0;
        }

    // LA CHASSE D'ABORD, puis le reste : le tri est STABLE, donc les valeurs
    // rendues à `startTick` précèdent tout ce qui tombe au même instant.
    chasse.insert(chasse.end(), result.begin(), result.end());
    std::stable_sort(chasse.begin(), chasse.end(),
                      [](const ScheduledEvent& a, const ScheduledEvent& b) {
                          return a.timeSeconds < b.timeSeconds;
                      });
    return chasse;
}

} // namespace vsm::sequencer
