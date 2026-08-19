#pragma once
#include "vsm/interchange/Json.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include <string>
#include <vector>

// Profil de RECHERCHE d'une machine (feuille de route globale, étape 8.2).
//
// LE PROBLÈME QU'IL RÉSOUT : l'optimiseur Python cherchait dans un espace
// écrit en dur -- sept paramètres, aux bornes fixées à la main, valables pour
// un soustractif et pour rien d'autre. Conséquences, toutes constatées :
//
//   - Une machine sans filtre (l'orgue à roues phoniques) ou dont le filtre
//     n'est pas le sujet (la table d'ondes, le supersaw) était cherchée sur
//     des paramètres secondaires, en ignorant ceux qui font son son.
//   - Ajouter une machine n'ajoutait RIEN à la recherche : elle héritait d'un
//     espace pensé pour une autre.
//   - Les bornes vivaient du côté Python, donc loin de la machine qui seule
//     sait ce qu'elles valent -- et personne ne les mettait à jour.
//
// CE QUE LE PROFIL DÉCLARE, et pourquoi chaque champ est nécessaire :
//
//   - des BORNES UTILES, plus étroites que les bornes techniques. Une coupure
//     va de 20 Hz à 18 kHz, mais chercher sous 80 Hz ou au-dessus de 12 kHz
//     revient à explorer « filtre fermé » et « filtre ouvert », deux régions
//     immenses où plus rien ne change. L'optimiseur y perdrait l'essentiel de
//     son budget.
//   - une ÉCHELLE. Entre 80 Hz et 12 kHz, une recherche linéaire passe 99 %
//     de son temps au-dessus de 6 kHz, là où l'oreille distingue le moins.
//     Les fréquences et les temps se cherchent en logarithmique.
//   - une IMPORTANCE. Le budget d'évaluations est fini et croît vite avec le
//     nombre de dimensions ; il faut donc pouvoir dire « cherche les six qui
//     comptent ». Sans ce classement, ajouter un réglage de détail à une
//     machine dégraderait la recherche de tous les autres.
//
// D'OÙ VIENNENT LES VALEURS : elles sont DÉDUITES de l'identité sémantique du
// paramètre, pas écrites machine par machine. C'est tout l'intérêt d'avoir des
// identités sémantiques : `filter.1.cutoff` DIT déjà qu'il s'agit d'une
// fréquence de coupure, quelle que soit la machine. Une nouvelle machine
// hérite donc d'un profil utilisable sans une ligne de plus -- et les cas où
// la règle générale est fausse font l'objet de surcharges explicites, listées
// et justifiées dans le .cpp.

namespace vsm::interchange {

/// Comment parcourir l'intervalle de recherche.
enum class SearchScale {
    Linear,       ///< quantités perçues linéairement (niveaux, dosages)
    Logarithmic,  ///< fréquences et durées : l'oreille les perçoit en rapport
};

const char* searchScaleName(SearchScale scale);

/// Une dimension de recherche : un paramètre, ses bornes utiles, son échelle.
struct SearchDimension {
    std::string semanticId;
    float low = 0.0f;             ///< borne utile basse, dans l'unité réelle
    float high = 1.0f;            ///< borne utile haute
    SearchScale scale = SearchScale::Linear;
    /// 0..1. Sert à choisir les N dimensions à chercher quand le budget est
    /// limité. Ce n'est PAS un poids dans la distance : la distance se mesure
    /// sur le son, jamais sur les paramètres.
    float importance = 0.5f;
    std::string unit;
};

/// Profil complet d'une machine, trié par importance décroissante.
///
/// PIÈGE À CONNAÎTRE : `dimensions()` rend une référence sur un membre. En
/// C++20, écrire `for (auto& d : buildSearchProfile(m).dimensions())` lie la
/// boucle à cette référence SANS prolonger la vie du profil temporaire, qui
/// est détruit avant la première itération -- la boucle parcourt alors de la
/// mémoire libérée, silencieusement. Nommer le profil d'abord :
///
///     const SearchProfile profile = buildSearchProfile(machine);
///     for (const auto& dimension : profile.dimensions()) { ... }
class SearchProfile {
public:
    SearchProfile() = default;
    SearchProfile(std::string pluginId, std::vector<SearchDimension> dimensions)
        : pluginId_(std::move(pluginId)), dimensions_(std::move(dimensions)) {}

    const std::string& pluginId() const { return pluginId_; }
    const std::vector<SearchDimension>& dimensions() const { return dimensions_; }
    bool empty() const { return dimensions_.empty(); }

    /// Les `count` dimensions les plus importantes. Rend tout si `count` est
    /// supérieur au nombre disponible.
    std::vector<SearchDimension> topDimensions(size_t count) const;

    const SearchDimension* find(const std::string& semanticId) const;

    JsonValue toJson() const;

private:
    std::string pluginId_;
    std::vector<SearchDimension> dimensions_;
};

/// Construit le profil de recherche d'une machine enregistrée. Profil vide si
/// la machine est inconnue.
SearchProfile buildSearchProfile(const std::string& pluginId);

} // namespace vsm::interchange
