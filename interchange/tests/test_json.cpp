#include "TestFramework.h"
#include "vsm/interchange/Json.h"
#include <cmath>

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
