#pragma once
#include "vsm/interchange/Json.h"
#include "vsm/sequencer/Groove.h"
#include <string>

namespace vsm::interchange {

/// UN GROOVE ENREGISTRÉ (D17.8) : le placement d'une partie, dans un fichier
/// `*.groove.json`, rangé dans la bibliothèque comme les presets de machine et
/// d'effet.
///
/// POURQUOI CELA S'ENREGISTRE. Un groove extrait d'une batterie reconstruite
/// n'a aucune raison de mourir avec le projet dont il vient : c'est justement
/// un objet qu'on veut porter d'un morceau à l'autre. C'est ce que Cubase
/// appelle un preset de quantification et Live le Groove Pool.
///
/// LE FICHIER NE CONTIENT AUCUN TICK, et c'est ce qui le rend portable : les
/// écarts sont en FRACTION de pas, et le nombre de pas par mesure est dit. Le
/// même fichier s'applique donc à un projet en 480 ppq comme en 960, à
/// n'importe quel tempo.
inline constexpr const char* kGroovePresetFormat = "vsm-groove";
inline constexpr int kGroovePresetVersion = 1;
inline constexpr const char* kGroovePresetExtension = ".groove.json";

JsonValue grooveToJson(const vsm::sequencer::Groove& groove);

struct GrooveLoadResult {
    bool success = false;
    vsm::sequencer::Groove groove;
    std::string error;
};
GrooveLoadResult grooveFromJson(const JsonValue& json);
GrooveLoadResult parseGroove(const std::string& jsonText);

} // namespace vsm::interchange
