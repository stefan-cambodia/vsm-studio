#pragma once
#include "vsm/sequencer/Project.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vsm::interchange {

/// IMPORT D'UN PROJET FAIT AILLEURS (docs/CDC-import-daw.md).
///
/// LA RÈGLE QUI PRIME, ET ELLE EXPLIQUE LA FORME DE CETTE INTERFACE : un import
/// partiel est utile, un import partiel **qu'on croit complet** est nuisible.
/// C'est pourquoi il n'y a pas de fonction qui rende un `Project` tout seul :
/// **tout import rend AUSSI son rapport**, et le rapport n'est pas un journal
/// de mise au point, c'est une partie du résultat. Le musicien doit pouvoir
/// lire, poste par poste, ce qui a été repris et ce qui ne pouvait pas l'être.
class DawImportError : public std::runtime_error {
public:
    explicit DawImportError(const std::string& quoi) : std::runtime_error(quoi) {}
};

/// Ce qu'un import a fait, et ce qu'il n'a pas pu faire.
struct DawImportReport {
    std::string sourceFormat;          ///< « Ableton Live », « FL Studio »…
    std::string sourceVersion;         ///< ce que le fichier dit de lui-même

    int midiTracksImported = 0;
    int notesImported = 0;
    int audioTracksSeen = 0;           ///< vues, et NON importées : dit pourquoi
    int clipsSeen = 0;
    int tracksWithoutInstrument = 0;   ///< toutes, aujourd'hui — voir le § 2 du CDC

    /// Une ligne par fait notable, dans l'ordre de la lecture. Ce sont ces
    /// lignes que l'interface montre : elles sont écrites pour un musicien,
    /// pas pour un programmeur.
    std::vector<std::string> lines;

    void note(const std::string& ligne) { lines.push_back(ligne); }
};

struct DawImportResult {
    vsm::sequencer::Project project;
    DawImportReport report;
};

/// Lit un projet **Ableton Live** (`.als`), gzippé ou non.
///
/// CE QUI EST REPRIS : tempo, nom du morceau, pistes MIDI avec leur nom, leur
/// couleur, leur état muet/solo, et toutes les notes des clips de
/// l'arrangement (hauteur, position, durée, vélocité).
///
/// CE QUI NE L'EST PAS, ET LE RAPPORT LE DIT : les instruments et les effets
/// (Operator, Wavetable, tout VST tiers) — une piste arrive donc SANS
/// instrument assigné, parce que convertir un patch d'Operator en `vsm.dx7`
/// reviendrait à inventer un son que personne n'a écrit ; les pistes audio,
/// dont on ne peut reprendre que la référence ; et l'automation.
DawImportResult importAbletonLive(const std::vector<uint8_t>& octets);

/// Même chose depuis un fichier. Lève `DawImportError` si le fichier manque.
DawImportResult importAbletonLiveFile(const std::string& chemin);

} // namespace vsm::interchange
