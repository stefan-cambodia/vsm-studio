#pragma once
#include "vsm/audio/engine/AudioTrackSource.h"
#include <cstddef>
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
    /// Le matériau est-il DIFFUSÉ depuis le disque plutôt que résident (D8.2) ?
    /// Le rapport le dit, comme il dit le rééchantillonnage : c'est une
    /// propriété de ce qui a été chargé, et l'interface doit pouvoir l'écrire.
    bool streamed = false;
    /// Ce que cette piste occupe en mémoire vive, une fois chargée.
    size_t residentBytes = 0;
};

/// COMMENT LE MATÉRIAU EST TENU (D8.2).
///
/// `Automatic` tranche sur la DURÉE, et c'est le bon critère : ce qui est court
/// est lu cent fois et doit répondre à l'échantillon près (un coup de caisse
/// claire de trois secondes n'a rien à faire sur le disque) ; ce qui est long
/// est lu une fois d'un bout à l'autre, et c'est exactement ce qu'un cache
/// glissant sert. Le seuil est à vingt secondes : au-dessus, plus rien n'est
/// un « échantillon ».
///
/// `Offline` applique EXACTEMENT le même seuil, mais la lecture va chercher
/// elle-même ce qui manque au lieu de l'attendre d'un thread. Un export dans
/// lequel il manquerait ce que le disque n'a pas eu le temps de livrer ne serait
/// pas un export, ce serait une loterie.
///
/// `ForceResident` ne diffuse jamais. Il existe pour les cas où le matériau
/// doit être là tout entier quoi qu'il en coûte -- et pour pouvoir comparer les
/// deux chemins dans un test.
enum class AudioLoadPolicy { Automatic, Offline, ForceResident };

/// Au-delà de cette durée, `Automatic` diffuse au lieu de charger.
inline constexpr double kStreamAboveSeconds = 20.0;

/// Lit un WAV et le prépare pour le graphe : décodage, puis rééchantillonnage
/// à la fréquence de la session s'il le faut.
///
/// LE RÉÉCHANTILLONNAGE EST UN NOYAU SINC SOUS FENÊTRE DE KAISER (D12.1,
/// `vsm/audio/dsp/SincResampler.h`), depuis le 04/09/2026. Il a remplacé
/// l'interpolation linéaire de D2, approximation assumée à l'époque et
/// chiffrée (10⁻³ sous 10 kHz sur 44,1 → 48 kHz) : mesuré, le noyau de 64
/// points tient 3 × 10⁻⁵ jusqu'à 18 kHz et 10⁻⁵ à 20 kHz, là où le linéaire
/// était à 10⁻¹ dès 10 kHz — et il coupe ce qui replierait en
/// sous-échantillonnage, que le linéaire laissait passer. Écrit ici, comme le
/// choix n° 3 du § 4 de `ROADMAP-daw.md` le demandait, plutôt qu'emprunté.
AudioTrackLoadResult loadAudioTrack(const std::string& path, double sessionSampleRate,
                                    AudioLoadPolicy policy = AudioLoadPolicy::Automatic);

} // namespace vsm::audio::io
