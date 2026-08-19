#include "vsm/interchange/ReconstructionReport.h"
#include "vsm/interchange/Json.h"
#include "vsm/interchange/ProjectBundle.h"
#include <algorithm>
#include <cmath>

namespace vsm::interchange {

const StemReport* ReconstructionReport::findStem(const std::string& name) const {
    for (const auto& stem : stems)
        if (stem.name == name) return &stem;
    return nullptr;
}

ReportLoadResult loadReconstructionReport(const std::string& path) {
    ReportLoadResult result;
    std::string text;
    if (!readTextFile(path, text, result.error)) return result;

    JsonParseResult parsed = parseJson(text);
    if (!parsed.success) { result.error = "JSON invalide : " + parsed.error; return result; }
    const JsonValue& root = parsed.value;
    if (!root.isObject()) { result.error = "objet JSON attendu"; return result; }

    const std::string format = root["format"].asString();
    if (format != kReconstructionReportFormat) {
        // Refus explicite : lire un fichier d'un autre format « au mieux »
        // produirait des confiances inventées, donc des notes signalées au
        // hasard -- pire que pas de signalement du tout.
        result.error = "format inattendu : \"" + format + "\" (attendu \""
                     + kReconstructionReportFormat + "\")";
        return result;
    }

    ReconstructionReport report;
    report.metric = root["metric"].asString("v1"); // ancien rapport sans champ = v1
    report.globalDistance = root["globalDistance"].asNumber(-1.0);

    for (const auto& entry : root["stems"].elements()) {
        StemReport stem;
        stem.name = entry["name"].asString();
        stem.machine = entry["machine"].asString();
        stem.distance = entry["distance"].asNumber(0.0);
        for (const auto& note : entry["noteConfidence"].elements()) {
            NoteConfidence confidence;
            confidence.noteNumber = static_cast<int>(note["note"].asNumber(60.0));
            confidence.startSeconds = note["start"].asNumber(0.0);
            confidence.confidence = static_cast<float>(note["confidence"].asNumber(1.0));
            stem.notes.push_back(confidence);
        }
        report.stems.push_back(std::move(stem));
    }

    result.success = true;
    result.report = std::move(report);
    return result;
}

size_t applyNoteConfidences(const ReconstructionReport& report,
                             vsm::sequencer::Project& project,
                             double toleranceSeconds) {
    size_t renseignees = 0;
    for (auto& track : project.tracks) {
        const StemReport* stem = report.findStem(track.name);
        if (stem == nullptr || stem->notes.empty()) continue;

        for (auto& note : track.notes) {
            const double instant = project.ticksToSeconds(note.startTick);
            // Meilleure correspondance : même hauteur, instant le plus proche.
            // On prend le MEILLEUR candidat et non le premier acceptable : sur
            // une note répétée rapidement, le premier dans la tolérance n'est
            // pas forcément le bon.
            const NoteConfidence* meilleur = nullptr;
            double meilleurEcart = toleranceSeconds;
            for (const auto& candidat : stem->notes) {
                if (candidat.noteNumber != static_cast<int>(note.number)) continue;
                const double ecart = std::abs(candidat.startSeconds - instant);
                if (ecart <= meilleurEcart) { meilleurEcart = ecart; meilleur = &candidat; }
            }
            if (meilleur != nullptr) {
                note.confidence = std::clamp(meilleur->confidence, 0.0f, 1.0f);
                ++renseignees;
            }
        }
    }
    return renseignees;
}

} // namespace vsm::interchange
