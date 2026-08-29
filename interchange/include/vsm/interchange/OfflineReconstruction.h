#pragma once
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/audio/io/WavFileWriter.h"
#include <string>
#include <vector>

// Reconstruction hors ligne (Phase 7, P8) : `dossier de projet -> render.wav`,
// sans interface, sans carte son, sans réseau.
//
// C'est la moitié DAW de la boucle décrite au § 9 de la roadmap :
//
//   original.wav -> Python -> (MIDI + presets + project.json)
//                -> VSM -> ProcessGraph -> OfflineRenderer -> reconstructed.wav
//                -> Python (comparaison, ajustement, on recommence)
//
// Le rendu passe par le MÊME `ProcessGraph::processBlock()` que la lecture
// temps réel (ARCHITECTURE.md § 5) : ce qu'on obtient ici est exactement ce
// qu'on entendrait dans l'application, à l'échantillon près. Sans cette
// propriété, une boucle d'optimisation pilotée par Python optimiserait un son
// que personne n'entend jamais.

namespace vsm::interchange {

struct RenderOptions {
    double sampleRate = 48000.0;
    int blockSize = 512;
    /// Durée ajoutée après la dernière note, pour laisser les résonances et
    /// les queues de réverbération s'éteindre au lieu d'être coupées net.
    double tailSeconds = 2.0;
    /// DÉBUT DE LA PLAGE EXPORTÉE (D6.1). 0 = le début du morceau.
    ///
    /// LE RENDU PART TOUJOURS DE ZÉRO ET LA PLAGE EST DÉCOUPÉE ENSUITE. C'est
    /// plus cher, et c'est le seul choix honnête : exporter une boucle prise au
    /// milieu d'un morceau doit rendre CE QU'ON Y ENTEND, c'est-à-dire avec la
    /// queue de réverbération, l'écho et le compresseur déjà engagés par ce qui
    /// précède. Un rendu qui démarrerait à froid à `startSeconds` produirait un
    /// extrait que personne n'a jamais entendu, et rien dans le fichier ne le
    /// dirait. Le surcoût est proportionnel à `startSeconds`, hors ligne, et
    /// payé une fois.
    double startSeconds = 0.0;
    /// Longueur du fichier PRODUIT (et non de la portion calculée).
    /// 0 = déduire du contenu du projet (dernière note + queue), à partir de
    /// `startSeconds`.
    double durationSeconds = 0.0;
    vsm::audio::io::SampleFormat format = vsm::audio::io::SampleFormat::Float32;
};

struct RenderResult {
    bool success = false;
    std::string error;
    double renderedSeconds = 0.0;
    size_t framesWritten = 0;
    float peakLevel = 0.0f;
    /// Pistes réellement sonorisées / pistes du projet : un projet dont
    /// l'instrument manque rend du silence, et doit le dire.
    size_t tracksWithInstrument = 0;
    size_t trackCount = 0;
    std::vector<std::string> warnings;

    std::string summary() const;
};

/// Rend un projet chargé vers un fichier WAV. Applique les presets sémantiques
/// aux machines avant de rendre ; chaque paramètre non pris en charge par la
/// machine cible est rapporté (jamais appliqué en douce).
RenderResult renderBundleToWav(const LoadedBundle& bundle, const std::string& wavPath,
                                const RenderOptions& options = {});

/// Le même rendu, mais rendu EN MÉMOIRE. Séparé depuis que le gel d'une piste
/// (D5.5) a besoin des échantillons sans avoir à écrire puis relire un fichier
/// temporaire.
RenderResult renderBundleToBuffer(const LoadedBundle& bundle,
                                   vsm::audio::engine::RenderedAudio& out,
                                   const RenderOptions& options = {});

/// GELER UNE PISTE (D5.5) : rendre ce qu'elle produit -- son instrument et ses
/// inserts -- pour qu'on cesse de le recalculer.
///
/// CE QUI EST CAPTURÉ EST LE SIGNAL D'AVANT LE FADER : le volume, le
/// panoramique, les départs, le muet et le solo doivent rester vivants, sinon
/// geler serait reporter. Or le mixage applique sa loi de panoramique en même
/// temps que le volume, et capturer le master les figerait dans le fichier.
///
/// D'OÙ DEUX RENDUS, ET CE N'EST PAS UNE RUSE. Aux extrémités, la loi de
/// panoramique vaut EXACTEMENT 1 et 0 : rendre la piste seule, tournée à fond à
/// gauche, donne le canal gauche INALTÉRÉ ; tournée à fond à droite, le canal
/// droit. Aucune division, aucun arrondi, et le résultat est bit à bit celui
/// que la piste produisait -- ce que le critère de l'étape demande en disant
/// « sonne identique ».
///
/// La piste rendue est isolée de tout le reste : ni départs, ni groupe, ni
/// tranche master, ni muet, ni solo. Ce qui l'entoure n'appartient pas à ce
/// qu'on gèle.
RenderResult renderTrackForFreeze(const LoadedBundle& bundle, size_t trackIndex,
                                   vsm::audio::engine::RenderedAudio& out,
                                   const RenderOptions& options = {});

// --- Export par stems (D6.2) ------------------------------------------------
//
// UN STEM EST LA CONTRIBUTION D'UNE PISTE AU MIXAGE, pas la piste toute seule.
// La différence est tout l'intérêt de la chose : le stem de la voix porte SON
// volume, SON panoramique, SES inserts et LA RÉVERBÉRATION QU'ELLE ENVOIE. On
// doit pouvoir le poser dans un autre logiciel, additionner les autres, et
// retrouver le mixage. C'est ce que le gel (D5.5) ne fait justement PAS : lui
// capture la piste avant son fader, pour que le mixage reste vivant.
//
// LA TRANCHE MASTER N'EST PAS DANS LES STEMS, et ce n'est pas un oubli. Un
// compresseur de master réagit au mixage entier ; il n'existe aucune façon de
// le répartir entre les pistes, et l'appliquer à chaque stem le ferait agir
// autant de fois qu'il y a de fichiers. Ce qui est exporté est donc ce qui
// ARRIVE au master. La chaîne de mastering appartient à celui qui reçoit les
// stems -- c'est d'ailleurs pour cela qu'on lui envoie des stems.

enum class StemGranularity {
    /// Une piste par fichier. Les bus de groupe sont traversés, pas exportés.
    Tracks,
    /// Un groupe par fichier ; les pistes hors groupe gardent le leur. Même
    /// somme, moins de fichiers.
    Groups,
};

struct Stem {
    std::string name;
    /// L'indice de la piste (ou du bus de groupe) dont ce fichier est la
    /// contribution.
    size_t trackIndex = 0;
    vsm::audio::engine::RenderedAudio audio;
};

struct StemResult {
    bool success = false;
    std::string error;
    std::vector<Stem> stems;
    std::vector<std::string> warnings;
    double renderedSeconds = 0.0;
};

/// Rend un stem par piste (ou par groupe). Chaque stem est un rendu complet du
/// projet où tout le reste est MUET : les départs, les groupes et l'automation
/// se comportent donc exactement comme dans le mixage.
///
/// CE QUE LA SOMME VAUT. Additionner les stems redonne le mixage AU BIT PRÈS
/// tant que tout ce qu'ils traversent est linéaire. Un insert non linéaire posé
/// sur un bus de groupe -- un compresseur, une saturation -- réagit au groupe
/// entier et ne se répartit pas : la somme s'écarte alors, et le rendu le DIT
/// dans ses avertissements plutôt que de laisser croire à une égalité.
StemResult renderStems(const LoadedBundle& bundle, StemGranularity granularity,
                        const RenderOptions& options = {});

/// Écrit les stems dans un dossier, un WAV par stem, nommés d'après la piste.
StemResult renderStemsToFolder(const LoadedBundle& bundle, const std::string& folderPath,
                                StemGranularity granularity, const RenderOptions& options = {});

/// Enchaîne chargement et rendu -- la fonction qu'appelle l'outil en ligne de
/// commande, et celle qu'un script Python pilote en pratique.
RenderResult renderProjectFolderToWav(const std::string& folderPath, const std::string& wavPath,
                                       const RenderOptions& options = {});

} // namespace vsm::interchange
