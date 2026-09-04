#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include <filesystem>
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>

namespace vsm::interchange {

using vsm::audio::engine::OfflineRenderer;
using vsm::audio::engine::ProcessGraph;

std::string RenderResult::summary() const {
    std::ostringstream out;
    if (!success) return "échec : " + error;
    out << "rendu " << renderedSeconds << " s (" << framesWritten << " échantillons), "
        << tracksWithInstrument << "/" << trackCount << " piste(s) sonorisée(s), pic "
        << peakLevel;
    for (const auto& warning : warnings) out << "\n  " << warning;
    return out.str();
}

RenderResult renderBundleToBuffer(const LoadedBundle& bundle,
                                   vsm::audio::engine::RenderedAudio& out,
                                   const RenderOptions& options) {
    RenderResult result;
    result.trackCount = bundle.project.tracks.size();

    if (options.sampleRate <= 0.0 || options.blockSize <= 0) {
        result.error = "fréquence d'échantillonnage ou taille de bloc invalide";
        return result;
    }

    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    ProcessGraph graph;
    graph.prepare(options.sampleRate, options.blockSize);
    // LE RENDU HORS LIGNE PROFITE DES MÊMES CŒURS QUE LA LECTURE (D8.1), et il
    // le peut sans rien risquer : le multicœur ne change pas un échantillon
    // (test `process_graph_multicore_render_is_bit_identical`), donc un export
    // rendu à huit threads est le même fichier qu'à un seul, simplement obtenu
    // plus vite. Le nombre est celui recommandé pour la machine : personne ne
    // règle un nombre de threads pour un export.
    graph.setRenderThreadCount(ProcessGraph::recommendedRenderThreadCount());
    graph.setProject(bundle.project);

    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const std::string& pluginId = bundle.project.tracks[i].instrumentId;
        if (pluginId.empty()) {
            // UNE PISTE AUDIO N'A PAS D'INSTRUMENT, et ce n'est pas une
            // anomalie : son matériau est un fichier. L'avertir de son silence
            // serait faux, et noierait les vrais avertissements.
            if (bundle.project.tracks[i].kind != vsm::sequencer::Track::Kind::Audio)
                result.warnings.push_back("Piste " + std::to_string(i) + " (" + bundle.project.tracks[i].name +
                                           ") : aucun instrument, elle restera silencieuse");
            continue;
        }
        graph.setTrackInstrument(i, pluginId);
        auto* instrument = graph.trackInstrument(i);
        if (instrument == nullptr) {
            result.warnings.push_back("Piste " + std::to_string(i) + " : instrument \"" + pluginId +
                                       "\" indisponible");
            continue;
        }
        ++result.tracksWithInstrument;

        // Presets : appliqués APRÈS l'assignation de l'instrument, et jamais
        // en silence -- ce que la machine ne sait pas faire est rapporté.
        auto preset = bundle.presetsByTrack.find(i);
        if (preset == bundle.presetsByTrack.end()) continue;
        const PresetApplyReport applyReport = applyPreset(preset->second, *instrument, pluginId);
        if (applyReport.unsupportedCount() > 0 || applyReport.clampedCount() > 0)
            result.warnings.push_back("Piste " + std::to_string(i) + " : " + applyReport.summary());

        // ÉCHANTILLONS. Chargés après le preset, et avant toute lecture : la
        // lecture de fichiers n'a rien à faire dans le thread audio.
        //
        // Sans cette étape, un projet utilisant le sampler se chargeait
        // proprement, sans le moindre avertissement... et rendait du silence.
        // C'est exactement le genre de panne que ce projet refuse : une piste
        // muette doit dire pourquoi elle est muette.
        const SampleLoadReport sampleReport =
            applyPresetSamples(preset->second, *instrument, bundle.folderPath);
        if (!sampleReport.failures.empty())
            result.warnings.push_back("Piste " + std::to_string(i) + " : " + sampleReport.summary());
    }

    // PISTES AUDIO. Le fichier est décodé et rééchantillonné ICI, sur le thread
    // appelant : le graphe ne lit jamais un fichier. Un chargement qui échoue
    // est RAPPORTÉ -- une piste audio muette ne se distingue pas, à l'oreille,
    // d'une piste dont on aurait baissé le volume, et c'est exactement le
    // genre de panne que le § 5 bis interdit.
    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const auto& track = bundle.project.tracks[i];
        if (track.kind != vsm::sequencer::Track::Kind::Audio) continue;
        if (track.audio.empty()) {
            result.warnings.push_back("Piste " + std::to_string(i) + " (" + track.name +
                                       ") : piste audio sans fichier, elle restera silencieuse");
            continue;
        }
        // Chemin RELATIF au dossier de projet, comme les échantillons et les
        // presets : c'est ce qui permet au dossier d'être ouvert ailleurs.
        const std::string chemin =
            (std::filesystem::path(bundle.folderPath) / track.audio.path).string();
        // LE RENDU HORS LIGNE DIFFUSE COMME LA LECTURE, mais en ATTENDANT le
        // disque au lieu de se taire (D8.2). C'est ce qui permet d'exporter un
        // projet dont l'audio ne tiendrait pas en mémoire -- exactement celui
        // que D8.2 débloque -- sans que l'export soit une loterie.
        auto charge = vsm::audio::io::loadAudioTrack(
            chemin, options.sampleRate, vsm::audio::io::AudioLoadPolicy::Offline);
        if (!charge.success || !charge.source) {
            result.warnings.push_back("Piste " + std::to_string(i) + " : " + charge.error);
            continue;
        }
        if (charge.resampled)
            result.warnings.push_back(
                "Piste " + std::to_string(i) + " (" + track.name + ") : audio rééchantillonné de " +
                std::to_string(static_cast<int>(charge.fileSampleRate)) + " à " +
                std::to_string(static_cast<int>(charge.sessionSampleRate)) + " Hz");
        // LA LONGUEUR VIENT DU FICHIER RÉELLEMENT CHARGÉ, et non du nombre de
        // trames que le projet DÉCLARE. Les deux devraient coïncider ; quand
        // ils divergent -- fichier remplacé, métadonnée écrite à la main, en-
        // tête mal deviné -- c'est le fichier qui a raison, et faire confiance
        // à la déclaration tronquait le son sans rien dire. Mesuré : une voix
        // de 532 s déclarée à 266 s se coupait au milieu du morceau.
        vsm::sequencer::Track pourLesClips = track;
        pourLesClips.audio.sampleRate = options.sampleRate;
        pourLesClips.audio.frames = charge.source->frames();
        charge.source->clips = vsm::audio::engine::spansFromTrack(
            pourLesClips, options.sampleRate,
            [&](int64_t tick) { return bundle.project.ticksToSeconds(tick); });
        if (charge.source->clips.empty()) {
            result.warnings.push_back("Piste " + std::to_string(i) + " (" + track.name +
                                       ") : aucun clip audio à jouer");
            continue;
        }
        // LES CLIPS QUI SUIVENT LE TEMPO (D12.5) : le rendu hors ligne DOIT
        // armer les portées étirées comme l'application le fait, sinon un
        // clip calé s'exporterait sans son calage -- une panne muette, et
        // c'est exactement ainsi qu'elle a été trouvée (la mesure du critère
        // de phase D12.7 rendait trois fichiers identiques au bit près).
        vsm::audio::engine::prepareWarpedSpans(*charge.source);
        graph.setTrackAudio(i, charge.source);
        ++result.tracksWithInstrument;
    }

    // INSERTS. Ils étaient décrits dans `project.json` et jamais posés sur le
    // graphe : un projet portant une réverbération se rendait sans elle, sans
    // le moindre avertissement. Le fichier rendu ne correspondait donc pas au
    // projet, et rien ne permettait de s'en apercevoir autrement qu'à
    // l'oreille.
    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const auto& described = bundle.project.tracks[i].effects;
        if (described.empty()) continue;
        auto chain = std::make_shared<ProcessGraph::EffectChain>();
        for (const auto& entry : described) {
            auto effect = vsm::audio::effect::EffectFactory::create(entry.type);
            if (!effect) {
                // Type inconnu : nommé, jamais remplacé par un autre effet.
                result.warnings.push_back("Piste " + std::to_string(i) + " : effet « " + entry.type +
                                           " » inconnu, non appliqué");
                continue;
            }
            effect->prepare(options.sampleRate, options.blockSize);
            const EffectApplyReport applyReport = applyEffectDescription(entry, *effect);
            for (const auto& unknown : applyReport.unknownParameters)
                result.warnings.push_back("Piste " + std::to_string(i) + " : effet « " + entry.type +
                                           " », réglage inconnu « " + unknown + " »");
            chain->push_back(std::move(effect));
        }
        graph.setTrackEffectChain(i, chain);
    }

    // BUS DE DÉPART (D4.2). MÊME HISTOIRE QUE LES INSERTS, ET TROUVÉE DE LA
    // MÊME FAÇON -- en écrivant le test de D6.2, où le stem d'une piste qui
    // envoie 40 % dans la réverbération sortait identique à celui d'une piste
    // qui n'envoie rien.
    //
    // `setProject` posait bien les NIVEAUX de départ de chaque piste et le
    // masque pré-fader, mais rien ne posait l'EFFET des bus : le signal partait
    // dans un bus vide et disparaissait. Tout projet exporté depuis
    // l'application perdait donc sa réverbération et son delay de départ, en
    // silence -- exactement la panne que D0.3 avait corrigée pour les inserts,
    // et qui attendait ici.
    for (size_t bus = 0; bus < bundle.project.sends.size() && bus < ProcessGraph::kMaxSends; ++bus) {
        const auto& decrit = bundle.project.sends[bus];
        auto effect = vsm::audio::effect::EffectFactory::create(decrit.effectType);
        if (!effect) {
            result.warnings.push_back("Bus de départ « " + decrit.name + " » : effet « "
                                       + decrit.effectType + " » inconnu, non appliqué");
            continue;
        }
        vsm::sequencer::TrackEffect described;
        described.type = decrit.effectType;
        described.parameters = decrit.parameters;
        const EffectApplyReport applyReport = applyEffectDescription(described, *effect);
        for (const auto& unknown : applyReport.unknownParameters)
            result.warnings.push_back("Bus de départ « " + decrit.name + " » : réglage inconnu « "
                                       + unknown + " »");
        effect->prepare(options.sampleRate, options.blockSize);
        graph.setSendEffect(bus, std::shared_ptr<vsm::audio::effect::IAudioEffect>(std::move(effect)));
        graph.setSendReturn(bus, decrit.returnGain);
    }

    // TRANCHE MASTER. Même histoire : décrite dans le projet depuis qu'elle
    // s'y écrit, et sans effet sur le rendu tant que personne ne la reposait.
    if (!bundle.project.masterParameters.empty())
        applyMasterDescription(bundle.project.masterParameters, graph.masterBus());

    // AUTOMATION. Les courbes du document ciblent des identités SÉMANTIQUES ;
    // le moteur, lui, parle en ParamId. La résolution se fait ici, une fois,
    // et jamais en silence : une courbe qui vise un paramètre que la machine
    // n'a pas est RAPPORTÉE, pas ignorée -- une automation muette qui ne dit
    // pas pourquoi est exactement le genre de panne que ce projet refuse.
    std::vector<vsm::audio::engine::AutomationLane> automationLanes;
    for (size_t i = 0; i < bundle.document.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const auto& documentTrack = bundle.document.tracks[i];
        if (documentTrack.automation.empty()) continue;
        if (i >= bundle.project.tracks.size()) continue;
        const std::string& pluginId = bundle.project.tracks[i].instrumentId;
        if (pluginId.empty()) {
            result.warnings.push_back("Piste " + std::to_string(i) +
                                       " : automation sans instrument, ignorée");
            continue;
        }
        const SemanticProfile profile = buildSemanticProfile(pluginId);
        for (const auto& lane : documentTrack.automation) {
            const ParameterDescriptor* descriptor = profile.findBySemanticId(lane.parameter);
            if (descriptor == nullptr) {
                result.warnings.push_back("Piste " + std::to_string(i) + " : automation « " +
                                           lane.parameter + " » : la machine n'a pas ce paramètre");
                continue;
            }
            vsm::audio::engine::AutomationLane engineLane;
            engineLane.targetTrackIndex = i;
            engineLane.targetParam = descriptor->paramId;
            for (const auto& point : lane.points) {
                // Bornée à la plage RÉELLE du paramètre : une valeur hors
                // bornes serait écrêtée par la machine de toute façon, mais
                // l'écrêter ici garde l'interpolation dans le vrai espace.
                const float value = std::clamp(point.value, descriptor->minimum, descriptor->maximum);
                engineLane.addPoint(static_cast<vsm::midi::Tick>(point.tick), value,
                                     point.step ? vsm::audio::engine::AutomationCurve::Step
                                                : vsm::audio::engine::AutomationCurve::Linear);
            }
            automationLanes.push_back(std::move(engineLane));
        }
    }
    if (!automationLanes.empty())
        graph.setAutomationLanes(std::move(automationLanes));

    // Durée : jusqu'à la dernière note, plus une queue pour ne pas couper les
    // résonances. Un projet vide produit tout de même un fichier valide (la
    // queue), plutôt qu'un WAV de zéro échantillon que certains lecteurs
    // refusent d'ouvrir.
    const double debut = std::max(0.0, options.startSeconds);
    double duration = options.durationSeconds;
    if (duration <= 0.0) {
        // LA DERNIÈRE CHOSE QUI SONNE, et non la dernière NOTE (D8.3) : un
        // projet uniquement audio n'a pas de note, et son export durait donc
        // la seule queue de réverbération -- un fichier de deux secondes pour
        // neuf minutes de prise.
        const double finSeconds = bundle.project.ticksToSeconds(bundle.project.lastSoundingTick());
        duration = finSeconds + std::max(0.0, options.tailSeconds) - debut;
    }
    if (duration <= 0.0) duration = std::max(0.1, options.tailSeconds);

    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    // ON CALCULE DEPUIS ZÉRO, ON GARDE LA PLAGE. Voir la note de
    // `RenderOptions::startSeconds` : ce qui précède la plage n'est pas du
    // gaspillage, c'est ce qui met les effets dans l'état où l'oreille les
    // attend au moment où la plage commence.
    // D6.5 : QUI EXIGE LE TEMPS RÉEL ? On le demande à chaque machine et à
    // chaque effet réellement posés sur le graphe -- pas au projet, qui ne fait
    // que les nommer. Un plugin qui l'exige est honoré ET nommé : le rendu
    // devient cent fois plus lent, l'utilisateur doit savoir pourquoi.
    bool tempsReel = options.realTimeRender;
    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        if (auto* instrument = graph.trackInstrument(i))
            if (instrument->requiresRealtimeRender()) {
                tempsReel = true;
                result.warnings.push_back("la machine de la piste " + std::to_string(i + 1)
                                           + " exige un rendu en temps réel : le rendu le suit");
            }
    }

    auto rendered = OfflineRenderer::render(graph, options.sampleRate, options.blockSize,
                                             debut + duration, tempsReel);
    graph.setPlaying(false);

    if (debut > 0.0) {
        const size_t coupe = std::min(rendered.numFrames(),
                                       static_cast<size_t>(debut * options.sampleRate));
        rendered.left.erase(rendered.left.begin(), rendered.left.begin() + static_cast<long>(coupe));
        rendered.right.erase(rendered.right.begin(), rendered.right.begin() + static_cast<long>(coupe));
    }

    for (size_t i = 0; i < rendered.left.size(); ++i)
        result.peakLevel = std::max(result.peakLevel,
                                     std::max(std::abs(rendered.left[i]), std::abs(rendered.right[i])));

    out = rendered;
    result.success = true;
    result.renderedSeconds = duration;
    result.framesWritten = rendered.numFrames();
    return result;
}

RenderResult renderBundleToWav(const LoadedBundle& bundle, const std::string& wavPath,
                                const RenderOptions& options) {
    // ÉCRIRE, C'EST RENDRE PUIS POSER SUR LE DISQUE. Les deux moitiés sont
    // séparées depuis que le GEL (D5.5) a besoin de la première sans la
    // seconde : il lui faut les échantillons, pas un fichier temporaire à
    // relire aussitôt.
    vsm::audio::engine::RenderedAudio rendered;
    RenderResult result = renderBundleToBuffer(bundle, rendered, options);
    if (!result.success) return result;

    try {
        vsm::audio::io::WavFileWriter::writeFile(rendered.left.data(), rendered.right.data(),
                                                  rendered.numFrames(), options.sampleRate,
                                                  options.format, wavPath);
    } catch (const std::exception& e) {
        result.success = false;
        result.error = std::string("écriture WAV impossible : ") + e.what();
    }
    return result;
}

RenderResult renderTrackForFreeze(const LoadedBundle& bundle, size_t trackIndex,
                                   vsm::audio::engine::RenderedAudio& out,
                                   const RenderOptions& options) {
    RenderResult result;
    if (trackIndex >= bundle.project.tracks.size()) {
        result.error = "piste inexistante";
        return result;
    }

    // LA PISTE, SEULE ET DÉBARRASSÉE DE CE QUI L'ENTOURE. Ni départs, ni
    // groupe, ni tranche master, ni muet, ni solo : ce qui l'entoure
    // n'appartient pas à ce qu'on gèle, et le laisser entrerait le mixage
    // entier dans le fichier.
    LoadedBundle isolee;
    isolee.folderPath = bundle.folderPath;
    isolee.project = bundle.project;
    isolee.project.tracks.clear();
    isolee.project.sends.clear();
    isolee.project.masterParameters.clear();

    vsm::sequencer::Track piste = bundle.project.tracks[trackIndex];
    piste.muted = false;
    piste.solo = false;
    piste.outputGroup = -1;
    piste.sendLevels.clear();
    piste.volume = 1.0f;
    // Une piste DÉJÀ gelée se regèle depuis son matériau, pas depuis son gel :
    // sinon on empilerait des rendus de rendus.
    piste.frozen = false;
    isolee.project.tracks.push_back(std::move(piste));

    // L'automation du VOLUME et du PANORAMIQUE reste vivante après le gel : la
    // capturer la figerait dans le fichier ET continuerait de s'appliquer,
    // c'est-à-dire deux fois. Le reste -- les réglages de machine et d'insert --
    // est bien ce qu'on gèle, et doit donc être rendu.
    auto& courbes = isolee.project.tracks[0].automation;
    courbes.erase(std::remove_if(courbes.begin(), courbes.end(),
                                  [](const vsm::sequencer::AutomationCurve& c) {
                                      return c.parameter.rfind("mix.", 0) == 0
                                          || c.parameter.rfind("master.", 0) == 0;
                                  }),
                   courbes.end());
    isolee.document = documentFromProject(isolee.project);

    auto preset = bundle.presetsByTrack.find(trackIndex);
    if (preset != bundle.presetsByTrack.end()) isolee.presetsByTrack[0] = preset->second;

    // DEUX RENDUS, AUX DEUX EXTRÊMES DU PANORAMIQUE. À -1 la loi vaut
    // exactement (1, 0), à +1 exactement (0, 1) : chaque rendu livre donc UN
    // canal inaltéré, sans division ni arrondi. Voir l'en-tête pour le pourquoi.
    vsm::audio::engine::RenderedAudio gauche, droite;
    isolee.project.tracks[0].pan = -1.0f;
    result = renderBundleToBuffer(isolee, gauche, options);
    if (!result.success) return result;
    isolee.project.tracks[0].pan = 1.0f;
    RenderResult second = renderBundleToBuffer(isolee, droite, options);
    if (!second.success) return second;

    out.left = std::move(gauche.left);
    out.right = std::move(droite.right);
    // Les deux rendus ont la même durée par construction ; on le VÉRIFIE
    // plutôt que de le supposer, parce qu'un décalage d'un échantillon entre
    // les deux canaux s'entendrait comme un déplacement de l'image stéréo.
    const size_t taille = std::min(out.left.size(), out.right.size());
    out.left.resize(taille);
    out.right.resize(taille);
    result.framesWritten = taille;
    result.peakLevel = 0.0f;
    for (size_t i = 0; i < taille; ++i)
        result.peakLevel = std::max(result.peakLevel,
                                     std::max(std::abs(out.left[i]), std::abs(out.right[i])));
    return result;
}

namespace {

/// Ce qu'un caractère hors ASCII devient dans un nom de fichier : la lettre
/// sans son accent, la ligature en deux lettres, le point médian des noms de
/// voix (« Voix · tête ») en tiret. Le reste devient `_`. Sans cette table,
/// « Voix · tête » s'écrivait `Voix __ t__te.wav` (constaté le 03/09/2026 sur
/// les stems d'un projet à parité) : lisible par personne.
std::string translittere(uint32_t codePoint) {
    switch (codePoint) {
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return "o";
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return "O";
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
        case 0xFD: case 0xFF: return "y";
        case 0xE7: return "c"; case 0xC7: return "C";
        case 0xF1: return "n"; case 0xD1: return "N";
        case 0x153: return "oe"; case 0x152: return "OE";
        case 0xE6: return "ae"; case 0xC6: return "AE";
        case 0xDF: return "ss";
        case 0xB7: case 0x2013: case 0x2014: case 0x2022: return "-";   // · – — •
        case 0x2019: case 0x2018: return "";                              // ’ ‘
        default: return "_";
    }
}

/// Un nom de fichier sûr tiré du nom de la piste : ni séparateur, ni accent
/// perdu en route. Deux pistes homonymes existent (« Guitare » deux fois) et
/// s'écraseraient l'une l'autre ; le numéro de piste les sépare, en tête pour
/// que le dossier se lise dans l'ordre du mixeur.
std::string stemFileName(size_t index, const std::string& name) {
    std::string propre;
    for (size_t i = 0; i < name.size();) {
        const unsigned char u = static_cast<unsigned char>(name[i]);
        if (u < 0x80) {
            const char c = name[i++];
            if (std::isalnum(u) || c == '-' || c == '_' || c == ' ') propre += c;
            else propre += '_';
            continue;
        }
        // Décodage UTF-8 minimal : la longueur de la séquence se lit dans le
        // premier octet ; une séquence tronquée vaut `_`.
        size_t longueur = (u & 0xE0) == 0xC0 ? 2 : (u & 0xF0) == 0xE0 ? 3 : (u & 0xF8) == 0xF0 ? 4 : 1;
        if (i + longueur > name.size()) { propre += '_'; break; }
        uint32_t point = longueur == 2 ? (u & 0x1Fu) : longueur == 3 ? (u & 0x0Fu) : longueur == 4 ? (u & 0x07u) : 0u;
        for (size_t k = 1; k < longueur; ++k)
            point = (point << 6) | (static_cast<unsigned char>(name[i + k]) & 0x3Fu);
        propre += longueur == 1 ? std::string("_") : translittere(point);
        i += longueur;
    }
    while (!propre.empty() && propre.back() == ' ') propre.pop_back();
    if (propre.empty()) propre = "piste";
    std::ostringstream flux;
    flux << (index + 1 < 10 ? "0" : "") << (index + 1) << " - " << propre;
    return flux.str();
}

} // namespace

StemResult renderStems(const LoadedBundle& bundle, StemGranularity granularity,
                        const RenderOptions& options) {
    StemResult result;
    const auto& project = bundle.project;
    if (project.tracks.empty()) {
        result.error = "aucune piste à exporter";
        return result;
    }

    // CE QUI ROMPRAIT L'ÉGALITÉ EST ANNONCÉ D'ABORD. Un insert sur un bus de
    // groupe réagit au groupe entier : rendre une de ses pistes seule ne lui
    // présente pas le même signal, et la somme s'écarte. On le dit ; on ne
    // refuse pas pour autant, parce que des stems légèrement disjoints restent
    // utiles et que c'est à l'utilisateur de savoir.
    for (size_t i = 0; i < project.tracks.size(); ++i) {
        const auto& piste = project.tracks[i];
        if (piste.kind == vsm::sequencer::Track::Kind::Group && !piste.effects.empty())
            result.warnings.push_back("le groupe \"" + piste.name
                                       + "\" porte des inserts : la somme des stems peut s'écarter du mixage");
    }
    if (!project.masterParameters.empty())
        result.warnings.push_back(
            "la tranche master n'est pas dans les stems (c'est voulu) : leur somme rend le "
            "mixage AVANT master");

    // QUI VA DANS QUEL FICHIER. En mode « pistes », chacune le sien ; en mode
    // « groupes », toutes celles d'un groupe partagent le leur. Les bus de
    // groupe eux-mêmes ne sont jamais un stem à part : ils sont TRAVERSÉS par
    // les pistes qu'ils portent, et les compter en plus doublerait le son.
    struct Sortie {
        size_t indexRepresentant = 0;
        std::string nom;
        std::vector<size_t> pistes;
    };
    std::vector<Sortie> sorties;
    std::map<int, size_t> parGroupe;
    for (size_t i = 0; i < project.tracks.size(); ++i) {
        const auto& piste = project.tracks[i];
        if (piste.kind == vsm::sequencer::Track::Kind::Group) continue;
        if (granularity == StemGranularity::Groups && piste.outputGroup >= 0) {
            const auto trouve = parGroupe.find(piste.outputGroup);
            if (trouve != parGroupe.end()) {
                sorties[trouve->second].pistes.push_back(i);
                continue;
            }
            const size_t bus = static_cast<size_t>(piste.outputGroup);
            Sortie sortie;
            sortie.indexRepresentant = bus < project.tracks.size() ? bus : i;
            sortie.nom = bus < project.tracks.size() ? project.tracks[bus].name : piste.name;
            sortie.pistes.push_back(i);
            parGroupe[piste.outputGroup] = sorties.size();
            sorties.push_back(std::move(sortie));
            continue;
        }
        Sortie sortie;
        sortie.indexRepresentant = i;
        sortie.nom = piste.name;
        sortie.pistes.push_back(i);
        sorties.push_back(std::move(sortie));
    }

    for (const auto& sortie : sorties) {
        LoadedBundle rendu = bundle;
        // TOUT LE RESTE EST MUET -- et le muet coupe aussi les départs (D4.2),
        // ce qui est exactement ce qu'il faut : sans cela, la réverbération des
        // autres pistes se retrouverait dans chaque fichier.
        //
        // LES BUS DE GROUPE NE SONT JAMAIS COUPÉS : ils ne produisent rien
        // par eux-mêmes, ils portent. Couper celui d'une piste rendrait son
        // stem silencieux.
        for (size_t i = 0; i < rendu.project.tracks.size(); ++i) {
            auto& piste = rendu.project.tracks[i];
            piste.solo = false;
            if (piste.kind == vsm::sequencer::Track::Kind::Group) continue;
            piste.muted = std::find(sortie.pistes.begin(), sortie.pistes.end(), i)
                          == sortie.pistes.end()
                          || project.tracks[i].muted;
        }
        // La tranche master retourne à l'usine, c'est-à-dire transparente :
        // voir l'en-tête pour la raison, qui n'est pas une commodité.
        rendu.project.masterParameters.clear();
        rendu.document = documentFromProject(rendu.project);

        Stem stem;
        stem.name = stemFileName(sortie.indexRepresentant, sortie.nom);
        stem.trackIndex = sortie.indexRepresentant;
        const RenderResult un = renderBundleToBuffer(rendu, stem.audio, options);
        if (!un.success) {
            result.error = un.error;
            return result;
        }
        result.renderedSeconds = un.renderedSeconds;
        result.stems.push_back(std::move(stem));
    }

    result.success = true;
    return result;
}

StemResult renderStemsToFolder(const LoadedBundle& bundle, const std::string& folderPath,
                                StemGranularity granularity, const RenderOptions& options) {
    StemResult result = renderStems(bundle, granularity, options);
    if (!result.success) return result;

    std::error_code code;
    std::filesystem::create_directories(folderPath, code);
    for (const auto& stem : result.stems) {
        const std::string chemin =
            (std::filesystem::path(folderPath) / (stem.name + ".wav")).string();
        try {
            vsm::audio::io::WavFileWriter::writeFile(stem.audio.left.data(), stem.audio.right.data(),
                                                      stem.audio.numFrames(), options.sampleRate,
                                                      options.format, chemin);
        } catch (const std::exception& e) {
            result.success = false;
            result.error = std::string("écriture WAV impossible : ") + e.what();
            return result;
        }
    }
    return result;
}

RenderResult renderProjectFolderToWav(const std::string& folderPath, const std::string& wavPath,
                                       const RenderOptions& options) {
    BundleLoadResult loaded = loadProjectBundle(folderPath);
    if (!loaded.success) {
        RenderResult result;
        result.error = loaded.error;
        return result;
    }
    RenderResult result = renderBundleToWav(loaded.bundle, wavPath, options);
    // Les avertissements du CHARGEMENT (preset manquant, machine absente)
    // comptent autant que ceux du rendu : ils sont réunis dans un seul rapport,
    // pour qu'un script Python n'ait qu'un endroit à lire.
    result.warnings.insert(result.warnings.begin(), loaded.warnings.begin(), loaded.warnings.end());
    return result;
}

} // namespace vsm::interchange
