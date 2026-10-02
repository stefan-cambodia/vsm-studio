#include "vsm/sequencer/MixSnapshot.h"
#include <algorithm>
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

void eraseSendLevelEverywhere(Project& project, size_t bus) {
    const auto effacer = [bus](std::vector<float>& departs) {
        if (bus < departs.size()) departs.erase(departs.begin() + static_cast<std::ptrdiff_t>(bus));
    };
    for (auto& piste : project.tracks) {
        effacer(piste.sendLevels);
        for (auto& [nom, etat] : piste.mixSnapshots) effacer(etat.sendLevels);
    }
}

} // namespace vsm::sequencer
