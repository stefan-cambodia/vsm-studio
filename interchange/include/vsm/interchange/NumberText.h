#pragma once
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

// UN NOMBRE ÉCRIT EN TEXTE N'APPARTIENT PAS À LA LOCALE DU PROCESSUS.
//
// Cette règle a été apprise par une panne, et elle est écrite ici pour qu'on
// n'ait plus à l'apprendre : `strtod`, `atof`, `stod` et `snprintf("%g")`
// consultent tous `LC_NUMERIC`. Un programme C++ n'installe aucune locale de
// lui-même -- mais JUCE le fait pour lui : `juce_SystemStats_linux.cpp` appelle
// `setlocale(LC_ALL, "")` puis « restaure » ce que cet appel vient de RENDRE,
// c'est-à-dire la nouvelle locale. Le processus reste donc dans celle de
// l'environnement pour de bon, et sur une machine réglée en français
// l'application écrivait `"EQ Mid Q": 0,8` dans ses sauvegardes -- que son
// propre lecteur refusait -- tout en lisant `0.8` comme `0`, sans un mot.
//
// `std::from_chars` est défini en locale C, toujours. Le problème ne se répare
// pas au cas par cas : il cesse d'exister.
//
// PÉRIMÈTRE : les nombres qui TRAVERSENT UNE FRONTIÈRE -- un fichier, une ligne
// de commande, un tube. Ce qui est montré à un être humain dans sa langue
// (l'affichage d'un paramètre par un hôte de plugin, par exemple) relève au
// contraire de sa locale, et n'a rien à faire ici.

namespace vsm::interchange {

/// Lit un nombre qui doit occuper TOUT le texte. Renvoie false si le texte est
/// vide, mal formé, ou suivi de quoi que ce soit -- une valeur à moitié lue est
/// la façon dont un mauvais argument devient un résultat plausible.
inline bool numberFromText(std::string_view text, double& out) {
    const char* first = text.data();
    const char* last = first + text.size();
    if (first == last) return false;
    double value = 0.0;
    const auto lu = std::from_chars(first, last, value);
    if (lu.ec != std::errc() || lu.ptr != last) return false;
    out = value;
    return true;
}

/// Variante pour les points d'entrée en ligne de commande : un argument absent
/// ou illisible rend la valeur par défaut ANNONCÉE dans l'aide, là où `atof`
/// rendait zéro -- c'est-à-dire une durée nulle ou une fréquence nulle, qu'on
/// aurait cherchées longtemps.
inline double numberFromTextOr(std::string_view text, double fallback) {
    double value = 0.0;
    return numberFromText(text, value) ? value : fallback;
}

} // namespace vsm::interchange
