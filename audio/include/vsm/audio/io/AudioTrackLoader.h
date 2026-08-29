#pragma once
#include "vsm/audio/engine/AudioTrackSource.h"
#include <memory>
#include <string>

namespace vsm::audio::io {

/// Ce que le chargement d'une piste audio a réellement pu faire.
///
/// Rien n'est jamais deviné en silence : un fichier introuvable, illisible ou
/// d'une fréquence inattendue produit un message, pas un tampon vide qui
/// passerait pour une piste muette. C'est la règle du § 5 bis de
/// `ROADMAP-fusion.md`, appliquée à un cas où elle mord particulièrement fort :
/// une piste audio qui ne charge pas ne se distingue pas, à l'oreille, d'une
/// piste dont on aurait baissé le volume.
struct AudioTrackLoadResult {
    bool success = false;
    std::shared_ptr<vsm::audio::engine::AudioTrackSource> source;
    std::string error;
    /// Fréquence du fichier et fréquence de la session, quand elles diffèrent :
    /// le rééchantillonnage a eu lieu, et le rapport le dit.
    double fileSampleRate = 0.0;
    double sessionSampleRate = 0.0;
    bool resampled = false;
};

/// Lit un WAV et le prépare pour le graphe : décodage, puis rééchantillonnage
/// à la fréquence de la session s'il le faut.
///
/// LE RÉÉCHANTILLONNAGE EST UNE INTERPOLATION LINÉAIRE, et c'est une
/// approximation ASSUMÉE, documentée ici plutôt que découverte à l'oreille.
/// Elle atténue légèrement l'aigu et laisse un peu de repliement ; sur les
/// rapports courants -- 44,1 vers 48 kHz, soit 1,088 -- l'erreur reste sous le
/// millième pour tout ce qui vit sous 10 kHz. Un rééchantillonneur à noyau
/// fenêtré ferait mieux, et c'est ce qu'exigera l'étirement temporel (choix
/// n° 3 du § 4 de `ROADMAP-daw.md`) : il sera écrit là, avec le reste, plutôt
/// qu'emprunté à une bibliothèque que le dépôt devrait télécharger.
AudioTrackLoadResult loadAudioTrack(const std::string& path, double sessionSampleRate);

} // namespace vsm::audio::io
