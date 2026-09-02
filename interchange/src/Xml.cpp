#include "vsm/interchange/Xml.h"

#include <cctype>

namespace vsm::interchange {
namespace {

bool estEspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// Résout les cinq entités prédéfinies et les références numériques. Une
/// entité INCONNUE est laissée telle quelle : dans un nom de piste, un `&`
/// isolé écrit par un DAW ne doit pas faire échouer tout l'import.
std::string resoudreEntites(const std::string& brut) {
    std::string sortie;
    sortie.reserve(brut.size());
    for (size_t i = 0; i < brut.size();) {
        if (brut[i] != '&') { sortie += brut[i++]; continue; }
        const size_t fin = brut.find(';', i);
        if (fin == std::string::npos || fin - i > 12) { sortie += brut[i++]; continue; }
        const std::string nom = brut.substr(i + 1, fin - i - 1);
        if (nom == "amp") sortie += '&';
        else if (nom == "lt") sortie += '<';
        else if (nom == "gt") sortie += '>';
        else if (nom == "quot") sortie += '"';
        else if (nom == "apos") sortie += '\'';
        else if (nom.size() > 1 && nom[0] == '#') {
            const int base = (nom[1] == 'x' || nom[1] == 'X') ? 16 : 10;
            const std::string chiffres = nom.substr(base == 16 ? 2 : 1);
            try {
                const long code = std::stol(chiffres, nullptr, base);
                // Encodage UTF-8 : les noms de pistes contiennent des accents.
                if (code < 0x80) {
                    sortie += static_cast<char>(code);
                } else if (code < 0x800) {
                    sortie += static_cast<char>(0xC0 | (code >> 6));
                    sortie += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    sortie += static_cast<char>(0xE0 | (code >> 12));
                    sortie += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    sortie += static_cast<char>(0x80 | (code & 0x3F));
                }
            } catch (...) {
                sortie += brut.substr(i, fin - i + 1);
            }
        } else {
            sortie += brut.substr(i, fin - i + 1);
            i = fin + 1;
            continue;
        }
        i = fin + 1;
    }
    return sortie;
}

class Analyseur {
public:
    explicit Analyseur(const std::string& texte) : t_(texte) {}

    std::unique_ptr<XmlNode> document() {
        std::unique_ptr<XmlNode> racine;
        for (;;) {
            sauterEspaces();
            if (i_ >= t_.size()) break;
            if (t_[i_] != '<') { ++i_; continue; }
            if (sauterEnTete()) continue;
            racine = element();
            break;
        }
        if (!racine) throw XmlError("document XML sans élément racine");
        return racine;
    }

private:
    void sauterEspaces() { while (i_ < t_.size() && estEspace(t_[i_])) ++i_; }

    /// Déclaration, commentaire, DOCTYPE : tout ce qui n'est pas un élément.
    /// Rend `true` si quelque chose a été sauté.
    bool sauterEnTete() {
        if (t_.compare(i_, 4, "<!--") == 0) {
            const size_t fin = t_.find("-->", i_ + 4);
            if (fin == std::string::npos) throw XmlError("commentaire XML non fermé");
            i_ = fin + 3;
            return true;
        }
        if (t_.compare(i_, 2, "<?") == 0) {
            const size_t fin = t_.find("?>", i_ + 2);
            if (fin == std::string::npos) throw XmlError("déclaration XML non fermée");
            i_ = fin + 2;
            return true;
        }
        if (t_.compare(i_, 9, "<!DOCTYPE") == 0) {
            const size_t fin = t_.find('>', i_);
            if (fin == std::string::npos) throw XmlError("DOCTYPE non fermé");
            i_ = fin + 1;
            return true;
        }
        return false;
    }

    std::string nom() {
        const size_t debut = i_;
        while (i_ < t_.size() && !estEspace(t_[i_]) && t_[i_] != '>' && t_[i_] != '/'
               && t_[i_] != '=' && t_[i_] != '<')
            ++i_;
        if (i_ == debut) throw XmlError("nom XML vide à l'octet " + std::to_string(i_));
        return t_.substr(debut, i_ - debut);
    }

    std::unique_ptr<XmlNode> element() {
        if (i_ >= t_.size() || t_[i_] != '<') throw XmlError("élément attendu");
        ++i_;
        auto noeud = std::make_unique<XmlNode>();
        noeud->name = nom();

        for (;;) {
            sauterEspaces();
            if (i_ >= t_.size()) throw XmlError("balise « " + noeud->name + " » non fermée");
            if (t_.compare(i_, 2, "/>") == 0) { i_ += 2; return noeud; }
            if (t_[i_] == '>') { ++i_; break; }
            XmlAttribute attribut;
            attribut.name = nom();
            sauterEspaces();
            if (i_ < t_.size() && t_[i_] == '=') {
                ++i_;
                sauterEspaces();
                if (i_ >= t_.size() || (t_[i_] != '"' && t_[i_] != '\''))
                    throw XmlError("valeur d'attribut sans guillemets dans « " + noeud->name + " »");
                const char guillemet = t_[i_++];
                const size_t debut = i_;
                while (i_ < t_.size() && t_[i_] != guillemet) ++i_;
                if (i_ >= t_.size()) throw XmlError("valeur d'attribut non fermée");
                attribut.value = resoudreEntites(t_.substr(debut, i_ - debut));
                ++i_;
            }
            noeud->attributes.push_back(std::move(attribut));
        }

        // Contenu jusqu'à la balise fermante.
        std::string texte;
        for (;;) {
            if (i_ >= t_.size()) throw XmlError("balise « " + noeud->name + " » non fermée");
            if (t_.compare(i_, 9, "<![CDATA[") == 0) {
                const size_t fin = t_.find("]]>", i_ + 9);
                if (fin == std::string::npos) throw XmlError("CDATA non fermé");
                texte += t_.substr(i_ + 9, fin - i_ - 9);
                i_ = fin + 3;
                continue;
            }
            if (t_.compare(i_, 2, "</") == 0) {
                i_ += 2;
                const std::string fermante = nom();
                if (fermante != noeud->name)
                    throw XmlError("« </" + fermante + "> » ferme « <" + noeud->name + "> »");
                sauterEspaces();
                if (i_ >= t_.size() || t_[i_] != '>') throw XmlError("balise fermante mal formée");
                ++i_;
                break;
            }
            if (t_[i_] == '<') {
                if (sauterEnTete()) continue;
                noeud->children.push_back(element());
                continue;
            }
            texte += t_[i_++];
        }

        // On ne garde le texte que s'il porte autre chose que des espaces :
        // un XML indenté en est plein, et les conserver gonflerait la mémoire
        // d'un projet de plusieurs mégaoctets pour rien.
        bool utile = false;
        for (char c : texte) if (!estEspace(c)) { utile = true; break; }
        if (utile) noeud->text = resoudreEntites(texte);
        return noeud;
    }

    const std::string& t_;
    size_t i_ = 0;
};

} // namespace

std::string XmlNode::attribute(const std::string& nom, const std::string& defaut) const {
    for (const auto& a : attributes) if (a.name == nom) return a.value;
    return defaut;
}

bool XmlNode::hasAttribute(const std::string& nom) const {
    for (const auto& a : attributes) if (a.name == nom) return true;
    return false;
}

const XmlNode* XmlNode::child(const std::string& nom) const {
    for (const auto& c : children) if (c->name == nom) return c.get();
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(const std::string& nom) const {
    std::vector<const XmlNode*> sortie;
    for (const auto& c : children) if (c->name == nom) sortie.push_back(c.get());
    return sortie;
}

const XmlNode* XmlNode::find(const std::string& nom) const {
    for (const auto& c : children) {
        if (c->name == nom) return c.get();
        if (const XmlNode* trouve = c->find(nom)) return trouve;
    }
    return nullptr;
}

void XmlNode::findAll(const std::string& nom, std::vector<const XmlNode*>& sortie) const {
    for (const auto& c : children) {
        if (c->name == nom) sortie.push_back(c.get());
        c->findAll(nom, sortie);
    }
}

std::vector<const XmlNode*> XmlNode::findAll(const std::string& nom) const {
    std::vector<const XmlNode*> sortie;
    findAll(nom, sortie);
    return sortie;
}

XmlDocument parseXml(const std::string& texte) {
    Analyseur analyseur(texte);
    XmlDocument doc;
    doc.root = analyseur.document();
    return doc;
}

} // namespace vsm::interchange
