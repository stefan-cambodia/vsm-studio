#include "vsm/interchange/MidiLearnStore.h"
#include "vsm/interchange/Json.h"
#include <array>

namespace vsm::interchange {

using vsm::audio::engine::MidiLearnKind;
using vsm::audio::engine::MidiLearnMap;
using vsm::audio::engine::MidiLearnTarget;

namespace {

/// LES GENRES SONT ÉCRITS EN TOUTES LETTRES, jamais par leur numéro. Un
/// `enum class` se réordonne un jour -- on insère une valeur au milieu -- et
/// tous les fichiers déjà écrits se mettraient alors à piloter autre chose,
/// silencieusement. Le nom, lui, survit au réordonnancement.
struct Nom { MidiLearnKind kind; const char* texte; const char* libelle; };

constexpr std::array<Nom, 10> kNoms{{
    {MidiLearnKind::InstrumentParam, "instrumentParam", "paramètre"},
    {MidiLearnKind::TrackVolume,     "trackVolume",     "volume"},
    {MidiLearnKind::TrackPan,        "trackPan",        "panoramique"},
    {MidiLearnKind::TrackMute,       "trackMute",       "muet"},
    {MidiLearnKind::TrackSolo,       "trackSolo",       "solo"},
    {MidiLearnKind::TrackSend,       "trackSend",       "départ"},
    {MidiLearnKind::TransportPlay,   "transportPlay",   "lecture"},
    {MidiLearnKind::TransportStop,   "transportStop",   "arrêt"},
    {MidiLearnKind::TransportRecord, "transportRecord", "enregistrement"},
    {MidiLearnKind::TransportLoop,   "transportLoop",   "boucle"},
}};

const char* texteDe(MidiLearnKind kind) {
    for (const auto& nom : kNoms) if (nom.kind == kind) return nom.texte;
    return "instrumentParam";
}

const char* libelleDe(MidiLearnKind kind) {
    for (const auto& nom : kNoms) if (nom.kind == kind) return nom.libelle;
    return "?";
}

bool kindDe(const std::string& texte, MidiLearnKind& out) {
    for (const auto& nom : kNoms)
        if (texte == nom.texte) { out = nom.kind; return true; }
    return false;
}

} // namespace

std::string midiLearnToJson(const MidiLearnMap& map) {
    JsonValue racine = JsonValue::makeObject();
    racine.set("format", JsonValue::makeString("vsm.midilearn.v1"));
    JsonValue liste = JsonValue::makeArray();
    for (const auto& entree : map.entries()) {
        JsonValue objet = JsonValue::makeObject();
        objet.set("controller", JsonValue::makeNumber(entree.controller));
        objet.set("kind", JsonValue::makeString(texteDe(entree.target.kind)));
        if (vsm::audio::engine::targetHasTrack(entree.target.kind))
            objet.set("track", JsonValue::makeNumber(static_cast<double>(entree.target.trackIndex)));
        if (entree.target.kind == MidiLearnKind::InstrumentParam)
            objet.set("param", JsonValue::makeNumber(static_cast<double>(entree.target.paramId)));
        if (entree.target.kind == MidiLearnKind::TrackSend)
            objet.set("slot", JsonValue::makeNumber(entree.target.slot));
        objet.set("min", JsonValue::makeFloat(entree.target.min));
        objet.set("max", JsonValue::makeFloat(entree.target.max));
        liste.append(std::move(objet));
    }
    racine.set("mappings", std::move(liste));
    return racine.toString(2);
}

MidiLearnLoadResult midiLearnFromJson(const std::string& text) {
    MidiLearnLoadResult resultat;
    // UN TEXTE VIDE EST UN SUCCÈS. Au premier lancement il n'y a rien
    // d'enregistré, et traiter cela comme une erreur ferait apparaître un
    // message au premier démarrage de chaque nouvelle installation.
    if (text.empty()) { resultat.success = true; return resultat; }

    JsonParseResult analyse = parseJson(text);
    if (!analyse.success) { resultat.error = "JSON invalide : " + analyse.error; return resultat; }
    const JsonValue& racine = analyse.value;
    if (!racine.isObject()) { resultat.error = "objet JSON attendu"; return resultat; }

    for (const auto& entree : racine["mappings"].elements()) {
        const double numero = entree["controller"].asNumber(-1.0);
        MidiLearnKind kind{};
        // ÉCARTÉE, PAS DEVINÉE. Un potentiomètre qui pilote autre chose que ce
        // qu'on croit est pire qu'un potentiomètre inerte.
        if (numero < 0.0 || numero > 127.0 || !kindDe(entree["kind"].asString(), kind)) {
            ++resultat.discarded;
            continue;
        }
        MidiLearnTarget cible;
        cible.kind = kind;
        cible.trackIndex = static_cast<size_t>(entree["track"].asNumber(0.0));
        cible.paramId = static_cast<vsm::audio::plugin::ParamId>(entree["param"].asNumber(0.0));
        cible.slot = static_cast<uint8_t>(entree["slot"].asNumber(0.0));
        cible.min = static_cast<float>(entree["min"].asNumber(0.0));
        cible.max = static_cast<float>(entree["max"].asNumber(1.0));
        cible.valid = true;
        resultat.map.bind(static_cast<uint8_t>(numero), cible);
    }
    resultat.success = true;
    return resultat;
}

std::string describeMidiLearnTarget(const MidiLearnTarget& target,
                                     const std::string& parameterName) {
    std::string texte;
    if (vsm::audio::engine::targetHasTrack(target.kind))
        texte += "piste " + std::to_string(target.trackIndex + 1) + " · ";
    if (target.kind == MidiLearnKind::InstrumentParam) {
        // LE NOM DU PARAMÈTRE VIENT DE LA MACHINE quand on l'a sous la main.
        // « paramètre 12 » n'aide personne à retrouver ce qu'il a réglé.
        texte += parameterName.empty() ? ("paramètre " + std::to_string(target.paramId))
                                        : parameterName;
    } else if (target.kind == MidiLearnKind::TrackSend) {
        texte += std::string(libelleDe(target.kind)) + " "
                 + std::string(1, static_cast<char>('A' + target.slot));
    } else {
        texte += libelleDe(target.kind);
    }
    return texte;
}

} // namespace vsm::interchange
