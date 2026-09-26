#pragma once

#include <cstdint>
#include <string>

namespace vsm::midi {

/// D386 : L'ENCODAGE DES TEXTES D'UN FICHIER MIDI (noms de piste, repères,
/// paroles). Le format n'en déclare aucun ; la convention que suivent les
/// lecteurs -- `mido`, et la chaîne d'analyse de ce dépôt depuis qu'un « œ »
/// l'a fait tomber (`nom_midi_lisible`) -- est le Latin-1. L'application, elle,
/// tient ses textes en UTF-8 : elle écrivait donc « Batterie · hihat » que les
/// autres lisaient « Batterie Â· hihat », et relisait un « é » Latin-1 (un seul
/// octet 0xE9) comme de l'UTF-8 invalide.

/// Vrai si `octets` est de l'UTF-8 bien formé (l'ASCII pur l'est).
inline bool estUtf8Valide(const std::string& octets) {
    size_t i = 0;
    const size_t n = octets.size();
    while (i < n) {
        const auto c = static_cast<uint8_t>(octets[i]);
        int suite = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (suite < 0 || (suite == 1 && c < 0xC2)) return false;   // 0xC0/0xC1 : sur-longs
        if (i + static_cast<size_t>(suite) >= n + (suite == 0 ? 1 : 0) && suite > 0) return false;
        for (int k = 1; k <= suite; ++k)
            if ((static_cast<uint8_t>(octets[i + static_cast<size_t>(k)]) & 0xC0) != 0x80) return false;
        i += static_cast<size_t>(suite) + 1;
    }
    return true;
}

/// Des octets Latin-1 rendus en UTF-8.
inline std::string latin1VersUtf8(const std::string& latin1) {
    std::string sortie;
    sortie.reserve(latin1.size() * 2);
    for (char ch : latin1) {
        const auto c = static_cast<uint8_t>(ch);
        if (c < 0x80) sortie.push_back(static_cast<char>(c));
        else {
            sortie.push_back(static_cast<char>(0xC0 | (c >> 6)));
            sortie.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return sortie;
}

/// À LA LECTURE : un texte qui est de l'UTF-8 valide reste tel quel (les
/// fichiers que l'application écrivait avant D386, et les logiciels qui
/// écrivent de l'UTF-8) ; sinon, il est lu en Latin-1.
inline std::string texteMidiVersUtf8(const std::string& octets) {
    return estUtf8Valide(octets) ? octets : latin1VersUtf8(octets);
}

/// À L'ÉCRITURE : la translittération de la chaîne (`nom_midi_lisible`,
/// `analyse/analyzer/vsm_project_export.py`) -- LA MÊME TABLE, pour que les
/// deux chemins d'export donnent les mêmes noms --, puis le Latin-1 ; un
/// caractère sans équivalent devient « ? », comme `errors="replace"`. Le nom
/// complet vit dans `project.json`.
inline std::string utf8VersTexteMidi(const std::string& utf8) {
    std::string sortie;
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t point = 0;
        int suite = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (suite < 0 || i + static_cast<size_t>(suite) >= n + (suite == 0 ? 1 : 0)) {
            sortie.push_back('?');   // octet isolé : ce n'était pas de l'UTF-8
            ++i;
            continue;
        }
        point = suite == 0 ? c : (c & (0x3F >> suite));
        for (int k = 1; k <= suite; ++k)
            point = (point << 6) | (static_cast<uint8_t>(utf8[i + static_cast<size_t>(k)]) & 0x3F);
        i += static_cast<size_t>(suite) + 1;
        switch (point) {
            case 0x0153: sortie += "oe"; continue;   // œ
            case 0x0152: sortie += "OE"; continue;   // Œ
            case 0x00E6: sortie += "ae"; continue;   // æ
            case 0x00C6: sortie += "AE"; continue;   // Æ
            case 0x00B7: sortie += "-"; continue;    // ·
            case 0x2014: sortie += "-"; continue;    // —
            case 0x2013: sortie += "-"; continue;    // –
            case 0x2019: sortie += "'"; continue;    // ’
            case 0x00AB: sortie += "\""; continue;   // «
            case 0x00BB: sortie += "\""; continue;   // »
            case 0x2026: sortie += "..."; continue;  // …
            default: break;
        }
        sortie.push_back(point <= 0xFF ? static_cast<char>(point) : '?');
    }
    return sortie;
}

}  // namespace vsm::midi
