#pragma once
#include "vsm/sequencer/Project.h"
#include <string>
#include <vector>

// Rapport de reconstruction (étape 11.3 de la feuille de route globale).
//
// À QUOI IL SERT ICI. La chaîne d'analyse transcrit un enregistrement en
// notes, et chaque note lui coûte plus ou moins d'effort : certaines sont
// franches, d'autres devinées au milieu d'un accord ou d'un bruit. Elle sait
// le dire -- elle produit une confiance par note -- mais cette information
// s'arrêtait au fichier JSON.
//
// La faire remonter jusqu'au piano roll change la nature du travail de
// correction : au lieu de réécouter le morceau entier en cherchant ce qui
// cloche, on voit d'emblée OÙ la transcription a hésité.
//
// CE QUE LA CONFIANCE NE FAIT PAS : elle ne modifie ni le rendu, ni l'export.
// Une note peu sûre se joue comme les autres. Décider à la place de
// l'utilisateur -- la taire, la supprimer -- serait pire que de ne rien
// signaler du tout.

namespace vsm::interchange {

inline constexpr const char* kReconstructionReportFormat = "vsm-reconstruction-report";

/// Confiance d'une note, telle que la transcription l'a relevée.
struct NoteConfidence {
    int noteNumber = 60;
    double startSeconds = 0.0;
    float confidence = 1.0f;
};

/// Ce qu'un rapport dit d'un stem.
struct StemReport {
    std::string name;          ///< nom de la piste, tel qu'il apparaît dans le projet
    std::string machine;
    double distance = 0.0;
    std::vector<NoteConfidence> notes;
};

struct ReconstructionReport {
    std::string metric = "v2";  ///< les distances v1 et v2 ne se comparent pas
    double globalDistance = -1.0;
    std::vector<StemReport> stems;

    const StemReport* findStem(const std::string& name) const;
};

struct ReportLoadResult {
    bool success = false;
    ReconstructionReport report;
    std::string error;
};

/// Lit un `rapport.json`. Absent ou illisible = échec explicite : un projet
/// s'ouvre très bien sans rapport, mais on ne fait pas semblant d'en avoir un.
ReportLoadResult loadReconstructionReport(const std::string& path);

/// Reporte les confiances du rapport sur les notes d'un projet.
///
/// L'APPARIEMENT SE FAIT PAR HAUTEUR ET INSTANT, à `toleranceSeconds` près, et
/// non par position dans la liste. Un index serait plus simple et faux : il
/// suffirait qu'une note soit ajoutée, supprimée ou déplacée dans l'éditeur
/// pour que toutes les confiances suivantes désignent la mauvaise note -- et
/// rien ne le signalerait.
///
/// Renvoie le nombre de notes effectivement renseignées.
size_t applyNoteConfidences(const ReconstructionReport& report,
                             vsm::sequencer::Project& project,
                             double toleranceSeconds = 0.03);

} // namespace vsm::interchange
