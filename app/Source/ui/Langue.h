#pragma once

#include <JuceHeader.h>

namespace vsm::app::ui {

// ---------------------------------------------------------------------------
// LA LANGUE DE L'INTERFACE — le français, et l'anglais EN PLUS (D73).
//
// POURQUOI CE MODULE EXISTE. L'application est née entièrement en français, et
// c'est la langue de son auteur, de ses documents et de ses commentaires. Rien
// de cela ne change ici : ce qui change, c'est que le TEXTE VU PAR LE MUSICIEN
// peut se lire en anglais. Le français reste le défaut.
//
// LE FRANÇAIS EST LA CLÉ, ET C'EST UNE DÉCISION. `juce::LocalisedStrings`
// cherche une traduction à partir de la chaîne d'origine ; celle-ci reste donc
// écrite en clair dans le code, là où elle se lit, et la table anglaise vit à
// un seul endroit. La conséquence qui compte : **une chaîne sans traduction
// ressort en français**, jamais vide et jamais sous forme de clé technique
// (`menu.file.open`). C'est ce qui permet de livrer une couverture partielle
// sans mentir sur son étendue -- ce qui manque se voit, en français, au milieu
// de l'anglais, et se corrige en ajoutant une ligne à la table.
//
// ET LE MÊME GESTE RÉPARE UN PIÈGE PAYÉ PLUSIEURS FOIS. `juce::String(const
// char*)` lit ses octets en **Latin-1** : c'est ce qui rendait « s'éclaircit »
// en « s'Â©claircit » dans le rack, et ce qui a obligé D71 à écrire un en-tête
// de colonne sans accent. `tr()` passe TOUJOURS par `fromUTF8`, et accepte les
// deux formes de littéral -- `"…"` et `u8"…"`, qui en C++20 est un
// `char8_t[]` et ne se convertit plus tout seul. Traduire une chaîne et lire
// ses accents correctement sont devenus le même geste.
// ---------------------------------------------------------------------------
class Langue {
public:
    /// Les langues offertes. L'ordre est celui du menu.
    enum class Choix { Francais, Anglais };

    /// La langue courante. Défaut : le français.
    static Choix courante();

    /// Installe une langue et la conserve pour les exécutions suivantes.
    /// Rend `true` si la langue a CHANGÉ -- l'appelant doit alors re-traduire
    /// les libellés qu'il a posés une fois pour toutes (la barre de menus, elle,
    /// se reconstruit à chaque ouverture et n'a rien à faire).
    static bool appliquer(Choix choix);

    /// À appeler au démarrage, avant de construire la moindre fenêtre.
    /// `VSM_LANGUE=fr|en|francais|anglais|english` l'emporte sur le réglage
    /// conservé, sans l'écraser : un banc choisit sa langue pour une course
    /// sans changer ce que l'utilisateur a réglé.
    static void appliquerAuDemarrage();

    /// Libellé d'une langue, dans SA propre langue -- « Français », « English ».
    /// C'est la convention de tous les sélecteurs de langue, et la seule qui
    /// permette de retrouver la sienne quand on ne lit pas celle qui est posée.
    static juce::String libelle(Choix choix);

    /// Nombre de chaînes de la table anglaise, et nombre de celles qui n'ont
    /// pas de traduction parmi celles qu'on lui a demandées. Exposés parce que
    /// la couverture d'une traduction est un CHIFFRE, et qu'un chiffre qu'on ne
    /// peut pas lire ne se vérifie pas (D73).
    static int tailleDeLaTable();
};

/// Traduit une chaîne de l'interface. Rend le français tel quel quand la langue
/// est le français, ou quand la table anglaise n'a pas cette chaîne.
juce::String tr(const char* texte);
juce::String tr(const char8_t* texte);   ///< littéraux `u8"…"` du C++20
juce::String tr(const juce::String& texte);

/// D90 : une chaîne dont le français a DEUX sens. « Annuler » est « Undo » dans
/// le menu Édition et « Cancel » sur le bouton d'une boîte : la table, indexée
/// par le français, ne peut pas porter les deux sous la même clé. La clé porte
/// donc son contexte (« Annuler@bouton ») ; en français, ou sans traduction pour
/// ce contexte, c'est la traduction ordinaire du texte qui ressort.
juce::String trSelon(const char* contexte, const char8_t* texte);

/// D82 : le NOM D'UN GESTE, tel que l'historique le garde (en français), traduit
/// à l'affichage. Un seul nom est fabriqué -- « Signature 3/4 » -- et il se
/// reconnaît à son modèle ; les autres sont des chaînes de la table.
juce::String trGeste(const juce::String& libelle);

/// D157 : UNE TOUCHE ÉCRITE DANS LA LANGUE DU MUSICIEN, et non dans celle de
/// JUCE.
///
/// CE QUI EXISTAIT, MESURÉ. La table transporte la syntaxe de JUCE — c'est son
/// rôle, son en-tête le dit : « cette couche ne l'interprète pas ». La page des
/// raccourcis et les menus la RECOPIAIENT, si bien qu'un musicien français lisait
/// **spacebar**, **escape**, **delete**, **home**, **end**, **alt + cursor down**
/// — treize touches sur cinquante-six —, plus trente-deux écrites « ctrl + shift
/// + S » là où toute application affiche « Ctrl+Maj+S ».
///
/// CE QU'ELLE NE FAIT PAS : changer la touche. « ctrl + shift + S » reste
/// Ctrl+Maj+S ; seule son ÉCRITURE change, et seulement pour l'affichage — ce
/// qui est enregistré dans les préférences reste la description de JUCE.
juce::String toucheLisible(const juce::String& description);

/// D89 : une PHRASE fabriquée par le moteur (`interchange/`) ou par l'application
/// à partir de données -- « Piste 2 (Oubliee) : aucun instrument… » --, traduite à
/// l'AFFICHAGE par modèles. Les arguments sont des données et passent intacts ;
/// un résumé composé se traduit segment par segment, et ce qu'aucun modèle ne
/// reconnaît passe tel quel. En français, rend son argument sans y toucher.
juce::String trPhrase(const juce::String& texte);

/// D131 : LES DEUX-POINTS D'UN TEXTE QUE LE CODE ASSEMBLE, dans la typographie de
/// la langue -- « Pistes : 2 » en français, « Tracks: 2 » en anglais. Pour coller
/// un libellé traduit et une valeur ; une phrase entière se traduit par la table
/// (et une DONNÉE que `trPhrase` traduira garde la forme française).
juce::String deuxPoints();

} // namespace vsm::app::ui
