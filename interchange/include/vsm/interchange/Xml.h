#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace vsm::interchange {

/// LECTURE XML, juste ce qu'il faut pour lire un projet de DAW.
///
/// POURQUOI ICI. Deux des trois formats visés par `docs/CDC-import-daw.md` sont
/// du XML : le `.als` d'Ableton Live (gzippé) et la *Track Archive* de Cubase.
/// `vsm_interchange` n'a aucune dépendance externe et c'est une propriété qu'on
/// garde — même raison que l'inflate juste à côté et que la transformée de
/// Fourier de `vsm.spectral`.
///
/// CE QU'IL SAIT LIRE, ET C'EST SUFFISANT POUR CES FICHIERS : éléments,
/// attributs, imbrication, texte, éléments auto-fermants, déclaration
/// `<?xml?>`, commentaires, sections CDATA, et les cinq entités prédéfinies.
///
/// CE QU'IL NE SAIT PAS FAIRE, ET QUI EST DIT PLUTÔT QUE SOUS-ENTENDU : ni DTD,
/// ni schémas, ni espaces de noms (un `ns:balise` est lu comme une balise
/// nommée « ns:balise », ce qui suffit pour retrouver ce qu'on cherche), ni
/// entités personnalisées. Un fichier qui en aurait besoin serait REFUSÉ avec
/// un message, jamais lu à moitié.
class XmlError : public std::runtime_error {
public:
    explicit XmlError(const std::string& quoi) : std::runtime_error(quoi) {}
};

struct XmlAttribute {
    std::string name;
    std::string value;
};

class XmlNode {
public:
    std::string name;
    std::string text;                       ///< texte direct, entités résolues
    std::vector<XmlAttribute> attributes;
    std::vector<std::unique_ptr<XmlNode>> children;

    /// La valeur d'un attribut, ou `defaut` s'il est absent. Ne lève pas :
    /// un attribut manquant est un cas ORDINAIRE dans ces fichiers.
    std::string attribute(const std::string& nom, const std::string& defaut = {}) const;
    bool hasAttribute(const std::string& nom) const;

    /// Tous les enfants portant ce nom (dans l'ordre du fichier).
    std::vector<const XmlNode*> childrenNamed(const std::string& nom) const;

    /// LE PREMIER DESCENDANT portant ce nom, à n'importe quelle profondeur.
    /// Indispensable ici : un `.als` enfouit ses notes sous une dizaine de
    /// niveaux dont les noms changent d'une version de Live à l'autre, et
    /// suivre le chemin exact rendrait le lecteur cassant pour rien.
    const XmlNode* find(const std::string& nom) const;
    /// Tous les descendants portant ce nom, en parcours préfixe.
    void findAll(const std::string& nom, std::vector<const XmlNode*>& sortie) const;
    std::vector<const XmlNode*> findAll(const std::string& nom) const;
};

struct XmlDocument {
    std::unique_ptr<XmlNode> root;
};

/// Analyse un document. Lève `XmlError` sur toute anomalie de structure —
/// balise non fermée, fermeture qui ne correspond pas, fin prématurée.
XmlDocument parseXml(const std::string& texte);

} // namespace vsm::interchange
