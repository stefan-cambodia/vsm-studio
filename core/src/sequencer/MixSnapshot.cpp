#include "vsm/sequencer/MixSnapshot.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>
#include <variant>

namespace vsm::sequencer {

namespace {

float departDe(const std::vector<float>& departs, size_t i) { return i < departs.size() ? departs[i] : 0.0f; }

/// Deux consoles identiques À L'OREILLE : un vecteur de départs court vaut « pas d'envoi » sur les
/// bus suivants (`Track::sendLevel`), il n'est donc pas « différent » d'un vecteur prolongé de zéros.
bool memeConsole(const TrackMixState& a, const TrackMixState& b) {
    const std::equal_to<float> egal;
    if (!egal(a.volume, b.volume) || !egal(a.pan, b.pan) || a.muted != b.muted || a.solo != b.solo
        || !egal(a.stereoWidth, b.stereoWidth)   // D573.1
        || !egal(a.inputTrimDb, b.inputTrimDb) || a.invertPhase != b.invertPhase || !(a.inserts == b.inserts)
        || a.sendsBypassed != b.sendsBypassed     // D567.3
        || a.sendsFollowPan != b.sendsFollowPan)  // D572.2
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
    etat.stereoWidth = garde.stereoWidth;   // D573.1
    etat.muted = garde.muted;
    etat.solo = garde.solo;
    etat.inputTrimDb = garde.inputTrimDb;
    etat.invertPhase = garde.invertPhase;
    etat.sendLevels = garde.sendLevels;
    etat.sendsBypassed = garde.sendsBypassed;   // D567.3
    etat.sendsFollowPan = garde.sendsFollowPan;   // D572.2
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
        ecrite.stereoWidth = apres.stereoWidth;   // D573.1
        ecrite.muted = apres.muted;
        ecrite.solo = apres.solo;
        ecrite.inputTrimDb = apres.inputTrimDb;
        ecrite.invertPhase = apres.invertPhase;
        ecrite.sendLevels = apres.sendLevels;
        ecrite.sendsBypassed = apres.sendsBypassed;   // D567.3
        ecrite.sendsFollowPan = apres.sendsFollowPan;   // D572.2
        for (size_t i = 0; i < ecrite.effects.size(); ++i) ecrite.effects[i].enabled = apres.inserts[i].enabled;
    }
    return bilan;
}

} // namespace

TrackMixState captureTrackMix(const Track& track) {
    TrackMixState etat;
    etat.volume = track.volume;
    etat.pan = track.pan;
    etat.stereoWidth = track.stereoWidth;   // D573.1
    etat.muted = track.muted;
    etat.solo = track.solo;
    etat.inputTrimDb = track.inputTrimDb;
    etat.invertPhase = track.invertPhase;
    etat.sendLevels = track.sendLevels;
    etat.sendsBypassed = track.sendsBypassed;   // D567.3
    etat.sendsFollowPan = track.sendsFollowPan;   // D572.2
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
           && t.stereoWidth == neuve.stereoWidth   // D573.1 : l'image entière
           && t.inputTrimDb == neuve.inputTrimDb && t.invertPhase == neuve.invertPhase
           && t.sendsBypassed == neuve.sendsBypassed   // D567.3 : une piste neuve verse à ses bus
           && t.sendsFollowPan == neuve.sendsFollowPan   // D572.2 : départs centrés
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
        t.stereoWidth = neuve.stereoWidth;   // D573.1
        t.muted = neuve.muted;
        t.solo = neuve.solo;
        t.inputTrimDb = neuve.inputTrimDb;
        t.invertPhase = neuve.invertPhase;
        std::fill(t.sendLevels.begin(), t.sendLevels.end(), 0.0f);
        t.sendsBypassed = neuve.sendsBypassed;   // D567.3
        t.sendsFollowPan = neuve.sendsFollowPan;   // D572.2
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
    return {track.inputTrimDb, track.invertPhase, track.sendLevels, track.effects, track.sendsBypassed,
            track.sendsFollowPan};
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
        || !memesDeparts(track.sendLevels, s.sendLevels) || !memesInserts(track.effects, s.effects)
        || track.sendsBypassed != s.sendsBypassed     // D567.3
        || track.sendsFollowPan != s.sendsFollowPan;  // D572.2
}

bool applyChannelSettings(Track& track, const ChannelSettings& s) {
    if (!channelSettingsWouldChange(track, s)) return false;
    track.inputTrimDb = s.inputTrimDb;
    track.invertPhase = s.invertPhase;
    track.sendLevels = s.sendLevels;
    track.effects = s.effects;
    track.sendsBypassed = s.sendsBypassed;   // D567.3
    track.sendsFollowPan = s.sendsFollowPan;   // D572.2
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

namespace {
/// D567.3 : une tranche qui a des départs à contourner — ni dossier ni VCA (ils n'en ont pas), un niveau non nul.
bool aDesDeparts(const Track& t) {
    return !t.isFolder() && !t.isVca()
           && std::any_of(t.sendLevels.begin(), t.sendLevels.end(), [](float d) { return d > 0.0f; });
}
} // namespace

SendsBypassPlan sendsBypassPlan(const Project& project, const std::set<size_t>& chosen) {
    SendsBypassPlan plan;
    size_t aContourner = 0, aRetablir = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        const Track& t = project.tracks[i];
        if (t.isFolder() || t.isVca()) continue;
        if (t.sendsBypassed) ++aRetablir;
        else if (aDesDeparts(t)) ++aContourner;
    }
    plan.bypass = aContourner > 0 || aRetablir == 0;
    plan.count = plan.bypass ? aContourner : aRetablir;
    return plan;
}

size_t applySendsBypass(Project& project, const std::set<size_t>& chosen, bool bypass) {
    size_t n = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        Track& t = project.tracks[i];
        if (t.isFolder() || t.isVca() || t.sendsBypassed == bypass) continue;
        if (bypass && !aDesDeparts(t)) continue;   // rien à contourner : la tranche reste telle quelle
        t.sendsBypassed = bypass;
        ++n;
    }
    return n;
}

SendsPanPlan sendsPanPlan(const Project& project, const std::set<size_t>& chosen) {
    SendsPanPlan plan;
    size_t aLier = 0, aDelier = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        const Track& t = project.tracks[i];
        if (t.isFolder() || t.isVca()) continue;
        if (t.sendsFollowPan) ++aDelier;
        else ++aLier;
    }
    plan.follow = aLier > 0 || aDelier == 0;
    plan.count = plan.follow ? aLier : aDelier;
    return plan;
}

size_t applySendsFollowPan(Project& project, const std::set<size_t>& chosen, bool follow) {
    size_t n = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        Track& t = project.tracks[i];
        if (t.isFolder() || t.isVca() || t.sendsFollowPan == follow) continue;
        t.sendsFollowPan = follow;
        ++n;
    }
    return n;
}

namespace {
/// D573.1 : une tranche qui porte une largeur — ni dossier, ni VCA (pas de signal), ni groupe (une balance).
bool porteUneLargeur(const Track& t) { return !t.isFolder() && !t.isVca() && t.kind != Track::Kind::Group; }
} // namespace

float commonStereoWidth(const Project& project, const std::set<size_t>& chosen) {
    float commune = -1.0f;
    for (size_t i : chosen) {
        if (i >= project.tracks.size() || !porteUneLargeur(project.tracks[i])) continue;
        const float w = project.tracks[i].stereoWidth;
        if (commune < 0.0f) commune = w;
        else if (commune != w) return -1.0f;
    }
    return commune;
}

size_t countStereoWidthChanges(const Project& project, const std::set<size_t>& chosen, float width) {
    const float w = std::clamp(width, 0.0f, 1.0f);
    size_t n = 0;
    for (size_t i : chosen)
        if (i < project.tracks.size() && porteUneLargeur(project.tracks[i]) && project.tracks[i].stereoWidth != w) ++n;
    return n;
}

size_t applyStereoWidth(Project& project, const std::set<size_t>& chosen, float width) {
    const float w = std::clamp(width, 0.0f, 1.0f);
    size_t n = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        Track& t = project.tracks[i];
        if (!porteUneLargeur(t) || t.stereoWidth == w) continue;
        t.stereoWidth = w;
        ++n;
    }
    return n;
}

InsertsBypassPlan insertsBypassPlan(const Project& project, const std::set<size_t>& chosen) {
    size_t actives = 0, contournees = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        const auto& fx = project.tracks[i].effects;
        if (std::any_of(fx.begin(), fx.end(), [](const TrackEffect& e) { return e.enabled; })) ++actives;
        if (std::any_of(fx.begin(), fx.end(), [](const TrackEffect& e) { return !e.enabled; })) ++contournees;
    }
    InsertsBypassPlan plan;
    plan.bypass = actives > 0 || contournees == 0;
    plan.count = plan.bypass ? actives : contournees;
    return plan;
}

size_t applyInsertsBypass(Project& project, const std::set<size_t>& chosen, bool bypass) {
    size_t n = 0;
    for (size_t i : chosen) {
        if (i >= project.tracks.size()) continue;
        bool change = false;
        for (auto& e : project.tracks[i].effects)
            if (e.enabled == bypass) { e.enabled = !bypass; change = true; }
        if (change) ++n;
    }
    return n;
}

namespace {
bool aLaCourbe(const Track& t, const std::string& nom) {
    return std::any_of(t.automation.begin(), t.automation.end(),
                       [&](const AutomationCurve& c) { return c.parameter == nom; });
}
/// Les CC 7 et 10 que la piste fait entendre : (tick, contrôleur, valeur), dans l'ordre du planning.
std::vector<std::tuple<Tick, uint8_t, uint8_t>> ccEntendus(const Project& project, size_t piste) {
    Project copie = project;
    for (auto& t : copie.tracks) { t.muted = false; t.solo = false; }
    std::vector<std::tuple<Tick, uint8_t, uint8_t>> lus;
    const Tick fin = std::max<Tick>(copie.lastUsedTick(), copie.lastSoundingTick()) + 1;
    for (const auto& e : PlaybackScheduler::build(copie, 0, fin)) {
        if (e.trackIndex != piste) continue;
        if (const auto* cc = std::get_if<midi::ControlChangeEvent>(&e.data); cc && (cc->controller == 7 || cc->controller == 10))
            lus.emplace_back(copie.secondsToTicks(e.timeSeconds), cc->controller, cc->value);
    }
    return lus;
}
MidiAutomationExtraction extraire(Project& project, size_t piste, bool ecrire) {
    MidiAutomationExtraction r;
    if (piste >= project.tracks.size() || project.tracks[piste].kind != Track::Kind::Midi) return r;
    const auto lus = ccEntendus(project, piste);
    Track& t = project.tracks[piste];
    r.volumeRefused = aLaCourbe(t, "mix.volume");
    r.panRefused = aLaCourbe(t, "mix.pan");
    AutomationCurve volume, pan;
    volume.parameter = "mix.volume";
    pan.parameter = "mix.pan";
    for (const auto& [tick, n, v] : lus) {
        const float x = static_cast<float>(v) / 127.0f;
        if (n == 7) {
            ++r.volumeCc;
            if (volume.points.empty() && tick > 0) setAutomationPoint(volume, 0, t.volume, true);
            setAutomationPoint(volume, tick, t.volume * x, true);
        } else {
            ++r.panCc;
            if (pan.points.empty() && tick > 0) setAutomationPoint(pan, 0, t.pan, true);
            setAutomationPoint(pan, tick, std::clamp(t.pan + x * 2.0f - 1.0f, -1.0f, 1.0f), true);
        }
    }
    const auto retirer = [&](uint8_t numero) {
        const auto avant = t.controlChanges.size();
        if (ecrire)
            t.controlChanges.erase(std::remove_if(t.controlChanges.begin(), t.controlChanges.end(),
                                                  [numero](const CcPoint& c) { return c.controller == numero; }),
                                   t.controlChanges.end());
        else
            return static_cast<size_t>(std::count_if(t.controlChanges.begin(), t.controlChanges.end(),
                                                     [numero](const CcPoint& c) { return c.controller == numero; }));
        return avant - t.controlChanges.size();
    };
    if (r.volumeCc > 0 && !r.volumeRefused) {
        r.ccRemoved += retirer(7);
        if (ecrire) t.automation.push_back(std::move(volume));
    }
    if (r.panCc > 0 && !r.panRefused) {
        r.ccRemoved += retirer(10);
        if (ecrire) t.automation.push_back(std::move(pan));
    }
    return r;
}
} // namespace

MidiAutomationExtraction previewMidiAutomationExtraction(const Project& project, size_t track) {
    Project copie = project;
    return extraire(copie, track, false);
}

MidiAutomationExtraction extractMidiAutomation(Project& project, size_t track) {
    return extraire(project, track, true);
}

} // namespace vsm::sequencer
