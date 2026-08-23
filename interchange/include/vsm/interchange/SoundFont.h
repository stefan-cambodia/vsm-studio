#pragma once
#include "vsm/audio/plugin/IMultisampleBank.h"
#include <cstdint>
#include <string>
#include <vector>

// Lecture du sous-ensemble UTILE d'un fichier SoundFont 2 (`*.sf2`), et
// conversion vers le format de profil de `vsm.multisample`
// (docs/CDC-multisample.md § 5, phase M2).
//
// CE QUE ÇA APPORTE : un lecteur multi-échantillons sans banque est un moteur
// sans carburant. Le SF2 est le seul format d'instrument échantillonné dont il
// existe des banques libres COMPLÈTES et attribuables — FluidR3, GeneralUser GS
// — couvrant l'orchestre General MIDI entier. Le lire, c'est ouvrir cent
// vingt-huit instruments d'un coup, sans une ligne de DSP nouvelle.
//
// OÙ CE CODE VIT, ET POURQUOI PAS AILLEURS. Le § 5 du cahier des charges
// demande que la conversion se fasse « hors du moteur, outil dans analyse/ ou
// tools/ ». L'outil EST dans `tools/` (`vsm-sf2`) ; ce qui vit ici, c'est la
// LECTURE, pour une raison que le dépôt applique partout ailleurs : dans ce
// projet, tout format a des tests. Un analyseur de format écrit en Python
// n'aurait, faute d'infrastructure de test Python, aucune couverture — et un
// analyseur de format sans tests est exactement le genre de pièce qui se met à
// mal interpréter un champ sans que personne ne le voie. L'invariant qui
// comptait est préservé : `core/` et `audio/` ignorent tout du SF2, le DAW ne
// charge jamais qu'un profil, et le moteur reste sans format tiers.
//
// CE QUI EST LU, ET CE QUI NE L'EST PAS. Le format SF2 déclare une soixantaine
// de « générateurs » ; une petite dizaine décrit un instrument échantillonné,
// le reste décrit une synthèse soustractive complète (filtre, LFO, enveloppes
// de modulation) que cette machine ne possède pas. Les générateurs non pris en
// charge sont IMPRIMÉS À LA CONVERSION, un par un, jamais avalés en silence :
// c'est la règle du § 5, et elle est vérifiée par un test.

namespace vsm::interchange {

/// Un preset SF2, tel que l'en-tête `phdr` le déclare.
struct SoundFontPreset {
    std::string name;
    int program = 0;   ///< numéro de programme (0..127)
    int bank = 0;      ///< banque (0 = mélodique General MIDI, 128 = percussions)
};

/// Ce qu'une conversion a produit, et ce qu'elle a laissé de côté.
struct SoundFontConversion {
    bool success = false;
    std::string error;

    /// Zones prêtes pour le profil. `samplePath` et `relativePath` sont
    /// remplis par `writeSoundFontProfile`, pas ici : la lecture ne décide pas
    /// où les fichiers iront.
    std::vector<vsm::audio::plugin::MultisampleZoneSpec> zones;
    /// Audio de chaque zone, dans le même ordre. Mono ou stéréo selon le
    /// `sampleType` du SF2.
    std::vector<std::vector<float>> audioLeft;
    std::vector<std::vector<float>> audioRight; ///< vide pour une zone mono
    std::vector<double> sampleRates;

    /// Enveloppe de volume du preset, en secondes, telle que le SF2 la déclare.
    /// Elle n'entre pas dans le profil — le format n'a pas d'enveloppe par zone
    /// — mais dans le preset `*.synth.json` écrit à côté, où elle devient la
    /// valeur de départ des réglages `envelope.1.{attack,release}` de la
    /// machine. Rien n'est perdu, et rien n'est inventé.
    float attackSeconds = 0.002f;
    float releaseSeconds = 0.3f;

    /// Générateurs rencontrés que cette version n'applique pas, avec leur
    /// nombre d'occurrences. Imprimés par l'outil, jamais tus.
    std::vector<std::pair<std::string, int>> ignoredGenerators;
    /// Autres écarts constatés (modulateurs présents, échantillon 24 bits,
    /// lien stéréo cassé…).
    std::vector<std::string> notes;
};

/// Presets déclarés par un fichier SF2, dans l'ordre du fichier.
struct SoundFontIndex {
    bool success = false;
    std::string error;
    std::string bankName;      ///< champ `INAM`
    std::string engineers;     ///< champ `IENG`, utile à l'attribution
    std::string copyright;     ///< champ `ICOP`
    int majorVersion = 0, minorVersion = 0;
    std::vector<SoundFontPreset> presets;
};

/// Lit uniquement les en-têtes : de quoi lister les instruments sans décoder
/// des dizaines de mégaoctets d'échantillons.
SoundFontIndex readSoundFontIndex(const std::string& path);

/// Convertit UN preset en zones. `bank`/`program` désignent le preset ;
/// `maxSeconds` tronque les échantillons (0 = ne pas tronquer).
SoundFontConversion convertSoundFontPreset(const std::string& path, int bank, int program,
                                            double maxSeconds = 0.0);

/// Écrit le profil et ses WAV dans `folder`, plus le preset `*.synth.json`
/// portant l'enveloppe. Rend le chemin du profil, vide en cas d'échec.
struct SoundFontWriteResult {
    bool success = false;
    std::string error;
    std::string profilePath;
    std::string presetPath;
    size_t memoryBytes = 0;
};
SoundFontWriteResult writeSoundFontProfile(const SoundFontConversion& conversion,
                                            const std::string& folder,
                                            const std::string& profileName,
                                            const std::string& attribution);

/// Écrit un SF2 MINIMAL, engendré, pour les tests : deux zones de notes, deux
/// couches de vélocité, des sinusoïdes. Quelques kilo-octets, donc commis sans
/// scrupule — et surtout, aucun test ne dépend d'une banque téléchargée.
bool writeMinimalSoundFont(const std::string& path, std::string& outError);

} // namespace vsm::interchange
