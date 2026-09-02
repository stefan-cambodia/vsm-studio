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

    /// LE GARDE-FOU DES FORMATS BINAIRES (voir le § 3 bis du CDC). Sur un
    /// `.flp`, la STRUCTURE est certaine mais le SENS des identifiants est
    /// reconstitué : compter ce qu'on a reconnu et ce qu'on n'a pas reconnu
    /// permet au musicien de voir, sans avoir à nous croire, si la lecture a
    /// mordu. Un lecteur qui se tromperait d'identifiants le montrerait ici.
    int eventsRead = 0;
    int eventsUnderstood = 0;

    /// LA GRAVITÉ D'UNE LIGNE, décidée PAR CELUI QUI L'ÉCRIT.
    ///
    /// La première interface devinait l'importance d'une ligne en cherchant
    /// des sous-chaînes dans sa prose (« ATTENTION », « AUCUN instrument »,
    /// « ignor »…) — et s'est déjà trompée : « Total : … 1 piste(s) audio
    /// ignorée(s) » contenait « ignor » et se peignait en avertissement, ce
    /// qu'une capture d'écran a montré et qu'aucun test ne gardait. Un lecteur
    /// qui reformule sa phrase ne doit pas pouvoir décolorer un avertissement
    /// en silence : l'auteur de la ligne SAIT sa gravité au moment où il
    /// l'écrit — il l'écrivait déjà, en majuscules, dans la prose.
    ///
    ///  - `info`      : un fait repris ou un décompte ;
    ///  - `attention` : quelque chose a été DEVINÉ ou est peut-être faux
    ///                  (arrangement posé bout à bout, identifiant suspect) ;
    ///  - `perte`     : quelque chose du projet d'origine n'arrive pas
    ///                  (piste audio, instrument non assigné).
    enum class Gravite : uint8_t { info, attention, perte };

    struct Ligne {
        std::string texte;
        Gravite gravite = Gravite::info;
    };

    /// Une ligne par fait notable, dans l'ordre de la lecture. Ce sont ces
    /// lignes que l'interface montre : elles sont écrites pour un musicien,
    /// pas pour un programmeur.
    std::vector<Ligne> lines;

    void note(std::string ligne, Gravite gravite = Gravite::info) {
        lines.push_back({std::move(ligne), gravite});
    }
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

/// Lit un projet **FL Studio** (`.flp`).
///
/// CE QUI EST REPRIS : tempo, PPQ, titre, noms des canaux du rack, et les
/// notes de tous les motifs — une piste par canal, comme dans le rack de FL.
///
/// CE QUI EST APPROCHÉ, ET LE RAPPORT LE DIT : **l'arrangement**. Si l'ordre
/// des motifs dans la playlist est lisible, il est suivi ; sinon les motifs
/// sont posés BOUT À BOUT dans l'ordre de leurs numéros. Un morceau dont
/// l'arrangement est deviné n'est pas le morceau du musicien, et il doit le
/// savoir avant de chercher pourquoi.
///
/// CE QUI NE L'EST PAS : les instruments (chaque canal du rack porte un
/// générateur — Sytrus, Harmless, un VST — qui n'existe pas ici), les effets,
/// et l'automation.
DawImportResult importFlStudio(const std::vector<uint8_t>& octets);
DawImportResult importFlStudioFile(const std::string& chemin);

/// Lit une **Track Archive de Cubase** (`.xml`), ce que Steinberg exporte par
/// *Fichier ▸ Exporter ▸ Archive de pistes*.
///
/// POURQUOI CE CHEMIN ET PAS LE `.cpr` : le format de projet de Cubase est
/// fermé et sans documentation exploitable ; un lecteur écrit au jugé marcherait
/// sur un fichier et casserait sur le suivant (§ 4 du CDC). La Track Archive,
/// elle, est du XML et contient ce qui compte : les pistes, leurs noms, et les
/// notes.
///
/// COMMENT IL LIT, ET C'EST DÉLIBÉRÉ : une Track Archive imbrique ses objets
/// différemment selon la version de Cubase et selon ce qu'on exporte. Le
/// lecteur ne suit donc AUCUN chemin fixe — il cherche les objets qui portent
/// la SIGNATURE d'une note (un début, une longueur, une hauteur) et ceux qui
/// portent un nom de piste. C'est plus robuste qu'un chemin, et cela se dit :
/// un fichier dont la structure changerait donnerait moins de notes, pas des
/// notes fausses.
DawImportResult importCubaseTrackArchive(const std::vector<uint8_t>& octets);
DawImportResult importCubaseTrackArchiveFile(const std::string& chemin);

/// Reconnaît le format d'après le contenu (et, à défaut, d'après l'extension)
/// puis appelle le bon lecteur.
///
/// **Devant un `.cpr`, cette fonction ne tente rien et EXPLIQUE** : elle lève
/// une erreur qui nomme les deux chemins praticables (Track Archive XML, MIDI
/// Type 1). Un message qui oriente vaut mieux qu'un import qui échoue sans
/// dire pourquoi — et infiniment mieux qu'un import qui invente.
DawImportResult importDawProject(const std::vector<uint8_t>& octets,
                                 const std::string& nomDuFichier = {});
DawImportResult importDawProjectFile(const std::string& chemin);

} // namespace vsm::interchange
