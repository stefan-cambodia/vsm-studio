#include "vsm/sequencer/MixSnapshot.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace vsm::sequencer {

namespace {

float departDe(const std::vector<float>& departs, size_t i) { return i < departs.size() ? departs[i] : 0.0f; }

/// Deux consoles identiques À L'OREILLE : un vecteur de départs court vaut « pas d'envoi » sur les
/// bus suivants (`Track::sendLevel`), il n'est donc pas « différent » d'un vecteur prolongé de zéros.
bool memeConsole(const TrackMixState& a, const TrackMixState& b) {
    const std::equal_to<float> egal;
    if (!egal(a.volume, b.volume) || !egal(a.pan, b.pan) || a.muted != b.muted || a.solo != b.solo
        || !egal(a.inputTrimDb, b.inputTrimDb) || a.invertPhase != b.invertPhase || !(a.inserts == b.inserts))
        return false;
    for (size_t i = 0; i < std::max(a.sendLevels.size(), b.sendLevels.size()); ++i)
        if (!egal(departDe(a.sendLevels, i), departDe(b.sendLevels, i))) return false;
    return true;
}

/// L'état que la piste aurait APRÈS le rappel de `garde` — un seul chemin pour l'aperçu et pour
/// le rappel. Les inserts : celui de même rang ET de même type reprend son état ; les autres
/// (remplacés, ajoutés, retirés depuis) sont comptés dans `laisses`.
TrackMixState rappele(const Track& piste, const TrackMixState& garde, size_t& laisses) {
    TrackMixState etat = captureTrackMix(piste);
    etat.volume = garde.volume;
    etat.pan = garde.pan;
    etat.muted = garde.muted;
    etat.solo = garde.solo;
    etat.inputTrimDb = garde.inputTrimDb;
    etat.invertPhase = garde.invertPhase;
    etat.sendLevels = garde.sendLevels;
    const size_t communs = std::min(etat.inserts.size(), garde.inserts.size());
    for (size_t i = 0; i < communs; ++i) {
        if (etat.inserts[i].type == garde.inserts[i].type) etat.inserts[i].enabled = garde.inserts[i].enabled;
        else ++laisses;
    }
    laisses += std::max(etat.inserts.size(), garde.inserts.size()) - communs;
    return etat;
}

MixRecallReport parcourir(const Project& project, const std::string& name, Project* cible) {
    MixRecallReport bilan;
    bilan.found = hasMixSnapshot(project, name);
    if (!bilan.found) return bilan;
    for (size_t t = 0; t < project.tracks.size(); ++t) {
        const Track& piste = project.tracks[t];
        const auto garde = piste.mixSnapshots.find(name);
        if (garde == piste.mixSnapshots.end()) {
            ++bilan.withoutState;
            continue;
        }
        ++bilan.recalled;
        const TrackMixState apres = rappele(piste, garde->second, bilan.insertsLeft);
        if (!memeConsole(captureTrackMix(piste), apres)) ++bilan.changed;
        if (cible == nullptr) continue;
        Track& ecrite = cible->tracks[t];
        ecrite.volume = apres.volume;
        ecrite.pan = apres.pan;
        ecrite.muted = apres.muted;
        ecrite.solo = apres.solo;
        ecrite.inputTrimDb = apres.inputTrimDb;
        ecrite.invertPhase = apres.invertPhase;
        ecrite.sendLevels = apres.sendLevels;
        for (size_t i = 0; i < ecrite.effects.size(); ++i) ecrite.effects[i].enabled = apres.inserts[i].enabled;
    }
    return bilan;
}

} // namespace

TrackMixState captureTrackMix(const Track& track) {
    TrackMixState etat;
    etat.volume = track.volume;
    etat.pan = track.pan;
    etat.muted = track.muted;
    etat.solo = track.solo;
    etat.inputTrimDb = track.inputTrimDb;
    etat.invertPhase = track.invertPhase;
    etat.sendLevels = track.sendLevels;
    for (const auto& insert : track.effects) etat.inserts.push_back({insert.type, insert.enabled});
    return etat;
}

bool hasMixSnapshot(const Project& project, const std::string& name) {
    return std::find(project.mixSnapshotNames.begin(), project.mixSnapshotNames.end(), name)
           != project.mixSnapshotNames.end();
}

std::string nextMixSnapshotName(const Project& project, const std::string& prefixe) {
    for (size_t n = project.mixSnapshotNames.size() + 1;; ++n) {
        const std::string nom = prefixe + " " + std::to_string(n);
        if (!hasMixSnapshot(project, nom)) return nom;
    }
}

bool takeMixSnapshot(Project& project, const std::string& name) {
    const bool existait = hasMixSnapshot(project, name);
    for (auto& piste : project.tracks) piste.mixSnapshots[name] = captureTrackMix(piste);
    if (!existait) project.mixSnapshotNames.push_back(name);
    return existait;
}

MixRecallReport previewMixRecall(const Project& project, const std::string& name) {
    return parcourir(project, name, nullptr);
}

MixRecallReport recallMixSnapshot(Project& project, const std::string& name) {
    return parcourir(project, name, &project);
}

bool removeMixSnapshot(Project& project, const std::string& name) {
    const auto ici = std::find(project.mixSnapshotNames.begin(), project.mixSnapshotNames.end(), name);
    if (ici == project.mixSnapshotNames.end()) return false;
    project.mixSnapshotNames.erase(ici);
    for (auto& piste : project.tracks) piste.mixSnapshots.erase(name);
    return true;
}

size_t countSoloedTracks(const Project& project) {
    return static_cast<size_t>(std::count_if(project.tracks.begin(), project.tracks.end(),
                                             [](const Track& t) { return t.solo; }));
}

size_t countMutedTracks(const Project& project) {
    return static_cast<size_t>(std::count_if(project.tracks.begin(), project.tracks.end(),
                                             [](const Track& t) { return t.muted; }));
}

size_t clearAllSolos(Project& project) {
    size_t n = 0;
    for (auto& t : project.tracks)
        if (t.solo) { t.solo = false; ++n; }
    return n;
}

size_t clearAllMutes(Project& project) {
    size_t n = 0;
    for (auto& t : project.tracks)
        if (t.muted) { t.muted = false; ++n; }
    return n;
}

namespace {

// D559.2 : l'état d'une piste NEUVE (`Track` construit par défaut), réglage par réglage — les départs à zéro sur toute
// la longueur du vecteur.
bool trancheNeutre(const Track& t) {
    const Track neuve;
    return t.volume == neuve.volume && t.pan == neuve.pan && t.muted == neuve.muted && t.solo == neuve.solo
           && t.inputTrimDb == neuve.inputTrimDb && t.invertPhase == neuve.invertPhase
           && std::all_of(t.sendLevels.begin(), t.sendLevels.end(), [](float d) { return d == 0.0f; });
}

} // namespace

size_t countTracksToReset(const Project& project) {
    return static_cast<size_t>(std::count_if(project.tracks.begin(), project.tracks.end(),
                                             [](const Track& t) { return !trancheNeutre(t); }));
}

size_t countTracksToReset(const Project& project, const std::set<size_t>& only) {
    size_t n = 0;
    for (size_t i : only)
        if (i < project.tracks.size() && !trancheNeutre(project.tracks[i])) ++n;
    return n;
}

size_t resetConsole(Project& project) {
    std::set<size_t> toutes;
    for (size_t i = 0; i < project.tracks.size(); ++i) toutes.insert(i);
    return resetConsole(project, toutes);
}

size_t resetConsole(Project& project, const std::set<size_t>& only) {
    const Track neuve;
    size_t n = 0;
    for (size_t i : only) {
        if (i >= project.tracks.size()) continue;
        auto& t = project.tracks[i];
        if (trancheNeutre(t)) continue;
        t.volume = neuve.volume;
        t.pan = neuve.pan;
        t.muted = neuve.muted;
        t.solo = neuve.solo;
        t.inputTrimDb = neuve.inputTrimDb;
        t.invertPhase = neuve.invertPhase;
        std::fill(t.sendLevels.begin(), t.sendLevels.end(), 0.0f);
        ++n;
    }
    return n;
}

void eraseSendLevelEverywhere(Project& project, size_t bus) {
    const auto effacer = [bus](std::vector<float>& departs) {
        if (bus < departs.size()) departs.erase(departs.begin() + static_cast<std::ptrdiff_t>(bus));
    };
    for (auto& piste : project.tracks) {
        effacer(piste.sendLevels);
        for (auto& [nom, etat] : piste.mixSnapshots) effacer(etat.sendLevels);
    }
}

ChannelSettings captureChannelSettings(const Track& track) {
    return {track.inputTrimDb, track.invertPhase, track.sendLevels, track.effects};
}

namespace {
bool memesInserts(const std::vector<TrackEffect>& a, const std::vector<TrackEffect>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type || a[i].parameters != b[i].parameters || a[i].nativeState != b[i].nativeState
            || a[i].enabled != b[i].enabled)
            return false;
    return true;
}
/// Deux vecteurs de départs égaux à « pas d'envoi » près : un vecteur court vaut zéro au-delà (`Track::sendLevel`).
bool memesDeparts(const std::vector<float>& a, const std::vector<float>& b) {
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
        if ((i < a.size() ? a[i] : 0.0f) != (i < b.size() ? b[i] : 0.0f)) return false;
    return true;
}
} // namespace

bool channelSettingsWouldChange(const Track& track, const ChannelSettings& s) {
    if (track.isFolder() || track.isVca()) return false;
    return track.inputTrimDb != s.inputTrimDb || track.invertPhase != s.invertPhase
        || !memesDeparts(track.sendLevels, s.sendLevels) || !memesInserts(track.effects, s.effects);
}

bool applyChannelSettings(Track& track, const ChannelSettings& s) {
    if (!channelSettingsWouldChange(track, s)) return false;
    track.inputTrimDb = s.inputTrimDb;
    track.invertPhase = s.invertPhase;
    track.sendLevels = s.sendLevels;
    track.effects = s.effects;
    return true;
}

float linkedVolumeDb(float gain) {
    if (!(gain > 0.0f)) return -60.0f;
    return std::clamp(20.0f * std::log10(gain), -60.0f, 6.0f);
}

namespace {
/// Une piste peut-elle suivre ce paramètre ? Un dossier n'a pas de fader, un VCA pas de panoramique.
bool peutSuivre(const Track& t, LinkedMixParam param) {
    if (t.isFolder()) return false;
    return param == LinkedMixParam::Volume || !t.isVca();
}
float valeurLiee(const Track& t, LinkedMixParam param) {
    return param == LinkedMixParam::Volume ? linkedVolumeDb(t.volume) : t.pan;
}
} // namespace

LinkedMixGesture beginLinkedMix(const Project& project, LinkedMixParam param, size_t touched,
                                const std::set<size_t>& selection) {
    LinkedMixGesture g;
    g.param = param;
    g.touched = touched;
    if (touched >= project.tracks.size() || selection.count(touched) == 0) return g;   // hors du choix : seule
    g.touchedStart = valeurLiee(project.tracks[touched], param);
    for (size_t i : selection)
        if (i != touched && i < project.tracks.size() && peutSuivre(project.tracks[i], param))
            g.followers.push_back({i, valeurLiee(project.tracks[i], param),
                                   param == LinkedMixParam::Volume ? project.tracks[i].volume : project.tracks[i].pan});
    return g;
}

size_t applyLinkedMix(Project& project, const LinkedMixGesture& g) {
    if (!g.active() || g.touched >= project.tracks.size()) return 0;
    const float ecart = valeurLiee(project.tracks[g.touched], g.param) - g.touchedStart;
    size_t bouge = 0;
    for (const auto& f : g.followers) {
        if (f.track >= project.tracks.size()) continue;
        auto& t = project.tracks[f.track];
        float& cible = g.param == LinkedMixParam::Volume ? t.volume : t.pan;
        float valeur = f.raw;   // le geste revenu à son départ rend la valeur exacte, sans aller-retour par les dB
        if (ecart != 0.0f) {
            if (g.param == LinkedMixParam::Volume) {
                const float db = std::clamp(f.start + ecart, -60.0f, 6.0f);
                valeur = db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
            } else {
                valeur = std::clamp(f.start + ecart, -1.0f, 1.0f);
            }
        }
        if (valeur != cible) { cible = valeur; ++bouge; }
    }
    return bouge;
}

} // namespace vsm::sequencer
