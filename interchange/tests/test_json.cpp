#include "LocaleAVirgule.h"
#include "TestFramework.h"
#include "vsm/interchange/Json.h"
#include <cmath>
#include <string>

using namespace vsm::interchange;

VSM_TEST(json_writes_and_reads_back_a_simple_object) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString("vsm-synth-preset"));
    root.set("version", JsonValue::makeNumber(1));
    root.set("active", JsonValue::makeBoolean(true));

    const auto parsed = parseJson(root.toString());
    VSM_ASSERT(parsed.success);
    VSM_ASSERT_EQ(parsed.value["format"].asString(), std::string("vsm-synth-preset"));
    VSM_ASSERT_NEAR(parsed.value["version"].asNumber(), 1.0, 1e-12);
    VSM_ASSERT(parsed.value["active"].asBoolean());
}

VSM_TEST(json_round_trips_nested_structures) {
    JsonValue track = JsonValue::makeObject();
    track.set("name", JsonValue::makeString("Bass"));
    JsonValue notes = JsonValue::makeArray();
    notes.append(JsonValue::makeNumber(36));
    notes.append(JsonValue::makeNumber(43));
    track.set("notes", std::move(notes));

    JsonValue root = JsonValue::makeObject();
    JsonValue tracks = JsonValue::makeArray();
    tracks.append(std::move(track));
    root.set("tracks", std::move(tracks));

    const auto parsed = parseJson(root.toString());
    VSM_ASSERT(parsed.success);
    VSM_ASSERT_EQ(parsed.value["tracks"].size(), size_t{1});
    VSM_ASSERT_EQ(parsed.value["tracks"].at(0)["name"].asString(), std::string("Bass"));
    VSM_ASSERT_EQ(parsed.value["tracks"].at(0)["notes"].size(), size_t{2});
    VSM_ASSERT_NEAR(parsed.value["tracks"].at(0)["notes"].at(1).asNumber(), 43.0, 1e-12);
}

VSM_TEST(json_preserves_float_values_exactly) {
    // L'enjeu concret : une fréquence de coupure écrite puis relue ne doit pas
    // dériver. Un écrivain qui tronquerait à 6 chiffres décalerait le filtre
    // d'un preset à chaque aller-retour.
    const double values[] = {0.1, 1234.5678, 1e-7, 19999.999, 0.30000000000000004};
    for (double value : values) {
        JsonValue root = JsonValue::makeObject();
        root.set("v", JsonValue::makeNumber(value));
        const auto parsed = parseJson(root.toString());
        VSM_ASSERT(parsed.success);
        VSM_ASSERT_NEAR(parsed.value["v"].asNumber(), value, 0.0); // identité stricte
    }
}

VSM_TEST(json_writes_floats_readably_without_losing_them) {
    // Un paramètre vaut 0,7 : le fichier doit dire "0.7", pas
    // "0.69999998807907104" -- un format destiné à être lu et corrigé à la
    // main perd sa raison d'être s'il est illisible. Sans jamais rien perdre :
    // la relecture doit rendre le float d'origine au bit près.
    const float values[] = {0.7f, 0.82f, 0.28f, 1200.0f, 0.001f, 19999.9f, -12.5f};
    for (float value : values) {
        JsonValue root = JsonValue::makeObject();
        root.set("v", JsonValue::makeFloat(value));
        const std::string text = root.toString(-1);
        VSM_ASSERT(text.size() < 24); // écriture courte

        const auto parsed = parseJson(text);
        VSM_ASSERT(parsed.success);
        const float readBack = static_cast<float>(parsed.value["v"].asNumber());
        VSM_ASSERT_NEAR(readBack, value, 0.0); // identité stricte en float
    }
    JsonValue root = JsonValue::makeObject();
    root.set("v", JsonValue::makeFloat(0.7f));
    VSM_ASSERT_EQ(root.toString(-1), std::string("{\"v\":0.7}"));
}

VSM_TEST(json_handles_escapes_and_unicode) {
    const std::string tricky = "guillemet \" antislash \\ tabulation \t saut \n accentué é";
    JsonValue root = JsonValue::makeObject();
    root.set("text", JsonValue::makeString(tricky));
    const auto parsed = parseJson(root.toString());
    VSM_ASSERT(parsed.success);
    VSM_ASSERT_EQ(parsed.value["text"].asString(), tricky);

    // Échappement \uXXXX, y compris une paire de substitution (clé de sol).
    const auto decoded = parseJson(R"({"a":"é","b":"𝄞"})");
    VSM_ASSERT(decoded.success);
    VSM_ASSERT_EQ(decoded.value["a"].asString(), std::string("é"));
    VSM_ASSERT_EQ(decoded.value["b"].asString(), std::string("\xF0\x9D\x84\x9E"));
}

VSM_TEST(json_rejects_malformed_documents) {
    // Un fichier douteux doit être REFUSÉ avec une position, pas deviné : une
    // interprétation approximative produirait un son faux sans prévenir.
    const char* invalid[] = {
        "{",                      // objet non fermé
        "{\"a\":}",               // valeur manquante
        "{\"a\":1,}",             // virgule finale (non standard)
        "[1, 2",                  // tableau non fermé
        "{'a': 1}",               // guillemets simples
        "{\"a\": 1} extra",       // données superflues
        "nul",                    // littéral tronqué
        "",                       // vide
    };
    for (const char* text : invalid) {
        const auto parsed = parseJson(text);
        VSM_ASSERT(!parsed.success);
        VSM_ASSERT(!parsed.error.empty());
    }
}

VSM_TEST(json_accessors_are_forgiving_on_type_mismatch) {
    // Un champ absent ou mal typé ne doit pas faire échouer tout un import :
    // il renvoie le repli fourni par l'appelant, qui décide.
    const auto parsed = parseJson(R"({"n": 42, "s": "texte"})");
    VSM_ASSERT(parsed.success);
    VSM_ASSERT_NEAR(parsed.value["n"].asNumber(-1.0), 42.0, 1e-12);
    VSM_ASSERT_EQ(parsed.value["n"].asString("repli"), std::string("repli"));
    VSM_ASSERT_NEAR(parsed.value["absent"].asNumber(7.5), 7.5, 1e-12);
    VSM_ASSERT(parsed.value["absent"].isNull());
}

VSM_TEST(json_compact_and_pretty_forms_are_equivalent) {
    JsonValue root = JsonValue::makeObject();
    root.set("a", JsonValue::makeNumber(1));
    JsonValue inner = JsonValue::makeObject();
    inner.set("b", JsonValue::makeString("x"));
    root.set("inner", std::move(inner));

    const auto pretty = parseJson(root.toString(2));
    const auto compact = parseJson(root.toString(-1));
    VSM_ASSERT(pretty.success && compact.success);
    VSM_ASSERT_EQ(pretty.value["inner"]["b"].asString(), compact.value["inner"]["b"].asString());
    VSM_ASSERT(root.toString(-1).find('\n') == std::string::npos); // vraiment compact
}

// ---------------------------------------------------------------------------
// LA LOCALE DU PROCESSUS N'A PAS SON MOT À DIRE SUR LE CONTENU D'UN FICHIER
//
// Trouvé en vrai, pas imaginé : sur la machine de développement, réglée en
// français, les trois sauvegardes automatiques laissées sur le disque
// contenaient `"EQ Mid Q": 0,8`. Ce n'est pas du JSON, et le lecteur de ce
// même fichier les refusait toutes les trois -- la récupération après plantage
// (D10.4) promettait de rendre un projet qu'elle n'était pas en mesure de
// relire. Le programme n'installe aucune locale ; JUCE le fait pour lui, et
// « restaure » ce que son propre appel vient de rendre.
//
// POURQUOI CE TEST NE POUVAIT PAS ÉCHOUER AVANT : la suite tourne en locale C,
// où la panne est invisible. Il faut donc l'installer exprès. Sur une machine
// qui n'a aucune locale à virgule, le contrôle ne peut pas avoir lieu -- il est
// alors DIT sur la sortie plutôt que compté comme réussi en silence.

VSM_TEST(json_ignore_la_locale_du_processus) {
    const vsm::test::LocaleAVirgule virgule;
    if (!virgule.annonce()) return;

    // 1. CE QU'ON ÉCRIT EST DU JSON. C'est le symptôme visible : un fichier
    //    qu'aucun autre outil ne peut lire, à commencer par la chaîne d'analyse.
    JsonValue racine = JsonValue::makeObject();
    racine.set("EQ Mid Q", JsonValue::makeFloat(0.8f));
    racine.set("Limiter Ceiling", JsonValue::makeFloat(-0.3f));
    racine.set("tempo", JsonValue::makeNumber(128.5));
    const std::string texte = racine.toString();
    VSM_ASSERT(texte.find("0,8") == std::string::npos);   // ce que le disque contenait
    VSM_ASSERT(texte.find("-0,3") == std::string::npos);
    VSM_ASSERT(texte.find("128,5") == std::string::npos);
    VSM_ASSERT(texte.find("0.8") != std::string::npos);
    VSM_ASSERT(texte.find("-0.3") != std::string::npos);
    VSM_ASSERT(texte.find("128.5") != std::string::npos);

    // 2. ON RELIT CE QU'ON A ÉCRIT. C'est la promesse de D10.4, et elle était
    //    fausse : le parseur s'arrêtait sur la virgule et refusait le fichier.
    const auto relu = parseJson(texte);
    VSM_ASSERT(relu.success);
    VSM_ASSERT_NEAR(relu.value["EQ Mid Q"].asNumber(), 0.8, 1e-9);
    VSM_ASSERT_NEAR(relu.value["Limiter Ceiling"].asNumber(), -0.3, 1e-9);

    // 3. ON LIT UN FICHIER VALIDE VENU D'AILLEURS, ET C'EST LE CÔTÉ SILENCIEUX.
    //    `strtod("0.8")` sous une locale à virgule s'arrête sur le point et
    //    rend 0 : un `project.json` écrit par la chaîne d'analyse se chargeait
    //    avec tous ses paramètres fractionnaires à zéro, sans un mot.
    const auto venuDAilleurs = parseJson(R"({"cutoff": 0.8, "gain": -0.3, "bpm": 128.5})");
    VSM_ASSERT(venuDAilleurs.success);
    VSM_ASSERT_NEAR(venuDAilleurs.value["cutoff"].asNumber(), 0.8, 1e-12);
    VSM_ASSERT_NEAR(venuDAilleurs.value["gain"].asNumber(), -0.3, 1e-12);
    VSM_ASSERT_NEAR(venuDAilleurs.value["bpm"].asNumber(), 128.5, 1e-12);
}

VSM_TEST(json_ecrit_les_memes_octets_sous_les_deux_locales) {
    // La correction ne doit RIEN changer quand la locale est déjà saine :
    // sinon tous les fichiers du dépôt bougeraient d'un octet à la relecture,
    // et les diffs deviendraient illisibles pour rien.
    JsonValue racine = JsonValue::makeObject();
    racine.set("a", JsonValue::makeNumber(0.1));
    racine.set("b", JsonValue::makeNumber(1.0 / 3.0));
    racine.set("c", JsonValue::makeFloat(0.7f));
    racine.set("d", JsonValue::makeNumber(1e-7));
    racine.set("e", JsonValue::makeNumber(-42));
    const std::string enC = racine.toString();

    const vsm::test::LocaleAVirgule virgule;
    if (!virgule.annonce()) return;
    VSM_ASSERT_EQ(racine.toString(), enC);
}
