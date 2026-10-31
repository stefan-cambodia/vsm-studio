#include "vsm/audio/engine/ProcessGraph.h"
#include <chrono>
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/Diapason.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace vsm::audio::engine {

/// Plage du pitch bend, en demi-tons pour l'excursion maximale. Deux demi-tons
/// est la convention par défaut depuis la General MIDI ; une machine qui
/// voudrait autre chose le fera dans son `handleControlEvent`, pas ici.
// D331 : la plage de pli n'est plus une constante (± 2) mais une tenue par piste, `bendRange_`, lue au RPN 0.

using namespace vsm::audio::plugin;
using namespace vsm::sequencer;
using namespace vsm::midi;

void ProcessGraph::prepare(double sampleRate, int maxBlockSize) {
    ccVolume_.fill(1.0f);   // D329 : `{}` initialise à ZÉRO -- un tableau de volumes à zéro rendrait tout muet
    ccPan_.fill(0.0f);      // D330
    rpnMsb_.fill(127); rpnLsb_.fill(127); bendRange_.fill(2.0f);   // D331
    // D599.3 : un décalage Trim au repos est un facteur 1 — `{}` initialise à ZÉRO, et rendrait muette toute courbe.
    for (auto& f : trimVolume_) f.store(1.0f, std::memory_order_relaxed);
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : 512;
    for (auto& pre : preSections_) pre.setSampleRate(sampleRate_);   // D593

    scratchMonoL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    scratchStereoL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    scratchStereoR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    widthL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);   // D573.1
    widthR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    apresL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);   // D576.1
    apresR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    largeurPreL_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    largeurPreR_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    for (size_t b = 0; b < kMaxSends; ++b) {
        sendL_[b].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        sendR_[b].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    }
    for (size_t g = 0; g < kMaxGroups; ++g) {
        groupL_[g].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
        groupR_[g].assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    }
    scratchEvents_.assign(static_cast<size_t>(kMaxEventsPerBlock), MidiNoteEvent{});
    // D18.7b : LES TAMPONS ANNEXES, alloués ici et jamais dans le rappel
    // audio. Ils coûtent kMaxExtraOutputs paires de maxBlockSize flottants --
    // un demi-mégaoctet à 1024 échantillons -- que le graphe paie qu'on
    // publie ou non ; c'est le prix de ne jamais allouer sous le temps réel.
    extraOutL_.assign(kMaxExtraOutputs, std::vector<float>(static_cast<size_t>(maxBlockSize_), 0.0f));
    extraOutR_.assign(kMaxExtraOutputs, std::vector<float>(static_cast<size_t>(maxBlockSize_), 0.0f));
    meters_.resetAll();
    masterBus_.prepare(sampleRate_, maxBlockSize_);
    referenceTrack_.prepare(sampleRate_);
    auditionPlayer_.prepare(sampleRate_);
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
    // H42 : LE DIAPASON DU PROJET, posé sur le moteur AVANT que les machines de ce
    // projet ne soient montées (le rendu hors ligne appelle `setProject` d'abord) ;
    // celles qui tiennent une table (diviseur, roues phoniques) se réaccordent au
    // bloc suivant quand il change en cours de séance.
    setDiapason(static_cast<float>(project.referenceA4Hz));
    ccVolume_.fill(1.0f);   // D329 : un projet republié repart au volume de canal plein ; la chasse (D16.2) le rétablit à la lecture
    ccPan_.fill(0.0f);      // D330
    rpnMsb_.fill(127); rpnLsb_.fill(127); bendRange_.fill(2.0f);   // D331
    auto snapshot = std::make_shared<GraphSnapshot>();
    snapshot->project = project;
    // D540 : JUSQU'À LA FIN DE CE QUE LE PROJET FAIT ENTENDRE, et non à celle de son matériau : une
    // copie liée posée au-delà, ou la fin d'un morceau où l'on a inséré du temps, tombait hors du
    // planning et ne sonnait ni à la lecture ni à l'export.
    Tick endTick = project.lastSoundingTick() + project.ticksPerQuarterNote;
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
    refreshLoopChase();      // D585.3 : ce qui traverse le début de la boucle a pu changer
    refreshPlayChain();      // D606.1 : les éléments de la chaîne, en secondes, et ce qui traverse leurs débuts
    refreshRenderOrder();    // les niveaux d'envoi ont pu changer
    refreshOutputRouting();  // et la publication des sorties : APRÈS l'ordre,
                             // dont elle dérive ses deux vagues
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

float ProcessGraph::instrumentParameter(size_t trackIndex, ParamId paramId) const {
    if (trackIndex >= kMaxTracks) return 0.0f;
    auto instrument = instruments_[trackIndex].load(std::memory_order_acquire);   // D521
    return instrument ? instrument->getParameter(paramId) : 0.0f;
}

void ProcessGraph::addAutomationLane(AutomationLane lane) {
    if (!lane.read) return;   // D600.2 : la lecture de sa piste est éteinte (voir `setAutomationLanes`)
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
    // D600.2 — LA LECTURE ÉTEINTE (le *R*) : une courbe marquée n'est pas jouée. Écartée ICI, à la publication, et nulle
    // part ailleurs : l'application, l'export et `vsm-render` passent tous par ce point. La liste de l'appelant garde la
    // courbe (elle refait le projet depuis elle) ; le moteur n'en voit rien, et une piste dont toutes les courbes sont
    // écartées rend comme une piste sans courbe, au bit près.
    lanes.erase(std::remove_if(lanes.begin(), lanes.end(), [](const AutomationLane& l) { return !l.read; }),
                lanes.end());
    automationLanes_.store(std::make_shared<std::vector<AutomationLane>>(std::move(lanes)),
                            std::memory_order_release);
    refreshAutomationMask();
}

void ProcessGraph::setHandOverride(size_t trackIndex, uint16_t masque) {
    if (trackIndex >= kMaxTracks) return;
    // D600.1 : TOUS LES BITS DES COURBES DE MIXAGE — le volume, le panoramique, les huit départs, le trim.
    constexpr uint16_t tous = static_cast<uint16_t>((1u << (kAutoSendFirst + kMaxSends + 1)) - 1u);
    static_assert(tous == (handVolume | handPan | 0x3FCu | handTrim) && handTrim == kAutoTrim);
    handMask_[trackIndex].store(static_cast<uint16_t>(masque & tous), std::memory_order_relaxed);
}

void ProcessGraph::setTrimOffset(size_t trackIndex, float facteurVolume, float decalagePan) {
    if (trackIndex >= kMaxTracks) return;
    trimVolume_[trackIndex].store(std::max(0.0f, facteurVolume), std::memory_order_relaxed);
    trimPan_[trackIndex].store(decalagePan, std::memory_order_relaxed);
}

float ProcessGraph::playedVolume(size_t trackIndex) const {
    return trackIndex < kMaxTracks ? playedVolume_[trackIndex].load(std::memory_order_relaxed) : 0.0f;
}

float ProcessGraph::playedPan(size_t trackIndex) const {
    return trackIndex < kMaxTracks ? playedPan_[trackIndex].load(std::memory_order_relaxed) : 0.0f;
}

float ProcessGraph::playedSend(size_t trackIndex, size_t bus) const {
    return trackIndex < kMaxTracks && bus < kMaxSends ? playedSend_[trackIndex][bus].load(std::memory_order_relaxed)
                                                      : 0.0f;
}

float ProcessGraph::playedTrim(size_t trackIndex) const {
    return trackIndex < kMaxTracks ? playedTrim_[trackIndex].load(std::memory_order_relaxed) : 0.0f;
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
                case AutomationTarget::TrackTrim:
                    masque[lane.targetTrackIndex] |= kAutoTrim; break;
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
    refreshOutputRouting();
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
            if ((ecoutes & (1u << b)) && project.tracks[t].audibleSendLevel(b) > 0.0f) alimente = true;   // D567.3
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

void ProcessGraph::setPlaybackSpeed(double facteur) {
    // Bornes : en dessous d'un quart, le noyau travaille sur des fenêtres si
    // larges qu'il coûte plus que le rendu ; au-delà de quatre, on saute plus
    // d'échantillons qu'on n'en lit. Borner est plus honnête que subir.
    const double borne = facteur < 0.25 ? 0.25 : (facteur > 4.0 ? 4.0 : facteur);
    // LA TABLE D'ABORD, LA VITESSE ENSUITE : le rendu qui verra la nouvelle
    // vitesse doit trouver le noyau déjà prêt pour elle. L'ordre inverse
    // laisserait un bloc lire une table calculée pour l'ancien rapport.
    if (borne != 1.0) speedKernel_.prepare(borne);
    playbackSpeed_.store(borne, std::memory_order_release);
}

void ProcessGraph::refreshOutputRouting() {
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (!snapshot) { outputRouting_.store(nullptr, std::memory_order_release); return; }
    const auto& project = snapshot->project;
    const size_t pistes = std::min(project.tracks.size(), kMaxTracks);

    // Y A-T-IL SEULEMENT UNE PISTE QUI PUBLIE ? Tant que non, on ne publie
    // AUCUNE table -- et le rendu emprunte alors exactement le chemin qu'il
    // avait avant que cette étape existe, au bit près.
    bool aucune = true;
    for (size_t t = 0; t < pistes; ++t)
        if (project.tracks[t].publishesInstrumentOutput()) { aucune = false; break; }
    if (aucune) { outputRouting_.store(nullptr, std::memory_order_release); return; }

    auto table = std::make_shared<OutputRouting>();
    for (auto& ligne : table->slotOf) ligne.fill(-1);
    table->readSlot.fill(-1);

    // PREMIÈRE PASSE : QUI RÉCLAME QUOI, sans encore rien allouer.
    //
    // CE QU'ON REFUSE, ET POURQUOI CHACUN. Une source hors du projet ou égale
    // à soi-même ne désigne rien ; une source qui publie elle-même ferait une
    // chaîne dont l'ordre de rendu n'a pas de fin ; la sortie 0 est déjà celle
    // de la piste source, la republier la jouerait deux fois.
    std::array<int, kMaxTracks> plusHauteSortie{};
    plusHauteSortie.fill(0);
    uint64_t refusees = 0;
    for (size_t t = 0; t < pistes; ++t) {
        const auto& piste = project.tracks[t];
        if (!piste.publishesInstrumentOutput()) continue;
        const int source = piste.outputSourceTrack;
        const int sortie = piste.outputIndex;
        const bool valide =
            source >= 0 && static_cast<size_t>(source) < pistes
            && static_cast<size_t>(source) != t
            && !project.tracks[static_cast<size_t>(source)].publishesInstrumentOutput()
            && sortie >= 1 && sortie < kMaxInstrumentOutputs;
        if (!valide) { ++refusees; continue; }
        int& haute = plusHauteSortie[static_cast<size_t>(source)];
        haute = std::max(haute, sortie);
    }

    // SECONDE PASSE : L'ALLOCATION, PAR SOURCE ET TOUT OU RIEN.
    //
    // Une machine à qui l'on demande n sorties les REMPLIT TOUTES : le contrat
    // de `processMultiOut` ne connaît pas les trous. Il faut donc un tampon
    // pour CHAQUE sortie de 1 à la plus haute réclamée, y compris celles que
    // personne ne lit -- faute de quoi deux machines rendues en même temps
    // écriraient dans le même tampon de rebut, et ce serait une course.
    //
    // Tout ou rien, parce qu'une source à moitié allouée est précisément le
    // cas où ce tampon partagé réapparaîtrait.
    size_t prochainSlot = 0;
    for (size_t sce = 0; sce < pistes; ++sce) {
        const int haute = plusHauteSortie[sce];
        if (haute < 1) continue;
        const size_t besoin = static_cast<size_t>(haute);   // les sorties 1..haute
        if (prochainSlot + besoin > kMaxExtraOutputs) {
            // Plus de tampons : cette source ne publiera rien, et chacune des
            // pistes qui l'attendaient est comptée.
            for (size_t t = 0; t < pistes; ++t) {
                const auto& p = project.tracks[t];
                if (p.publishesInstrumentOutput()
                    && p.outputSourceTrack == static_cast<int>(sce))
                    ++refusees;
            }
            continue;
        }
        for (int k = 1; k <= haute; ++k)
            table->slotOf[sce][static_cast<size_t>(k)] = static_cast<int16_t>(prochainSlot++);
        table->outputsToRender[sce] = haute + 1;
    }
    table->usedSlots = prochainSlot;

    // TROISIÈME PASSE : QUI LIT QUOI. Deux pistes qui réclament la même sortie
    // la PARTAGENT -- elles lisent le même tampon, la machine ne la rend
    // qu'une fois.
    for (size_t t = 0; t < pistes; ++t) {
        const auto& piste = project.tracks[t];
        if (!piste.publishesInstrumentOutput()) continue;
        const int source = piste.outputSourceTrack;
        const int sortie = piste.outputIndex;
        if (source < 0 || static_cast<size_t>(source) >= pistes) continue;
        if (sortie < 1 || sortie >= kMaxInstrumentOutputs) continue;
        table->readSlot[t] = table->slotOf[static_cast<size_t>(source)][static_cast<size_t>(sortie)];
    }
    if (refusees > 0) droppedInstrumentOutputs_.fetch_add(refusees, std::memory_order_relaxed);

    // LES DEUX VAGUES. Elles suivent l'ordre de rendu quand il y en a un, et
    // l'ordre naturel sinon : on ne réordonne pas pour le plaisir.
    auto ordre = renderOrder_.load(std::memory_order_acquire);
    table->firstWave.reserve(pistes);
    table->secondWave.reserve(pistes);
    for (size_t rang = 0; rang < pistes; ++rang) {
        const size_t t = ordre && rang < ordre->size() ? (*ordre)[rang] : rang;
        if (t >= kMaxTracks) continue;
        (table->readSlot[t] >= 0 ? table->secondWave : table->firstWave).push_back(t);
    }
    outputRouting_.store(std::move(table), std::memory_order_release);
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
    refreshLoopChase();   // D585.3
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
    // D606.1 : EN LECTURE, le déplacement désigne le premier élément de la chaîne qui contient la position — aucun : la
    // lecture QUITTE l'ordre et file droit ; À L'ARRÊT, aucun : le départ choisira (`startPlayChain`).
    if (auto chaine = playChain_.load(std::memory_order_acquire))
        chainIndex_.store(playing_.load(std::memory_order_acquire) ? elementContenant(*chaine, seconds) : -1,
                          std::memory_order_release);
    // Un déplacement de la tête de lecture recommence le compte des passes :
    // ce qui précède appartient à un autre enregistrement.
    loopWrapCount_.store(0, std::memory_order_release);
    // D585.3 : EN LECTURE, LES NOTES QUE LE DÉPLACEMENT PREND EN ROUTE sont relancées ; à l'arrêt, elles le seront au
    // départ (`setPlaying`) — poser la tête au milieu d'une nappe ne doit pas la faire sonner.
    if (playing_.load(std::memory_order_acquire)) queueChasedNotes(seconds);

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
    for (const auto& chasse : controlesChasses(project, tick))   // D611.1 : la conversion unique
        if (!chaseQueue_.push(chasse)) ++perdus;
    if (perdus > 0) droppedChasedControls_.fetch_add(perdus, std::memory_order_relaxed);
}

std::vector<ProcessGraph::ChasedControlEvent> ProcessGraph::controlesChasses(const vsm::sequencer::Project& project,
                                                                            vsm::sequencer::Tick tick) {
    std::vector<ChasedControlEvent> resultat;
    if (tick <= 0 || project.tracks.empty()) return resultat;
    for (const auto& ev : vsm::sequencer::PlaybackScheduler::chaseAt(project, tick)) {
        ChasedControlEvent chasse;
        chasse.trackIndex = static_cast<uint32_t>(ev.trackIndex);
        bool utile = std::visit([&chasse](auto&& data) {
            using T = std::decay_t<decltype(data)>;
            using namespace vsm::midi;
            if constexpr (std::is_same_v<T, PitchBendEvent>) {
                chasse.event.kind = MidiControlEvent::Kind::PitchBend;
                chasse.event.channel = data.channel;
                chasse.event.value = static_cast<float>(data.value) / 8192.0f;   // D331 : BRUT, converti à la livraison avec la plage de la piste
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
        resultat.push_back(chasse);
    }
    return resultat;
}

void ProcessGraph::setTrackGmController(size_t trackIndex, uint8_t controller, vsm::audio::plugin::ParamId param,
                                        float minimum, float maximum, bool logarithmique) {
    if (trackIndex >= kMaxTracks) return;
    // D332 : la case libre, ou celle du même contrôleur. Écrite hors du fil
    // audio : le drapeau tombe, les champs s'écrivent, le drapeau remonte.
    auto& cases = gmControllers_[trackIndex];
    size_t cible = kGmControllersPerTrack;
    for (size_t i = 0; i < kGmControllersPerTrack; ++i)
        if (cases[i].valide.load(std::memory_order_acquire) && cases[i].controller == controller) { cible = i; break; }
    if (cible == kGmControllersPerTrack)
        for (size_t i = 0; i < kGmControllersPerTrack; ++i)
            if (!cases[i].valide.load(std::memory_order_acquire)) { cible = i; break; }
    if (cible == kGmControllersPerTrack) return;
    auto& gm = cases[cible];
    gm.valide.store(false, std::memory_order_release);
    gm.controller = controller; gm.param = param; gm.minimum = minimum; gm.maximum = maximum; gm.logarithmique = logarithmique;
    gm.valide.store(true, std::memory_order_release);
}

void ProcessGraph::clearTrackGmControllers(size_t trackIndex) {
    if (trackIndex >= kMaxTracks) return;
    for (auto& gm : gmControllers_[trackIndex]) gm.valide.store(false, std::memory_order_release);
}

void ProcessGraph::appliquerLesTenues(size_t trackIndex, vsm::audio::plugin::MidiControlEvent& e,
                                      vsm::audio::plugin::ISynthPlugin* instrument) {
    using Kind = vsm::audio::plugin::MidiControlEvent::Kind;
    if (trackIndex >= kMaxTracks) return;
    if (e.kind == Kind::PitchBend) {
        e.value *= bendRange_[trackIndex];   // brut (-1..1) -> demi-tons, avec la plage de LA piste
        return;
    }
    if (e.kind != Kind::ControlChange) return;
    const int brut = std::clamp(static_cast<int>(std::lround(e.value * 127.0f)), 0, 127);
    switch (e.index) {
        case 7:  ccVolume_[trackIndex] = e.value; break;                    // D329
        case 10: ccPan_[trackIndex] = e.value * 2.0f - 1.0f; break;         // D330
        case 101: rpnMsb_[trackIndex] = static_cast<uint8_t>(brut); break;  // D331
        case 100: rpnLsb_[trackIndex] = static_cast<uint8_t>(brut); break;
        case 6:
            if (rpnMsb_[trackIndex] == 0 && rpnLsb_[trackIndex] == 0) bendRange_[trackIndex] = static_cast<float>(brut);
            break;
        case 38:
            if (rpnMsb_[trackIndex] == 0 && rpnLsb_[trackIndex] == 0)
                bendRange_[trackIndex] = std::floor(bendRange_[trackIndex]) + static_cast<float>(brut) / 100.0f;
            break;
        default: break;
    }
    // D332 : un contrôleur GM déclaré par l'application pilote un paramètre.
    if (instrument != nullptr)
        for (auto& gm : gmControllers_[trackIndex]) {
            if (!gm.valide.load(std::memory_order_acquire) || gm.controller != e.index) continue;
            const float t = std::clamp(e.value, 0.0f, 1.0f);
            const float v = (gm.logarithmique && gm.minimum > 0.0f && gm.maximum > gm.minimum)
                                ? gm.minimum * std::pow(gm.maximum / gm.minimum, t)
                                : gm.minimum + t * (gm.maximum - gm.minimum);
            instrument->setParameter(gm.param, v);
        }
}

void ProcessGraph::drainChasedControls() {
    drainedChaseCount_ = 0;
    ChasedControlEvent event;
    while (static_cast<size_t>(drainedChaseCount_) < kMaxChasedPerBlock && chaseQueue_.pop(event))
        drainedChase_[static_cast<size_t>(drainedChaseCount_++)] = event;
}


void ProcessGraph::setPlaying(bool playing) {
    // D585.3 : AU DÉPART, les notes commencées sont mises en file AVANT que le premier bloc joué ne les cherche.
    const bool jouait = playing_.load(std::memory_order_acquire);
    if (playing && !jouait) queueChasedNotes(currentSeconds_.load(std::memory_order_acquire));
    playing_.store(playing, std::memory_order_release);
    // D605.2 : L'ARRÊT RELÂCHE CE QUE LA LECTURE TENAIT. À l'arrêt, le planning n'est plus rendu : le NoteOff écrit
    // n'arrivait jamais, et une note commencée sonnait jusqu'au départ suivant (le banc de l'accordeur l'a entendue,
    // −25,8 dBFS 1,3 s après Stop). Posé APRÈS `playing_` : le bloc qui consomme la demande se voit déjà arrêté.
    if (!playing && jouait) stopReleaseRequested_.store(true, std::memory_order_release);
}

void ProcessGraph::setChaseNotes(bool oui) {
    chaseNotes_.store(oui, std::memory_order_release);
    chaseNoteGeneration_.fetch_add(1, std::memory_order_acq_rel);   // ce qui était en file ne sonne plus
}

void ProcessGraph::queueChasedNotes(double seconds) {
    // LA GÉNÉRATION AVANCE TOUJOURS, même sans rien à chasser : un départ à zéro, ou la chasse décochée, périment ce
    // qu'un départ précédent avait mis en file.
    const uint32_t generation = chaseNoteGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (!chaseNotes_.load(std::memory_order_acquire)) return;
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (!snapshot || snapshot->project.tracks.empty()) return;
    const auto tick = snapshot->project.secondsToTicks(seconds);
    if (tick <= 0) return;
    uint64_t perdus = 0;
    for (const auto& ev : vsm::sequencer::PlaybackScheduler::chaseNotesAt(snapshot->project, tick)) {
        const auto* on = std::get_if<NoteOnEvent>(&ev.data);
        if (on == nullptr || ev.trackIndex >= kMaxTracks) continue;
        ChasedNoteEvent note;
        note.trackIndex = static_cast<uint32_t>(ev.trackIndex);
        note.channel = on->channel;
        note.note = on->note;
        note.velocity = on->velocity;
        note.generation = generation;
        if (!chaseNoteQueue_.push(note)) ++perdus;
    }
    if (perdus > 0) droppedChasedNotes_.fetch_add(perdus, std::memory_order_relaxed);
}

void ProcessGraph::drainChasedNotes() {
    drainedChaseNoteCount_ = 0;
    const uint32_t generation = chaseNoteGeneration_.load(std::memory_order_acquire);
    ChasedNoteEvent note;
    while (static_cast<size_t>(drainedChaseNoteCount_) < kMaxChasedNotesPerBlock && chaseNoteQueue_.pop(note))
        if (note.generation == generation) drainedChaseNotes_[static_cast<size_t>(drainedChaseNoteCount_++)] = note;
}

int ProcessGraph::elementContenant(const PlayChain& chaine, double seconds) {
    for (size_t i = 0; i < chaine.elements.size(); ++i)
        if (seconds >= chaine.elements[i].debut && seconds < chaine.elements[i].fin) return static_cast<int>(i);
    return -1;
}

void ProcessGraph::setPlayChain(std::vector<std::pair<Tick, Tick>> elements) {
    chainTicks_ = std::move(elements);
    refreshPlayChain();
    auto chaine = playChain_.load(std::memory_order_acquire);
    chainIndex_.store(chaine && playing_.load(std::memory_order_acquire)
                          ? elementContenant(*chaine, currentSeconds_.load(std::memory_order_acquire)) : -1,
                      std::memory_order_release);
}

bool ProcessGraph::hasPlayChain() const {
    auto chaine = playChain_.load(std::memory_order_acquire);
    return chaine && !chaine->elements.empty();
}

int ProcessGraph::playChainSize() const {
    auto chaine = playChain_.load(std::memory_order_acquire);
    return chaine ? static_cast<int>(chaine->elements.size()) : 0;
}

void ProcessGraph::startPlayChain() {
    auto chaine = playChain_.load(std::memory_order_acquire);
    if (!chaine || chaine->elements.empty() || recording_.load(std::memory_order_relaxed)) return;
    const int i = chainIndex_.load(std::memory_order_acquire);
    const double t = currentSeconds_.load(std::memory_order_acquire);
    if (i >= 0 && i < static_cast<int>(chaine->elements.size()) && t >= chaine->elements[static_cast<size_t>(i)].debut
        && t < chaine->elements[static_cast<size_t>(i)].fin)
        return;   // la tête est dans l'élément courant : la lecture y reprend
    seekSeconds(chaine->elements.front().debut);
    chainIndex_.store(0, std::memory_order_release);
}

void ProcessGraph::refreshPlayChain() {
    // HORS DU FIL AUDIO, comme la chasse de la boucle : au saut, le fil audio ne fait que lire ces listes.
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (chainTicks_.empty() || !snapshot) {
        playChain_.store(nullptr, std::memory_order_release);
        chainIndex_.store(-1, std::memory_order_release);
        return;
    }
    const auto& projet = snapshot->project;
    auto chaine = std::make_shared<PlayChain>();
    for (size_t i = 0; i < chainTicks_.size(); ++i) {
        const auto [debut, fin] = chainTicks_[i];
        PlayChain::Element e;
        e.contigu = i > 0 && chainTicks_[i - 1].second == debut;
        e.debut = projet.ticksToSeconds(debut);
        e.fin = projet.ticksToSeconds(std::max(debut, fin));
        if (!projet.tracks.empty())
            for (const auto& ev : vsm::sequencer::PlaybackScheduler::chaseNotesAt(projet, debut)) {
                const auto* on = std::get_if<NoteOnEvent>(&ev.data);
                if (on == nullptr || ev.trackIndex >= kMaxTracks) continue;
                ChasedNoteEvent note;
                note.trackIndex = static_cast<uint32_t>(ev.trackIndex);
                note.channel = on->channel;
                note.note = on->note;
                note.velocity = on->velocity;
                e.relances.push_back(note);
            }
        e.controles = controlesChasses(projet, debut);   // D611.1
        chaine->elements.push_back(std::move(e));
    }
    playChain_.store(std::move(chaine), std::memory_order_release);
}

void ProcessGraph::refreshLoopChase() {
    // HORS DU FIL AUDIO, comme la chasse d'un déplacement : au rebouclage, le fil audio ne fait que lire cette liste.
    auto boucle = std::make_shared<LoopChase>();
    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (snapshot && !snapshot->project.tracks.empty()) {
        const auto tick = snapshot->project.secondsToTicks(loopStartSeconds_.load(std::memory_order_acquire));
        for (const auto& ev : vsm::sequencer::PlaybackScheduler::chaseNotesAt(snapshot->project, tick)) {
            const auto* on = std::get_if<NoteOnEvent>(&ev.data);
            if (on == nullptr || ev.trackIndex >= kMaxTracks) continue;
            ChasedNoteEvent note;
            note.trackIndex = static_cast<uint32_t>(ev.trackIndex);
            note.channel = on->channel;
            note.note = on->note;
            note.velocity = on->velocity;
            boucle->relances.push_back(note);
        }
        boucle->controles = controlesChasses(snapshot->project, tick);   // D611.1
    }
    loopChase_.store(std::move(boucle), std::memory_order_release);
}

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

bool ProcessGraph::sendLiveControl(LiveNoteSource source, size_t trackIndex,
                                    const vsm::audio::plugin::MidiControlEvent& event) {
    if (trackIndex >= kMaxTracks) return false;
    const size_t queueIndex = static_cast<size_t>(source);
    if (queueIndex >= kNumLiveSources) return false;
    LiveControlEvent live;
    live.trackIndex = static_cast<uint32_t>(trackIndex);
    live.event = event;
    live.event.sampleOffset = 0;
    return liveControlQueues_[queueIndex].push(live);
}

void ProcessGraph::drainLiveControls() {
    drainedLiveControlCount_ = 0;
    for (auto& queue : liveControlQueues_) {
        LiveControlEvent event;
        while (static_cast<size_t>(drainedLiveControlCount_) < kMaxLiveEventsPerBlock && queue.pop(event))
            drainedLiveControls_[static_cast<size_t>(drainedLiveControlCount_++)] = event;
    }
}

void ProcessGraph::emitMidiOut(size_t trackIndex, int sampleOffset, uint8_t status, uint8_t data1, uint8_t data2) {
    MidiOutEvent e;
    e.trackIndex = static_cast<uint32_t>(trackIndex);
    e.hostSeconds = blockHostSeconds_ + static_cast<double>(std::max(0, sampleOffset)) / sampleRate_;
    e.status = status;
    e.data1 = static_cast<uint8_t>(data1 & 0x7F);
    e.data2 = static_cast<uint8_t>(data2 & 0x7F);
    if (trackIndex >= kMaxTracks || !midiOutQueues_[trackIndex].push(e)) droppedMidiOut_.fetch_add(1, std::memory_order_relaxed);
}

void ProcessGraph::emitSystem(int sampleOffset, uint8_t status, uint8_t data1, uint8_t data2) {
    MidiOutEvent e;
    e.trackIndex = 0xFFFFFFFFu;
    e.hostSeconds = blockHostSeconds_ + static_cast<double>(std::max(0, sampleOffset)) / sampleRate_;
    e.status = status;
    e.data1 = data1;
    e.data2 = data2;
    if (!midiOutSystemQueue_.push(e)) droppedMidiOut_.fetch_add(1, std::memory_order_relaxed);
}

void ProcessGraph::emitMidiClockAndTransport(const Project& project, double blockStartSeconds,
                                             double blockDurationSeconds, bool playing) {
    // D28.1 : rien de tout cela sans port employé -- un projet sans matériel
    // ne fabrique pas de messages pour personne.
    bool unPort = false;
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t)
        if (!project.tracks[t].midiOutputDevice.empty()) { unPort = true; break; }
    if (!unPort) { wasPlayingForMidiOut_ = playing; return; }

    const double vitesse = playbackSpeed_.load(std::memory_order_acquire);
    const auto ppq = static_cast<double>(std::max<int>(1, project.ticksPerQuarterNote));
    if (playing && !wasPlayingForMidiOut_) {
        if (blockStartSeconds <= 1.0e-9) {
            emitSystem(0, 0xFA);   // Start : du début
        } else {
            // Song Position en doubles-croches (14 bits, LSB d'abord), puis Continue.
            const double tick = static_cast<double>(project.secondsToTicks(blockStartSeconds));
            const int position = std::clamp(static_cast<int>(std::floor(tick / (ppq / 4.0))), 0, 16383);
            emitSystem(0, 0xF2, static_cast<uint8_t>(position & 0x7F), static_cast<uint8_t>((position >> 7) & 0x7F));
            emitSystem(0, 0xFB);
        }
        // D28.2 : le programme de chaque piste repart avec la lecture.
        lastProgramSent_.fill(-2);
    }
    if (!playing && wasPlayingForMidiOut_) emitSystem(0, 0xFC);
    wasPlayingForMidiOut_ = playing;

    // D28.2 : LE PROGRAMME ET LA BANQUE, dès qu'ils changent.
    for (size_t t = 0; t < project.tracks.size() && t < kMaxTracks; ++t) {
        const Track& track = project.tracks[t];
        if (track.midiOutputDevice.empty() || track.midiProgram < 0) continue;
        const int cle = (std::max(-1, track.midiBank) + 1) * 128 + std::clamp(track.midiProgram, 0, 127);
        if (lastProgramSent_[t] == cle) continue;
        lastProgramSent_[t] = cle;
        const uint8_t canal = static_cast<uint8_t>(track.channel & 0x0F);
        if (track.midiBank >= 0) {
            emitMidiOut(t, 0, static_cast<uint8_t>(0xB0 | canal), 0, static_cast<uint8_t>((track.midiBank >> 7) & 0x7F));
            emitMidiOut(t, 0, static_cast<uint8_t>(0xB0 | canal), 32, static_cast<uint8_t>(track.midiBank & 0x7F));
        }
        emitMidiOut(t, 0, static_cast<uint8_t>(0xC0 | canal), static_cast<uint8_t>(std::clamp(track.midiProgram, 0, 127)), 0);
    }

    if (!playing || blockDurationSeconds <= 0.0) return;
    // L'HORLOGE : 24 impulsions par noire, aux ticks multiples de ppq/24 dans
    // [début, fin) du bloc, chacune datée à l'échantillon par la carte de tempo.
    const double parImpulsion = ppq / 24.0;
    const double tickDebut = static_cast<double>(project.secondsToTicks(blockStartSeconds));
    const double tickFin = static_cast<double>(project.secondsToTicks(blockStartSeconds + blockDurationSeconds));
    if (tickFin <= tickDebut) return;
    long long k = static_cast<long long>(std::ceil(tickDebut / parImpulsion - 1.0e-9));
    for (; static_cast<double>(k) * parImpulsion < tickFin; ++k) {
        const auto tick = static_cast<vsm::midi::Tick>(std::llround(static_cast<double>(k) * parImpulsion));
        const double secondes = project.ticksToSeconds(tick);
        const int offset = static_cast<int>(std::llround((secondes - blockStartSeconds) * sampleRate_ / vitesse));
        emitSystem(std::max(0, offset), 0xF8);
    }
}

void ProcessGraph::emitControlOut(size_t trackIndex, uint8_t channel,
                                   const vsm::audio::plugin::MidiControlEvent& event) {
    using K = vsm::audio::plugin::MidiControlEvent::Kind;
    // Le canal est celui de l'ÉVÉNEMENT (comme pour les notes), pas celui de la
    // piste : un fichier multi-canal ressort tel qu'il est écrit.
    (void)channel;
    const uint8_t canal = static_cast<uint8_t>(event.channel & 0x0F);
    auto a7 = [](float v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v * 127.0f)), 0, 127)); };
    switch (event.kind) {
        case K::PitchBend: {
            // Demi-tons vers 14 bits, l'inverse exact du planning (± 2 demi-tons).
            const int brut = std::clamp(static_cast<int>(std::lround(event.value / bendRange_[trackIndex] * 8192.0f)) + 8192, 0, 16383);   // D331
            emitMidiOut(trackIndex, event.sampleOffset, static_cast<uint8_t>(0xE0 | canal),
                        static_cast<uint8_t>(brut & 0x7F), static_cast<uint8_t>((brut >> 7) & 0x7F));
            break;
        }
        case K::ControlChange:
            emitMidiOut(trackIndex, event.sampleOffset, static_cast<uint8_t>(0xB0 | canal), event.index, a7(event.value));
            break;
        case K::ChannelPressure:
            emitMidiOut(trackIndex, event.sampleOffset, static_cast<uint8_t>(0xD0 | canal), a7(event.value), 0);
            break;
        case K::PolyPressure:
            emitMidiOut(trackIndex, event.sampleOffset, static_cast<uint8_t>(0xA0 | canal), event.index, a7(event.value));
            break;
        case K::ProgramChange:
            emitMidiOut(trackIndex, event.sampleOffset, static_cast<uint8_t>(0xC0 | canal), event.index, 0);
            break;
    }
}

bool ProcessGraph::interceptSustain(size_t trackIndex, uint8_t channel,
                                     const vsm::audio::plugin::MidiControlEvent& event,
                                     MidiNoteEvent* events, int& numEvents) {
    using vsm::audio::plugin::MidiControlEvent;
    if (event.kind != MidiControlEvent::Kind::ControlChange || event.index != 64) return false;
    const bool enfoncee = event.value >= 0.5f;
    sustainDown_[trackIndex] = enfoncee;
    if (enfoncee) return true;
    // RELÂCHÉE : tout ce qui était retenu part maintenant -- À L'ÉCHANTILLON DU CC (D554.3, H20). Il partait en tête
    // de bloc (`sampleOffset = 0`) : jusqu'à un bloc trop tôt, 102 échantillons mesurés sur un export de 512, et une
    // note tenue par la pédale ne sonnait pas comme la même note écrite jusqu'au relâchement. La chasse et les
    // contrôleurs en direct arrivent en tête de bloc (décalage 0) : pour eux, rien ne change.
    auto& retenus = heldNoteOffs_[trackIndex];
    for (size_t note = 0; note < retenus.size() && numEvents < kMaxEventsPerBlock; ++note) {
        if (!retenus[note]) continue;
        MidiNoteEvent off;
        off.kind = MidiNoteEvent::Kind::NoteOff;
        off.sampleOffset = event.sampleOffset;
        off.channel = channel;
        off.note = static_cast<uint8_t>(note);
        off.velocity = 64;
        events[static_cast<size_t>(numEvents++)] = off;
        marquerSonnante(trackIndex, static_cast<uint8_t>(note), false);   // D32.3
        retenus[note] = false;
    }
    return true;
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
    rendreLeBloc(outputL, outputR, numSamples);
    if (numSamples <= 0) return;
    // D589.3 : L'ÉCOUTE ATTÉNUÉE, APRÈS TOUT CE QUE LE BLOC A RENDU — le master, le clic, la référence, la pré-écoute.
    // Éteinte, aucun échantillon n'est touché : un export (son propre graphe, où elle est toujours éteinte) reste au bit
    // près. La crête de ce qui sort se relève dans les deux cas, avec celle du master de ce bloc-ci.
    const bool attenuee = dim();
    float crete = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        if (attenuee) {
            outputL[i] *= kDimGain;
            outputR[i] *= kDimGain;
        }
        crete = std::max(crete, std::max(std::abs(outputL[i]), std::abs(outputR[i])));
    }
    const uint64_t paire = (static_cast<uint64_t>(std::bit_cast<uint32_t>(crete)) << 32)
                           | std::bit_cast<uint32_t>(masterBus_.outputPeak());
    cretesDuBloc_.store(paire, std::memory_order_relaxed);
}

void ProcessGraph::rendreLeBloc(float* outputL, float* outputR, int numSamples) {
    vsm::audio::dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0) return;
    std::fill(outputL, outputL + numSamples, 0.0f);
    std::fill(outputR, outputR + numSamples, 0.0f);

    drainLiveNotes();
    drainChasedControls();
    drainedChaseNoteCount_ = 0;   // D585.3 : tirées des seuls blocs joués, plus bas
    drainLiveControls();                                                       // D24.1
    panicThisBlock_ = panicRequested_.exchange(false, std::memory_order_acq_rel);   // D24.4
    stopReleaseThisBlock_ = stopReleaseRequested_.exchange(false, std::memory_order_acq_rel);   // D605.2
    // D27.2 : l'heure du bloc sur l'horloge monotone, pour dater la sortie MIDI.
    // ANCRÉE SUR L'HORLOGE D'ÉCHANTILLONS : l'heure du bloc est l'ancre plus
    // les échantillons rendus depuis elle, jamais l'heure de l'appel -- deux
    // rappels espacés de 9 puis 12 ms feraient sinon se chevaucher leurs
    // événements. L'ancre ne se repose que si elle dérive de plus de 20 ms
    // (départ, décrochage, dérive d'horloge accumulée).
    {
        const double maintenant =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double attendu = hostAnchorSeconds_ + static_cast<double>(samplesSinceAnchor_) / sampleRate_;
        if (hostAnchorSeconds_ <= 0.0 || std::abs(maintenant - attendu) > 0.02) {
            hostAnchorSeconds_ = maintenant;
            samplesSinceAnchor_ = 0;
        }
        blockHostSeconds_ = hostAnchorSeconds_ + static_cast<double>(samplesSinceAnchor_) / sampleRate_;
        samplesSinceAnchor_ += static_cast<uint64_t>(numSamples);
    }
    // D28.1 / D28.2 : l'horloge, le transport et les programmes vers les
    // ports -- EN TÊTE DE BLOC, avant le chemin « au repos » qui sort tôt :
    // c'est là que le Stop d'un arrêt se voit. Un rebouclage à l'intérieur
    // du bloc décale ses impulsions d'au plus un bloc, et c'est dit.
    if (auto snapshotMidi = snapshot_.load(std::memory_order_acquire); snapshotMidi && !snapshotMidi->project.tracks.empty()) {
        const double vitesseMidi = playbackSpeed_.load(std::memory_order_acquire);
        emitMidiClockAndTransport(snapshotMidi->project, currentSeconds_.load(std::memory_order_acquire),
                                  static_cast<double>(numSamples) * vitesseMidi / sampleRate_,
                                  playing_.load(std::memory_order_acquire));
    }

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
        //
        // D32.1 : LA PRÉ-ÉCOUTE SE MÉLANGE SUR CHACUNE DES SORTIES DE CE
        // CHEMIN, y compris les deux court-circuits ci-dessous. C'est le
        // défaut qu'un test a attrapé : mélangée seulement en fin de bloc,
        // elle ne s'entendait que si un instrument avait par ailleurs quelque
        // chose à dire -- c'est-à-dire jamais, puisqu'on parcourt un dossier
        // d'échantillons transport arrêté et morceau muet. Elle passe après le
        // master, et sur ces deux sorties le master n'a rien traité : elle est
        // donc bien à sa place, pas avant lui.
        auto sortieAvecPreEcoute = [&](int n) { auditionPlayer_.mixInto(outputL, outputR, n); };

        if (drainedLiveCount_ == 0 && drainedChaseCount_ == 0 && drainedLiveControlCount_ == 0
            && !panicThisBlock_ && !stopReleaseThisBlock_ && !wrapNoteOffPending_ && totalActiveVoices() == 0) {
            sortieAvecPreEcoute(numSamples);
            return;
        }

        auto idleSnapshot = snapshot_.load(std::memory_order_acquire);
        if (!idleSnapshot || idleSnapshot->project.tracks.empty()) {
            sortieAvecPreEcoute(numSamples);
            return;
        }

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

        const bool idleAnySolo = vsm::sequencer::anySoloActive(idleSnapshot->project.tracks);
        renderTrackRange(*idleSnapshot, idleAnySolo, 0, idleSamples,
                          currentSeconds_.load(std::memory_order_acquire), outputL, outputR,
                          /*includeScheduledEvents=*/false);
        wrapNoteOffPending_ = false;   // D606.1 : la fin d'une chaîne tombée pile au bout du bloc d'avant, relâchée ici
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
        //
        // D32.1 : LA PRÉ-ÉCOUTE, ELLE, JOUE À L'ARRÊT -- c'est même le cas
        // normal : on parcourt un dossier d'échantillons transport arrêté.
        // Elle ne suit pas la tête de lecture mais sa propre position, et
        // c'est ce qui la distingue de la référence. Ce chemin-ci l'oubliait,
        // et un test l'a attrapé : le clic ne rendait rien tant que le morceau
        // ne jouait pas, c'est-à-dire presque toujours.
        auditionPlayer_.mixInto(outputL, outputR, idleSamples);
        return;
    }

    drainChasedNotes();   // D585.3 : en lecture seulement — à l'arrêt, elles attendent le départ

    // Ne traite jamais plus que ce que les buffers de travail pré-alloués
    // permettent (règle realtime : pas de redimensionnement ici). En usage
    // normal, prepare() a été appelé avec le block size réel du device, donc
    // ce clamp ne devrait jamais réellement raccourcir le rendu.
    const int samplesToProcess = std::min(numSamples, static_cast<int>(scratchMonoL_.size()));
    const size_t actifs = activeSends_.load(std::memory_order_acquire);

    // D579 bis : la position LUE au début du bloc, gardée telle quelle pour l'écriture de la fin (voir plus bas).
    const double positionLue = currentSeconds_.load(std::memory_order_acquire);
    double blockStartSeconds = positionLue;
    // D18.5 : LA VITESSE DE LECTURE, lue UNE FOIS pour le bloc entier -- la
    // changer au milieu d'un bloc ferait sauter la position d'un demi-bloc.
    //
    // ELLE MULTIPLIE PARTOUT OÙ DES ÉCHANTILLONS DEVIENNENT DES SECONDES DE
    // MORCEAU, et nulle part ailleurs. Écrite `x * vitesse / sampleRate_`
    // plutôt que `x * (vitesse / sampleRate_)` : à 1,0 la multiplication est
    // EXACTE en IEEE 754, donc l'expression se réduit littéralement à celle
    // d'avant, arrondi pour arrondi. C'est ce qui rend l'addition sans risque
    // pour tous les rendus existants.
    const double vitesse = playbackSpeed_.load(std::memory_order_acquire);
    const double blockDurationSeconds =
        static_cast<double>(samplesToProcess) * vitesse / sampleRate_;
    // Valeur par défaut : sans boucle, la position avance simplement de la
    // durée du bloc. Le rebouclage la remplace par la position réellement
    // atteinte (voir plus bas).
    double blockEndSeconds = blockStartSeconds + blockDurationSeconds;

    auto snapshot = snapshot_.load(std::memory_order_acquire);
    if (snapshot && !snapshot->project.tracks.empty()) {
        const Project& project = snapshot->project;
        auto lanes = automationLanes_.load(std::memory_order_acquire);

        const bool anySolo = vsm::sequencer::anySoloActive(project.tracks);

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
        // D606.1 : LA CHAÎNE DE L'ORDRE DE JEU, tenue pour tout le bloc (les relances qu'elle porte y sont lues) ; la
        // boucle s'efface devant elle, et elle devant l'enregistrement — une prise s'écrit sur la ligne de temps.
        const auto chaine = playChain_.load(std::memory_order_acquire);
        const bool suitLaChaine = chaine && !chaine->elements.empty() && !recording_.load(std::memory_order_relaxed);
        // D611 : UN SAUT TOMBÉ PILE À LA FIN DU BLOC D'AVANT attend encore son relâchement — ce qu'il désigne est gardé
        // (sinon le relâchement retombait sur les listes de la boucle : ni relances, ni contrôleurs).
        if (!wrapNoteOffPending_) {
            relancesDuSaut_ = nullptr;
            controlesDuSaut_ = nullptr;   // D611.1
            chaineDuSaut_.reset();
        }
        bool chaineFinie = false;
        const bool loopActive = loopActive_.load(std::memory_order_acquire) && !suitLaChaine;
        const double loopStart = loopStartSeconds_.load(std::memory_order_acquire);
        const double loopEnd = loopEndSeconds_.load(std::memory_order_acquire);

        int rendered = 0;
        double spanStartSeconds = blockStartSeconds;
        while (rendered < samplesToProcess) {
            int count = samplesToProcess - rendered;
            int element = suitLaChaine ? chainIndex_.load(std::memory_order_acquire) : -1;
            if (element >= static_cast<int>(chaine ? chaine->elements.size() : 0)) element = -1;
            if (element >= 0) {
                // Comme la fin de boucle : le bloc est découpé à la fin de l'élément.
                const double jusquALaFin = chaine->elements[static_cast<size_t>(element)].fin - spanStartSeconds;
                const int avantLaFin = static_cast<int>(std::ceil(jusquALaFin * sampleRate_ / vitesse));
                count = std::min(count, std::max(1, avantLaFin));
            }

            if (loopActive && loopEnd > loopStart && spanStartSeconds < loopEnd) {
                const double untilLoopEnd = loopEnd - spanStartSeconds;
                // Combien d'échantillons de SORTIE avant la fin de boucle :
                // à demi-vitesse, il en faut deux fois plus pour parcourir la
                // même distance de morceau.
                const int samplesUntilEnd =
                    static_cast<int>(std::ceil(untilLoopEnd * sampleRate_ / vitesse));
                count = std::min(count, std::max(1, samplesUntilEnd));
            }

            renderSpan(*snapshot, anySolo, rendered, count, spanStartSeconds, outputL, outputR, lanes.get());
            wrapNoteOffPending_ = false; // consommé par TOUTES les pistes du segment
            relancesDuSaut_ = nullptr;
            controlesDuSaut_ = nullptr;   // D611.1

            rendered += count;
            spanStartSeconds += static_cast<double>(count) * vitesse / sampleRate_;

            // D606.1 : LA FIN D'UN ÉLÉMENT. La tête saute au début du suivant, les notes tenues relâchées et celles qui
            // traversent la cible relancées — le rebouclage, dont la cible est un autre endroit. Après le dernier, les
            // notes relâchées à cet échantillon, le reste du bloc rendu SANS le planning, et le graphe s'arrête.
            if (element >= 0 && spanStartSeconds >= chaine->elements[static_cast<size_t>(element)].fin - 1.0e-12) {
                if (static_cast<size_t>(element) + 1 < chaine->elements.size()) {
                    const auto& suivant = chaine->elements[static_cast<size_t>(element) + 1];
                    chainIndex_.store(element + 1, std::memory_order_release);
                    if (suivant.contigu) continue;   // un seul tenant : la lecture continue, rien n'est relâché
                    wrapNoteOffPending_ = true;
                    wrapTraite_.fill(false);
                    spanStartSeconds = suivant.debut;
                    relancesDuSaut_ = &suivant.relances;
                    controlesDuSaut_ = &suivant.controles;   // D611.1
                    chaineDuSaut_ = chaine;                   // D611 : tenue en vie si le saut finit le bloc
                    continue;
                }
                wrapNoteOffPending_ = true;
                wrapTraite_.fill(false);
                relancesDuSaut_ = &aucuneRelance_;
                controlesDuSaut_ = &aucunControle_;   // la fin de la chaîne ne chasse rien
                if (rendered < samplesToProcess) {
                    renderTrackRange(*snapshot, anySolo, rendered, samplesToProcess - rendered, spanStartSeconds,
                                     outputL, outputR, /*includeScheduledEvents=*/false);
                    wrapNoteOffPending_ = false;
                    rendered = samplesToProcess;
                }
                chaineFinie = true;
                break;
            }

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
                wrapTraite_.fill(false);   // D585.3
            }
        }
        blockEndSeconds = spanStartSeconds;
        if (chaineFinie) {
            chainIndex_.store(-1, std::memory_order_release);
            playing_.store(false, std::memory_order_release);
            chainEnded_.store(true, std::memory_order_release);
        }

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
    // D16.6 : deux restrictions par-dessus l'interrupteur, et le décompte qui
    // passe outre les trois. « Seulement au décompte » et « seulement à
    // l'enregistrement » se cumulent ; le décompte, lui, n'est pas réglable.
    // D546.2 : « avant zéro » devient « avant la fin du décompte », qui vaut zéro tant que
    // l'application ne dit rien -- le punch au milieu du morceau compte sur des positions positives.
    const bool decompte = blockStartSeconds < countInEndSeconds_.load(std::memory_order_relaxed);
    bool clic = metronomeEnabled_.load(std::memory_order_relaxed);
    if (clic && metronomeCountInOnly_.load(std::memory_order_relaxed)) clic = false;
    if (clic && metronomeRecordOnly_.load(std::memory_order_relaxed)
        && !recording_.load(std::memory_order_relaxed))
        clic = false;
    const bool clicAudible = clic || decompte;
    if (clicAudible) {
        // D590.1 : CHAQUE CLIC PART À L'ÉCHANTILLON DE SON TEMPS. Avant, le tick
        // était jeté et le clic rendu dès le premier échantillon du bloc : en
        // avance de 0 à un bloc entier (448, 384 … 64 échantillons à 48 kHz par
        // blocs de 512, mesuré), une grille qui tremble sous ce qu'on joue. Le
        // clic se rend jusqu'à l'échantillon du temps — la queue d'un clic du
        // bloc d'avant comprise —, se déclenche, et reprend.
        int rendu = 0;
        const auto rendreJusqua = [&](int fin) {
            for (; rendu < fin; ++rendu) {
                const float clic = metronome_.nextSample();
                outputL[rendu] += clic;
                outputR[rendu] += clic;
            }
        };
        const auto snapshot = snapshot_.load(std::memory_order_acquire);
        if (snapshot && blockDurationSeconds > 0.0) {
            const Project& projet = snapshot->project;
            // La position DU DÉBUT DU BLOC, prise telle quelle et non
            // recalculée depuis la fin : au rebouclage, la fin du bloc est
            // repartie au début de la boucle, et lui soustraire la durée du
            // bloc donnerait un intervalle qui n'a jamais été joué.
            const double debutSecondes = blockStartSeconds;
            const double finSecondes = blockStartSeconds + blockDurationSeconds;
            const int64_t debutTick = projet.secondsToTicks(debutSecondes);
            const int64_t finTick = projet.secondsToTicks(finSecondes);
            // Avant zéro, au tempo du début, comme la durée du décompte
            // (`countInSeconds`) : la carte du tempo rend 0 pour tout tick négatif.
            const double bpmDuDebut = projet.bpmAt(0);   // D595 : le tempo qu'on entend, fixe compris
            const auto secondesDuTemps = [&](int64_t tick) {
                if (tick >= 0 || projet.tempoMap.isSmpte() || bpmDuDebut <= 0.0) return projet.ticksToSeconds(tick);
                return static_cast<double>(tick) * 60.0 / (bpmDuDebut * static_cast<double>(projet.ticksPerQuarterNote));
            };
            // L'échantillon du temps se compte dans la durée du bloc, qui porte
            // déjà la vitesse de lecture ; il est borné au bloc, et jamais
            // avant un clic déjà déclenché dans ce bloc.
            forEachClickInRange(projet.timeSignatureMap, projet.ticksPerQuarterNote, debutTick, finTick,
                                [&](int64_t tick, bool accent) {
                                    const double ou = (secondesDuTemps(tick) - debutSecondes) / blockDurationSeconds
                                                      * static_cast<double>(samplesToProcess);
                                    const int position = std::clamp(static_cast<int>(std::lround(ou)), rendu,
                                                                    std::max(rendu, samplesToProcess - 1));
                                    rendreJusqua(position);
                                    metronome_.trigger(accent);
                                });
        }
        rendreJusqua(samplesToProcess);
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

    // D32.1 : LA PRÉ-ÉCOUTE, au même étage que la référence et pour les mêmes
    // raisons -- après le master (un échantillon qu'on essaie ne traverse pas
    // le compresseur du morceau) et jamais dans l'export (le rendu la coupe).
    // Elle vient APRÈS le « original seul » de la référence, sans quoi ce
    // dernier l'effacerait : on veut pouvoir essayer un échantillon même en
    // écoutant l'original.
    auditionPlayer_.mixInto(outputL, outputR, samplesToProcess);

    // D579 bis : LA FIN DU BLOC N'ÉCRASE PAS UNE TÊTE POSÉE PENDANT LE BLOC. L'interface arrête et pose la tête
    // « exactement » (D553.2, D578.2, D579.3) pendant que ce bloc est en vol ; l'écriture inconditionnelle la
    // remplaçait par la fin du bloc, 20 ms plus loin à 48 kHz. N'écrire que si la position est encore celle qu'on a lue :
    // une pose faite entre-temps l'emporte. Hors de cette fenêtre, rien ne change — le rendu hors ligne ne pose jamais
    // la tête pendant un bloc, et ses octets restent les mêmes.
    double attendue = positionLue;
    currentSeconds_.compare_exchange_strong(attendue, blockEndSeconds, std::memory_order_acq_rel,
                                            std::memory_order_acquire);
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
                case AutomationTarget::TrackTrim:
                    if (t < kMaxTracks) autoTrim_[t] = valeur;
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
    const double vitesse = playbackSpeed_.load(std::memory_order_acquire);
    for (int s = 0; s < sampleCount; s += kAutomationChunk) {
        const int count = std::min(kAutomationChunk, sampleCount - s);
        // D18.5 : le sous-segment avance lui aussi à la vitesse de lecture.
        // C'était LA moitié manquante de la première tentative : ralentir
        // l'avance de bloc sans ralentir celle-ci revenait à ralentir la
        // pendule sans ralentir la trotteuse.
        const double segStart =
            startSeconds + static_cast<double>(s) * vitesse / sampleRate_;
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

float ProcessGraph::facteurVca(const vsm::sequencer::Project& project, size_t trackIndex) const {
    const auto* vca = vsm::sequencer::vcaOf(project.tracks, trackIndex);
    if (vca == nullptr) return 1.0f;
    const size_t v = static_cast<size_t>(project.tracks[trackIndex].vcaTrack);
    // D599.2 : la main sur le fader du VCA l'emporte sur sa courbe, comme sur une piste ; D599.3 : son Trim la décale.
    if (v < kMaxTracks && (autoMask_[v] & kAutoVolume) && !(handMask_[v].load(std::memory_order_relaxed) & handVolume)) {
        const float facteurTrim = trimVolume_[v].load(std::memory_order_relaxed);
        return facteurTrim == 1.0f ? autoVolume_[v] : std::min(autoVolume_[v] * facteurTrim, kTrimGainMax);
    }
    return vca->volume;
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

        // D35.4 : sur l'ARBRE, pour qu'un dossier muet taise ce qu'il contient.
        const bool audible = vsm::sequencer::trackAudible(project.tracks, trackIndex, anySolo);
        // D23.1 : la polarité d'un groupe, par le signe de son volume (voir
        // la piste plus bas) -- un bus de batterie en opposition s'inverse
        // entier, départs compris.
        const float signeGroupe = track.invertPhase ? -1.0f : 1.0f;
        // D532.1 : le fader du groupe, multiplié par celui de son VCA s'il en a un.
        const float faderGroupe = track.volume * facteurVca(project, trackIndex);
        // BALANCE et non panoramique : le groupe reçoit un signal déjà stéréo,
        // qui a déjà traversé la loi à puissance constante de ses pistes. La
        // lui appliquer une seconde fois lui coûterait encore 3 dB, et grouper
        // deviendrait un choix qu'on paie. Voir `stereoBalance`.
        const float peak = mixStereoBalancedInto(groupL_[g].data(), groupR_[g].data(), numSamples,
                                                  faderGroupe * signeGroupe, track.pan, audible,
                                                  outputL, outputR);
        blockPeak_[trackIndex] = std::max(blockPeak_[trackIndex], peak);

        // Un groupe se mesure comme une piste (D4.7), sur ce qu'il envoie.
        for (int i = 0; i < numSamples; ++i) {
            const double l = static_cast<double>(groupL_[g][static_cast<size_t>(i)]) * faderGroupe;
            const double r = static_cast<double>(groupR_[g][static_cast<size_t>(i)]) * faderGroupe;
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
                const float apresFader = (preFader & (1u << b)) ? signeGroupe : faderGroupe * signeGroupe;
                const float lvl = track.audibleSendLevel(b) * apresFader;   // D567.3 : contournés, rien
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
    transport.tempoBpm = project.bpmAt(tick);   // D595 : le tempo fixe, s'il est allumé
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
    // D42.1 : LE CHRONOMÈTRE EST ICI, ET NULLE PART AILLEURS. Les deux chemins
    // de rendu -- le séquentiel et le parallèle qui porte les 64 pistes --
    // passent tous deux par cette fonction : instrumenter ici couvre les deux
    // PAR CONSTRUCTION, là où deux chronomètres posés dans chaque chemin
    // auraient fini par diverger, ou par n'être posés que dans celui qu'on
    // regarde. `steady_clock` ne fait ni allocation, ni verrou, ni
    // entrée-sortie : l'invariant n° 2 du § 6 tient.
    // UN GARDE PLUTÔT QU'UN APPEL À CHAQUE `return` : cette fonction en compte
    // une dizaine, et le onzième qu'on ajoutera un jour oublierait le
    // chronomètre sans que rien ne le dise -- la piste concernée afficherait
    // alors un coût figé, c'est-à-dire un chiffre plausible et faux.
    struct GardeChrono {
        MeterBank& banque;
        size_t piste;
        std::chrono::steady_clock::time_point debut;
        ~GardeChrono() {
            banque.reportRenderMicros(
                piste, static_cast<float>(std::chrono::duration<double, std::micro>(
                           std::chrono::steady_clock::now() - debut).count()));
        }
    } gardeChrono{ meters_, trackIndex, std::chrono::steady_clock::now() };
    const Project& project = snapshot.project;
    if (trackIndex >= project.tracks.size() || trackIndex >= kMaxTracks) return false;
    // D30.2 : UNE PISTE DÉSACTIVÉE N'EST PAS RENDUE DU TOUT. Le muet, lui,
    // rend puis jette : c'est ce qui fait qu'une piste muette coûte encore
    // tout ce qu'elle coûtait. Ici on sort avant l'instrument, avant le
    // fichier, avant les inserts -- c'est là qu'est l'économie promise.
    if (project.tracks[trackIndex].disabled) return false;
    const double vitesse = playbackSpeed_.load(std::memory_order_acquire);
    const double rangeEndSeconds =
        rangeStartSeconds + static_cast<double>(sampleCount) * vitesse / sampleRate_;

    auto instrument = instruments_[trackIndex].load(std::memory_order_acquire);
    auto audioSource = audioSources_[trackIndex].load(std::memory_order_acquire);

    // D18.7b : LA TABLE DE PUBLICATION, chargée une fois pour la piste. Nulle
    // tant qu'aucune piste ne publie -- et tout ce qui suit se réduit alors au
    // chemin d'avant.
    auto routage = outputRouting_.load(std::memory_order_acquire);
    const int lireSlot = routage ? routage->readSlot[trackIndex] : -1;
    if (lireSlot >= 0) {
        // UNE PISTE QUI PUBLIE N'A PAS D'INSTRUMENT À ELLE, et si on lui en a
        // mis un, le graphe l'ignore : ses notes seraient jouées une seconde
        // fois, par-dessus la sortie qu'elle est censée porter.
        instrument = nullptr;
    }

    // UNE PISTE AUDIO N'A PAS D'INSTRUMENT, et c'est normal : son matériau
    // est un fichier. La condition portait sur le seul instrument, ce qui
    // aurait fait sauter la piste entière en silence. Une piste qui PUBLIE
    // n'a ni l'un ni l'autre, et doit tout de même être rendue.
    // D27.2 : UNE PISTE À PORT MIDI PASSE PAR LE RENDU même sans machine --
    // ses notes doivent être construites pour être déposées.
    const bool externe = !project.tracks[trackIndex].midiOutputDevice.empty();
    if (!instrument && !audioSource && lireSlot < 0 && !externe) return false;

    const Track& track = project.tracks[trackIndex];

    int numEvents = 0;

    // Rebouclage : relâche ce que cette piste tenait encore à la frontière
    // de boucle. Sans ça, une note dont le NoteOff tombe APRÈS la fin de
    // boucle ne serait jamais relâchée et sonnerait indéfiniment -- le
    // "note bloquée" classique des séquenceurs.
    if (wrapNoteOffPending_ && !wrapTraite_[trackIndex]) {
        wrapTraite_[trackIndex] = true;   // D585.3 : une fois par rebouclage, pas une fois par sous-segment
        clearSustain(trackIndex);   // D25.1 : la pédale ne traverse pas la frontière de boucle
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
            marquerSonnante(trackIndex, static_cast<uint8_t>(note), false);   // D32.3
        }
        // D585.3 : ET CE QUI TRAVERSE LE DÉBUT DE LA BOUCLE EST RELANCÉ, juste après les relâchements — sans quoi une
        // nappe posée au milieu de la boucle se tait à chaque tour.
        // D606.1 : au saut d'une chaîne, celles du début de l'élément d'après (`relancesDuSaut_`) ; sinon celles de la boucle.
        std::shared_ptr<const LoopChase> boucle;
        const std::vector<ChasedNoteEvent>* relances = relancesDuSaut_;
        const std::vector<ChasedControlEvent>* controles = controlesDuSaut_;
        if ((relances == nullptr || controles == nullptr) && (boucle = loopChase_.load(std::memory_order_acquire))) {
            if (relances == nullptr) relances = &boucle->relances;
            if (controles == nullptr) controles = &boucle->controles;
        }
        // D611.1 : LES CONTRÔLEURS CHASSÉS AU SAUT, AVANT les notes relancées — la chasse du déplacement (D16.2), aux
        // mêmes tenues (D331) : la section d'arrivée reprend la dernière valeur écrite avant elle, comme le créneau de
        // l'aplatissement (D537). Sans elle, elle sonnait avec le volume, la pédale ou le filtre de celle qu'on quitte.
        if (controles != nullptr && (instrument != nullptr || externe))
            for (const auto& c : *controles) {
                if (c.trackIndex != trackIndex) continue;
                ChasedControlEvent chasse = c;
                appliquerLesTenues(trackIndex, chasse.event, instrument.get());
                if (externe) emitControlOut(trackIndex, track.channel, chasse.event);   // D27.2
                if (interceptSustain(trackIndex, track.channel, chasse.event, events, numEvents)) {   // D25.1
                    if (instrument) instrument->handleControlEvent(chasse.event);
                    continue;
                }
                if (instrument && !instrument->handleControlEvent(chasse.event))
                    ignoredControlEvents_.fetch_add(1, std::memory_order_relaxed);
            }
        if (chaseNotes_.load(std::memory_order_acquire) && relances != nullptr)
                for (const auto& c : *relances) {
                    if (c.trackIndex != trackIndex || numEvents >= kMaxEventsPerBlock) continue;
                    MidiNoteEvent on;
                    on.kind = MidiNoteEvent::Kind::NoteOn;
                    on.sampleOffset = 0;
                    on.channel = c.channel;
                    on.note = c.note;
                    on.velocity = c.velocity;
                    heldNoteOffs_[trackIndex][c.note] = false;   // D25.1 : un NoteOn oublie la retenue
                    marquerSonnante(trackIndex, c.note, true);
                    notesSentToInstruments_.fetch_add(1, std::memory_order_relaxed);
                    events[static_cast<size_t>(numEvents++)] = on;
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
            // D609.1 : LA CARTE DE BATTERIE AU CLAVIER AUSSI — on entend en jouant ce qu'on réentendra à la lecture ; la
            // prise, elle, garde la note jouée au clavier (la note ÉCRITE).
            const uint8_t note = track.drumOutput(live.note);
            MidiNoteEvent pluginEvent;
            pluginEvent.kind = live.noteOn ? MidiNoteEvent::Kind::NoteOn : MidiNoteEvent::Kind::NoteOff;
            pluginEvent.sampleOffset = 0;
            pluginEvent.channel = track.channel;
            pluginEvent.note = note;
            pluginEvent.velocity = live.velocity;
            // D25.1 : sous pédale, le NoteOff est retenu ; un NoteOn oublie la retenue.
            if (live.noteOn) heldNoteOffs_[trackIndex][note] = false;
            else if (holdNoteOffIfSustained(trackIndex, note)) continue;
            marquerSonnante(trackIndex, note, live.noteOn);   // D32.3
            if (live.noteOn) tenuesAuClavier_[trackIndex][note] = true;   // D605.2
            if (live.noteOn) notesSentToInstruments_.fetch_add(1, std::memory_order_relaxed);
            events[static_cast<size_t>(numEvents++)] = pluginEvent;
        }
        // D585.3 : LES NOTES CHASSÉES d'un départ ou d'un déplacement, au même endroit que les notes d'écoute ; une
        // note qui sonne déjà n'est pas relancée (un déplacement à l'intérieur de la note, le scrub).
        for (int i = 0; i < drainedChaseNoteCount_ && numEvents < kMaxEventsPerBlock; ++i) {
            const ChasedNoteEvent& c = drainedChaseNotes_[static_cast<size_t>(i)];
            if (c.trackIndex != trackIndex || soundingNotes_[trackIndex][c.note]) continue;
            MidiNoteEvent on;
            on.kind = MidiNoteEvent::Kind::NoteOn;
            on.sampleOffset = 0;
            on.channel = c.channel;
            on.note = c.note;
            on.velocity = c.velocity;
            heldNoteOffs_[trackIndex][c.note] = false;   // D25.1
            marquerSonnante(trackIndex, c.note, true);   // D32.3
            notesSentToInstruments_.fetch_add(1, std::memory_order_relaxed);
            events[static_cast<size_t>(numEvents++)] = on;
        }
    }

    // LES VALEURS CHASSÉES (D16.2) : livrées EN TÊTE DE BLOC, avant tout ce
    // que le planning apporte -- une pédale rendue après la note qu'elle
    // devait tenir ne la tient pas. Comme les notes d'écoute, seulement dans
    // le premier sous-segment : le découpage de l'automation les rejouerait
    // sinon à chaque segment.
    if (sampleStart == 0 && (instrument != nullptr || externe)) {
        for (int i = 0; i < drainedChaseCount_; ++i) {
            ChasedControlEvent chasse = drainedChase_[static_cast<size_t>(i)];
            if (chasse.trackIndex != trackIndex) continue;
            // D331 : la chasse passe par les MÊMES tenues que le planning -- un
            // morceau lancé à la mesure 40 a son volume, son panoramique et sa
            // plage de pli (D329 les avait posées sur le seul planning : un
            // départ en cours de morceau les manquait).
            appliquerLesTenues(trackIndex, chasse.event, instrument.get());
            if (externe) emitControlOut(trackIndex, track.channel, chasse.event);   // D27.2
            if (interceptSustain(trackIndex, track.channel, chasse.event, events, numEvents)) {   // D25.1
                if (instrument) instrument->handleControlEvent(chasse.event);   // les huit machines qui ont leur propre étouffoir le gardent
                continue;
            }
            if (instrument && !instrument->handleControlEvent(chasse.event))
                ignoredControlEvents_.fetch_add(1, std::memory_order_relaxed);
        }
        // D24.1 : LES CONTRÔLEURS EN DIRECT, au même endroit et pour la même
        // raison que les valeurs chassées -- avant les notes du bloc.
        for (int i = 0; i < drainedLiveControlCount_; ++i) {
            const LiveControlEvent& live = drainedLiveControls_[static_cast<size_t>(i)];
            if (live.trackIndex != trackIndex) continue;
            if (externe) emitControlOut(trackIndex, track.channel, live.event);   // D27.2
            if (interceptSustain(trackIndex, track.channel, live.event, events, numEvents)) {   // D25.1
                if (instrument) instrument->handleControlEvent(live.event);   // transmis quand même : huit machines ont leur étouffoir
                liveControlsDelivered_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!instrument) continue;
            if (instrument->handleControlEvent(live.event))
                liveControlsDelivered_.fetch_add(1, std::memory_order_relaxed);
            else
                ignoredControlEvents_.fetch_add(1, std::memory_order_relaxed);
        }
        // D24.4 : LE PANIC. Un NoteOff par note qui sonne, la pédale relâchée
        // avant -- un NoteOff sous pédale ne coupe rien.
        if (panicThisBlock_) {
            vsm::audio::plugin::MidiControlEvent pedale;
            pedale.kind = vsm::audio::plugin::MidiControlEvent::Kind::ControlChange;
            pedale.channel = track.channel;
            pedale.index = 64;
            pedale.value = 0.0f;
            if (instrument) instrument->handleControlEvent(pedale);
            if (externe) emitControlOut(trackIndex, track.channel, pedale);   // D27.2
            clearSustain(trackIndex);   // D25.1
            auto& sonnent = soundingNotes_[trackIndex];
            for (size_t note = 0; note < sonnent.size() && numEvents < kMaxEventsPerBlock; ++note) {
                if (!sonnent[note]) continue;
                MidiNoteEvent off;
                off.kind = MidiNoteEvent::Kind::NoteOff;
                off.sampleOffset = 0;
                off.channel = track.channel;
                off.note = static_cast<uint8_t>(note);
                off.velocity = 0;
                events[static_cast<size_t>(numEvents++)] = off;
                marquerSonnante(trackIndex, static_cast<uint8_t>(note), false);   // D32.3
            }
        }
        // D605.2 : L'ARRÊT. Un NoteOff par note que la LECTURE tenait — celle que la main tient au clavier sonne
        // encore, sauf si son NoteOff n'attendait que la pédale : l'arrêt vide la retenue (le critère de D25.1, tombé
        // de sa clôture et du code). La pédale relâchée d'abord pour les machines qui ont leur propre étouffoir.
        if (stopReleaseThisBlock_ && !panicThisBlock_) {
            if (sustainDown_[trackIndex]) {
                vsm::audio::plugin::MidiControlEvent pedale;
                pedale.kind = vsm::audio::plugin::MidiControlEvent::Kind::ControlChange;
                pedale.channel = track.channel;
                pedale.index = 64;
                pedale.value = 0.0f;
                if (instrument) instrument->handleControlEvent(pedale);
                if (externe) emitControlOut(trackIndex, track.channel, pedale);   // D27.2
            }
            auto& sonnent = soundingNotes_[trackIndex];
            const auto& retenus = heldNoteOffs_[trackIndex];
            for (size_t note = 0; note < sonnent.size() && numEvents < kMaxEventsPerBlock; ++note) {
                if (!sonnent[note] || (tenuesAuClavier_[trackIndex][note] && !retenus[note])) continue;
                MidiNoteEvent off;
                off.kind = MidiNoteEvent::Kind::NoteOff;
                off.sampleOffset = 0;
                off.channel = track.channel;
                off.note = static_cast<uint8_t>(note);
                off.velocity = 64;
                events[static_cast<size_t>(numEvents++)] = off;
                marquerSonnante(trackIndex, static_cast<uint8_t>(note), false);   // D32.3
            }
            clearSustain(trackIndex);   // D25.1
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
    // Un quart d'échantillon de SORTIE, exprimé en secondes de morceau.
    const double margeFrontiere = 0.25 * vitesse / sampleRate_;
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
        const long long decalageMorceau = echantillonEvenement - echantillonBloc;
        // DU MORCEAU VERS LA SORTIE : à demi-vitesse, un événement situé mille
        // échantillons de morceau plus loin sort deux mille échantillons plus
        // tard. La division par 1,0 est exacte, et l'entier traverse donc le
        // `double` sans bouger tant que la vitesse est normale.
        const long long decalage =
            vitesse == 1.0 ? decalageMorceau
                           : std::llround(static_cast<double>(decalageMorceau) / vitesse);
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
                controlEvent.value = static_cast<float>(data.value) / 8192.0f;   // D331 : BRUT, converti par `appliquerLesTenues`
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
            // D25.1 : sous pédale, le NoteOff est retenu ; un NoteOn oublie la retenue.
            if (pluginEvent.kind == MidiNoteEvent::Kind::NoteOn)
                heldNoteOffs_[trackIndex][pluginEvent.note] = false;
            else if (holdNoteOffIfSustained(trackIndex, pluginEvent.note))
                continue;
            marquerSonnante(trackIndex, pluginEvent.note,             // D32.3
                             pluginEvent.kind == MidiNoteEvent::Kind::NoteOn);
            if (pluginEvent.kind == MidiNoteEvent::Kind::NoteOn) {
                notesSentToInstruments_.fetch_add(1, std::memory_order_relaxed);
                tenuesAuClavier_[trackIndex][pluginEvent.note] = false;   // D605.2 : rejouée par la lecture, elle est sienne
            }
            events[static_cast<size_t>(numEvents++)] = pluginEvent;
        } else if (issue == Issue::Control && (appliquerLesTenues(trackIndex, controlEvent, instrument.get()), false)) {
            // D329-D331 : LES TENUES DU GRAPHE -- volume de canal (CC 7),
            // panoramique (CC 10), plage de pli (RPN 0) -- notées ici, puis
            // l'événement est traité comme tout contrôleur (la virgule rend
            // faux : on continue dans les branches suivantes, port et machine).
        } else if (issue == Issue::Control && externe && (emitControlOut(trackIndex, track.channel, controlEvent), false)) {
            // D27.2 : déposé sur le port, puis traité comme avant (la virgule
            // rend faux : on continue dans les branches suivantes).
        } else if (issue == Issue::Control && interceptSustain(trackIndex, track.channel, controlEvent, events, numEvents)) {
            // D25.1 : la pédale est tenue par le graphe pour toutes les machines,
            // ET transmise -- huit machines (piano, clavecin...) ont leur propre
            // étouffoir, et le leur retirer changerait leur son. Le résultat de
            // la machine n'est pas compté « ignoré » : la pédale a agi.
            if (instrument) instrument->handleControlEvent(controlEvent);
        } else if (issue == Issue::Control && instrument == nullptr) {
            // Piste à port sans machine : le contrôle est parti sur le port, rien d'autre à faire.
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

    // D27.2 : LES NOTES DU BLOC PARTENT SUR LE PORT -- celles du planning,
    // de l'écoute, les NoteOff de pédale, de rebouclage et de panic : tout
    // ce que la machine interne aurait reçu, au même échantillon.
    if (externe)
        for (int i = 0; i < numEvents; ++i) {
            const MidiNoteEvent& ev = events[static_cast<size_t>(i)];
            const bool on = ev.kind == MidiNoteEvent::Kind::NoteOn;
            emitMidiOut(trackIndex, sampleStart + ev.sampleOffset,
                        static_cast<uint8_t>((on ? 0x90 : 0x80) | (ev.channel & 0x0F)), ev.note, ev.velocity);
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
        // D18.7b : LES SORTIES SÉPARÉES, mais SEULEMENT si quelqu'un les
        // réclame. Une machine dont personne ne demande les sorties annexes --
        // c'est-à-dire toutes, dans tout projet écrit jusqu'ici -- passe par
        // `process` tel quel, au bit près.
        const int reclamees = routage ? routage->outputsToRender[trackIndex] : 0;
        if (reclamees >= 2) {
            const int possibles = std::max(1, instrument->outputCount());
            const int rendues = std::min(reclamees, possibles);
            float* sortiesL[kMaxInstrumentOutputs];
            float* sortiesR[kMaxInstrumentOutputs];
            sortiesL[0] = destL;
            sortiesR[0] = destR;
            // CHAQUE SORTIE A SON PROPRE TAMPON, y compris celles que personne
            // ne lit : `refreshOutputRouting` les alloue toutes ou aucune,
            // pour qu'aucune machine n'écrive là où une autre écrit.
            bool complet = true;
            for (int k = 1; k < rendues; ++k) {
                const int16_t slot = routage->slotOf[trackIndex][static_cast<size_t>(k)];
                if (slot < 0) { complet = false; break; }
                sortiesL[k] = extraOutL_[static_cast<size_t>(slot)].data();
                sortiesR[k] = extraOutR_[static_cast<size_t>(slot)].data();
            }
            if (complet) {
                instrument->processMultiOut(events, numEvents, sortiesL, sortiesR,
                                            rendues, sampleCount);
                // RÉCLAMÉE ET INEXISTANTE : la piste qui l'attend restera muette
                // (son tampon a été mis à zéro en tête de segment), et on le DIT.
                if (reclamees > possibles)
                    droppedInstrumentOutputs_.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Ne devrait pas arriver -- mais s'il arrivait, la machine
                // garde son chemin d'avant plutôt que d'écrire n'importe où.
                instrument->process(events, numEvents, destL, destR, sampleCount);
                droppedInstrumentOutputs_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            instrument->process(events, numEvents, destL, destR, sampleCount);
        }
    }
    // D18.7b : L'AUTRE BOUT — la piste qui LIT une sortie annexe. Le tampon a
    // été rempli par la première vague, ou laissé à zéro si la machine source
    // ne tournait pas (piste gelée, muette d'instrument, sortie inexistante).
    if (lireSlot >= 0) {
        const float* srcL = extraOutL_[static_cast<size_t>(lireSlot)].data();
        const float* srcR = extraOutR_[static_cast<size_t>(lireSlot)].data();
        for (int i = 0; i < sampleCount; ++i) {
            destL[i] = srcL[static_cast<size_t>(i)];
            destR[i] = srcR[static_cast<size_t>(i)];
        }
    }
    if (audioSource && !audioSource->empty()) {
        // La position sur la LIGNE DE TEMPS, en échantillons. Elle vient du
        // temps du segment et non d'un compteur de blocs : c'est ce qui
        // fait qu'un bouclage ou un saut de tête de lecture tombe juste.
        // LE DÉCALAGE DE PISTE (D16.7) : la position de LECTURE recule de ce
        // que la piste avance. Un décalage de -10 ms fait sonner la piste dix
        // millisecondes plus tôt, donc à l'instant t on lit ce qui se trouvait
        // à t + 10 ms. Le signe opposé à celui du planning n'est pas une
        // faute : là on déplace l'événement, ici on déplace la fenêtre par
        // laquelle on regarde le fichier.
        const double decalage = track.delayMs / 1000.0;
        // D18.5 : À VITESSE ≠ 1, LA LECTURE CHANGE DE PAS. C'est la moitié du
        // travail que le second examen avait isolée : ralentir l'horloge ne
        // ralentit pas une lecture de trames consécutives, il faut
        // rééchantillonner. À 1,0 exactement, `mixIntoAtSpeed` délègue au
        // chemin d'avant, sans un test de plus par échantillon.
        const double depart = (rangeStartSeconds - decalage) * sampleRate_;
        audioSource->mixIntoAtSpeed(destL, destR, depart, sampleCount, vitesse, speedKernel_);
    }

    // D30.4 : LE TRIM D'ENTRÉE, AVANT LES INSERTS ET APRÈS TOUT LE RESTE.
    // C'est là qu'il gagne son existence : appliqué ailleurs, il serait un
    // second fader. Ici il change ce que la saturation, le compresseur et la
    // distorsion REÇOIVENT, donc ce qu'ils rendent -- et sur une chaîne vide
    // ou linéaire il redonne exactement ce qu'un fader inverse lui reprend.
    //
    // PAS SUR UNE PISTE GELÉE, pour la raison qui vaut pour les inserts : le
    // trim est DANS le fichier gelé, et le repasser l'appliquerait deux fois.
    {
        // D600.1 : la main sur le trim l'emporte sur sa courbe, comme sur le fader (D599.2).
        const bool trimPilote = (autoMask_[trackIndex] & kAutoTrim)
                                && !(handMask_[trackIndex].load(std::memory_order_relaxed) & handTrim);
        const float trimDb = trimPilote ? autoTrim_[trackIndex] : track.inputTrimDb;
        playedTrim_[trackIndex].store(trimDb, std::memory_order_relaxed);   // D600.1 : pour le relevé
        if (!track.frozen && trimDb != 0.0f) {
            const float g = std::pow(10.0f, trimDb / 20.0f);
            for (int i = 0; i < sampleCount; ++i) { destL[i] *= g; destR[i] *= g; }
        }
    }

    // D593 : LE COUPE-BAS ET LE COUPE-HAUT, APRÈS LE TRIM ET AVANT LES INSERTS — ce qu'on coupe n'entre pas dans le
    // compresseur. Pas sur une piste gelée : ils sont dans le fichier gelé, comme le trim. Éteints, rien n'est calculé.
    if (!track.frozen && trackIndex < kMaxTracks && (track.lowCutHz > 0.0f || track.highCutHz > 0.0f))
        preSections_[trackIndex].process(destL, destR, sampleCount, track.lowCutHz, track.highCutHz);

    // Chaîne d'inserts (section 5) : TRACK -> SYNTH -> EFFECTS -> MIX.
    // LES INSERTS NON PLUS : ils sont DANS le fichier gelé, et les
    // repasser dessus les appliquerait deux fois.
    auto chain = track.frozen ? nullptr
                              : effectChains_[trackIndex].load(std::memory_order_acquire);
    if (trackIndex < kMaxTracks) transportPiste_[trackIndex] = transport;   // D576.1
    if (chain) {
        for (const auto& fx : *chain) {
            // D576.1 : un insert d'APRÈS le fader tourne dans `mixTrackInto`, après le volume.
            if (!fx || fx->runsAfterFader()) continue;
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
    const bool audible = vsm::sequencer::trackAudible(project.tracks, trackIndex, anySolo);  // D35.4

    // MIXAGE VERS SA DESTINATION : le master, ou le tampon d'un groupe. Le
    // groupe sera traité en fin de bloc, quand tous ses membres y auront
    // écrit -- voir `renderGroupBuses`.
    const int groupe = groupBufferFor(project, trackIndex);
    float* destL = groupe >= 0 ? groupL_[static_cast<size_t>(groupe)].data() : outputL;
    float* destR = groupe >= 0 ? groupR_[static_cast<size_t>(groupe)].data() : outputR;

    // LE VOLUME ET LE PANORAMIQUE VIENNENT DE L'AUTOMATION QUAND ELLE LES
    // PILOTE (D4.6), du projet sinon. Un entier consulté par piste, pas un
    // parcours des courbes : voir `refreshAutomationMask`.
    // D599.2 : CE QUE LA MAIN TIENT N'EST PLUS PILOTÉ PAR LA COURBE — la piste joue alors le projet, c'est-à-dire le
    // fader que la console vient de republier. D600.1 : et les départs (plus bas), le trim (plus haut).
    const uint16_t pilotes =
        static_cast<uint16_t>(autoMask_[trackIndex] & ~handMask_[trackIndex].load(std::memory_order_relaxed));
    // D599.3 : une passe Trim DÉCALE la courbe pendant qu'elle court, bornée au haut du fader. Au repos (facteur 1,
    // décalage 0), la courbe passe TELLE QU'AVANT — sans borne ajoutée : les rendus d'avant restent au bit près.
    const float facteurTrim = trimVolume_[trackIndex].load(std::memory_order_relaxed);
    const float faderVolume = !(pilotes & kAutoVolume) ? track.volume
                            : facteurTrim == 1.0f      ? autoVolume_[trackIndex]
                                                       : std::min(autoVolume_[trackIndex] * facteurTrim, kTrimGainMax);
    const float faderPan = (pilotes & kAutoPan)
        ? autoPan_[trackIndex] + trimPan_[trackIndex].load(std::memory_order_relaxed)   // borné plus bas, CC 10 compris
        : track.pan;
    playedVolume_[trackIndex].store(faderVolume, std::memory_order_relaxed);   // D599 : pour le relevé
    playedPan_[trackIndex].store(faderPan, std::memory_order_relaxed);
    // D23.1 : LA POLARITÉ EST LE SIGNE DU VOLUME. Portée par le fader, elle
    // suit partout où le fader compte -- la sortie, les départs post-fader --
    // et le signe est appliqué aussi aux départs pré-fader plus bas : deux
    // micros en opposition le sont dans la réverbération aussi.
    const float signe = track.invertPhase ? -1.0f : 1.0f;
    const float volume = faderVolume * signe
                         * ccVolume_[trackIndex]    // D329 : le CC 7 se multiplie au fader (ou à son automation)
                         * facteurVca(project, trackIndex);   // D532.1 : et le VCA qui le commande
    const float pan = std::clamp(faderPan + ccPan_[trackIndex],
                                 -1.0f, 1.0f);   // D330 : le CC 10 s'ajoute au potentiomètre

    // D576.1 : LES INSERTS APRÈS LE FADER. Sans eux — le cas de toute piste d'avant — `sortieL` est la source et le
    // gain du fader reste à appliquer : le chemin d'avant, appel pour appel. Avec eux, le fader est appliqué ICI (volume,
    // polarité, CC 7, VCA, automation : `volume`), les inserts d'après traversent ce signal dans leur ordre, et ce qui
    // suit (largeur, panoramique, départs post-fader, mesures) prend leur sortie au gain 1. Une piste GELÉE ne les
    // repasse pas : le gel les a pris (D5.5), comme tous ses inserts.
    const float* sortieL = srcL;
    const float* sortieR = srcR;
    float gainFader = volume;
    if (!track.frozen && sampleCount <= static_cast<int>(apresL_.size())) {
        auto chaine = effectChains_[trackIndex].load(std::memory_order_acquire);
        bool apres = false;
        if (chaine)
            for (const auto& fx : *chaine)
                if (fx && fx->runsAfterFader()) { apres = true; break; }
        if (apres) {
            for (int i = 0; i < sampleCount; ++i) {
                apresL_[static_cast<size_t>(i)] = srcL[static_cast<size_t>(i)] * volume;
                apresR_[static_cast<size_t>(i)] = srcR[static_cast<size_t>(i)] * volume;
            }
            for (const auto& fx : *chaine) {
                if (!fx || !fx->runsAfterFader()) continue;
                const int bus = fx->sidechainBus();
                if (bus >= 1 && bus <= static_cast<int>(kMaxSends))
                    fx->setSidechainInput(sendL_[static_cast<size_t>(bus - 1)].data() + sampleStart,
                                          sendR_[static_cast<size_t>(bus - 1)].data() + sampleStart, sampleCount);
                fx->setTransportInfo(transportPiste_[trackIndex]);
                fx->process(apresL_.data(), apresR_.data(), sampleCount);
            }
            sortieL = apresL_.data();
            sortieR = apresR_.data();
            gainFader = 1.0f;
        }
    }

    // D573.1 : LA LARGEUR STÉRÉO resserre l'image AVANT le panoramique ; c'est le panneau (`panL`, `panR`) qui va à la
    // sortie, et aux départs qui suivent le panoramique (D572.2). Le RMS, la corrélation et les départs centrés prennent
    // la source ; la crête, que le mélange rend, est celle de l'image resserrée — une moyenne pondérée des deux canaux,
    // jamais plus haute. À 1, rien n'est calculé : les pointeurs sont ceux de la source, le chemin d'avant au bit près.
    // La borne est celle de `sendL_` (`sampleStart + sampleCount` ≤ `maxBlockSize_`) : elle ne tombe jamais.
    const float* panL = sortieL;
    const float* panR = sortieR;
    if (track.stereoWidth < 1.0f && sampleCount <= static_cast<int>(widthL_.size())) {
        narrowStereo(sortieL, sortieR, sampleCount, std::max(0.0f, track.stereoWidth), widthL_.data(), widthR_.data());
        panL = widthL_.data();
        panR = widthR_.data();
    }
    // D576.1 : UN DÉPART PRÉ-FADER LIÉ AU PANORAMIQUE prend la SOURCE (resserrée) ; sans inserts d'après, c'est `panL`.
    const float* prePanL = panL;
    const float* prePanR = panR;
    if (sortieL != srcL) {
        prePanL = srcL;
        prePanR = srcR;
        if (track.stereoWidth < 1.0f && track.sendsFollowPan && sampleCount <= static_cast<int>(largeurPreL_.size())) {
            narrowStereo(srcL, srcR, sampleCount, std::max(0.0f, track.stereoWidth), largeurPreL_.data(),
                         largeurPreR_.data());
            prePanL = largeurPreL_.data();
            prePanR = largeurPreR_.data();
        }
    }

    // D571.2 : LA LOI DE PANORAMIQUE DU PROJET. Le défaut (−3 dB) reste l'appel d'avant, mot pour mot.
    float peak = 0.0f;
    if (project.panLaw == vsm::sequencer::PanLaw::Minus3dB) {
        peak = mixStereoInto(panL, panR, sampleCount,
                             gainFader, pan, audible,
                             destL + sampleStart, destR + sampleStart);
    } else {
        float gainL = 1.0f, gainR = 1.0f;
        vsm::sequencer::panLawGains(project.panLaw, pan, gainL, gainR);
        peak = mixStereoWithGainsInto(panL, panR, sampleCount, gainFader, gainL, gainR, audible,
                                      destL + sampleStart, destR + sampleStart);
    }
    blockPeak_[trackIndex] = std::max(blockPeak_[trackIndex], peak);

    // RMS ET CORRÉLATION (D4.7) : on accumule les sommes ici et on ne
    // conclut qu'en fin de bloc. Le signal mesuré est celui d'APRÈS le
    // fader et AVANT le panoramique, c'est-à-dire ce que la piste envoie --
    // la même convention que la crête, pour que les trois chiffres parlent
    // du même son.
    for (int i = 0; i < sampleCount; ++i) {   // D576.1 : après les inserts d'après le fader, s'il y en a
        const double l = static_cast<double>(sortieL[static_cast<size_t>(i)]) * gainFader;
        const double r = static_cast<double>(sortieR[static_cast<size_t>(i)]) * gainFader;
        blockSumL2_[trackIndex] += l * l;
        blockSumR2_[trackIndex] += r * r;
        blockSumLR_[trackIndex] += l * r;
    }
    blockCount_[trackIndex] += sampleCount;

    // Sends post-fader vers les bus auxiliaires (section 15).
    const size_t actifs = activeSends_.load(std::memory_order_acquire);
    const uint32_t preFader = preFaderMask_.load(std::memory_order_acquire);
    // D572.2 : LES DÉPARTS QUI SUIVENT LE PANORAMIQUE reçoivent les gains du fader ci-dessus — le même panoramique
    // (automation et CC 10 compris), la même loi ; sans l'option, 1 et 1 : le chemin d'avant, au bit près.
    float departG = 1.0f, departD = 1.0f;
    if (track.sendsFollowPan) {
        if (project.panLaw == vsm::sequencer::PanLaw::Minus3dB) equalPowerPan(pan, departG, departD);
        else vsm::sequencer::panLawGains(project.panLaw, pan, departG, departD);
    }
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
            // D576.1 : un départ POST-fader prend la sortie des inserts d'après (`sortieL`, au gain `gainFader`).
            const bool avantFader = (preFader & (1u << b)) != 0;
            const float apresFader = avantFader ? signe : gainFader;
            const uint16_t bitDepart = static_cast<uint16_t>(1u << (kAutoSendFirst + b));
            // D567.3 : DES DÉPARTS CONTOURNÉS NE VERSENT RIEN, même pilotés par l'automation : le contournement
            // est un état de la tranche, pas un niveau, et la courbe le traverse sans le défaire.
            // D600.1 : `pilotes` a déjà retiré ce que la main tient — les départs compris.
            const float niveau = track.sendsBypassed                ? 0.0f
                                 : (pilotes & bitDepart) ? autoSend_[trackIndex][b]
                                                         : track.sendLevel(b);
            playedSend_[trackIndex][b].store(niveau, std::memory_order_relaxed);   // D600.1 : pour le relevé
            const float lvl = niveau * apresFader;
            if (lvl <= 0.0f) continue;
            if (!track.sendsFollowPan) {
                const float* tapL = avantFader ? srcL : sortieL;
                const float* tapR = avantFader ? srcR : sortieR;
                for (int i = 0; i < sampleCount; ++i) {
                    sendL_[b][static_cast<size_t>(sampleStart + i)] += tapL[static_cast<size_t>(i)] * lvl;
                    sendR_[b][static_cast<size_t>(sampleStart + i)] += tapR[static_cast<size_t>(i)] * lvl;
                }
            } else {
                const float lvlG = lvl * departG, lvlD = lvl * departD;
                const float* tapL = avantFader ? prePanL : panL;
                const float* tapR = avantFader ? prePanR : panR;
                for (int i = 0; i < sampleCount; ++i) {
                    sendL_[b][static_cast<size_t>(sampleStart + i)] += tapL[static_cast<size_t>(i)] * lvlG;
                    sendR_[b][static_cast<size_t>(sampleStart + i)] += tapR[static_cast<size_t>(i)] * lvlD;
                }
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

    // D18.7b : LES DEUX VAGUES DE RENDU. Sans publication de sortie, il n'y en
    // a qu'une et c'est l'ordre d'avant -- pas une addition ne change de place.
    // Avec, on rend d'abord les pistes qui ne lisent rien (les machines y
    // remplissent leurs tampons annexes), puis celles qui lisent.
    auto routage = outputRouting_.load(std::memory_order_acquire);
    const std::vector<size_t>* vagues[2] = {ordre.get(), nullptr};
    size_t tailles[2] = {combien, 0};
    size_t nombreDeVagues = 1;
    if (routage) {
        // REMISE À ZÉRO DES TAMPONS ANNEXES, en tête de segment et avant toute
        // vague. C'est ce qui fait qu'une piste dont la machine source ne
        // tourne pas (gelée, absente, sortie inexistante) sort SILENCIEUSE au
        // lieu de rejouer le segment précédent -- un défaut qui ne
        // ressemblerait pas à un défaut.
        for (size_t slot = 0; slot < routage->usedSlots && slot < kMaxExtraOutputs; ++slot) {
            std::fill_n(extraOutL_[slot].data(), sampleCount, 0.0f);
            std::fill_n(extraOutR_[slot].data(), sampleCount, 0.0f);
        }
        vagues[0] = &routage->firstWave;
        tailles[0] = routage->firstWave.size();
        vagues[1] = &routage->secondWave;
        tailles[1] = routage->secondWave.size();
        nombreDeVagues = 2;
    }

    if (parallele) {
        for (size_t v = 0; v < nombreDeVagues; ++v) {
            if (tailles[v] == 0) continue;
            VoiceBatch lot;
            lot.self = this;
            lot.snapshot = &snapshot;
            lot.order = vagues[v];
            lot.compensation = compensation.get();
            lot.sampleStart = sampleStart;
            lot.sampleCount = sampleCount;
            lot.rangeStartSeconds = rangeStartSeconds;
            lot.includeScheduledEvents = includeScheduledEvents;
            parallelSpans_.fetch_add(1, std::memory_order_relaxed);
            renderPool_.runParallel(&ProcessGraph::renderVoiceJob, &lot, tailles[v]);
        }

        // ET LE MIXAGE, LUI, DANS L'ORDRE : c'est ce qui rend le résultat
        // identique au bit près à celui du chemin mono-cœur. Il suit l'ordre
        // de rendu d'origine, PAS les vagues -- une piste publiée se mélange à
        // sa place dans la liste, là où l'utilisateur l'a mise.
        for (size_t rang = 0; rang < combien; ++rang) {
            const size_t trackIndex = ordre ? (*ordre)[rang] : rang;
            if (trackIndex >= kMaxTracks || !parallelActive_[trackIndex]) continue;
            mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                         parallelL_[trackIndex].data(), parallelR_[trackIndex].data(),
                         outputL, outputR);
        }
        return;
    }

    // LE CHEMIN MONO-CŒUR. Sans publication il n'a qu'une vague et le mixage
    // suit le rendu, exactement comme avant. Avec, il faut RENDRE en deux
    // vagues mais MÉLANGER dans l'ordre d'origine : on garde donc le résultat
    // de chaque piste jusqu'au mélange, et c'est à cela que servent les
    // tampons par piste -- ceux du rendu multicœur, qui existent déjà.
    if (!routage) {
        for (size_t rang = 0; rang < combien; ++rang) {
            const size_t trackIndex = ordre ? (*ordre)[rang] : rang;
            if (!renderTrackVoice(snapshot, trackIndex, sampleStart, sampleCount, rangeStartSeconds,
                                  includeScheduledEvents, compensation.get(), scratchEvents_.data(),
                                  scratchStereoL_.data(), scratchStereoR_.data()))
                continue;
            mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                         scratchStereoL_.data(), scratchStereoR_.data(), outputL, outputR);
        }
        return;
    }

    // SANS LES TAMPONS PAR PISTE, ON NE SAIT PAS DIFFÉRER LE MÉLANGE : on rend
    // alors dans l'ordre des vagues et l'on mélange au fil, ce qui reste
    // correct (chaque piste est mélangée une fois et une seule) mais change
    // l'ordre des additions. C'est le cas d'un graphe sans rendu multicœur
    // préparé, et il est DIT plutôt que subi.
    const bool tamponsParPiste = parallelL_.size() >= kMaxTracks;
    for (size_t v = 0; v < nombreDeVagues; ++v) {
        for (size_t rang = 0; rang < tailles[v]; ++rang) {
            const size_t trackIndex = (*vagues[v])[rang];
            if (trackIndex >= kMaxTracks) continue;
            float* gaucheL = tamponsParPiste ? parallelL_[trackIndex].data()
                                             : scratchStereoL_.data();
            float* gaucheR = tamponsParPiste ? parallelR_[trackIndex].data()
                                             : scratchStereoR_.data();
            const bool rendu = renderTrackVoice(
                snapshot, trackIndex, sampleStart, sampleCount, rangeStartSeconds,
                includeScheduledEvents, compensation.get(), scratchEvents_.data(),
                gaucheL, gaucheR);
            if (tamponsParPiste) { parallelActive_[trackIndex] = rendu ? 1u : 0u; continue; }
            if (rendu)
                mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                             gaucheL, gaucheR, outputL, outputR);
        }
    }
    if (!tamponsParPiste) return;
    for (size_t rang = 0; rang < combien; ++rang) {
        const size_t trackIndex = ordre ? (*ordre)[rang] : rang;
        if (trackIndex >= kMaxTracks || !parallelActive_[trackIndex]) continue;
        mixTrackInto(snapshot, anySolo, trackIndex, sampleStart, sampleCount,
                     parallelL_[trackIndex].data(), parallelR_[trackIndex].data(),
                     outputL, outputR);
    }
}

} // namespace vsm::audio::engine
