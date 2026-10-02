#include "vsm/interchange/AutomationResolution.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include <algorithm>
#include <cstdlib>

namespace vsm::interchange {

namespace {

using vsm::audio::engine::AutomationTarget;
using vsm::audio::engine::ProcessGraph;

bool commencePar(const std::string& texte, const char* prefixe) {
    return texte.rfind(prefixe, 0) == 0;
}

} // namespace

AutomationResolution resolveAutomation(const vsm::sequencer::Project& project) {
    AutomationResolution resultat;
    // La liste des réglages de la tranche master est celle de TOUTE tranche :
    // le constructeur la pose, rien ne la change ensuite.
    const vsm::audio::engine::MasterBus maitre;

    for (size_t i = 0; i < project.tracks.size(); ++i) {
        const auto& track = project.tracks[i];
        for (const auto& curve : track.automation) {
            if (curve.points.empty()) continue;
            const std::string& nom = curve.parameter;
            auto dire = [&](const std::string& pourquoi) {
                resultat.warnings.push_back(libellePiste(i, track.name) + " : automation « " + nom + " » : " +
                                            pourquoi);
            };
            if (i >= ProcessGraph::kMaxTracks) {
                dire("au-delà des pistes que le moteur joue");
                continue;
            }

            vsm::audio::engine::AutomationLane lane;
            lane.targetTrackIndex = i;
            // La plage du réglage visé, quand un descripteur la donne.
            const ParameterDescriptor* borne = nullptr;

            // LA RÉSOLUTION DU NOM. Chaque préfixe désigne une famille ; sans
            // préfixe connu, c'est un réglage de la machine de la piste, ce qui
            // fait que les projets d'avant D4.6 se relisent inchangés.
            if (nom == "mix.volume") {
                lane.target = AutomationTarget::TrackVolume;
            } else if (nom == "mix.pan") {
                lane.target = AutomationTarget::TrackPan;
            } else if (nom == "mix.trim") {   // D30.4
                lane.target = AutomationTarget::TrackTrim;
            } else if (commencePar(nom, "mix.send.")) {
                const int numero = std::atoi(nom.substr(9).c_str());
                if (numero < 1 || numero > static_cast<int>(ProcessGraph::kMaxSends)) {
                    dire("pas de départ de ce numéro (1 à " + std::to_string(ProcessGraph::kMaxSends) + ")");
                    continue;
                }
                lane.target = AutomationTarget::TrackSend;
                lane.targetSlot = static_cast<size_t>(numero - 1);
            } else if (commencePar(nom, "master.")) {
                const std::string reglage = nom.substr(7);
                bool trouve = false;
                for (const auto& info : maitre.parameterList())
                    if (info.name == reglage) {
                        lane.target = AutomationTarget::MasterParam;
                        lane.targetParam = info.id;
                        trouve = true;
                    }
                if (!trouve) {
                    dire("la tranche master n'a pas ce réglage");
                    continue;
                }
            } else if (commencePar(nom, "insert.")) {
                const size_t point = nom.find('.', 7);
                const int numero = point != std::string::npos ? std::atoi(nom.substr(7, point - 7).c_str()) : 0;
                if (numero < 1 || static_cast<size_t>(numero) > track.effects.size()) {
                    dire("la piste n'a pas d'insert de ce numéro");
                    continue;
                }
                const size_t slot = static_cast<size_t>(numero - 1);
                const auto profil = buildSemanticProfile(effectSemanticPluginId(track.effects[slot].type));
                const auto* d = profil.findBySemanticId(nom.substr(point + 1));
                if (d == nullptr) {
                    dire("l'insert « " + track.effects[slot].type + " » n'a pas ce réglage");
                    continue;
                }
                lane.target = AutomationTarget::InsertParam;
                lane.targetSlot = slot;
                lane.targetParam = d->paramId;
                borne = d;
            } else {
                if (track.instrumentId.empty()) {
                    dire("la piste n'a pas de machine");
                    continue;
                }
                const auto profil = buildSemanticProfile(track.instrumentId);
                const auto* d = profil.findBySemanticId(nom);
                if (d == nullptr) {
                    dire("la machine n'a pas ce paramètre");
                    continue;
                }
                lane.target = AutomationTarget::InstrumentParam;
                lane.targetParam = d->paramId;
                borne = d;
            }

            for (const auto& point : curve.points) {
                const float valeur =
                    borne != nullptr ? std::clamp(point.value, borne->minimum, borne->maximum) : point.value;
                lane.addPoint(point.tick, valeur,
                              point.step ? vsm::audio::engine::AutomationCurve::Step
                                         : vsm::audio::engine::AutomationCurve::Linear,
                              point.curve);
            }
            resultat.lanes.push_back(std::move(lane));
        }
    }
    return resultat;
}

} // namespace vsm::interchange
