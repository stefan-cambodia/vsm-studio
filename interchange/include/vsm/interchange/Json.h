#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

// Lecteur/écrivain JSON minimal, écrit ici plutôt qu'importé.
//
// POURQUOI PAS UNE BIBLIOTHÈQUE EXISTANTE : le projet tient à ce qu'un
// `cmake && make` fonctionne hors ligne, sur n'importe quelle machine, sans
// rien télécharger (c'est déjà la raison d'être du parseur MIDI et du
// framework de tests maison). Récupérer nlohmann/json via FetchContent
// imposerait une connexion réseau au premier build de la couche interop --
// pour un besoin qui tient en quelques centaines de lignes, puisque les
// formats du projet (`*.synth.json`, `project.json`) sont écrits ET lus par
// nous : pas de JSON exotique à avaler, pas de flux gigantesques à streamer.
//
// PÉRIMÈTRE ASSUMÉ : JSON complet côté valeurs (null, booléens, nombres,
// chaînes avec échappements et \uXXXX en BMP, tableaux, objets), sans
// tolérance aux extensions non standard (commentaires, virgules finales) --
// mieux vaut refuser un fichier douteux que l'interpréter de travers.
//
// Cette couche est la SEULE du projet autorisée à connaître JSON. Ni `core/`,
// ni `audio/`, ni le chemin temps réel ne l'incluent (voir
// docs/ROADMAP-interop.md § 0).

namespace vsm::interchange {

class JsonValue {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    JsonValue() = default;
    static JsonValue makeNull() { return JsonValue(); }
    static JsonValue makeBoolean(bool value);
    static JsonValue makeNumber(double value);
    /// Nombre issu d'un `float`. À utiliser pour toute valeur de paramètre :
    /// un float converti en double s'écrit "0.69999998807907104" alors que la
    /// valeur EST 0,7 à la précision d'un float. Cette variante retient la
    /// plus courte écriture qui se relit au bit près en float -- fichiers
    /// lisibles et modifiables à la main, sans perte.
    static JsonValue makeFloat(float value);
    static JsonValue makeString(std::string value);
    static JsonValue makeArray();
    static JsonValue makeObject();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBoolean() const { return type_ == Type::Boolean; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    /// Accès tolérant : renvoie la valeur par défaut fournie si le type ne
    /// correspond pas. Un preset dont un champ est absent ou mal typé doit
    /// se charger avec une valeur saine plutôt que faire échouer tout
    /// l'import -- les erreurs bloquantes sont réservées au JSON invalide et
    /// aux incohérences de format/version, signalées explicitement.
    bool asBoolean(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::string asString(const std::string& fallback = {}) const;

    // --- Objet -------------------------------------------------------------
    bool has(const std::string& key) const;
    const JsonValue& operator[](const std::string& key) const; ///< Null si absent
    void set(const std::string& key, JsonValue value);
    const std::map<std::string, JsonValue>& members() const { return members_; }

    // --- Tableau -----------------------------------------------------------
    size_t size() const { return elements_.size(); }
    const JsonValue& at(size_t index) const;
    void append(JsonValue value);
    const std::vector<JsonValue>& elements() const { return elements_; }

    /// Sérialisation. `indent >= 0` produit une sortie indentée et lisible
    /// (les fichiers du projet sont censés être relus et versionnés par des
    /// humains) ; `indent < 0` produit une ligne compacte.
    std::string toString(int indent = 2) const;

private:
    void writeTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> elements_;
    std::map<std::string, JsonValue> members_; // ordonné : sortie stable, diffs lisibles
};

struct JsonParseResult {
    bool success = false;
    JsonValue value;
    std::string error;      ///< message lisible, vide si succès
    size_t errorOffset = 0; ///< position dans le texte, pour situer l'erreur
};

JsonParseResult parseJson(const std::string& text);

} // namespace vsm::interchange
