#pragma once
#include <string>
#include <vector>

namespace vsm::interchange {

/// TROUVER UN SON SANS OUVRIR UN DOSSIER (D10.1).
///
/// **CE QUE L'APPLICATION SAVAIT FAIRE, ET CE QU'ELLE NE SAVAIT PAS.** Elle
/// savait charger un preset `*.synth.json`, un profil `*.profile.json`, un
/// échantillon — chacun par un sélecteur de fichiers, c'est-à-dire à condition
/// de savoir déjà où il est. Trente-quatre machines, autant de presets par
/// projet, des profils multi-échantillons et des dossiers de samples : la
/// matière existait, et le seul moyen d'y accéder était de s'en souvenir.
///
/// **CE MODULE NE FAIT QUE L'INVENTAIRE ET LA RECHERCHE.** Aucune lecture de
/// contenu : on lit des NOMS de fichiers et des extensions. Un dossier
/// d'échantillons contient parfois des milliers de fichiers, et ouvrir chacun
/// pour savoir ce qu'il est ferait de l'ouverture du navigateur une attente.
/// Ce qu'on affiche à côté d'une entrée (la machine d'un preset, la durée d'un
/// échantillon) est lu à la demande, quand elle est retenue.
enum class BrowserItemKind {
    Machine = 0,   ///< une machine du parc, ou un plugin tiers balayé
    Preset,        ///< `*.synth.json`
    Profile,       ///< `*.profile.json` (multi-échantillons)
    Sample,        ///< un fichier audio
    EffectPreset,  ///< `*.effect.json` (D15.4) -- en dernier : le numéro voyage dans le glisser-déposer
};

struct BrowserItem {
    BrowserItemKind kind = BrowserItemKind::Machine;
    /// Ce qu'on montre.
    std::string name;
    /// L'identifiant de la machine (`vsm.tb303`, `clap:...`) pour `Machine`,
    /// le chemin du fichier pour les trois autres.
    std::string reference;
    /// D'où elle vient, montré en gris : le dossier du projet, la bibliothèque
    /// de l'utilisateur, le parc. Une liste où deux entrées portent le même nom
    /// sans dire d'où elles viennent oblige à les essayer.
    std::string origin;
};

/// Ajoute les fichiers d'un dossier (récursivement) à l'inventaire.
/// `origin` est l'étiquette montrée à l'utilisateur.
///
/// `maxDepth` existe parce qu'un dossier d'échantillons peut être n'importe
/// quoi -- y compris la racine d'un disque, si on la désigne par mégarde. Une
/// exploration sans fond transformerait une erreur de clic en gel de plusieurs
/// minutes.
void indexFolder(const std::string& folderPath, const std::string& origin,
                  std::vector<BrowserItem>& out, int maxDepth = 4,
                  size_t maxItems = 20000);

/// LA RECHERCHE, ET ELLE EST DÉLIBÉRÉMENT SIMPLE : tous les mots de la requête
/// doivent apparaître quelque part dans le nom ou l'origine, sans tenir compte
/// de la casse ni de l'ordre. « 303 acid » trouve « TB-303 Acid Lead » comme
/// « acid lead (tb303) ». Une recherche floue rendrait des résultats qu'on ne
/// saurait pas expliquer, et la seule chose qu'on demande à un navigateur est
/// qu'on comprenne pourquoi ce qu'il montre est là.
std::vector<BrowserItem> filterBrowserItems(const std::vector<BrowserItem>& items,
                                             const std::string& query);

/// Le libellé d'une famille, au pluriel, pour les en-têtes.
const char* browserKindLabel(BrowserItemKind kind);

/// LE LIBELLÉ COURT DE LA COLONNE DE GAUCHE, au singulier et ENTIER.
///
/// Il a d'abord été fabriqué en coupant le pluriel à trois lettres, ce qui
/// donnait « Pre » et « Pro » côte à côte -- deux familles qu'on ne distingue
/// pas d'un coup d'œil, dans la colonne dont c'est la seule fonction. Une
/// abréviation qu'il faut décoder ne rend pas la liste plus lisible, elle la
/// rend plus courte.
const char* browserKindShortLabel(BrowserItemKind kind);

/// Reconnaît un fichier d'après son nom. Exposé parce que le glisser-déposer
/// s'en sert aussi -- deux réponses différentes à « qu'est-ce que ce
/// fichier ? » finiraient par se contredire.
bool isPresetFile(const std::string& path);
bool isProfileFile(const std::string& path);
bool isSampleFile(const std::string& path);

} // namespace vsm::interchange
