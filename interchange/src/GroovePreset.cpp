#include "vsm/interchange/GroovePreset.h"

namespace vsm::interchange {

JsonValue grooveToJson(const vsm::sequencer::Groove& groove) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kGroovePresetFormat));
    root.set("version", JsonValue::makeNumber(kGroovePresetVersion));
    root.set("name", JsonValue::makeString(groove.name));
    root.set("stepsPerBar", JsonValue::makeNumber(groove.stepsPerBar));

    JsonValue pas = JsonValue::makeArray();
    for (const auto& step : groove.steps) {
        JsonValue s = JsonValue::makeObject();
        // UN PAS ABSENT S'ÉCRIT ABSENT, et non « écart nul » : les deux se
        // liraient pareil et se comporteraient différemment (un pas muet ne
        // déplace rien, un écart nul remet la note sur la grille).
        s.set("present", JsonValue::makeBoolean(step.present));
        if (step.present) {
            s.set("offset", JsonValue::makeFloat(step.offset));
            if (step.velocity > 0.0f) s.set("velocity", JsonValue::makeFloat(step.velocity));
        }
        pas.append(std::move(s));
    }
    root.set("steps", std::move(pas));
    return root;
}

GrooveLoadResult grooveFromJson(const JsonValue& json) {
    GrooveLoadResult resultat;
    const std::string format = json["format"].asString();
    if (format != kGroovePresetFormat) {
        // NOMMÉ, JAMAIS DEVINÉ : un `*.synth.json` déposé là ne doit pas être
        // lu comme un groove vide, il doit dire ce qu'il est.
        resultat.error = "Ce fichier n'est pas un groove (format « " + format + " »).";
        return resultat;
    }
    const int version = static_cast<int>(json["version"].asNumber(0.0));
    if (version <= 0 || version > kGroovePresetVersion) {
        resultat.error = "Version de groove inconnue : " + std::to_string(version);
        return resultat;
    }

    vsm::sequencer::Groove groove;
    groove.name = json["name"].asString();
    groove.stepsPerBar = static_cast<int>(json["stepsPerBar"].asNumber(16.0));
    if (groove.stepsPerBar <= 0) {
        resultat.error = "Un groove sans pas par mesure ne s'applique à rien.";
        return resultat;
    }
    for (const auto& s : json["steps"].elements()) {
        vsm::sequencer::GrooveStep step;
        step.present = s["present"].asBoolean(false);
        step.offset = s["offset"].asNumber(0.0);
        step.velocity = static_cast<float>(s["velocity"].asNumber(0.0));
        groove.steps.push_back(step);
    }
    if (groove.steps.empty()) {
        resultat.error = "Un groove sans pas ne décrit aucun placement.";
        return resultat;
    }
    resultat.groove = std::move(groove);
    resultat.success = true;
    return resultat;
}

GrooveLoadResult parseGroove(const std::string& jsonText) {
    const auto lu = parseJson(jsonText);
    if (!lu.success) {
        GrooveLoadResult resultat;
        resultat.error = lu.error;
        return resultat;
    }
    return grooveFromJson(lu.value);
}

} // namespace vsm::interchange
