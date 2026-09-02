#pragma once
#include <cstdint>
#include <string>

namespace vsm::interchange {

/// Encode UN point de code en UTF-8, à la suite de `sortie`.
///
/// POURQUOI CE MINUSCULE EN-TÊTE EXISTE. La cascade `0xC0 | (code >> 6)`…
/// avait fini par être écrite QUATRE fois dans ce module : dans le lecteur
/// JSON (échappements `\uXXXX`), dans les entités numériques du lecteur XML,
/// et dans les deux branches (UTF-16 et Latin-1) du décodeur de textes FL
/// Studio. Les copies divergeaient déjà : deux d'entre elles ignoraient le
/// plan au-delà de 0xFFFF et tronquaient un émoji à trois octets — en
/// silence, naturellement. Quatre copies, c'est quatre endroits où corriger
/// la prochaine surprise, et trois qu'on oubliera.
///
/// Les points de code au-delà de la plage Unicode (> 0x10FFFF) et les moitiés
/// de paires de substitution isolées (0xD800–0xDFFF) sont remplacés par
/// U+FFFD, le caractère de remplacement : un octet inventé serait pire qu'un
/// « caractère illisible » assumé.
inline void appendUtf8(std::string& sortie, uint32_t code) {
    if ((code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) code = 0xFFFD;
    if (code < 0x80) {
        sortie += static_cast<char>(code);
    } else if (code < 0x800) {
        sortie += static_cast<char>(0xC0 | (code >> 6));
        sortie += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        sortie += static_cast<char>(0xE0 | (code >> 12));
        sortie += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        sortie += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        sortie += static_cast<char>(0xF0 | (code >> 18));
        sortie += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        sortie += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        sortie += static_cast<char>(0x80 | (code & 0x3F));
    }
}

} // namespace vsm::interchange
