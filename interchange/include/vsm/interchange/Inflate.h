#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vsm::interchange {

/// DÉCOMPRESSION DEFLATE ET GZIP (RFC 1950/1951/1952).
///
/// POURQUOI ICI ET NON `zlib`. Un projet Ableton Live est un XML gzippé : sans
/// inflate, pas d'import. `zlib` est présent sur la machine, mais
/// `vsm_interchange` **n'a aucune dépendance externe** et c'est une propriété
/// qu'on garde — le même jour, `vsm.spectral` a reçu sa propre transformée de
/// Fourier plutôt qu'une bibliothèque, pour la même raison. Deux cents lignes
/// qu'on relit et qu'on teste valent mieux qu'une dépendance qu'on porte, qu'on
/// fige et qu'on explique pour toujours.
///
/// CE QU'ELLE GARANTIT : toute anomalie lève `InflateError` avec un message
/// qui dit OÙ et QUOI. Un flux tronqué ne rend jamais un résultat partiel qu'on
/// prendrait pour un succès — c'est la règle de la panne muette, et elle est
/// ici la différence entre « ce projet n'a pas pu être lu » et « ce projet est
/// vide ».
class InflateError : public std::runtime_error {
public:
    explicit InflateError(const std::string& quoi) : std::runtime_error(quoi) {}
};

class Inflate {
public:
    /// Décompresse un flux DEFLATE brut (sans en-tête).
    static std::vector<uint8_t> raw(const std::vector<uint8_t>& entree);

    /// Décompresse un flux GZIP (en-tête 1f 8b, RFC 1952).
    static std::vector<uint8_t> gzip(const std::vector<uint8_t>& entree);

    /// Décompresse un flux zlib (en-tête 78 xx, RFC 1950).
    static std::vector<uint8_t> zlib(const std::vector<uint8_t>& entree);

    /// Reconnaît l'enveloppe et décompresse en conséquence. Si le flux n'est
    /// compressé d'aucune de ces façons, il est rendu TEL QUEL : un `.als`
    /// enregistré sans compression existe, et refuser de le lire serait un
    /// défaut, pas une rigueur.
    static std::vector<uint8_t> any(const std::vector<uint8_t>& entree);
};

} // namespace vsm::interchange
