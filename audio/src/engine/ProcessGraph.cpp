#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vsm::audio::engine {

/// Plage du pitch bend, en demi-tons pour l'excursion maximale. Deux demi-tons
/// est la convention par défaut depuis la General MIDI ; une machine qui
/// voudrait autre chose le fera dans son `handleControlEvent`, pas ici.
constexpr float kPitchBendRangeSemitones = 2.0f;

using namespace vsm::audio::plugin;
using namespace vsm::sequencer;
using namespace vsm::midi;

void ProcessGraph::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : 512;

    scratchMonoL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    scratchStereoL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    scratchStereoR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    for (size_t b = 0; b < kMaxSends; ++b) {
        sendL_[b].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        sendR_[b].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    }
    for (size_t g = 0; g < kMaxGroups; ++g) {
        groupL_[g].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        groupR_[g].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    }
    scratchEvents_.assign(static_cast<size_t>(kMaxEventsPerBlock), MidiNoteEvent{});
    meters_.resetAll();
    masterBus_.prepare(sampleRate_, maxBlockSize_);
    referenceTrack_.prepare(sampleRate_);
    // LE MÉTRONOME AUSSI, et il ne l'était PAS. Il gardait sa fréquence
    // d'échantillonnage par défaut de 48 kHz quoi que fasse la carte : à
    // 44,1 kHz son clic sortait un demi-ton trop bas et durait 9 % de trop, et
    // c'est justement le régime le plus courant. Personne ne s'en plaignait
    // parce qu'un clic faux ressemble à un clic.
    metronome_.prepare(sampleRate_);

    for (auto& slot : instruments_) {
        auto instrument = slot.load(std::memory_order_acquire);
        if (instrument) instrument->initialize(sampleRate_, maxBlockSize_);
    }

    // La taille de bloc vient de changer : les tampons par piste du rendu
    // multicœur doivent la suivre, sinon le chemin parallèle écrirait dans des
    // tampons trop courts. Ne fait rien tant qu'aucun thread n'existe.
    ensureParallelBuffers();
}

void ProcessGraph::setRenderThreadCount(size_t workerCount) {
    // ON FERME D'ABORD LA PORTE, ON ATTEND ENSUITE QUE LA PIÈCE SE VIDE.
    // Détruire un thread qui est en train de rendre un bloc, ou réallouer les
    // tampons qu'il écrit, produirait exactement le genre de plantage qu'on ne
    // reproduit jamais. Les deux atomiques sont en `seq_cst` : c'est ce qui
    // garantit qu'entre ce thread-ci et le thread audio, au moins l'un voit
    // l'autre (si le rendu a commencé avant qu'on ferme, on le voit occupé).
    parallelAllowed_.store(false, std::memory_order_seq_cst);
    while (renderBusy_.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();

    renderPool_.resize(workerCount);
    ensureParallelBuffers();
    parallelAllowed_.store(renderPool_.workerCount() > 0, std::memory_order_seq_cst);
}

void ProcessGraph::ensureParallelBuffers() {
    if (renderPool_.workerCount() == 0) {
        // RIEN N'EST GARDÉ : à zéro thread, le graphe doit occuper exactement la
        // mémoire qu'il occupait avant que le multicœur existe.
        parallelL_ = {};
        parallelR_ = {};
        parallelEvents_ = {};
        parallelActive_.fill(0);
        return;
    }
    const size_t taille = static_cast<size_t>(maxBlockSize_);
    parallelL_.assign(kMaxTracks, std::vector<float>(taille, 0.0f));
    parallelR_.assign(kMaxTracks, std::vector<float>(taille, 0.0f));
    // UN TABLEAU D'ÉVÉNEMENTS PAR THREAD, et non par piste : il ne sert que le
    // temps d'une piste, alors que les tampons audio, eux, doivent survivre
    // jusqu'au mixage. Cent vingt-huit tableaux au lieu de huit seraient deux
    // mégaoctets payés pour rien.
    parallelEvents_.assign(renderPool_.parallelism(),
                            std::vector<MidiNoteEvent>(static_cast<size_t>(kMaxEventsPerBlock)));
    parallelActive_.fill(0);
}

void ProcessGraph::setProject(const Project& project) {
    auto snapshot = std::make_shared<GraphSnapshot>();
    snapshot->project = project;
    Tick endTick = project.lastUsedTick() + project.ticksPerQuarterNote;
    snapshot->schedule = PlaybackScheduler::build(project, 0, endTick);

    // RANGÉ PAR PISTE, UNE FOIS ICI PLUTÔT QUE PARCOURU À CHAQUE BLOC (D8.4).
    // Le tri est STABLE : le planificateur a déjà trié par temps, et trier
    // ensuite sur la seule piste conserve cet ordre à l'intérieur de chacune.
    // C'est ce qui permet au rendu d'y entrer par dichotomie.
    std::stable_sort(snapshot->schedule.begin(), snapshot->schedule.end(),
                      [](const ScheduledEvent& a, const ScheduledEvent& b) {
                          return a.trackIndex < b.trackIndex;
                      });
    for (auto& borne : snapshot->trackRange) borne = {0u, 0u};
    for (uint32_t i = 0; i < snapshot->schedule.size(); ++i) {
        const size_t piste = snapshot->schedule[i].trackIndex;
        if (piste >= kMaxTracks) continue;
        auto& borne = snapshot->trackRange[piste];
        if (borne.second == 0 && borne.first == 0) borne.first = i;
        borne.second = i + 1;
    }
    // LE NOMBRE DE DÉPARTS VIENT DU PROJET, et il est publié avec lui : le lire
    // dans le snapshot à chaque bloc obligerait le chemin audio à déréférencer
    // le projet même quand il n'y a aucun départ, alors qu'un entier suffit.
    // Au-delà du plafond, on COMPTE plutôt que d'ignorer : un départ qu'on
    // aurait réglé et qui ne sonnerait pas est exactement le genre de silence
    // qu'on cherche des heures.
    size_t groupes = 0;
    for (const auto& t : project.tracks)
        if (t.kind == vsm::sequencer::Track::Kind::Group) ++groupes;
    if (groupes > kMaxGroups) droppedGroupBuses_.fetch_add(1, std::memory_order_relaxed);

    const size_t declares = project.sends.size();
    if (declares > kMaxSends) droppedSendBuses_.fetch_add(1, std::memory_order_relaxed);
    uint32_t masque = 0;
    for (size_t b = 0; b < declares && b < kMaxSends; ++b)
        if (project.sends[b].preFader) masque |= (1u << b);
    preFaderMask_.store(masque, std::memory_order_release);
    activeSends_.store(std::min(declares, kMaxSends), std::memory_order_release);
    snapshot_.store(snapshot, std::memory_order_release);
    refreshRenderOrder();    // les niveaux d'envoi ont pu changer
    refreshCompensation();   // le routage vers les groupes aussi
}

void ProcessGraph::setTrackInstrument(size_t trackIndex, const std::string& pluginId) {
    if (trackIndex >= kMaxTracks) return;
    instrumentIds_[trackIndex] = pluginId;

    if (pluginId.empty()) {
        instruments_[trackIndex].store(nullptr, std::memory_order_release);
        return;
    }
    auto plugin = PluginRegistry::instance().create(pluginId);
    if (plugin) plugin->initialize(sampleRate_, maxBlockSize_);
    instruments_[trackIndex].store(std::move(plugin), std::memory_order_release);
    refreshCompensation();   // une machine peut déclarer une latence
}

void ProcessGraph::setTrackInstrumentInstance(size_t trackIndex, vsm::audio::plugin::SynthPluginPtr instrument,
                                               const std::string& identifier) {
    if (trackIndex >= kMaxTracks) return;
    instrumentIds_[trackIndex] = instrument ? identifier : std::string();
    // Même contrat que le chemin par registre : le graphe prépare l'instrument
    // à SA fréquence d'échantillonnage et à SA taille de bloc. L'appelant ne
    // peut pas les deviner, et un instrument non préparé rend du silence -- une
    // panne d'autant plus déroutante qu'elle est parfaitement muette.
    if (instrument) instrument->initialize(sampleRate_, maxBlockSize_);
    instruments_[trackIndex].store(std::move(instrument), std::memory_order_release);
}

std::string ProcessGraph::trackInstrumentId(size_t trackIndex) const {
    return trackIndex < kMaxTracks ? instrumentIds_[trackIndex] : std::string{};
}

ISynthPlugin* ProcessGraph::trackInstrument(size_t trackIndex) const {
    if (trackIndex >= kMaxTracks) return nullptr;
    auto instrument = instruments_[trackIndex].load(std::memory_order_acquire);
    return instrument.get();
}

void ProcessGraph::setInstrumentParameter(size_t trackIndex, ParamId paramId, float value) {
    if (trackIndex >= kMaxTracks) return;
    // Copie locale du shared_ptr : garde l'instrument en vie le temps de
    // l'appel même si un autre thread le remplace entre-temps.
    auto instrument = instruments_[trackIndex].load(std::memory_order_acquire);
    if (instrument) instrument->setParameter(paramId, value);
}

void ProcessGraph::addAutomationLane(AutomationLane lane) {
    auto current = automationLanes_.load(std::memory_order_acquire);
    auto next = current ? std::make_shared<std::vector<AutomationLane>>(*current)
                        : std::make_shared<std::vector<AutomationLane>>();
    next->push_back(std::move(lane));
    automationLanes_.store(std::move(next), std::memory_order_release);
}

void ProcessGraph::clearAutomationLanes() {
    automationLanes_.store(std::make_shared<std::vector<AutomationLane>>(), std::memory_order_release);
}

void ProcessGraph::setAutomationLanes(std::vector<AutomationLane> lanes) {
    automationLanes_.store(std::make_shared<std::vector<AutomationLane>>(std::move(lanes)),
                            std::memory_order_release);
    refreshAutomationMask();
}

void ProcessGraph::refreshAutomationMask() {
    // Le masque dit, pour chaque piste, QUELS réglages de mixage sont pilotés.
    // Calculé ici, sur le thread UI, pour que le mixage n'ait qu'un entier à
    // consulter par piste au lieu de parcourir toutes les courbes.
    std::array<uint16_t, kMaxTracks> masque{};
    auto lanes = automationLanes_.load(std::memory_order_acquire);
    if (lanes) {
        for (const auto& lane : *lanes) {
            if (lane.targetTrackIndex >= kMaxTracks) continue;
            switch (lane.target) {
                case AutomationTarget::TrackVolume:
                    masque[lane.targetTrackIndex] |= kAutoVolume; break;
                case AutomationTarget::TrackPan:
                    masque[lane.targetTrackIndex] |= kAutoPan; break;
                case AutomationTarget::TrackSend:
                    if (lane.targetSlot < kMaxSends)
                        masque[lane.targetTrackIndex] |=
                            static_cast<uint16_t>(1u << (kAutoSendFirst + lane.targetSlot));
                    break;
                default: break;
            }
        }
    }
    autoMask_ = masque;
}

void ProcessGraph::setTrackAudio(size_t trackIndex, std::shared_ptr<const AudioTrackSource> source) {
    if (trackIndex >= kMaxTracks) return;
    audioSources_[trackIndex].store(std::move(source), std::memory_order_release);
}

void ProcessGraph::setTrackEffectChain(size_t trackIndex, std::shared_ptr<const EffectChain> chain) {
    if (trackIndex >= kMaxTracks) return;
    effectChains_[trackIndex].store(std::move(chain), std::memory_order_release);
    // Une chaîne peut contenir un effet qui ÉCOUTE un bus (l'ordre de rendu en
    // dépend) et un effet qui RETARDE (la compensation en dépend).
    refreshRenderOrder();
    refreshCompensation();
}

void ProcessGraph::refreshCompensation() {
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (!snapshot) { compensation_.store(nullptr, std::memory_order_release); return; }
    const auto& project = snapshot->project;

    // La latence PROPRE de chaque piste : son instrument, plus ses inserts.
    std::array<int, kMaxTracks> propre{};
    for (size_t t = 0; t < kMaxTracks; ++t) {
        int total = 0;
        if (auto instrument = instruments_[t].load(std::memory_order_acquire))
            total += std::max(0, instrument->latencySamples());
        if (auto chain = effectChains_[t].load(std::memory_order_acquire))
            for (const auto& fx : *chain)
                if (fx) total += std::max(0, fx->latencySamples());
        propre[t] = total;
    }

    // La latence d'un CHEMIN : celle de la piste, plus celle du groupe qui la
    // reçoit. Une piste groupée traverse deux chaînes avant le master, et ne
    // compter que la sienne la laisserait décalée du retard de son groupe.
    std::array<int, kMaxTracks> chemin{};
    int maximum = 0;
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t) {
        int total = propre[t];
        const auto& piste = project.tracks[t];
        if (piste.kind != vsm::sequencer::Track::Kind::Group && piste.outputGroup >= 0) {
            const size_t g = static_cast<size_t>(piste.outputGroup);
            if (g < project.tracks.size() && g < kMaxTracks
                && project.tracks[g].kind == vsm::sequencer::Track::Kind::Group)
                total += propre[g];
        }
        chemin[t] = total;
        maximum = std::max(maximum, total);
    }

    // RIEN N'A DE LATENCE : aucun plan publié, et le rendu emprunte exactement
    // le chemin qu'il avait -- pas une ligne à retard de longueur zéro à
    // traverser pour rien.
    if (maximum <= 0) { compensation_.store(nullptr, std::memory_order_release); return; }

    auto plan = std::make_shared<Compensation>();
    plan->graphLatency = maximum;
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t) {
        // UNE PISTE DE GROUPE NE SE COMPENSE PAS ELLE-MÊME : ses membres sont
        // déjà arrivés alignés, et sa propre chaîne retarde tout le monde de la
        // même façon -- c'est pris en compte dans le chemin de ses membres.
        if (project.tracks[t].kind == vsm::sequencer::Track::Kind::Group) continue;
        const int retard = maximum - chemin[t];
        if (retard <= 0) continue;
        plan->delay[t] = retard;
        plan->lineL[t].assign(static_cast<size_t>(retard), 0.0f);
        plan->lineR[t].assign(static_cast<size_t>(retard), 0.0f);
        plan->writePos[t] = 0;
    }
    compensation_.store(std::move(plan), std::memory_order_release);
}

void ProcessGraph::applyCompensation(Compensation& plan, size_t trackIndex,
                                      float* left, float* right, int numSamples) {
    const int retard = plan.delay[trackIndex];
    if (retard <= 0) return;
    auto& ligneL = plan.lineL[trackIndex];
    auto& ligneR = plan.lineR[trackIndex];
    if (ligneL.size() != static_cast<size_t>(retard)) return;   // plan incohérent : on ne touche à rien
    int pos = plan.writePos[trackIndex];
    for (int i = 0; i < numSamples; ++i) {
        const float sortieL = ligneL[static_cast<size_t>(pos)];
        const float sortieR = ligneR[static_cast<size_t>(pos)];
        ligneL[static_cast<size_t>(pos)] = left[i];
        ligneR[static_cast<size_t>(pos)] = right[i];
        left[i] = sortieL;
        right[i] = sortieR;
        if (++pos >= retard) pos = 0;
    }
    plan.writePos[trackIndex] = pos;
}

void ProcessGraph::refreshRenderOrder() {
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (!snapshot) { renderOrder_.store(nullptr, std::memory_order_release); return; }
    const auto& project = snapshot->project;

    // Quels bus sont ÉCOUTÉS par un effet ?
    uint32_t ecoutes = 0;
    for (size_t t = 0; t < kMaxTracks; ++t) {
        auto chain = effectChains_[t].load(std::memory_order_acquire);
        if (!chain) continue;
        for (const auto& fx : *chain) {
            if (!fx) continue;
            const int bus = fx->sidechainBus();
            if (bus >= 1 && bus <= static_cast<int>(kMaxSends)) ecoutes |= (1u << (bus - 1));
        }
    }
    // ET C'EST ICI QU'ON SAIT SI LE RENDU PEUT SE PARALLÉLISER (D8.1) : la
    // recherche vient d'être faite, il serait absurde de la refaire ailleurs.
    sidechainActive_.store(ecoutes != 0, std::memory_order_release);

    // AUCUNE CHAÎNE LATÉRALE : ordre naturel, et surtout AUCUN ordre publié --
    // le rendu emprunte alors exactement le chemin qu'il avait, au bit près.
    if (ecoutes == 0) { renderOrder_.store(nullptr, std::memory_order_release); return; }

    auto ordre = std::make_shared<std::vector<size_t>>();
    ordre->reserve(project.tracks.size());
    // D'abord celles qui alimentent un bus écouté...
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t) {
        bool alimente = false;
        for (size_t b = 0; b < kMaxSends; ++b)
            if ((ecoutes & (1u << b)) && project.tracks[t].sendLevel(b) > 0.0f) alimente = true;
        if (alimente) ordre->push_back(t);
    }
    // ... puis toutes les autres, dans leur ordre d'origine.
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t) {
        bool deja = false;
        for (size_t d : *ordre) if (d == t) deja = true;
        if (!deja) ordre->push_back(t);
    }
    renderOrder_.store(std::move(ordre), std::memory_order_release);
}

void ProcessGraph::setSendEffect(size_t busIndex, std::shared_ptr<vsm::audio::effect::IAudioEffect> effect) {
    if (busIndex >= kMaxSends) return;
    sends_[busIndex].effect.store(std::move(effect), std::memory_order_release);
}

void ProcessGraph::setSendReturn(size_t busIndex, float gain) {
    if (busIndex >= kMaxSends) return;
    sends_[busIndex].returnGain.store(gain, std::memory_order_release);
}

void ProcessGraph::setLoopRegion(double startSeconds, double endSeconds, bool active) {
    loopStartSeconds_.store(std::max(0.0, startSeconds), std::memory_order_release);
    loopEndSeconds_.store(std::max(0.0, endSeconds), std::memory_order_release);
    loopActive_.store(active && endSeconds > startSeconds, std::memory_order_release);
}

void ProcessGraph::seekSeconds(double seconds) {
    // LES POSITIONS NÉGATIVES SONT LÉGITIMES, et elles ne l'étaient pas : la
    // position était rabotée à zéro. C'est le DÉCOMPTE (D3.2/D3.3) -- deux
    // mesures de clic AVANT le début du morceau -- et le modéliser comme un
    // morceau de ligne de temps situé avant zéro évite d'écrire un second
    // ordonnanceur pour la seule pré-écoute. Tout le reste suit sans rien
    // changer : le planning n'a aucun événement là, les clips audio et la piste
    // de référence ne rencontrent que du silence, et la boucle ne se referme
    // qu'une fois sa fin franchie.
    currentSeconds_.store(seconds, std::memory_order_release);
    // Un déplacement de la tête de lecture recommence le compte des passes :
    // ce qui précède appartient à un autre enregistrement.
    loopWrapCount_.store(0, std::memory_order_release);

    // LA CHASSE AUX CONTRÔLEURS (D16.2) — « Chase Events » de Cubase, actif
    // par défaut comme chez lui.
    //
    // POURQUOI ICI ET PAS DANS LE PLANNING. Le planning est construit UNE
    // fois, du début à la fin du morceau (`setProject`), et le rendu s'y
    // déplace par dichotomie : sauter au refrain ne « saute » aucun
    // événement, il commence simplement à les lire plus loin. Les
    // contrôleurs posés avant ne sont donc jamais joués, et démarrer au
    // refrain perdait la pédale du couplet, le balayage de filtre en cours
    // et le programme de la première mesure. La chasse appartient au
    // DÉPLACEMENT, et c'est ici qu'il a lieu.
    //
    // CALCULÉE SUR CE FIL-CI, qui est celui de l'interface, et livrée au fil
    // audio par une file sans verrou : lui faire remonter le planning à
    // rebours serait un coût non borné là où il n'y en a pas le droit.
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (!snapshot || snapshot->project.tracks.empty()) return;
    const auto& project = snapshot->project;
    const auto tick = project.secondsToTicks(seconds);
    if (tick <= 0) return;

    size_t perdus = 0;
    for (const auto& ev : vsm::sequencer::PlaybackScheduler::chaseAt(project, tick)) {
        ChasedControlEvent chasse;
        chasse.trackIndex = static_cast<uint32_t>(ev.trackIndex);
        bool utile = std::visit([&chasse](auto&& data) {
            using T = std::decay_t<decltype(data)>;
            using namespace vsm::midi;
            if constexpr (std::is_same_v<T, PitchBendEvent>) {
                chasse.event.kind = MidiControlEvent::Kind::PitchBend;
                chasse.event.channel = data.channel;
                chasse.event.value =
                    static_cast<float>(data.value) / 8192.0f * kPitchBendRangeSemitones;
                return true;
            } else if constexpr (std::is_same_v<T, ControlChangeEvent>) {
                chasse.event.kind = MidiControlEvent::Kind::ControlChange;
                chasse.event.channel = data.channel;
                chasse.event.index = data.controller;
                chasse.event.value = static_cast<float>(data.value) / 127.0f;
                return true;
            } else if constexpr (std::is_same_v<T, ChannelPressureEvent>) {
                chasse.event.kind = MidiControlEvent::Kind::ChannelPressure;
                chasse.event.channel = data.channel;
                chasse.event.value = static_cast<float>(data.pressure) / 127.0f;
                return true;
            } else if constexpr (std::is_same_v<T, ProgramChangeEvent>) {
                chasse.event.kind = MidiControlEvent::Kind::ProgramChange;
                chasse.event.channel = data.channel;
                chasse.event.index = data.program;
                return true;
            }
            return false;
        }, ev.data);
        if (!utile) continue;
        chasse.event.sampleOffset = 0;
        if (!chaseQueue_.push(chasse)) ++perdus;
    }
    if (perdus > 0) droppedChasedControls_.fetch_add(perdus, std::memory_order_relaxed);
}

void ProcessGraph::drainChasedControls() {
    drainedChaseCount_ = 0;
    ChasedControlEvent event;
    while (static_cast<size_t>(drainedChaseCount_) < kMaxChasedPerBlock && chaseQueue_.pop(event))
        drainedChase_[static_cast<size_t>(drainedChaseCount_++)] = event;
}


void ProcessGraph::setPlaying(bool playing) { playing_.store(playing, std::memory_order_release); }

bool ProcessGraph::sendLiveNote(LiveNoteSource source, size_t trackIndex, uint8_t note,
                                 uint8_t velocity, bool noteOn) {
    if (trackIndex >= kMaxTracks) return false;
    const size_t queueIndex = static_cast<size_t>(source);
    if (queueIndex >= kNumLiveSources) return false;
    LiveNoteEvent event;
    event.trackIndex = static_cast<uint32_t>(trackIndex);
    event.note = note;
    event.velocity = velocity;
    event.noteOn = noteOn;
    return liveQueues_[queueIndex].push(event);
}

void ProcessGraph::drainLiveNotes() {
    drainedLiveCount_ = 0;
    for (auto& queue : liveQueues_) {
        LiveNoteEvent event;
        while (static_cast<size_t>(drainedLiveCount_) < kMaxLiveEventsPerBlock && queue.pop(event))
            drainedLive_[static_cast<size_t>(drainedLiveCount_++)] = event;
    }
}

int ProcessGraph::totalActiveVoices() const {
    int total = 0;
    for (const auto& slot : instruments_) {
        auto instrument = slot.load(std::memory_order_acquire);
        if (instrument) total += instrument->activeVoiceCount();
    }
    return total;
}

void ProcessGraph::processBlock(float* outputL, float* outputR, int numSamples) {
    vsm::audio::dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0) return;
    std::fill(outputL, outputL + numSamples, 0.0f);
    std::fill(outputR, outputR + numSamples, 0.0f);

    drainLiveNotes();
    drainChasedControls();

    if (!playing_.load(std::memory_order_acquire)) {
        // À L'ARRÊT, la position ne bouge pas et le planning n'est pas rejoué
        // -- mais l'instrument doit quand même être rendu s'il a quelque chose
        // à dire : une note d'écoute qu'on vient de déclencher, ou la queue de
        // release d'une note relâchée. Sans ça, cliquer sur le clavier du
        // piano roll transport arrêté ne produirait aucun son.
        //
        // Court-circuit quand il n'y a rien à jouer : c'est le cas immensément
        // majoritaire (application ouverte, transport à l'arrêt), et on ne veut
        // pas y brûler du CPU en rendant des instruments silencieux.
        // LES VALEURS CHASSÉES COMPTENT AUSSI (D16.2) : poser la tête au
        // refrain transport à l'arrêt doit régler les machines tout de suite,
        // sinon la première note jouée au clavier sonnerait avec les
        // contrôleurs d'avant le déplacement.
        if (drainedLiveCount_ == 0 && drainedChaseCount_ == 0 && totalActiveVoices() == 0) return;

        auto idleSnapshot = snapshot_.load(std::memory_order_acquire);
        if (!idleSnapshot || idleSnapshot->project.tracks.empty()) return;

        const size_t actifs = activeSends_.load(std::memory_order_acquire);
        const int idleSamples = std::min(numSamples, static_cast<int>(scratchMonoL_.size()));
        for (size_t b = 0; b < actifs; ++b) {
            std::fill(sendL_[b].begin(), sendL_[b].begin() + idleSamples, 0.0f);
            std::fill(sendR_[b].begin(), sendR_[b].begin() + idleSamples, 0.0f);
        }
        for (size_t g = 0; g < kMaxGroups; ++g) {
            std::fill(groupL_[g].begin(), groupL_[g].begin() + idleSamples, 0.0f);
            std::fill(groupR_[g].begin(), groupR_[g].begin() + idleSamples, 0.0f);
        }
        blockPeak_.fill(0.0f);
        blockSumL2_.fill(0.0);
        blockSumR2_.fill(0.0);
        blockSumLR_.fill(0.0);
        blockCount_.fill(0);

        const bool idleAnySolo = std::any_of(idleSnapshot->project.tracks.begin(),
                                              idleSnapshot->project.tracks.end(),
                                              [](const Track& t) { return t.solo; });
        renderTrackRange(*idleSnapshot, idleAnySolo, 0, idleSamples,
                          currentSeconds_.load(std::memory_order_acquire), outputL, outputR,
                          /*includeScheduledEvents=*/false);
        // Même à l'arrêt : une note d'écoute jouée sur une piste groupée doit
        // sortir par son groupe, sinon elle serait muette là et audible en
        // lecture, ce qui est le genre d'incohérence qu'on met une heure à
        // comprendre.
        renderGroupBuses(*idleSnapshot, idleAnySolo, idleSamples, outputL, outputR);

        for (size_t t = 0; t < kMaxTracks; ++t) publishMeasurement(t);
        for (size_t b = 0; b < actifs; ++b) {
            auto fx = sends_[b].effect.load(std::memory_order_acquire);
            if (!fx) continue;
            fx->process(sendL_[b].data(), sendR_[b].data(), idleSamples);
            const float ret = sends_[b].returnGain.load(std::memory_order_acquire);
            for (int i = 0; i < idleSamples; ++i) {
                outputL[i] += sendL_[b][static_cast<size_t>(i)] * ret;
                outputR[i] += sendR_[b][static_cast<size_t>(i)] * ret;
            }
        }
        masterBus_.process(outputL, outputR, idleSamples);
        spectrumTap_.write(outputL, outputR, idleSamples);
        // À l'arrêt, la position ne bouge pas : la référence n'a rien à jouer.
        return;
    }

    // Ne traite jamais plus que ce que les buffers de travail pré-alloués
    // permettent (règle realtime : pas de redimensionnement ici). En usage
    // normal, prepare() a été appelé avec le block size réel du device, donc
    // ce clamp ne devrait jamais réellement raccourcir le rendu.
    const int samplesToProcess = std::min(numSamples, static_cast<int>(scratchMonoL_.size()));
    const size_t actifs = activeSends_.load(std::memory_order_acquire);

    double blockStartSeconds = currentSeconds_.load(std::memory_order_acquire);
    const double blockDurationSeconds = static_cast<double>(samplesToProcess) / sampleRate_;
    // Valeur par défaut : sans boucle, la position avance simplement de la
    // durée du bloc. Le rebouclage la remplace par la position réellement
    // atteinte (voir plus bas).
    double blockEndSeconds = blockStartSeconds + blockDurationSeconds;

    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (snapshot && !snapshot->project.tracks.empty()) {
        const Project& project = snapshot->project;
        auto lanes = automationLanes_.load(std::memory_order_acquire);

        bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                    [](const Track& t) { return t.solo; });

        // Réinitialise les buffers de sends et les pics pour ce bloc.
        for (size_t b = 0; b < actifs; ++b) {
            std::fill(sendL_[b].begin(), sendL_[b].begin() + samplesToProcess, 0.0f);
            std::fill(sendR_[b].begin(), sendR_[b].begin() + samplesToProcess, 0.0f);
        }
        for (size_t g = 0; g < kMaxGroups; ++g) {
            std::fill(groupL_[g].begin(), groupL_[g].begin() + samplesToProcess, 0.0f);
            std::fill(groupR_[g].begin(), groupR_[g].begin() + samplesToProcess, 0.0f);
        }
        blockPeak_.fill(0.0f);
        blockSumL2_.fill(0.0);
        blockSumR2_.fill(0.0);
        blockSumLR_.fill(0.0);
        blockCount_.fill(0);

        // Découpage du bloc à la frontière de boucle, pour que le rebouclage
        // soit exact à l'échantillon près plutôt qu'arrondi à la taille de
        // bloc (une boucle arrondie dériverait audiblement en quelques tours).
        const bool loopActive = loopActive_.load(std::memory_order_acquire);
        const double loopStart = loopStartSeconds_.load(std::memory_order_acquire);
        const double loopEnd = loopEndSeconds_.load(std::memory_order_acquire);

        int rendered = 0;
        double spanStartSeconds = blockStartSeconds;
        while (rendered < samplesToProcess) {
            int count = samplesToProcess - rendered;

            if (loopActive && loopEnd > loopStart && spanStartSeconds < loopEnd) {
                const double untilLoopEnd = loopEnd - spanStartSeconds;
                const int samplesUntilEnd = static_cast<int>(std::ceil(untilLoopEnd * sampleRate_));
                count = std::min(count, std::max(1, samplesUntilEnd));
            }

            renderSpan(*snapshot, anySolo, rendered, count, spanStartSeconds, outputL, outputR, lanes.get());
            wrapNoteOffPending_ = false; // consommé par TOUTES les pistes du segment

            rendered += count;
            spanStartSeconds += static_cast<double>(count) / sampleRate_;

            if (loopActive && loopEnd > loopStart && spanStartSeconds >= loopEnd - 1.0e-12) {
                spanStartSeconds = loopStart;
                // La boucle vient de se refermer : on le COMPTE. C'est le seul
                // moyen de distinguer deux passes d'enregistrement, qui occupent
                // exactement les mêmes positions sur la ligne de temps.
                loopWrapCount_.fetch_add(1, std::memory_order_release);
                // Les notes encore tenues à la fin de la boucle n'auront jamais
                // leur NoteOff (il se trouve après la frontière) : on les
                // relâche au saut, sinon elles sonneraient indéfiniment.
                wrapNoteOffPending_ = true;
            }
        }
        blockEndSeconds = spanStartSeconds;

        // LES GROUPES, une fois que toutes leurs pistes ont écrit : leurs
        // inserts traitent le groupe entier, puis il rejoint le master et
        // alimente les départs. Avant la lecture des mètres, pour que le mètre
        // d'un groupe montre ce qu'il envoie vraiment.
        renderGroupBuses(*snapshot, anySolo, samplesToProcess, outputL, outputR);

        for (size_t t = 0; t < kMaxTracks; ++t) publishMeasurement(t);

        // Traite chaque bus de send par son effet, puis ajoute le retour au
        // master (avant la tranche master).
        for (size_t b = 0; b < actifs; ++b) {
            auto fx = sends_[b].effect.load(std::memory_order_acquire);
            if (!fx) continue;
            fx->process(sendL_[b].data(), sendR_[b].data(), samplesToProcess);
            const float ret = sends_[b].returnGain.load(std::memory_order_acquire);
            for (int i = 0; i < samplesToProcess; ++i) {
                outputL[i] += sendL_[b][static_cast<size_t>(i)] * ret;
                outputR[i] += sendR_[b][static_cast<size_t>(i)] * ret;
            }
        }
    }

    // Tranche master sur le bus stéréo final. No-op tant qu'elle n'est pas
    // activée (bypass par défaut) -> comportement historique préservé.
    masterBus_.process(outputL, outputR, samplesToProcess);
    spectrumTap_.write(outputL, outputR, samplesToProcess);   // D15.3 : ce que l'analyseur voit est ce qui sort

    // MÉTRONOME, mélangé APRÈS le master et pour la même raison que la piste de
    // référence : il n'appartient pas au morceau. Le faire passer par le
    // compresseur ferait plonger tout le mixage à chaque temps.
    //
    // ET IL BAT TOUJOURS PENDANT UN DÉCOMPTE, même éteint. Un décompte qu'on
    // n'entend pas ne compte rien : c'est sa seule raison d'être. Les positions
    // négatives sont celles du décompte (voir `seekSeconds`), d'où la
    // condition -- qui donne aussi, gratuitement, le dernier clic sur le premier
    // temps du morceau, celui sur lequel on entre.
    const bool clicAudible = metronomeEnabled_.load(std::memory_order_relaxed)
                             || blockStartSeconds < 0.0;
    if (clicAudible) {
        const auto snapshot = snapshot_.load(std::memory_order_acquire);
        if (snapshot) {
            const Project& projet = snapshot->project;
            // La position DU DÉBUT DU BLOC, prise telle quelle et non
            // recalculée depuis la fin : au rebouclage, la fin du bloc est
            // repartie au début de la boucle, et lui soustraire la durée du
            // bloc donnerait un intervalle qui n'a jamais été joué.
            const double debutSecondes = blockStartSeconds;
            const double finSecondes = blockStartSeconds + blockDurationSeconds;
            const int64_t debutTick = projet.secondsToTicks(debutSecondes);
            const int64_t finTick = projet.secondsToTicks(finSecondes);
            const int numerateur = projet.timeSignatureMap.numeratorAt(debutTick);
            forEachBeatInRange(debutTick, finTick, projet.ticksPerQuarterNote, numerateur,
                                [&](int64_t tick, bool accent) {
                                    (void)tick;
                                    metronome_.trigger(accent);
                                });
        }
        for (int i = 0; i < samplesToProcess; ++i) {
            const float clic = metronome_.nextSample();
            outputL[i] += clic;
            outputR[i] += clic;
        }
    }

    // RÉFÉRENCE : mélangée APRÈS le master, et jamais avant. La tranche master
    // appartient à la reconstruction ; la faire agir sur l'enregistrement
    // d'origine reviendrait à comparer deux sons également traités au lieu de
    // comparer une copie à son modèle.
    //
    // En mode « original seul », la reconstruction est TUE ici plutôt qu'en
    // amont : les instruments continuent de tourner, donc leurs enveloppes et
    // leurs filtres restent à jour, et revenir à la reconstruction ne produit
    // ni silence ni claquement le temps qu'ils se remettent en marche.
    if (referenceTrack_.silencesReconstruction()) {
        std::fill(outputL, outputL + samplesToProcess, 0.0f);
        std::fill(outputR, outputR + samplesToProcess, 0.0f);
    }
    referenceTrack_.mixInto(outputL, outputR, samplesToProcess, blockStartSeconds);

    currentSeconds_.store(blockEndSeconds, std::memory_order_release);
}

void ProcessGraph::renderSpan(const GraphSnapshot& snapshot, bool anySolo, int sampleStart, int sampleCount,
                               double startSeconds, float* outputL, float* outputR,
                               const std::vector<AutomationLane>* lanes) {
    auto applyAutomationAt = [&](double seconds) {
        if (!lanes) return;
        const Tick tick = snapshot.project.secondsToTicks(seconds);
        for (const auto& lane : *lanes) {
            const size_t t = lane.targetTrackIndex;
            const float valeur = lane.valueAt(tick);
            switch (lane.target) {
                case AutomationTarget::InstrumentParam: {
                    if (t >= kMaxTracks) break;
                    auto instrument = instruments_[t].load(std::memory_order_acquire);
                    if (instrument) instrument->setParameter(lane.targetParam, valeur);
                    break;
                }
                case AutomationTarget::TrackVolume:
                    if (t < kMaxTracks) autoVolume_[t] = valeur;
                    break;
                case AutomationTarget::TrackPan:
                    if (t < kMaxTracks) autoPan_[t] = valeur;
                    break;
                case AutomationTarget::TrackSend:
                    if (t < kMaxTracks && lane.targetSlot < kMaxSends)
                        autoSend_[t][lane.targetSlot] = valeur;
                    break;
                case AutomationTarget::InsertParam: {
                    if (t >= kMaxTracks) break;
                    auto chain = effectChains_[t].load(std::memory_order_acquire);
                    if (chain && lane.targetSlot < chain->size()) {
                        const auto& fx = (*chain)[lane.targetSlot];
                        if (fx) fx->setParameter(lane.targetParam, valeur);
                    }
                    break;
                }
                case AutomationTarget::MasterParam:
                    // LA TRANCHE MASTER N'APPARTIENT À AUCUNE PISTE : l'index
                    // de piste est ignoré, et c'est ce que dit `MasterParam`.
                    masterBus_.setParameter(lane.targetParam, valeur);
                    break;
            }
        }
    };

    if (!lanes || lanes->empty()) {
        // Aucune automation : rendu d'un seul tenant (chemin historique,
        // bit-identique -> aucune régression sur les pistes sans lane).
        renderTrackRange(snapshot, anySolo, sampleStart, sampleCount, startSeconds, outputL, outputR);
        return;
    }

    // Automation active (section 17) : découpage en sous-segments de
    // kAutomationChunk échantillons, valeur ré-appliquée au début de chaque
    // segment -> automation quasi sample-accurate (granularité ~1,3 ms à
    // 48 kHz) sans exiger que les synthés relisent leurs paramètres à chaque
    // échantillon.
    constexpr int kAutomationChunk = 64;
    for (int s = 0; s < sampleCount; s += kAutomationChunk) {
        const int count = std::min(kAutomationChunk, sampleCount - s);
        const double segStart = startSeconds + static_cast<double>(s) / sampleRate_;
        applyAutomationAt(segStart);
        renderTrackRange(snapshot, anySolo, sampleStart + s, count, segStart, outputL, outputR);
    }
}

void ProcessGraph::publishMeasurement(size_t trackIndex) {
    TrackMeasurement mesure;
    mesure.peak = blockPeak_[trackIndex];
    const int n = blockCount_[trackIndex];
    if (n > 0) {
        // Le RMS des DEUX canaux ensemble : c'est le niveau de la piste, pas
        // celui d'un de ses côtés.
        mesure.rms = static_cast<float>(
            std::sqrt((blockSumL2_[trackIndex] + blockSumR2_[trackIndex]) / (2.0 * n)));
        const double denominateur = std::sqrt(blockSumL2_[trackIndex] * blockSumR2_[trackIndex]);
        mesure.correlation = denominateur > 1.0e-20
            ? static_cast<float>(std::clamp(blockSumLR_[trackIndex] / denominateur, -1.0, 1.0))
            : 1.0f;   // silence : deux canaux vides sont identiques, pas « sans rapport »
    }
    meters_.reportMeasurement(trackIndex, mesure);
}

int ProcessGraph::groupBufferFor(const vsm::sequencer::Project& project, size_t trackIndex) const {
    if (trackIndex >= project.tracks.size()) return -1;
    const auto& track = project.tracks[trackIndex];
    // UN GROUPE NE VA JAMAIS DANS UN GROUPE : un seul niveau, décidé dans
    // `Track::outputGroup`. Sans cette ligne, un routage circulaire ferait
    // tourner le rendu en rond -- littéralement.
    if (track.kind == vsm::sequencer::Track::Kind::Group) return -1;
    if (track.outputGroup < 0) return -1;
    const size_t cible = static_cast<size_t>(track.outputGroup);
    if (cible >= project.tracks.size()) return -1;
    if (project.tracks[cible].kind != vsm::sequencer::Track::Kind::Group) return -1;

    // Le tampon d'un groupe est son RANG PARMI LES GROUPES, et non son index de
    // piste : huit tampons suffisent à huit groupes, où qu'ils se trouvent
    // parmi cent pistes.
    int rang = 0;
    for (size_t i = 0; i < cible; ++i)
        if (project.tracks[i].kind == vsm::sequencer::Track::Kind::Group) ++rang;
    if (static_cast<size_t>(rang) >= kMaxGroups) return -1;
    return rang;
}

void ProcessGraph::renderGroupBuses(const GraphSnapshot& snapshot, bool anySolo, int numSamples,
                                     float* outputL, float* outputR) {
    const auto& project = snapshot.project;
    const size_t actifs = activeSends_.load(std::memory_order_acquire);
    int rang = 0;
    for (size_t trackIndex = 0; trackIndex < project.tracks.size() && trackIndex < kMaxTracks;
         ++trackIndex) {
        const auto& track = project.tracks[trackIndex];
        if (track.kind != vsm::sequencer::Track::Kind::Group) continue;
        if (static_cast<size_t>(rang) >= kMaxGroups) break;
        const size_t g = static_cast<size_t>(rang++);

        // Les inserts du groupe traitent ce que TOUS ses membres y ont versé.
        auto chain = effectChains_[trackIndex].load(std::memory_order_acquire);
        if (chain)
            for (const auto& fx : *chain)
                if (fx) fx->process(groupL_[g].data(), groupR_[g].data(), numSamples);

        const bool audible = anySolo ? track.solo : !track.muted;
        // BALANCE et non panoramique : le groupe reçoit un signal déjà stéréo,
        // qui a déjà traversé la loi à puissance constante de ses pistes. La
        // lui appliquer une seconde fois lui coûterait encore 3 dB, et grouper
        // deviendrait un choix qu'on paie. Voir `stereoBalance`.
        const float peak = mixStereoBalancedInto(groupL_[g].data(), groupR_[g].data(), numSamples,
                                                  track.volume, track.pan, audible,
                                                  outputL, outputR);
        blockPeak_[trackIndex] = std::max(blockPeak_[trackIndex], peak);

        // Un groupe se mesure comme une piste (D4.7), sur ce qu'il envoie.
        for (int i = 0; i < numSamples; ++i) {
            const double l = static_cast<double>(groupL_[g][static_cast<size_t>(i)]) * track.volume;
            const double r = static_cast<double>(groupR_[g][static_cast<size_t>(i)]) * track.volume;
            blockSumL2_[trackIndex] += l * l;
            blockSumR2_[trackIndex] += r * r;
            blockSumLR_[trackIndex] += l * r;
        }
        blockCount_[trackIndex] += numSamples;

        // Un groupe alimente les départs comme une piste : c'est ce qui permet
        // d'envoyer toute une batterie dans une réverbération d'un seul geste.
        if (audible) {
            const uint32_t preFader = preFaderMask_.load(std::memory_order_acquire);
            for (size_t b = 0; b < actifs; ++b) {
                const float apresFader = (preFader & (1u << b)) ? 1.0f : track.volume;
                const float lvl = track.sendLevel(b) * apresFader;
                if (lvl <= 0.0f) continue;
                for (int i = 0; i < numSamples; ++i) {
                    sendL_[b][static_cast<size_t>(i)] += groupL_[g][static_cast<size_t>(i)] * lvl;
                    sendR_[b][static_cast<size_t>(i)] += groupR_[g][static_cast<size_t>(i)] * lvl;
                }
            }
        }
    }
}

vsm::audio::plugin::TransportInfo ProcessGraph::transportFor(const Project& project,
                                                              double seconds, bool playing) {
    vsm::audio::plugin::TransportInfo transport;
    transport.playing = playing;
    transport.positionSeconds = seconds;

    const Tick tick = project.secondsToTicks(seconds);
    transport.tempoBpm = project.tempoMap.bpmAt(tick);
    transport.timeSignatureNumerator = project.timeSignatureMap.numeratorAt(tick);
    transport.timeSignatureDenominator =
        static_cast<int>(project.timeSignatureMap.denominatorAt(tick));

    // LA POSITION EN NOIRES, ET NON EN TEMPS DE LA MESURE. « Beat » veut dire
    // la noire dans tous les formats de plugin -- CLAP le dit explicitement,
    // VST3 aussi -- même en 6/8, où le temps musical est la croche pointée.
    // Convertir en temps de mesure ferait sauter un delay synchronisé d'un
    // facteur trois dès qu'on quitte le 4/4.
    const double ppq = project.ticksPerQuarterNote > 0
                           ? static_cast<double>(project.ticksPerQuarterNote) : 480.0;
    transport.positionBeats = static_cast<double>(tick) / ppq;

    transport.looping = project.loopEnabled && project.loopEndTick > project.loopStartTick;
    transport.loopStartBeats = static_cast<double>(project.loopStartTick) / ppq;
    transport.loopEndBeats = static_cast<double>(project.loopEndTick) / ppq;
    return transport;
}

bool ProcessGraph::renderTrackVoice(const GraphSnapshot& snapshot, size_t trackIndex,
                                    int sampleStart, int sampleCount, double rangeStartSeconds,
                                    bool includeScheduledEvents, Compensation* compensation,
                                    MidiNoteEvent* events, float* destL, float* destR) {
    const Project& project = snapshot.project;
    if (trackIndex >= project.tracks.size() || trackIndex >= kMaxTracks) return false;
    const double rangeEndSeconds = rangeStartSeconds + static_cast<double>(sampleCount) / sampleRate_;

    auto instrument = instruments_[trackIndex].load(std::memory_order_acquire);
    auto audioSource = audioSources_[trackIndex].load(std::memory_order_acquire);
    // UNE PISTE AUDIO N'A PAS D'INSTRUMENT, et c'est normal : son matériau
    // est un fichier. La condition portait sur le seul instrument, ce qui
    // aurait fait sauter la piste entière en silence.
    if (!instrument && !audioSource) return false;

    const Track& track = project.tracks[trackIndex];

    int numEvents = 0;

    // Rebouclage : relâche ce que cette piste tenait encore à la frontière
    // de boucle. Sans ça, une note dont le NoteOff tombe APRÈS la fin de
    // boucle ne serait jamais relâchée et sonnerait indéfiniment -- le
    // "note bloquée" classique des séquenceurs.
    if (wrapNoteOffPending_) {
        auto& sounding = soundingNotes_[trackIndex];
        for (int note = 0; note < 128 && numEvents < kMaxEventsPerBlock; ++note) {
            if (!sounding[static_cast<size_t>(note)]) continue;
            MidiNoteEvent off;
            off.kind = MidiNoteEvent::Kind::NoteOff;
            off.sampleOffset = 0;
            off.channel = track.channel;
            off.note = static_cast<uint8_t>(note);
            off.velocity = 64;
            events[static_cast<size_t>(numEvents++)] = off;
            sounding[static_cast<size_t>(note)] = false;
        }
    }

    // Notes d'écoute (clic sur le clavier, clavier MIDI) : placées en tête
    // de bloc. Uniquement dans le PREMIER sous-segment (sampleStart == 0),
    // sinon le découpage en sous-segments de l'automation les rejouerait à
    // chaque segment -- une seule note déclencherait huit attaques.
    if (sampleStart == 0) {
        for (int i = 0; i < drainedLiveCount_ && numEvents < kMaxEventsPerBlock; ++i) {
            const LiveNoteEvent& live = drainedLive_[static_cast<size_t>(i)];
            if (live.trackIndex != trackIndex) continue;
            MidiNoteEvent pluginEvent;
            pluginEvent.kind = live.noteOn ? MidiNoteEvent::Kind::NoteOn : MidiNoteEvent::Kind::NoteOff;
            pluginEvent.sampleOffset = 0;
            pluginEvent.channel = track.channel;
            pluginEvent.note = live.note;
            pluginEvent.velocity = live.velocity;
            soundingNotes_[trackIndex][live.note] = live.noteOn;
            events[static_cast<size_t>(numEvents++)] = pluginEvent;
        }
    }

    // LES VALEURS CHASSÉES (D16.2) : livrées EN TÊTE DE BLOC, avant tout ce
    // que le planning apporte -- une pédale rendue après la note qu'elle
    // devait tenir ne la tient pas. Comme les notes d'écoute, seulement dans
    // le premier sous-segment : le découpage de l'automation les rejouerait
    // sinon à chaque segment.
    if (sampleStart == 0 && instrument != nullptr) {
        for (int i = 0; i < drainedChaseCount_; ++i) {
            const ChasedControlEvent& chasse = drainedChase_[static_cast<size_t>(i)];
            if (chasse.trackIndex != trackIndex) continue;
            if (!instrument->handleControlEvent(chasse.event))
                ignoredControlEvents_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // SEULEMENT LA TRANCHE DE CETTE PISTE, ET ON Y ENTRE PAR DICHOTOMIE (D8.4).
    // Le planning était parcouru EN ENTIER par chaque piste, à chaque
    // sous-segment, pour n'en garder que ce qui la concernait : le coût d'un
    // bloc valait « pistes x événements », et trente-deux pistes de quatre
    // mille notes consommaient 99,5 % du budget rien qu'à écarter des notes qui
    // ne sonnaient pas encore. Le snapshot le range désormais par piste (voir
    // `GraphSnapshot`), et le temps de départ se cherche au lieu de se
    // parcourir.
    const auto& planning = snapshot.schedule;
    const auto borne = snapshot.trackRange[trackIndex];
    const auto debutDePiste = planning.begin() + borne.first;
    const auto finDePiste = planning.begin() + borne.second;
    // UN QUART D'ÉCHANTILLON DE MARGE, ET C'EST LA CORRECTION D'UN DÉFAUT
    // MESURÉ (ROADMAP-daw § 6, invariant n° 3). `rangeStartSeconds` s'accumule
    // en flottant bloc après bloc ; un événement PILE sur une frontière tombe
    // donc d'un côté ou de l'autre selon l'erreur d'accumulation. L'ancienne
    // écriture le comparait en SECONDES puis le plaçait en ÉCHANTILLONS avec
    // un clamp à [0, n-1] : quand l'erreur le faisait entrer dans le bloc de
    // trop, le clamp posait son relâchement UN échantillon trop tôt, et la
    // queue divergeait entre deux tailles de bloc (~-76 dB, mesuré).
    // Désormais l'appartenance se décide à l'ÉCHANTILLON -- la marge d'un
    // quart d'échantillon sur la borne de recherche garantit qu'un événement
    // de frontière est VU par les deux blocs, et l'offset tranche : exactement
    // l'un des deux le joue.
    const double margeFrontiere = 0.25 / sampleRate_;
    const auto premierEvenement =
        std::lower_bound(debutDePiste, finDePiste, rangeStartSeconds - margeFrontiere,
                          [](const ScheduledEvent& ev, double t) { return ev.timeSeconds < t; });

    if (includeScheduledEvents)
    for (auto it = premierEvenement; it != finDePiste; ++it) {
        const auto& ev = *it;
        // TRIÉE PAR TEMPS DANS LA PISTE : le premier événement trop tard est
        // aussi le dernier qu'on ait à regarder.
        if (ev.timeSeconds >= rangeEndSeconds + margeFrontiere) break;
        if (numEvents >= kMaxEventsPerBlock) {
            // PLUS DE `break` MUET. Le plafond existe pour garder le
            // tableau de travail à taille fixe (le chemin temps réel
            // n'alloue pas) ; le franchir reste possible, mais il n'est
            // plus permis qu'une note disparaisse sans que rien ne le
            // dise. Le compteur est lu par l'interface.
            droppedNoteEvents_.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        MidiNoteEvent pluginEvent;
        MidiControlEvent controlEvent;
        // L'ÉCHANTILLON ABSOLU ARRONDI EST L'ARBITRE, pas une soustraction de
        // secondes accumulées : `llround(t x sr)` donne à chaque événement et
        // à chaque début de bloc UNE position entière stable, identique d'un
        // tour de boucle à l'autre -- la soustraction en secondes, elle,
        // héritait de l'erreur d'accumulation du découpage et faisait vaciller
        // les événements de frontière d'un échantillon selon le tour (attrapé
        // par `process_graph_loop_renders_the_same_audio_every_turn`).
        // Négatif : l'événement appartenait au bloc précédent, qui l'a joué --
        // la marge le refait seulement passer sous nos yeux. Au-delà : il
        // appartient au suivant, et la piste étant triée, tous ceux d'après
        // aussi. Aucun clamp : un clamp déplace, et un événement déplacé est
        // un événement faux.
        const long long echantillonBloc = std::llround(rangeStartSeconds * sampleRate_);
        const long long echantillonEvenement = std::llround(ev.timeSeconds * sampleRate_);
        const long long decalage = echantillonEvenement - echantillonBloc;
        if (decalage < 0) continue;
        if (decalage >= sampleCount) break;
        pluginEvent.sampleOffset = static_cast<int>(decalage);
        controlEvent.sampleOffset = static_cast<int>(decalage);

        // Trois issues, et plus seulement deux : une note, un contrôle, ou
        // un méta-événement qui n'a rien à faire dans une machine (tempo,
        // nom de piste...). La troisième est la SEULE qu'on ait le droit
        // d'écarter sans le dire.
        enum class Issue { Note, Control, NotForTheMachine };
        const Issue issue = std::visit([&pluginEvent, &controlEvent](auto&& data) -> Issue {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, NoteOnEvent>) {
                pluginEvent.kind = MidiNoteEvent::Kind::NoteOn;
                pluginEvent.channel = data.channel;
                pluginEvent.note = data.note;
                pluginEvent.velocity = data.velocity;
                return Issue::Note;
            } else if constexpr (std::is_same_v<T, NoteOffEvent>) {
                pluginEvent.kind = MidiNoteEvent::Kind::NoteOff;
                pluginEvent.channel = data.channel;
                pluginEvent.note = data.note;
                pluginEvent.velocity = data.velocity;
                return Issue::Note;
            } else if constexpr (std::is_same_v<T, PitchBendEvent>) {
                // Converti en DEMI-TONS ici, une fois : une machine n'a pas
                // à connaître les 14 bits signés du MIDI. La plage est de
                // +/- 2 demi-tons, la convention par défaut de tous les
                // instruments depuis la General MIDI.
                controlEvent.kind = MidiControlEvent::Kind::PitchBend;
                controlEvent.channel = data.channel;
                controlEvent.value = static_cast<float>(data.value) / 8192.0f * kPitchBendRangeSemitones;
                return Issue::Control;
            } else if constexpr (std::is_same_v<T, ControlChangeEvent>) {
                controlEvent.kind = MidiControlEvent::Kind::ControlChange;
                controlEvent.channel = data.channel;
                controlEvent.index = data.controller;
                controlEvent.value = static_cast<float>(data.value) / 127.0f;
                return Issue::Control;
            } else if constexpr (std::is_same_v<T, ChannelPressureEvent>) {
                controlEvent.kind = MidiControlEvent::Kind::ChannelPressure;
                controlEvent.channel = data.channel;
                controlEvent.value = static_cast<float>(data.pressure) / 127.0f;
                return Issue::Control;
            } else if constexpr (std::is_same_v<T, PolyPressureEvent>) {
                controlEvent.kind = MidiControlEvent::Kind::PolyPressure;
                controlEvent.channel = data.channel;
                controlEvent.index = data.note;
                controlEvent.value = static_cast<float>(data.pressure) / 127.0f;
                return Issue::Control;
            } else if constexpr (std::is_same_v<T, ProgramChangeEvent>) {
                controlEvent.kind = MidiControlEvent::Kind::ProgramChange;
                controlEvent.channel = data.channel;
                controlEvent.index = data.program;
                return Issue::Control;
            }
            return Issue::NotForTheMachine;
        }, ev.data);

        if (issue == Issue::Note) {
            soundingNotes_[trackIndex][pluginEvent.note] =
                (pluginEvent.kind == MidiNoteEvent::Kind::NoteOn);
            events[static_cast<size_t>(numEvents++)] = pluginEvent;
        } else if (issue == Issue::Control) {
            // Livré TOUT DE SUITE : les contrôles ne passent pas par le
            // tableau d'événements de note, dont le contrat (et les
            // vingt-deux machines qui le lisent) ne connaît que NoteOn et
            // NoteOff. La granularité est celle du sous-segment
            // d'automation, ~1,3 ms, et c'est la même que celle des
            // paramètres automatisés -- une machine ne peut donc pas voir
            // ses deux sources de modulation se contredire.
            if (!instrument->handleControlEvent(controlEvent))
                ignoredControlEvents_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Rendu STÉRÉO de la piste (L/R séparés) : un instrument, du matériau
    // audio, ou les deux -- rien n'interdit à une piste audio de porter
    // aussi des notes, et le graphe n'a pas à en décider.
    std::fill(destL, destL + sampleCount, 0.0f);
    std::fill(destR, destR + sampleCount, 0.0f);
    // UNE PISTE GELÉE NE FAIT PLUS TOURNER SON INSTRUMENT (D5.5) : c'est
    // tout l'intérêt du gel, et c'est ce qui la fait « coûter le prix d'une
    // lecture audio ». Son matériau reste dans la piste et revient au
    // dégel ; seul le calcul s'arrête.
    // LE TRANSPORT (D7.4), calculé UNE FOIS pour la piste et livré à
    // l'instrument comme aux inserts, juste avant qu'ils travaillent.
    // Position, tempo et signature sont lus à `rangeStartSeconds`, c'est-
    // à-dire au début du segment traité : un plugin qui lit le tempo au
    // milieu d'un ritardando doit voir celui de l'instant qu'il rend, pas
    // celui du bloc précédent.
    const vsm::audio::plugin::TransportInfo transport = transportFor(
        project, rangeStartSeconds, playing_.load(std::memory_order_acquire));

    if (instrument && !track.frozen) {
        instrument->setTransportInfo(transport);
        instrument->process(events, numEvents, destL, destR, sampleCount);
    }
    if (audioSource && !audioSource->empty()) {
        // La position sur la LIGNE DE TEMPS, en échantillons. Elle vient du
        // temps du segment et non d'un compteur de blocs : c'est ce qui
        // fait qu'un bouclage ou un saut de tête de lecture tombe juste.
        const int64_t depart = static_cast<int64_t>(std::llround(rangeStartSeconds * sampleRate_));
        audioSource->mixInto(destL, destR, depart, sampleCount);
    }

    // Chaîne d'inserts (section 5) : TRACK -> SYNTH -> EFFECTS -> MIX.
    // LES INSERTS NON PLUS : ils sont DANS le fichier gelé, et les
    // repasser dessus les appliquerait deux fois.
    auto chain = track.frozen ? nullptr
                              : effectChains_[trackIndex].load(std::memory_order_acquire);
    if (chain) {
        for (const auto& fx : *chain) {
            if (!fx) continue;
            // CHAÎNE LATÉRALE : si l'effet écoute un bus, on lui tend le
            // contenu de ce bus POUR CE SEGMENT, juste avant qu'il
            // travaille. Le bus a déjà reçu les pistes qui l'alimentent :
            // c'est ce que garantit l'ordre de rendu.
            const int bus = fx->sidechainBus();
            if (bus >= 1 && bus <= static_cast<int>(kMaxSends))
                fx->setSidechainInput(sendL_[static_cast<size_t>(bus - 1)].data() + sampleStart,
                                       sendR_[static_cast<size_t>(bus - 1)].data() + sampleStart,
                                       sampleCount);
            fx->setTransportInfo(transport);
            fx->process(destL, destR, sampleCount);
        }
    }

    // COMPENSATION DE LATENCE (D4.5), APRÈS la chaîne et AVANT le mixage :
    // ce qu'on aligne est ce qui part vers le master et vers les départs,
    // pas ce qui entre dans les effets.
    if (compensation) applyCompensation(*compensation, trackIndex, destL, destR, sampleCount);
    return true;
}

void ProcessGraph::renderVoiceJob(void* context, size_t index, size_t workerId) {
    auto& lot = *static_cast<VoiceBatch*>(context);
    ProcessGraph& self = *lot.self;
    const size_t trackIndex = lot.order ? (*lot.order)[index] : index;
    if (trackIndex >= kMaxTracks) return;
    const bool rendu = self.renderTrackVoice(
        *lot.snapshot, trackIndex, lot.sampleStart, lot.sampleCount, lot.rangeStartSeconds,
        lot.includeScheduledEvents, lot.compensation,
        self.parallelEvents_[workerId].data(),
        self.parallelL_[trackIndex].data(), self.parallelR_[trackIndex].data());
    // Une case par piste, écrite par un seul thread : des voisines de tableau
    // sont des emplacements mémoire distincts, donc pas une course.
    self.parallelActive_[trackIndex] = rendu ? uint8_t{1} : uint8_t{0};
}

void ProcessGraph::mixTrackInto(const GraphSnapshot& snapshot, bool anySolo, size_t trackIndex,
                                int sampleStart, int sampleCount, const float* srcL,
                                const float* srcR, float* outputL, float* outputR) {
    const Project& project = snapshot.project;
    if (trackIndex >= project.tracks.size() || trackIndex >= kMaxTracks) return;
    const Track& track = project.tracks[trackIndex];
    const bool audible = anySolo ? track.solo : !track.muted;

    // MIXAGE VERS SA DESTINATION : le master, ou le tampon d'un groupe. Le
    // groupe sera traité en fin de bloc, quand tous ses membres y auront
    // écrit -- voir `renderGroupBuses`.
    const int groupe = groupBufferFor(project, trackIndex);
    float* destL = groupe >= 0 ? groupL_[static_cast<size_t>(groupe)].data() : outputL;
    float* destR = groupe >= 0 ? groupR_[static_cast<size_t>(groupe)].data() : outputR;

    // LE VOLUME ET LE PANORAMIQUE VIENNENT DE L'AUTOMATION QUAND ELLE LES
    // PILOTE (D4.6), du projet sinon. Un entier consulté par piste, pas un
    // parcours des courbes : voir `refreshAutomationMask`.
    const uint16_t pilotes = autoMask_[trackIndex];
    const float volume = (pilotes & kAutoVolume) ? autoVolume_[trackIndex] : track.volume;
    const float pan = (pilotes & kAutoPan) ? autoPan_[trackIndex] : track.pan;

    float peak = mixStereoInto(srcL, srcR, sampleCount,
                                volume, pan, audible,
                                destL + sampleStart, destR + sampleStart);
    blockPeak_[trackIndex] = std::max(blockPeak_[trackIndex], peak);

    // RMS ET CORRÉLATION (D4.7) : on accumule les sommes ici et on ne
    // conclut qu'en fin de bloc. Le signal mesuré est celui d'APRÈS le
    // fader et AVANT le panoramique, c'est-à-dire ce que la piste envoie --
    // la même convention que la crête, pour que les trois chiffres parlent
    // du même son.
    for (int i = 0; i < sampleCount; ++i) {
        const double l = static_cast<double>(srcL[static_cast<size_t>(i)]) * volume;
        const double r = static_cast<double>(srcR[static_cast<size_t>(i)]) * volume;
        blockSumL2_[trackIndex] += l * l;
        blockSumR2_[trackIndex] += r * r;
        blockSumLR_[trackIndex] += l * r;
    }
    blockCount_[trackIndex] += sampleCount;

    // Sends post-fader vers les bus auxiliaires (section 15).
    const size_t actifs = activeSends_.load(std::memory_order_acquire);
    const uint32_t preFader = preFaderMask_.load(std::memory_order_acquire);
    if (audible) {
        for (size_t b = 0; b < actifs; ++b) {
            // PRÉ-FADER : le départ prélève AVANT le fader, donc le volume
            // de la piste ne le multiplie pas. C'est ce qui permet
            // d'envoyer une piste dans un effet sans l'entendre en direct.
            //
            // Le fader employé ici est celui de l'AUTOMATION quand elle le
            // pilote : un fondu écrit en automation doit emporter les
            // départs post-fader avec lui, comme le ferait la main sur le
            // fader.
            const float apresFader = (preFader & (1u << b)) ? 1.0f : volume;
            const uint16_t bitDepart = static_cast<uint16_t>(1u << (kAutoSendFirst + b));
            const float niveau = (pilotes & bitDepart) ? autoSend_[trackIndex][b]
                                                        : track.sendLevel(b);
            const float lvl = niveau * apresFader;
            if (lvl <= 0.0f) continue;
            for (int i = 0; i < sampleCount; ++i) {
                sendL_[b][static_cast<size_t>(sampleStart + i)] += srcL[static_cast<size_t>(i)] * lvl;
                sendR_[b][static_cast<size_t>(sampleStart + i)] += srcR[static_cast<size_t>(i)] * lvl;
            }
        }
    }
}

void ProcessGraph::renderTrackRange(const GraphSnapshot& snapshot, bool anySolo,
                                    int sampleStart, int sampleCount, double rangeStartSeconds,
                                    float* outputL, float* outputR, bool includeScheduledEvents) {
    const Project& project = snapshot.project;

    // L'ORDRE DE RENDU : le naturel, sauf si une chaîne latérale l'impose (voir
    // `refreshRenderOrder`). Une piste écoutée doit avoir versé dans son bus
    // avant que le compresseur qui l'écoute ne travaille.
    auto ordre = renderOrder_.load(std::memory_order_acquire);
    // CHARGÉ UNE SEULE FOIS : c'est un `atomic<shared_ptr>` unique, que tous
    // les cœurs se disputeraient s'ils le lisaient chacun de leur côté.
    auto compensation = compensation_.load(std::memory_order_acquire);
    const size_t combien = ordre ? ordre->size()
                                 : std::min(project.tracks.size(), kMaxTracks);

    // Signale au thread UI qu'un rendu est en cours (voir `parallelAllowed_`) :
    // il ne détruira pas les threads sous nos pieds tant que ce compteur n'est
    // pas retombé à zéro.
    renderBusy_.fetch_add(1, std::memory_order_seq_cst);
    struct SortieDeRendu {
        std::atomic<int>& compteur;
        ~SortieDeRendu() { compteur.fetch_sub(1, std::memory_order_seq_cst); }
    } sortie{renderBusy_};

    // TROIS CONDITIONS, ET CHACUNE DIT NON À UN CAS PRÉCIS : pas de chaîne
    // latérale (le calcul d'une piste dépendrait du mélange d'une autre), assez
    // d'échantillons pour que la ronde se rembourse, assez de pistes pour qu'il
    // y ait quelque chose à répartir.
    const bool parallele = parallelAllowed_.load(std::memory_order_seq_cst)
                           && !sidechainActive_.load(std::memory_order_acquire)
                           && sampleCount >= kMinParallelSamples
                           && combien >= kMinParallelTracks
                           && parallelL_.size() >= kMaxTracks
                           && !parallelEvents_.empty();

    if (parallele) {
        VoiceBatch lot;
        lot.self = this;
        lot.snapshot = &snapshot;
        lot.order = ordre.get();
        lot.compensation = compensation.get();
        lot.sampleStart = sampleStart;
        lot.sampleCount = sampleCount;
        lot.rangeStartSeconds = rangeStartSeconds;
        lot.includeScheduledEvents = includeScheduledEvents;
        parallelSpans_.fetch_add(1, std::memory_order_relaxed);
        renderPool_.runParallel(&ProcessGraph::renderVoiceJob, &lot, combien);

        // ET LE MIXAGE, LUI, DANS L'ORDRE : c'est ce qui rend le résultat
        // identique au bit près à celui du chemin mono-cœur.
        for (size_t rang = 0; rang < combien; ++rang) {
            const size_t trackIndex = ordre ? (*ordre)[rang] : rang;
            if (trackIndex >= kMaxTracks || !parallelActive_[trackIndex]) continue;
            mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                         parallelL_[trackIndex].data(), parallelR_[trackIndex].data(),
                         outputL, outputR);
        }
        return;
    }

    for (size_t rang = 0; rang < combien; ++rang) {
        const size_t trackIndex = ordre ? (*ordre)[rang] : rang;
        if (!renderTrackVoice(snapshot, trackIndex, sampleStart, sampleCount, rangeStartSeconds,
                              includeScheduledEvents, compensation.get(), scratchEvents_.data(),
                              scratchStereoL_.data(), scratchStereoR_.data()))
            continue;
        mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                     scratchStereoL_.data(), scratchStereoR_.data(), outputL, outputR);
    }
}

} // namespace vsm::audio::engine
