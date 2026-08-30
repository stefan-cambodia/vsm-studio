#pragma once
#include <cstdint>
#include <string>

namespace vsm::interchange {

/// CE QU'UNE SAUVEGARDE AUTOMATIQUE DOIT SAVOIR D'ELLE-MÊME (D10.4).
///
/// Le dossier de récupération contient un projet complet — `project.json`, le
/// MIDI, les presets — mais **pas les médias** : recopier une prise de deux
/// cents mégaoctets toutes les trente secondes ferait de la sauvegarde
/// automatique une panne à elle seule. Les chemins de médias restent donc
/// relatifs au dossier D'ORIGINE du projet, et c'est ce fichier-ci qui s'en
/// souvient. Sans lui, un projet récupéré rouvrirait avec toutes ses pistes
/// audio muettes, et rien n'expliquerait pourquoi.
///
/// Il porte aussi de quoi POSER LA QUESTION correctement : on ne demande pas
/// « récupérer une session ? » mais « récupérer *Sky and Sand*, enregistré
/// automatiquement il y a quatre minutes ? ». Un utilisateur ne peut répondre à
/// la première.
struct RecoveryRecord {
    /// Le dossier du projet au moment de la sauvegarde. Vide = projet jamais
    /// enregistré, et c'est justement celui qu'on aurait tout perdu.
    std::string originalFolder;
    std::string title;
    /// Date de la dernière écriture, en secondes depuis l'époque Unix.
    int64_t savedAtEpochSeconds = 0;
    /// Ce que la session tenait au moment de la sauvegarde, pour que la
    /// question dise quelque chose de vérifiable.
    int trackCount = 0;
    int noteCount = 0;
};

std::string recoveryRecordToJson(const RecoveryRecord& record);

/// Relit un enregistrement. Renvoie faux si le texte n'est pas exploitable --
/// un dossier de récupération illisible est IGNORÉ, jamais proposé à moitié :
/// proposer de récupérer puis échouer serait la deuxième mauvaise nouvelle
/// d'affilée.
bool recoveryRecordFromJson(const std::string& text, RecoveryRecord& out);

/// Le nom du fichier dans le dossier de récupération.
inline constexpr const char* kRecoveryRecordFileName = "recuperation.json";

} // namespace vsm::interchange
