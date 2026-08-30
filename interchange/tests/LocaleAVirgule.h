#pragma once
#include <clocale>
#include <iostream>
#include <string>

// UNE LOCALE À VIRGULE DÉCIMALE, POUR LES TESTS QUI EN ONT BESOIN.
//
// La suite tourne en locale C, où le séparateur décimal est le point ; c'est
// exactement pourquoi elle n'a rien vu pendant que l'application écrivait
// `"EQ Mid Q": 0,8` dans ses sauvegardes automatiques. Les tests qui vérifient
// qu'un format de fichier ne dépend PAS de la locale doivent donc en installer
// une exprès.
//
// SUR UNE MACHINE QUI N'EN A AUCUNE, LE CONTRÔLE NE PEUT PAS AVOIR LIEU, et
// c'est dit sur la sortie plutôt que compté comme une réussite : un test qui
// se tait quand il n'a rien pu vérifier est pire qu'un test absent, puisqu'il
// laisse croire que la vérification existe.

namespace vsm::test {

class LocaleAVirgule {
public:
    LocaleAVirgule() : precedente_(std::setlocale(LC_ALL, nullptr)) {
        for (const char* candidate : {"fr_FR.UTF-8", "fr_BE.UTF-8", "de_DE.UTF-8",
                                      "fr_FR.utf8", "de_DE.utf8", "fr_FR", "de_DE"}) {
            if (std::setlocale(LC_ALL, candidate) == nullptr) continue;
            const char* separateur = std::localeconv()->decimal_point;
            if (separateur != nullptr && separateur[0] == ',') { nom_ = candidate; return; }
        }
        std::setlocale(LC_ALL, precedente_.c_str());
    }
    /// Un test ne laisse pas le processus dans un état que le suivant n'a pas
    /// demandé -- ici moins qu'ailleurs, puisque l'état en question est
    /// justement celui qui fait écrire des fichiers faux.
    ~LocaleAVirgule() { std::setlocale(LC_ALL, precedente_.c_str()); }
    LocaleAVirgule(const LocaleAVirgule&) = delete;
    LocaleAVirgule& operator=(const LocaleAVirgule&) = delete;

    bool installee() const { return !nom_.empty(); }
    const std::string& nom() const { return nom_; }

    /// Annonce ce qui a pu être vérifié, ou ce qui ne l'a pas été.
    bool annonce() const {
        if (installee()) std::cout << "       locale d'épreuve : " << nom_ << "\n";
        else std::cout << "       (aucune locale à virgule installée : le contrôle n'a PAS eu lieu)\n";
        return installee();
    }

private:
    std::string precedente_;
    std::string nom_;
};

} // namespace vsm::test
