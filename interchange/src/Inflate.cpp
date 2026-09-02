#include "vsm/interchange/Inflate.h"

#include <array>
#include <cstring>

namespace vsm::interchange {
namespace {

/// Lecteur de bits, du bit de POIDS FAIBLE d'abord — c'est la convention de
/// DEFLATE, et s'en écarter donne un flux qui se décompresse « presque »,
/// c'est-à-dire n'importe quoi.
class LecteurDeBits {
public:
    explicit LecteurDeBits(const std::vector<uint8_t>& octets, size_t depart)
        : octets_(octets), position_(depart) {}

    uint32_t bits(int combien) {
        uint32_t valeur = 0;
        for (int i = 0; i < combien; ++i) valeur |= static_cast<uint32_t>(unBit()) << i;
        return valeur;
    }

    int unBit() {
        if (nbBits_ == 0) {
            if (position_ >= octets_.size())
                throw InflateError("flux DEFLATE tronqué : plus d'octets à lire");
            tampon_ = octets_[position_++];
            nbBits_ = 8;
        }
        const int b = tampon_ & 1;
        tampon_ >>= 1;
        --nbBits_;
        return b;
    }

    /// Se replace au début de l'octet suivant (blocs non compressés).
    void alignerSurOctet() { nbBits_ = 0; tampon_ = 0; }

    size_t position() const { return position_; }
    void avancer(size_t n) { position_ += n; }
    const std::vector<uint8_t>& octets() const { return octets_; }

private:
    const std::vector<uint8_t>& octets_;
    size_t position_ = 0;
    uint8_t tampon_ = 0;
    int nbBits_ = 0;
};

/// Arbre de Huffman CANONIQUE, décrit par la seule longueur de chaque code.
/// C'est ainsi que DEFLATE les transmet, et cela suffit à les reconstruire.
class Huffman {
public:
    void construire(const std::vector<uint8_t>& longueurs) {
        longueurs_ = longueurs;
        compte_.assign(16, 0);
        for (uint8_t l : longueurs_) if (l > 0) ++compte_[l];
        // Premier code de chaque longueur (algorithme de la RFC 1951, § 3.2.2).
        premier_.assign(16, 0);
        indexPremier_.assign(16, 0);
        uint32_t code = 0;
        int index = 0;
        for (int bits = 1; bits <= 15; ++bits) {
            code = (code + static_cast<uint32_t>(compte_[bits - 1])) << 1;
            premier_[bits] = code;
            indexPremier_[bits] = index;
            index += compte_[bits];
        }
        symboles_.assign(static_cast<size_t>(index), 0);
        std::vector<int> curseur = indexPremier_;
        for (size_t s = 0; s < longueurs_.size(); ++s) {
            const uint8_t l = longueurs_[s];
            if (l > 0) symboles_[static_cast<size_t>(curseur[l]++)] = static_cast<int>(s);
        }
    }

    int decoder(LecteurDeBits& lecteur) const {
        uint32_t code = 0;
        for (int bits = 1; bits <= 15; ++bits) {
            code = (code << 1) | static_cast<uint32_t>(lecteur.unBit());
            const int n = compte_[static_cast<size_t>(bits)];
            if (n > 0) {
                const uint32_t base = premier_[static_cast<size_t>(bits)];
                if (code - base < static_cast<uint32_t>(n))
                    return symboles_[static_cast<size_t>(indexPremier_[static_cast<size_t>(bits)])
                                     + (code - base)];
            }
        }
        throw InflateError("code de Huffman invalide : le flux n'est pas du DEFLATE valide");
    }

private:
    std::vector<uint8_t> longueurs_;
    std::vector<int> compte_, indexPremier_;
    std::vector<uint32_t> premier_;
    std::vector<int> symboles_;
};

// Tables de la RFC 1951, § 3.2.5.
constexpr std::array<uint16_t, 29> kBaseLongueur{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kBitsLongueur{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kBaseDistance{
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kBitsDistance{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void arbresFixes(Huffman& litteraux, Huffman& distances) {
    std::vector<uint8_t> l(288);
    for (int i = 0; i < 144; ++i) l[static_cast<size_t>(i)] = 8;
    for (int i = 144; i < 256; ++i) l[static_cast<size_t>(i)] = 9;
    for (int i = 256; i < 280; ++i) l[static_cast<size_t>(i)] = 7;
    for (int i = 280; i < 288; ++i) l[static_cast<size_t>(i)] = 8;
    litteraux.construire(l);
    distances.construire(std::vector<uint8_t>(30, 5));
}

void arbresDynamiques(LecteurDeBits& lecteur, Huffman& litteraux, Huffman& distances) {
    const auto hlit = static_cast<size_t>(lecteur.bits(5)) + 257;
    const auto hdist = static_cast<size_t>(lecteur.bits(5)) + 1;
    const auto hclen = static_cast<size_t>(lecteur.bits(4)) + 4;
    static constexpr std::array<int, 19> kOrdre{
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    std::vector<uint8_t> longueursDeCode(19, 0);
    for (size_t i = 0; i < hclen; ++i)
        longueursDeCode[static_cast<size_t>(kOrdre[i])] = static_cast<uint8_t>(lecteur.bits(3));
    Huffman arbreDeCodes;
    arbreDeCodes.construire(longueursDeCode);

    std::vector<uint8_t> toutes;
    toutes.reserve(hlit + hdist);
    while (toutes.size() < hlit + hdist) {
        const int symbole = arbreDeCodes.decoder(lecteur);
        if (symbole < 16) {
            toutes.push_back(static_cast<uint8_t>(symbole));
        } else if (symbole == 16) {
            if (toutes.empty()) throw InflateError("répétition de longueur sans précédent");
            const uint8_t precedent = toutes.back();
            const auto n = static_cast<size_t>(lecteur.bits(2)) + 3;
            for (size_t i = 0; i < n; ++i) toutes.push_back(precedent);
        } else if (symbole == 17) {
            const auto n = static_cast<size_t>(lecteur.bits(3)) + 3;
            for (size_t i = 0; i < n; ++i) toutes.push_back(0);
        } else {
            const auto n = static_cast<size_t>(lecteur.bits(7)) + 11;
            for (size_t i = 0; i < n; ++i) toutes.push_back(0);
        }
    }
    if (toutes.size() != hlit + hdist)
        throw InflateError("table de longueurs incohérente");
    litteraux.construire(std::vector<uint8_t>(toutes.begin(), toutes.begin() + static_cast<long>(hlit)));
    distances.construire(std::vector<uint8_t>(toutes.begin() + static_cast<long>(hlit), toutes.end()));
}

} // namespace

std::vector<uint8_t> Inflate::raw(const std::vector<uint8_t>& entree) {
    LecteurDeBits lecteur(entree, 0);
    std::vector<uint8_t> sortie;
    bool dernier = false;

    while (!dernier) {
        dernier = lecteur.unBit() != 0;
        const uint32_t type = lecteur.bits(2);

        if (type == 0) {
            lecteur.alignerSurOctet();
            const size_t p = lecteur.position();
            if (p + 4 > entree.size()) throw InflateError("bloc non compressé tronqué");
            const auto len = static_cast<size_t>(entree[p]) | (static_cast<size_t>(entree[p + 1]) << 8);
            lecteur.avancer(4);
            const size_t debut = lecteur.position();
            if (debut + len > entree.size())
                throw InflateError("bloc non compressé plus long que le flux");
            sortie.insert(sortie.end(), entree.begin() + static_cast<long>(debut),
                          entree.begin() + static_cast<long>(debut + len));
            lecteur.avancer(len);
            continue;
        }
        if (type == 3) throw InflateError("type de bloc DEFLATE réservé (3) : flux invalide");

        Huffman litteraux, distances;
        if (type == 1) arbresFixes(litteraux, distances);
        else arbresDynamiques(lecteur, litteraux, distances);

        for (;;) {
            const int symbole = litteraux.decoder(lecteur);
            if (symbole == 256) break;
            if (symbole < 256) {
                sortie.push_back(static_cast<uint8_t>(symbole));
                continue;
            }
            const auto i = static_cast<size_t>(symbole - 257);
            if (i >= kBaseLongueur.size()) throw InflateError("code de longueur hors table");
            const size_t longueur = kBaseLongueur[i] + lecteur.bits(kBitsLongueur[i]);

            const auto d = static_cast<size_t>(distances.decoder(lecteur));
            if (d >= kBaseDistance.size()) throw InflateError("code de distance hors table");
            const size_t distance = kBaseDistance[d] + lecteur.bits(kBitsDistance[d]);
            if (distance > sortie.size())
                throw InflateError("référence arrière au-delà du début : flux corrompu");

            // La copie est VOLONTAIREMENT échantillon par échantillon : les
            // plages peuvent se chevaucher (c'est ainsi que DEFLATE encode une
            // répétition), et un `memcpy` donnerait un résultat faux.
            const size_t depart = sortie.size() - distance;
            for (size_t k = 0; k < longueur; ++k) sortie.push_back(sortie[depart + k]);
        }
    }
    return sortie;
}

std::vector<uint8_t> Inflate::gzip(const std::vector<uint8_t>& entree) {
    if (entree.size() < 18 || entree[0] != 0x1f || entree[1] != 0x8b)
        throw InflateError("ce n'est pas un flux gzip (en-tête 1f 8b attendu)");
    if (entree[2] != 8) throw InflateError("gzip : méthode de compression inconnue");
    const uint8_t drapeaux = entree[3];
    size_t p = 10;
    if (drapeaux & 0x04) {   // FEXTRA
        if (p + 2 > entree.size()) throw InflateError("gzip : champ EXTRA tronqué");
        const auto n = static_cast<size_t>(entree[p]) | (static_cast<size_t>(entree[p + 1]) << 8);
        p += 2 + n;
    }
    if (drapeaux & 0x08) { while (p < entree.size() && entree[p] != 0) ++p; ++p; }   // FNAME
    if (drapeaux & 0x10) { while (p < entree.size() && entree[p] != 0) ++p; ++p; }   // FCOMMENT
    if (drapeaux & 0x02) p += 2;                                                     // FHCRC
    if (p >= entree.size()) throw InflateError("gzip : en-tête tronqué");
    return raw(std::vector<uint8_t>(entree.begin() + static_cast<long>(p), entree.end()));
}

std::vector<uint8_t> Inflate::zlib(const std::vector<uint8_t>& entree) {
    if (entree.size() < 6) throw InflateError("flux zlib trop court");
    if ((entree[0] & 0x0f) != 8) throw InflateError("zlib : méthode de compression inconnue");
    return raw(std::vector<uint8_t>(entree.begin() + 2, entree.end()));
}

std::vector<uint8_t> Inflate::any(const std::vector<uint8_t>& entree) {
    if (entree.size() >= 2 && entree[0] == 0x1f && entree[1] == 0x8b) return gzip(entree);
    if (entree.size() >= 2 && (entree[0] & 0x0f) == 8
        && ((static_cast<unsigned>(entree[0]) << 8 | entree[1]) % 31) == 0)
        return zlib(entree);
    // NI GZIP NI ZLIB : on rend tel quel. Un projet Live enregistré sans
    // compression existe, et refuser de le lire serait un défaut et non une
    // rigueur.
    return entree;
}

} // namespace vsm::interchange
