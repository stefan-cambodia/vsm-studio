#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/Track.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace vsm::interchange;
using namespace vsm::sequencer;
namespace fs = std::filesystem;

// D55.1 de docs/ROADMAP-daw.md — CE QUE « ENREGISTRER PUIS ROUVRIR » PERD.
//
// LE MODÈLE EST LA LISTE DE CONTRÔLE. Chercher le champ oublié en relisant le
// sérialiseur ne prouve rien : le champ EXISTE, le geste EXISTE, la sauvegarde
// RÉUSSIT, et le réglage n'est plus là à la réouverture. Ce banc prend donc TOUS
// les champs du modèle, leur donne une valeur DISTINCTIVE (jamais leur défaut,
// sans quoi « conservé » ne prouverait rien), écrit le projet par le chemin réel
// de l'application (`saveProjectBundle`) et le relit par le chemin réel
// (`loadProjectBundle`).
//
// SA VALEUR N'EST PAS DANS CE QU'IL A TROUVÉ LE JOUR OÙ IL A ÉTÉ ÉCRIT — rien :
// 120 champs sur 122 conservés, et les deux autres volontairement absents. Elle
// est dans le PROCHAIN champ ajouté au modèle et oublié dans le sérialiseur.
// C'était le défaut de D51, puis celui de D53 : deux fois le même, et personne
// ne l'a vu avant qu'un utilisateur ne le cherche.
//
// DEUX CHAMPS NE SURVIVENT PAS, ET LES DEUX SONT DES DÉCISIONS ÉCRITES :
//
//   `Track::armed` — D22.5 l'a tranché : l'armement est un ÉTAT DE SÉANCE, comme
//                    le varispeed de D18.5, « écrit NULLE PART dans le projet ».
//                    Rouvrir un projet dont une piste s'arme toute seule est un
//                    piège, pas un service. Le test l'AFFIRME au lieu de le
//                    taire : si l'armement se mettait à survivre, il faudrait
//                    que ce soit une décision et non un effet de bord.
//   `Clip::id`     — une poignée de séance (sélection, `clipById`) que rien dans
//                    le fichier ne référence et que `assignClipIds()` repose à
//                    l'ouverture. Le LIEN entre deux clips, lui, ne passe pas
//                    par l'identifiant mais par la FENÊTRE (`clipIsShared`
//                    compare `sourceStart`/`sourceLength`) : il survit, et le
//                    test le vérifie plutôt que de le supposer.

namespace {

fs::path dossierNeuf(const std::string& nom) {
    const fs::path dossier = fs::temp_directory_path() / ("vsm-aller-retour-" + nom);
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    return dossier;
}

/// UN PROJET DONT CHAQUE CHAMP PORTE UNE VALEUR DISTINCTIVE. Six pistes, une
/// par forme que le modèle sait prendre : MIDI chargée, audio avec son clip,
/// publiée, dossier, membre de dossier, groupe.
Project projetDistinctif() {
    Project p;
    p.title = "Titre d'audit";
    p.exportFormat = vsm::midi::SmfFormat::Type0;
    p.ticksPerQuarterNote = 960;
    p.tempoMap.addTempoChange(0, 436363u);
    p.timeSignatureMap.addChange(0, 7, 3);
    p.notes = "Notes de projet";
    p.loopEnabled = true;  p.loopStartTick = 960;  p.loopEndTick = 3840;
    p.punchEnabled = true; p.punchStartTick = 480; p.punchEndTick = 1920;
    p.masterParameters["gain"] = 0.75f;
    p.crossfadeShape = FadeShape::Fast;
    SendBusDescription bus;
    bus.name = "Reverb A"; bus.effectType = "reverb";
    bus.parameters["mix"] = 0.42f; bus.returnGain = 0.8f; bus.preFader = true;
    p.sends.push_back(bus);
    p.markers.push_back(Marker{1920, "Refrain"});

    Track t;
    t.kind = Track::Kind::Midi;
    t.name = "Piste d'audit";
    t.colorRgba = 0xFF123456u;
    t.channel = 3;
    uint64_t idc = 1;
    t.addNote(0, 480, 64, 99, 3, idc);
    t.notes[0].releaseVelocity = 77;
    t.notes[0].muted = true;
    t.notes[0].confidence = 0.5f;
    t.controlChanges.push_back(CcPoint{240, 3, 74, 88});
    t.pitchBends.push_back(PitchBendPoint{240, 3, 3000});
    t.polyAftertouch.push_back(PolyAftertouchPoint{240, 3, 64, 55});
    t.channelPressure.push_back(ChannelPressurePoint{240, 3, 44});
    t.programChanges.push_back(ProgramChangePoint{0, 3, 42});
    t.muted = true; t.solo = true; t.armed = true;
    t.volume = 0.6f; t.pan = -0.4f;
    t.invertPhase = true; t.soloSafe = true; t.disabled = true;
    t.inputTrimDb = -3.5f;
    t.setSendLevel(0, 0.33f); t.setSendLevel(1, 0.66f);
    // « vsm.minimoog » et non un identifiant inventé : une machine que cette
    // installation n'a pas est DÉLIBÉRÉMENT écartée à la lecture (et signalée),
    // et le banc mesurerait alors cette décision-là au lieu du champ.
    t.instrumentId = "vsm.minimoog";
    t.midiOutputDevice = "Port virtuel";
    t.midiProgram = 12; t.midiBank = 5; t.midiInputChannel = 7;
    MidiEffect me;
    me.type = "arpeggiator"; me.parameters["rate"] = 0.25f; me.enabled = false;
    t.midiEffects.push_back(me);
    t.frozen = true;
    t.frozenAudio = AudioSource{"audio/gel_00.wav", 48000.0, 96000, 2};
    t.locked = true; t.hidden = true;
    t.transposeSemitones = -5;
    t.editGroup = 2;
    t.delayMs = -12.5;
    TrackEffect fx;
    fx.type = "delay"; fx.parameters["time"] = 0.375f;
    fx.nativeState = "AAAA"; fx.enabled = false;
    t.effects.push_back(fx);
    AutomationCurve ac;
    ac.parameter = "volume";
    ac.points.push_back(AutomationPoint{0, 0.2f, true, 0.5f});
    ac.points.push_back(AutomationPoint{960, 0.9f, false, -0.5f});
    t.automation.push_back(ac);
    t.automationMode = AutomationMode::Latch;
    Take tk;
    tk.name = "Prise 2"; tk.startTick = 0; tk.endTick = 960;
    tk.notes.push_back(Note{0, 240, 3, 62, 90, 64, 900});
    tk.audio = AudioSource{"audio/prise_02.wav", 48000.0, 48000, 2};
    Clip tkc;
    tkc.startTick = 0; tkc.length = 960; tkc.sourceLength = 960;
    tkc.name = "Clip de prise"; tkc.gain = 0.25f; tkc.reversed = true; tkc.id = 21;
    tk.clips.push_back(tkc);
    t.takes.push_back(tk);
    t.arrangementHeight = 91;
    t.folded = true;
    t.activeTake = 0;
    // D55.2 : la recette de l'assemblage, sur la seule piste qui a des prises.
    t.compSegments.push_back(CompSegment{0, 0, 960});

    Track a;
    a.kind = Track::Kind::Audio;
    a.name = "Audio d'audit";
    a.audio = AudioSource{"audio/prise.wav", 44100.0, 88200, 1};
    Clip c;
    c.sourceStart = 100; c.sourceLength = 4800; c.startTick = 480; c.length = 1920;
    c.muted = true; c.name = "Clip d'audit"; c.colorRgba = 0xFF654321u;
    c.sourceStartSeconds = 1.25; c.fadeInSeconds = 0.3; c.fadeOutSeconds = 0.7;
    c.gain = 0.5f; c.invertPhase = true; c.warpMode = WarpMode::KeepPitchWsola;
    c.warpMarkers.push_back(WarpMarker{0.0, 0});
    c.warpMarkers.push_back(WarpMarker{2.0, 1920});
    c.reversed = true; c.pitchSemitones = 7.0; c.id = 11;
    c.fadeShape = FadeShape::Slow;
    a.clips.push_back(c);
    // LE SECOND CLIP LIT LA MÊME FENÊTRE : c'est cela, et non l'identifiant,
    // qui fait deux copies LIÉES. Il sert à vérifier que le lien survit.
    Clip lie = c;
    lie.startTick = 3840; lie.id = 12; lie.name = "Copie liée";
    a.clips.push_back(lie);

    Track pub;
    pub.kind = Track::Kind::Midi;
    pub.name = "Sortie publiée";
    pub.outputSourceTrack = 0;
    pub.outputIndex = 3;

    Track dossier;
    dossier.kind = Track::Kind::Folder;
    dossier.name = "Dossier";

    Track dedans;
    dedans.kind = Track::Kind::Midi;
    dedans.name = "Dans le dossier";
    dedans.folderDepth = 1;
    dedans.outputGroup = 5;

    Track groupe;
    groupe.kind = Track::Kind::Group;
    groupe.name = "Bus batterie";
    groupe.volume = 0.7f;

    p.tracks = {t, a, pub, dossier, dedans, groupe};
    p.ensureClipIdAbove(12);
    return p;
}

} // namespace

VSM_TEST(aller_retour_disque_champ_par_champ) {
    const Project p = projetDistinctif();
    const fs::path dossier = dossierNeuf("champs");
    const auto ecrit = saveProjectBundle(p, dossier.string());
    VSM_ASSERT(ecrit.success);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    const Project& q = relu.bundle.project;

    // --- le projet
    VSM_ASSERT_EQ(q.title, p.title);
    VSM_ASSERT_EQ(static_cast<int>(q.exportFormat), static_cast<int>(p.exportFormat));
    VSM_ASSERT_EQ(static_cast<int>(q.ticksPerQuarterNote), static_cast<int>(p.ticksPerQuarterNote));
    VSM_ASSERT_NEAR(q.tempoMap.bpmAt(0), p.tempoMap.bpmAt(0), 1e-6);
    VSM_ASSERT_EQ(static_cast<int>(q.timeSignatureMap.changes().front().numerator), 7);
    VSM_ASSERT_EQ(q.notes, p.notes);
    VSM_ASSERT(q.loopEnabled);
    VSM_ASSERT_EQ(q.loopStartTick, p.loopStartTick);
    VSM_ASSERT_EQ(q.loopEndTick, p.loopEndTick);
    VSM_ASSERT(q.punchEnabled);
    VSM_ASSERT_EQ(q.punchStartTick, p.punchStartTick);
    VSM_ASSERT_EQ(q.punchEndTick, p.punchEndTick);
    VSM_ASSERT_NEAR(q.masterParameters.at("gain"), 0.75f, 1e-6);
    VSM_ASSERT_EQ(static_cast<int>(q.crossfadeShape), static_cast<int>(p.crossfadeShape));
    VSM_ASSERT_EQ(q.markers.size(), p.markers.size());
    VSM_ASSERT_EQ(q.markers[0].tick, p.markers[0].tick);
    VSM_ASSERT_EQ(q.markers[0].name, p.markers[0].name);
    VSM_ASSERT_EQ(q.sends.size(), p.sends.size());
    VSM_ASSERT_EQ(q.sends[0].name, p.sends[0].name);
    VSM_ASSERT_EQ(q.sends[0].effectType, p.sends[0].effectType);
    VSM_ASSERT_NEAR(q.sends[0].parameters.at("mix"), 0.42f, 1e-6);
    VSM_ASSERT_NEAR(q.sends[0].returnGain, p.sends[0].returnGain, 1e-6);
    VSM_ASSERT(q.sends[0].preFader);
    VSM_ASSERT_EQ(q.tracks.size(), p.tracks.size());

    // --- la piste MIDI chargée
    const Track& T = q.tracks[0];
    const Track& R = p.tracks[0];
    VSM_ASSERT_EQ(static_cast<int>(T.kind), static_cast<int>(R.kind));
    VSM_ASSERT_EQ(T.name, R.name);
    VSM_ASSERT_EQ(T.colorRgba, R.colorRgba);
    VSM_ASSERT_EQ(static_cast<int>(T.channel), static_cast<int>(R.channel));
    VSM_ASSERT_EQ(T.notes.size(), R.notes.size());
    VSM_ASSERT_EQ(T.notes[0].startTick, R.notes[0].startTick);
    VSM_ASSERT_EQ(T.notes[0].endTick, R.notes[0].endTick);
    VSM_ASSERT_EQ(static_cast<int>(T.notes[0].number), static_cast<int>(R.notes[0].number));
    VSM_ASSERT_EQ(static_cast<int>(T.notes[0].velocity), static_cast<int>(R.notes[0].velocity));
    VSM_ASSERT_EQ(static_cast<int>(T.notes[0].releaseVelocity),
                  static_cast<int>(R.notes[0].releaseVelocity));
    VSM_ASSERT(T.notes[0].muted);
    // LA CONFIANCE PASSE PAR UN ENTIER 16 BITS dans le bloc privé du SMF : elle
    // revient à 1/65535 près, et l'écrire ainsi vaut mieux que de prétendre à
    // l'exactitude d'un flottant qui ne traverse pas le fichier.
    VSM_ASSERT_NEAR(T.notes[0].confidence, R.notes[0].confidence, 2e-5);
    VSM_ASSERT_EQ(T.controlChanges.size(), R.controlChanges.size());
    VSM_ASSERT_EQ(T.pitchBends.size(), R.pitchBends.size());
    VSM_ASSERT_EQ(T.polyAftertouch.size(), R.polyAftertouch.size());
    VSM_ASSERT_EQ(T.channelPressure.size(), R.channelPressure.size());
    VSM_ASSERT_EQ(T.programChanges.size(), R.programChanges.size());
    VSM_ASSERT(T.muted);
    VSM_ASSERT(T.solo);
    VSM_ASSERT_NEAR(T.volume, R.volume, 1e-6);
    VSM_ASSERT_NEAR(T.pan, R.pan, 1e-6);
    VSM_ASSERT(T.invertPhase);
    VSM_ASSERT(T.soloSafe);
    VSM_ASSERT(T.disabled);
    VSM_ASSERT_NEAR(T.inputTrimDb, R.inputTrimDb, 1e-6);
    VSM_ASSERT_NEAR(T.sendLevel(0), R.sendLevel(0), 1e-6);
    VSM_ASSERT_NEAR(T.sendLevel(1), R.sendLevel(1), 1e-6);
    VSM_ASSERT_EQ(T.instrumentId, R.instrumentId);
    VSM_ASSERT_EQ(T.midiOutputDevice, R.midiOutputDevice);
    VSM_ASSERT_EQ(T.midiProgram, R.midiProgram);
    VSM_ASSERT_EQ(T.midiBank, R.midiBank);
    VSM_ASSERT_EQ(T.midiInputChannel, R.midiInputChannel);
    VSM_ASSERT_EQ(T.midiEffects.size(), R.midiEffects.size());
    VSM_ASSERT_EQ(T.midiEffects[0].type, R.midiEffects[0].type);
    VSM_ASSERT(!T.midiEffects[0].enabled);
    VSM_ASSERT_NEAR(T.midiEffects[0].parameters.at("rate"), 0.25f, 1e-6);
    VSM_ASSERT(T.frozen);
    VSM_ASSERT_EQ(T.frozenAudio.path, R.frozenAudio.path);
    VSM_ASSERT_NEAR(T.frozenAudio.sampleRate, R.frozenAudio.sampleRate, 1e-6);
    VSM_ASSERT_EQ(T.frozenAudio.frames, R.frozenAudio.frames);
    VSM_ASSERT_EQ(T.frozenAudio.channels, R.frozenAudio.channels);
    VSM_ASSERT(T.locked);
    VSM_ASSERT(T.hidden);
    VSM_ASSERT_EQ(T.transposeSemitones, R.transposeSemitones);
    VSM_ASSERT_EQ(T.editGroup, R.editGroup);
    VSM_ASSERT_NEAR(T.delayMs, R.delayMs, 1e-6);
    VSM_ASSERT_EQ(T.effects.size(), R.effects.size());
    VSM_ASSERT_EQ(T.effects[0].type, R.effects[0].type);
    VSM_ASSERT_NEAR(T.effects[0].parameters.at("time"), 0.375f, 1e-6);
    VSM_ASSERT_EQ(T.effects[0].nativeState, R.effects[0].nativeState);
    VSM_ASSERT(!T.effects[0].enabled);
    VSM_ASSERT_EQ(T.automation.size(), R.automation.size());
    VSM_ASSERT_EQ(T.automation[0].parameter, R.automation[0].parameter);
    VSM_ASSERT_EQ(T.automation[0].points.size(), R.automation[0].points.size());
    VSM_ASSERT(T.automation[0].points[0].step);
    VSM_ASSERT_NEAR(T.automation[0].points[0].curve, 0.5f, 1e-6);
    VSM_ASSERT_NEAR(T.automation[0].points[1].value, 0.9f, 1e-6);
    VSM_ASSERT_EQ(static_cast<int>(T.automationMode), static_cast<int>(R.automationMode));
    VSM_ASSERT_EQ(T.takes.size(), R.takes.size());
    VSM_ASSERT_EQ(T.takes[0].name, R.takes[0].name);
    VSM_ASSERT_EQ(T.takes[0].notes.size(), R.takes[0].notes.size());
    VSM_ASSERT_EQ(T.takes[0].startTick, R.takes[0].startTick);
    VSM_ASSERT_EQ(T.takes[0].endTick, R.takes[0].endTick);
    VSM_ASSERT_EQ(T.takes[0].audio.path, R.takes[0].audio.path);
    VSM_ASSERT_EQ(T.takes[0].clips.size(), R.takes[0].clips.size());
    VSM_ASSERT_EQ(T.takes[0].clips[0].name, R.takes[0].clips[0].name);
    VSM_ASSERT_NEAR(T.takes[0].clips[0].gain, R.takes[0].clips[0].gain, 1e-6);
    VSM_ASSERT(T.takes[0].clips[0].reversed);
    VSM_ASSERT_EQ(T.arrangementHeight, R.arrangementHeight);
    VSM_ASSERT(T.folded);
    VSM_ASSERT_EQ(T.activeTake, R.activeTake);
    VSM_ASSERT_EQ(T.compSegments.size(), R.compSegments.size());

    // --- la piste audio et ses clips
    const Track& A = q.tracks[1];
    const Track& B = p.tracks[1];
    VSM_ASSERT_EQ(static_cast<int>(A.kind), static_cast<int>(B.kind));
    VSM_ASSERT_EQ(A.audio.path, B.audio.path);
    VSM_ASSERT_NEAR(A.audio.sampleRate, B.audio.sampleRate, 1e-6);
    VSM_ASSERT_EQ(A.audio.frames, B.audio.frames);
    VSM_ASSERT_EQ(A.audio.channels, B.audio.channels);
    VSM_ASSERT_EQ(A.clips.size(), B.clips.size());
    const Clip& C = A.clips[0];
    const Clip& D = B.clips[0];
    VSM_ASSERT_EQ(C.sourceStart, D.sourceStart);
    VSM_ASSERT_EQ(C.sourceLength, D.sourceLength);
    VSM_ASSERT_EQ(C.startTick, D.startTick);
    VSM_ASSERT_EQ(C.length, D.length);
    VSM_ASSERT(C.muted);
    VSM_ASSERT_EQ(C.name, D.name);
    VSM_ASSERT_EQ(C.colorRgba, D.colorRgba);
    VSM_ASSERT_NEAR(C.sourceStartSeconds, D.sourceStartSeconds, 1e-6);
    VSM_ASSERT_NEAR(C.fadeInSeconds, D.fadeInSeconds, 1e-6);
    VSM_ASSERT_NEAR(C.fadeOutSeconds, D.fadeOutSeconds, 1e-6);
    VSM_ASSERT_NEAR(C.gain, D.gain, 1e-6);
    VSM_ASSERT(C.invertPhase);
    VSM_ASSERT_EQ(static_cast<int>(C.warpMode), static_cast<int>(D.warpMode));
    VSM_ASSERT_EQ(C.warpMarkers.size(), D.warpMarkers.size());
    VSM_ASSERT_NEAR(C.warpMarkers[1].sourceSeconds, D.warpMarkers[1].sourceSeconds, 1e-6);
    VSM_ASSERT_EQ(C.warpMarkers[1].tick, D.warpMarkers[1].tick);
    VSM_ASSERT(C.reversed);
    VSM_ASSERT_NEAR(C.pitchSemitones, D.pitchSemitones, 1e-6);
    VSM_ASSERT_EQ(static_cast<int>(C.fadeShape), static_cast<int>(D.fadeShape));

    // --- la piste publiée, le dossier, son membre, le groupe
    VSM_ASSERT_EQ(q.tracks[2].outputSourceTrack, p.tracks[2].outputSourceTrack);
    VSM_ASSERT_EQ(q.tracks[2].outputIndex, p.tracks[2].outputIndex);
    VSM_ASSERT_EQ(static_cast<int>(q.tracks[3].kind), static_cast<int>(Track::Kind::Folder));
    VSM_ASSERT_EQ(q.tracks[4].folderDepth, p.tracks[4].folderDepth);
    VSM_ASSERT_EQ(q.tracks[4].outputGroup, p.tracks[4].outputGroup);
    VSM_ASSERT_EQ(static_cast<int>(q.tracks[5].kind), static_cast<int>(Track::Kind::Group));
    VSM_ASSERT_NEAR(q.tracks[5].volume, p.tracks[5].volume, 1e-6);
}

// LES DEUX CHAMPS QUI NE SURVIVENT PAS, AFFIRMÉS PLUTÔT QUE TUS. Un jour où
// l'un des deux se mettrait à survivre, ce test tombe et l'on décide — au lieu
// de le découvrir en s'étonnant qu'une piste s'arme toute seule.
VSM_TEST(deux_champs_de_seance_ne_survivent_pas_et_c_est_voulu) {
    const Project p = projetDistinctif();
    VSM_ASSERT(p.tracks[0].armed);
    const fs::path dossier = dossierNeuf("seance");
    VSM_ASSERT(saveProjectBundle(p, dossier.string()).success);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    // L'ARMEMENT est un état de séance (D22.5) : il ne revient pas.
    VSM_ASSERT(!relu.bundle.project.tracks[0].armed);
    // L'IDENTIFIANT d'un clip est une poignée de séance : rien dans le fichier
    // ne le référence, et il est reposé à l'ouverture.
    Project rouvert = relu.bundle.project;
    rouvert.assignClipIds();
    VSM_ASSERT(rouvert.tracks[1].clips[0].id != 0);

    // ET CE QUE CELA NE COÛTE PAS : le LIEN entre deux clips ne passe pas par
    // l'identifiant mais par la FENÊTRE, et il survit donc à l'aller-retour.
    VSM_ASSERT_EQ(rouvert.tracks[1].clips.size(), size_t{2});
    VSM_ASSERT(clipIsShared(rouvert.tracks[1].clips, rouvert.tracks[1].clips[0].id));
    VSM_ASSERT(clipIsShared(rouvert.tracks[1].clips, rouvert.tracks[1].clips[1].id));
    VSM_ASSERT(rouvert.tracks[1].clips[0].id != rouvert.tracks[1].clips[1].id);
}

// --- D55.2 : la recette de l'assemblage des prises -------------------------
//
// Elle était détenue par le seul panneau, qui la vidait à chaque ouverture, et
// n'allait nulle part. « Corriger une frontière sans avoir à tout refaire »,
// que l'en-tête du panneau promet depuis D18.2, était donc faux.

VSM_TEST(la_recette_d_assemblage_survit_au_disque) {
    Project p;
    Track t;
    t.kind = Track::Kind::Midi;
    t.name = "Voix";
    uint64_t id = 1;
    t.addNote(0, 480, 60, 100, 0, id);
    for (int n = 0; n < 3; ++n) {
        Take prise;
        prise.name = "Passe " + std::to_string(n + 1);
        prise.startTick = 0;
        prise.endTick = 7680;
        prise.notes.push_back(Note{0, 480, 0, static_cast<uint8_t>(48 + n), 100, 64, id++});
        t.takes.push_back(std::move(prise));
    }
    t.compSegments.push_back(CompSegment{1, 0, 3840});
    t.compSegments.push_back(CompSegment{2, 3840, 7680});
    p.tracks.push_back(std::move(t));

    const fs::path dossier = dossierNeuf("assemblage");
    VSM_ASSERT(saveProjectBundle(p, dossier.string()).success);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    const auto& troncons = relu.bundle.project.tracks[0].compSegments;
    VSM_ASSERT_EQ(troncons.size(), size_t{2});
    VSM_ASSERT_EQ(troncons[0].takeIndex, 1);
    VSM_ASSERT_EQ(troncons[0].fromTick, vsm::midi::Tick{0});
    VSM_ASSERT_EQ(troncons[0].toTick, vsm::midi::Tick{3840});
    VSM_ASSERT_EQ(troncons[1].takeIndex, 2);
    VSM_ASSERT_EQ(troncons[1].fromTick, vsm::midi::Tick{3840});
    VSM_ASSERT_EQ(troncons[1].toTick, vsm::midi::Tick{7680});
}

// UN PROJET SANS ASSEMBLAGE GARDE SON FICHIER OCTET POUR OCTET. Le critère est
// vérifié sur le TEXTE et non sur l'absence d'erreur : un champ écrit vide
// (« "comp": [] ») allongerait tous les fichiers déjà sur le disque, et c'est
// exactement ce que ce projet refuse depuis D17.1.
VSM_TEST(un_projet_sans_assemblage_ecrit_le_meme_fichier) {
    Project p;
    Track t;
    t.kind = Track::Kind::Midi;
    t.name = "Voix";
    uint64_t id = 1;
    t.addNote(0, 480, 60, 100, 0, id);
    Take prise;
    prise.name = "Passe 1";
    prise.endTick = 1920;
    prise.notes.push_back(Note{0, 480, 0, 48, 100, 64, id++});
    t.takes.push_back(std::move(prise));
    p.tracks.push_back(std::move(t));

    const auto document = documentFromProject(p);
    const std::string texte = projectDocumentToJson(document).toString();
    VSM_ASSERT(texte.find("\"comp\"") == std::string::npos);
    // ET LA VERSION NE MONTE PAS : la recette ne change pas ce qu'on entend --
    // le matériau composé est déjà dans les notes. Un lecteur qui l'ignore joue
    // le même morceau ; il perd seulement le moyen de recomposer autrement.
    VSM_ASSERT(texte.find("\"version\": 2") != std::string::npos);
}

// UN TRONÇON QUI DÉSIGNE UNE PRISE ABSENTE EST ÉCARTÉ ET DIT. Le garder
// pointant à côté ferait recomposer autre chose que ce que la liste annonce ;
// le taire serait la panne muette que ce projet refuse.
VSM_TEST(un_troncon_sans_prise_est_ecarte_et_nomme) {
    ProjectDocument document;
    ProjectTrack piste;
    piste.name = "Voix";
    ProjectTake prise;
    prise.name = "Passe 1";
    prise.endTick = 1920;
    piste.takes.push_back(prise);
    piste.compSegments.push_back({0, 0, 960});    // valide : la prise 0 existe
    piste.compSegments.push_back({4, 0, 960});    // la prise 4 n'existe pas
    piste.compSegments.push_back({0, 960, 960});  // bornes vides
    document.tracks.push_back(std::move(piste));

    Project projet;
    projet.tracks.push_back(Track{});
    const auto rapport = applyDocumentToProject(document, projet);
    VSM_ASSERT_EQ(projet.tracks[0].compSegments.size(), size_t{1});
    VSM_ASSERT_EQ(projet.tracks[0].compSegments[0].takeIndex, 0);
    VSM_ASSERT_EQ(rapport.warnings.size(), size_t{1});
    VSM_ASSERT(rapport.warnings[0].find("2") != std::string::npos);
}
