#pragma once
#include <array>
#include "vsm/midi/MidiEvent.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vsm::sequencer {

using vsm::midi::Tick;

/// Une note "appariée" : contrairement au NoteOn/NoteOff bruts du fichier
/// MIDI, une Note a un début ET une fin explicites, ce qui est indispensable
/// pour l'édition dans le piano roll (déplacement, resize, sélection...).
struct Note {
    Tick startTick = 0;
    Tick endTick = 0;      // exclusif, endTick > startTick
    uint8_t channel = 0;
    uint8_t number = 60;   // 0-127, 60 = C4/C3 selon convention
    uint8_t velocity = 100;
    uint8_t releaseVelocity = 64;
    uint64_t id = 0;       // identifiant stable, utilisé par la sélection,
                            // l'automation liée et le RNG déterministe (humanize)
    /// Note rendue muette SANS être supprimée : elle reste visible et éditable
    /// dans le piano roll (affichée hachurée) mais n'est ni jouée ni exportée.
    /// C'est un concept d'ÉDITEUR : le format SMF n'a rien pour le
    /// représenter, d'où son absence à l'export (documenté dans Project.cpp).
    /// Placé APRÈS `id` volontairement : tout le code existant construit une
    /// Note par agrégat positionnel `Note{start, end, canal, num, vel, relVel,
    /// id}`, qui reste donc valide tel quel.
    bool muted = false;

    /// CONFIANCE de la transcription, entre 0 et 1. Vaut 1 pour une note
    /// saisie à la main : ce qu'on a joué soi-même n'est pas douteux.
    ///
    /// Elle ne sert QU'À L'AFFICHAGE, et c'est délibéré : une note peu sûre se
    /// joue et s'exporte exactement comme les autres. La masquer ou la taire
    /// reviendrait à décider à la place de l'utilisateur ; la signaler lui
    /// permet d'aller l'écouter et de trancher. Le format SMF n'a rien pour la
    /// représenter, elle ne part donc pas à l'export MIDI -- comme `muted`.
    ///
    /// Placée APRÈS `muted`, même raison que lui : tout le code existant
    /// construit une Note par agrégat positionnel, qui reste valide tel quel.
    float confidence = 1.0f;

    Tick durationTicks() const { return endTick - startTick; }
};

// Chaque point de lane porte son propre canal MIDI : une piste VSM
// correspond en général à un seul canal (cf. Track Editor), mais on
// préserve le canal d'origine de chaque événement pour un export fidèle,
// y compris pour les fichiers source multi-canaux sur une même piste.
struct CcPoint              { Tick tick; uint8_t channel; uint8_t controller; uint8_t value; };
struct PitchBendPoint       { Tick tick; uint8_t channel; int16_t value; };
struct PolyAftertouchPoint  { Tick tick; uint8_t channel; uint8_t note; uint8_t pressure; };
struct ChannelPressurePoint { Tick tick; uint8_t channel; uint8_t pressure; };
struct ProgramChangePoint   { Tick tick; uint8_t channel; uint8_t program; };

/// Une piste MIDI éditable : notes + lanes de contrôleurs, plus les
/// attributs de mixage/routing exposés par le Track Editor (section 4 du
/// cahier des charges). Le routing vers un synthé virtuel (`instrumentId`)
/// est préparé ici mais activé en Phase 2 (Synth Rack).
class Track {
public:
    std::string name;
    uint32_t colorRgba = 0xFF6B9BFFu; // ARGB, couleur par défaut (bleu doux)
    uint8_t channel = 0;              // 0-15

    std::vector<Note> notes;
    std::vector<CcPoint> controlChanges;
    std::vector<PitchBendPoint> pitchBends;
    std::vector<PolyAftertouchPoint> polyAftertouch;
    std::vector<ChannelPressurePoint> channelPressure;
    std::vector<ProgramChangePoint> programChanges;

    /// Méta-événements non modélisés (texte, sysex, méta inconnus) conservés
    /// tels quels pour un export fidèle. Voir vsm::midi::UnknownMetaEvent.
    std::vector<vsm::midi::MidiEvent> miscEvents;

    bool muted = false;
    bool solo = false;
    bool armed = false;
    bool monitoring = false;
    float volume = 1.0f;  // gain linéaire, 1.0 = 0 dB
    float pan = 0.0f;      // -1 (gauche) .. +1 (droite)
    /// Niveaux d'envoi vers les 2 bus auxiliaires (sends, section 15).
    /// 0 = pas d'envoi. Portés par Track (donnée de mixage) comme volume/pan.
    std::array<float, 2> sendLevels{{0.0f, 0.0f}};

    /// Identifiant du plugin instrument assigné (vide = aucun). Résolu par
    /// le Synth Rack en Phase 2 via ISynthPlugin / PluginRegistry.
    std::string instrumentId;
    std::string presetId;

    /// Trie toutes les lanes par tick croissant. À appeler après toute
    /// édition manuelle en dehors des méthodes utilitaires ci-dessous.
    void sortEvents();

    /// Ajoute une note et lui assigne un id unique via le compteur fourni.
    Note& addNote(Tick start, Tick end, uint8_t number, uint8_t velocity,
                   uint8_t channelOverride, uint64_t& idCounter);
};

} // namespace vsm::sequencer
