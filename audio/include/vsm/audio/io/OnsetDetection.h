#pragma once
#include <cstdint>
#include <functional>
#include <vector>

// D20.3 de docs/ROADMAP-daw.md — DÉTECTER LES ATTAQUES.
//
// `detectSound` (D17.6) trouve les BORNES de ce qui sonne ; rien ne trouvait
// les ATTAQUES à l'intérieur. Découper une boucle de batterie en huit coups se
// faisait à l'œil, sur une forme d'onde où la caisse claire et la grosse
// caisse se ressemblent. Cubase appelle cela des hitpoints, Live des slices ;
// c'est le geste manuel dont la chaîne d'analyse fait l'équivalent automatique
// quand elle découpe un stem de batterie en kit.
//
// LA MÉTHODE, ET CE QUE LA MESURE A CHANGÉ. La première version était un flux
// d'énergie TOTALE : l'énergie efficace de trames courtes (20 ms, pas de 5 ms)
// contre la moyenne glissante de ce qui précède (50 ms), une attaque étant une
// trame qui bondit de `sensitivityDb`. Elle passait ses tests sur des impulsions
// dans du bruit et ne trouvait RIEN sur le stem de TR-909 du banc : une grosse
// caisse et un charleston ouvert y traînent à -20 dB pendant toute la mesure,
// et chaque frappe de charleston ne fait monter le tout que de 0,8 à 2,4 dB
// (mesuré aux huit instants vrais ; seule la seconde grosse caisse atteignait
// 7,4 dB). Dans la bande haute, la même frappe bondit de vingt décibels. Le
// flux se calcule donc PAR BANDE -- le tout, le grave sous 200 Hz, le médium,
// l'aigu au-dessus de 2 kHz, trois biquads --, chaque bande avec sa propre
// moyenne de ce qui précède ; une attaque est un bond de `sensitivityDb` dans
// l'une d'elles, le tout étant au-dessus d'un plancher absolu (une bande peut
// bondir dix décibels plus bas que lui). Deux attaques ne se suivent jamais à
// moins de `minGapSeconds` : le rebond d'une peau ne fait qu'un coup.
//
// L'INSTANT rendu est la montée la plus raide dans la trame qui a bondi --
// l'échantillon où l'énergie de la milliseconde qui suit dépasse le plus
// celle des cinq millisecondes qui précèdent --,
// moins une marge avant l'attaque : couper à l'échantillon exact rabote le
// transitoire, la même raison que pour `detectSound`. « Le premier échantillon
// au-dessus du plancher » aurait été n'importe où dans une queue.
//
namespace vsm::audio::io {

/// Les attaques trouvées dans `[0, frames)`, en trames, croissantes.
///
/// `frameAt` rend un échantillon (les deux canaux), comme pour `detectSound` :
/// un rappel plutôt qu'un pointeur, parce que le matériau peut être diffusé
/// depuis le disque.
///
/// `sensitivityDb` : de combien l'énergie doit bondir au-dessus de ce qui
/// précède (8 dB : net sur une batterie, sans se déclencher sur un vibrato).
/// `minGapSeconds` : l'écart minimal entre deux attaques (50 ms).
/// `floorDb` : le plancher absolu de crête sous lequel rien n'est une attaque.
/// `preAttackSeconds` : la marge rendue AVANT l'instant trouvé.
std::vector<int64_t> detectOnsets(const std::function<bool(int64_t, float&, float&)>& frameAt,
                                  int64_t frames, double sampleRate,
                                  double sensitivityDb = 8.0, double minGapSeconds = 0.050,
                                  double floorDb = -50.0, double preAttackSeconds = 0.003);

} // namespace vsm::audio::io
