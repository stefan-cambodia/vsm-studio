#pragma once
#include "vsm/audio/engine/MidiLearnMap.h"
#include <string>

namespace vsm::interchange {

/// ÉCRIRE ET RELIRE LES ASSOCIATIONS MIDI (D10.2).
///
/// **POURQUOI ICI ET NON DANS `audio/`.** `MidiLearnMap` est de la logique
/// pure, sans dépendance ; JSON n'entre que dans cette couche-ci (§ 0 de
/// `ROADMAP-interop.md`). La carte ne doit pas apprendre à s'écrire pour
/// pouvoir être conservée.
///
/// **POURQUOI DU JSON ET NON UNE LIGNE PAR ASSOCIATION.** Un fichier de
/// préférences se retrouve édité à la main le jour où quelque chose cloche, et
/// un format positionnel — `74,3,12,0,1` — ne se relit pas. Chaque champ porte
/// donc son nom.
///
/// **CE QUE LA RELECTURE REFUSE.** Une association dont le genre est inconnu
/// (fichier écrit par une version future) est ÉCARTÉE, pas devinée : un
/// potentiomètre qui pilote autre chose que ce qu'on croit est pire qu'un
/// potentiomètre inerte. Le compte des associations écartées est rendu, pour
/// que l'application puisse le dire au lieu de laisser chercher.
struct MidiLearnLoadResult {
    bool success = false;
    vsm::audio::engine::MidiLearnMap map;
    /// Associations lues mais écartées (genre inconnu, contrôleur hors plage).
    size_t discarded = 0;
    std::string error;
};

/// Sérialise la carte. Toujours valide, même vide.
std::string midiLearnToJson(const vsm::audio::engine::MidiLearnMap& map);

/// Relit une carte. Un texte VIDE est un succès qui rend une carte vide : au
/// premier lancement il n'y a rien d'enregistré, et ce n'est pas une erreur.
MidiLearnLoadResult midiLearnFromJson(const std::string& text);

/// Le nom lisible d'une cible, pour la liste des associations. C'est ici et
/// non dans l'interface : le même libellé sert à l'affichage et aux tests, et
/// deux formulations finiraient par se contredire.
std::string describeMidiLearnTarget(const vsm::audio::engine::MidiLearnTarget& target,
                                     const std::string& parameterName = {});

} // namespace vsm::interchange
