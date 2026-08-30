#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "vsm/audio/io/WavStreamReader.h"

// LE CACHE D'APERÇU D'UNE FORME D'ONDE (D5.7 de docs/ROADMAP-daw.md).
//
// LE PROBLÈME, ET IL EST DE TAILLE. Neuf minutes de stéréo à 48 kHz font
// cinquante-deux millions d'échantillons. Les parcourir pour dessiner un clip
// large de deux cents pixels, à chaque rafraîchissement, gèlerait l'interface
// -- et il faudrait recommencer au moindre défilement.
//
// LA RÉPONSE EST DE NE LES PARCOURIR QU'UNE FOIS. On garde, par tranche de
// quelques centaines d'échantillons, le minimum et le maximum : c'est ce qu'il
// faut pour dessiner, et c'est tout ce qu'il faut. Neuf minutes tiennent alors
// dans deux cent mille couples de flottants, soit un mégaoctet et demi -- et le
// dessin ne lit plus que ce qui est à l'écran.
//
// MINIMUM ET MAXIMUM, ET NON LA VALEUR ABSOLUE. Une forme d'onde dessinée à
// partir de l'amplitude seule est symétrique, donc fausse : elle cache les
// asymétries d'une caisse claire ou d'une voix, qui sont précisément ce qu'on
// reconnaît d'un coup d'œil en cherchant un passage.
//
// TOUT EST PUR ET SANS ALLOCATION CACHÉE : le calcul se fait sur un thread de
// fond, le dessin lit un tableau déjà là.

namespace vsm::audio::io {

/// Ce qu'on garde d'une tranche d'échantillons : ses deux extrêmes.
struct PeakBin {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

/// Nombre d'échantillons par tranche du cache.
///
/// 256 est un compromis mesuré et non choisi au hasard : à ce pas, une seconde
/// tient dans 188 tranches, donc un clip d'une seconde large de deux cents
/// pixels dispose de presque une tranche par pixel -- assez pour que la forme
/// soit juste --, et neuf minutes ne coûtent qu'un mégaoctet et demi.
inline constexpr int kSamplesPerPeakBin = 256;

/// Calcule le cache d'un signal stéréo (ou mono, en passant deux fois le même
/// canal). Les deux canaux sont RÉUNIS : on dessine la forme du clip, pas celle
/// d'un de ses côtés, et deux tracés superposés dans un rectangle de cinquante
/// pixels ne se distinguent pas.
std::vector<PeakBin> computePeaks(const float* left, const float* right, int64_t frames,
                                   int samplesPerBin = kSamplesPerPeakBin);

/// LE MÊME CACHE, MAIS SANS JAMAIS TENIR LE FICHIER EN MÉMOIRE (D8.2).
///
/// C'est le pendant obligé de la diffusion depuis le disque : il ne servirait à
/// rien de ne plus charger une prise de neuf minutes si dessiner son aperçu
/// exigeait quand même de la charger une fois. Le fichier est parcouru par
/// tranches de quelques secondes, et seuls les extrêmes sont gardés.
///
/// Les tranches sont indexées en trames de la SESSION, comme celles de
/// `computePeaks` : un fichier à 44,1 kHz dans une session à 48 kHz doit
/// dessiner sa forme d'onde à l'endroit où on l'entend.
std::vector<PeakBin> computePeaksFromFile(WavStreamReader& reader, double sessionSampleRate,
                                           int samplesPerBin = kSamplesPerPeakBin);

/// Réduit le cache à `columns` colonnes couvrant [startFrame, endFrame).
///
/// C'EST CETTE FONCTION QUE LE DESSIN APPELLE, et sa durée ne dépend QUE du
/// nombre de colonnes et de la portion visible -- jamais de la longueur du
/// fichier. C'est ce qui fait que neuf minutes s'affichent comme neuf secondes.
///
/// Une portion sans tranche (hors du fichier) rend des colonnes plates plutôt
/// qu'un tableau court : l'appelant dessine alors une ligne droite, ce qui est
/// exactement ce qu'il y a à voir.
std::vector<PeakBin> peaksForRange(const std::vector<PeakBin>& cache, int64_t startFrame,
                                    int64_t endFrame, int columns,
                                    int samplesPerBin = kSamplesPerPeakBin);

} // namespace vsm::audio::io
