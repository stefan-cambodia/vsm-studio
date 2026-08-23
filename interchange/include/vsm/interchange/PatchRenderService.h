#pragma once
#include "vsm/interchange/Json.h"
#include <string>
#include <vector>

// Service de rendu de patch (Phase 7, P9 « API locale » — forme la plus
// simple qui tienne : des lignes JSON sur l'entrée/sortie standard).
//
// À QUOI ÇA SERT : le projet d'analyse (analyse/) cherche, par optimisation,
// les réglages qui reproduisent un son. Il a donc besoin de rendre UNE note
// avec UN jeu de paramètres, des milliers de fois. Passer par un dossier de
// projet et un processus par rendu coûte ~24 ms l'unité -- soit des heures
// pour une seule note, l'optimiseur faisant environ 1650 évaluations par
// forme d'onde. Ici, le processus reste vivant, les machines sont
// instanciées une fois, et une requête ne coûte plus que son rendu.
//
// POURQUOI PAS UNE API RÉSEAU : il n'y a rien à gagner. Les deux programmes
// tournent sur la même machine ; un tube fait le travail sans port à choisir,
// sans authentification à inventer, sans serveur à laisser tourner. La
// roadmap plaçait l'API locale en dernier précisément pour ne l'ajouter que
// si les fichiers ne suffisaient pas -- ils ne suffisent pas pour une boucle
// d'optimisation, ils suffisent pour tout le reste.

namespace vsm::interchange {

/// Une note à jouer dans la requête.
struct PatchNote {
    int noteNumber = 60;
    int velocity = 100;
    double startSeconds = 0.0;
    double durationSeconds = 1.0;
};

struct PatchRenderRequest {
    std::string requestId;            ///< renvoyé tel quel, pour apparier les réponses
    std::string machineId = "vsm.minimoog";
    double sampleRate = 48000.0;
    double durationSeconds = 1.0;
    int blockSize = 256;
    /// Paramètres par identifiant SÉMANTIQUE (`filter.1.cutoff`...) : le
    /// client n'a pas à connaître les identifiants internes de la machine.
    std::vector<std::pair<std::string, float>> parameters;
    std::vector<PatchNote> notes;
    /// Échantillons à charger avant le rendu, pour les machines qui en
    /// acceptent (`emplacement -> chemin de fichier`). Sans cela, le sampler
    /// serait inutilisable depuis le service -- et c'est précisément la
    /// machine dont la reconstruction a le plus besoin, puisque l'analyse
    /// découpe elle-même les coups à rejouer.
    std::vector<std::pair<int, std::string>> samples;
    /// PROFIL multi-échantillons à installer avant le rendu, pour les machines
    /// qui en lisent un. Explicite, jamais deviné : une machine à profil qui
    /// choisirait toute seule « le premier profil installé » rendrait des
    /// mesures dépendantes de ce qui traîne dans un dossier, et deux
    /// exécutions sur deux postes ne se compareraient plus.
    std::string profilePath;
    /// Chemin WAV de sortie (facultatif). Vide + `returnAudio` = audio renvoyé
    /// dans la réponse.
    std::string outputPath;
    /// "base64-f32-mono" pour recevoir l'audio directement dans la réponse.
    std::string returnAudio;

    /// LOT de jeux de paramètres à rendre en une seule requête (étape 10.1).
    ///
    /// Tous partagent la machine, les notes, la durée et la fréquence
    /// d'échantillonnage : c'est exactement le cas d'une génération
    /// d'optimiseur, où seuls les réglages changent. Vide = requête simple,
    /// et `parameters` s'applique alors normalement.
    ///
    /// CE QUE ÇA ÉCONOMISE, et ce que ça n'économise pas : le rendu lui-même
    /// coûte le même temps ; ce sont les N-1 allers-retours de tube, les
    /// analyses JSON et les créations d'instance qui disparaissent. Mesuré à
    /// 2,2 ms par requête, soit environ un cinquième du coût d'une évaluation.
    std::vector<std::vector<std::pair<std::string, float>>> batch;
};

struct PatchRenderResponse {
    std::string requestId;
    bool ok = false;
    std::string error;
    size_t frames = 0;
    float peak = 0.0f;
    double renderSeconds = 0.0;      ///< temps de calcul, pour mesurer
    std::string audioBase64;         ///< si returnAudio demandé
    /// Un élément par jeu de paramètres quand la requête portait un lot.
    /// `audioBase64` reste vide dans ce cas -- il n'y a pas « un » rendu.
    std::vector<std::string> batchAudioBase64;
    std::vector<float> batchPeaks;
    std::vector<std::string> warnings;

    JsonValue toJson() const;
};

/// Analyse une requête JSON. `error` non vide = requête refusée.
struct PatchRequestParseResult {
    bool success = false;
    PatchRenderRequest request;
    std::string error;
};
PatchRequestParseResult parsePatchRequest(const std::string& jsonLine);

/// Rend une requête. Garde en cache les machines déjà instanciées : c'est ce
/// qui rend le service utilisable dans une boucle d'optimisation.
class PatchRenderService {
public:
    PatchRenderService();
    ~PatchRenderService();

    PatchRenderResponse render(const PatchRenderRequest& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Boucle de service : lit des requêtes ligne par ligne sur `input`, écrit une
/// réponse JSON par ligne sur `output`. Une ligne vide est ignorée ; la fin de
/// flux termine la boucle.
int runPatchRenderLoop(std::istream& input, std::ostream& output, std::ostream& diagnostics);

} // namespace vsm::interchange
