#pragma once
#include <array>
#include "vsm/midi/MidiEvent.h"
#include <cstdint>
#include <map>
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

/// UN CLIP : une RÉGION du matériau de la piste, posée sur la ligne de temps.
///
/// LE CHOIX DE CONCEPTION, ET CELUI QUI A ÉTÉ ÉCARTÉ. Un clip pourrait être un
/// CONTENEUR qui emporte ses notes : la piste ne serait plus qu'une liste de
/// clips, chacun avec son propre vecteur de notes en ticks relatifs. C'est ce
/// que fait Ableton. On a retenu l'autre modèle, celui de la RÉGION -- comme
/// Pro Tools ou Logic : la piste garde son matériau sur une seule ligne de
/// temps, et un clip est une fenêtre sur ce matériau, posée ailleurs.
///
/// Trois raisons, dans cet ordre :
///
///  1. **Un clip posé deux fois partage ses notes SANS QU'ON AIT RIEN À
///     FAIRE.** Deux fenêtres sur le même matériau lisent les mêmes notes ;
///     éditer l'une modifie l'autre parce qu'il n'y a jamais eu deux copies.
///     Le modèle conteneur exige, lui, une indirection explicite (un « clip
///     source » partagé et des « instances » qui le référencent) et tout le
///     comptage de références qui va avec.
///  2. **Aucune note ne change de place.** Le modèle conteneur oblige à
///     réécrire les ticks de toutes les notes en relatif, donc à toucher les
///     quatre-vingt-trois endroits qui les manipulent aujourd'hui, dont le
///     piano roll entier -- pour un rendu qui doit rester identique au bit
///     près. Beaucoup de risque, aucun gain audible.
///  3. **Une piste SANS clip garde exactement le comportement qu'elle avait**
///     (voir `Track::clips`), ce qui rend la migration des projets existants
///     vide et les rend impossibles à casser.
///
/// Ce que ce modèle coûte, et qui est assumé : éditer les notes « dans » un
/// clip posé ailleurs sur la ligne de temps édite le matériau À SA POSITION
/// D'ORIGINE. C'est le comportement d'un éditeur de régions, et c'est celui
/// qu'on veut ici -- un enregistrement reconstruit a UNE ligne de temps.
struct Clip {
    /// Début de la fenêtre DANS LE MATÉRIAU de la piste.
    Tick sourceStart = 0;
    /// Longueur de la fenêtre. Zéro signifie « jusqu'à la fin du matériau ».
    Tick sourceLength = 0;
    /// Où la fenêtre est POSÉE sur la ligne de temps.
    Tick startTick = 0;
    /// Durée jouée. Zéro = celle de la fenêtre. Plus longue que la fenêtre, le
    /// clip la RÉPÈTE -- c'est la boucle de clip, et elle n'exige aucune copie.
    Tick length = 0;
    bool muted = false;
    std::string name;
    uint32_t colorRgba = 0xFF6B9BFFu;

    /// POUR UN CLIP AUDIO : le décalage dans le FICHIER, en secondes.
    ///
    /// POURQUOI DES SECONDES ICI ALORS QUE TOUT LE RESTE EST EN TICKS. Un tick
    /// est une position MUSICALE : le convertir en temps passe par la carte de
    /// tempo, et suppose donc que le matériau suit le tempo. Une note le fait ;
    /// un enregistrement, non -- pas tant que l'étirement temporel n'est pas
    /// écrit (choix n° 3 du § 4 de `ROADMAP-daw.md`). Exprimer la fenêtre d'un
    /// clip audio en ticks reviendrait à promettre un suivi de tempo qui
    /// n'existe pas, et à décaler silencieusement le son au premier changement
    /// de tempo. La POSITION du clip reste musicale (`startTick`), son CONTENU
    /// est du temps réel.
    double sourceStartSeconds = 0.0;
    /// Fondus d'entrée et de sortie, en secondes. Zéro = attaque franche.
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    /// Gain linéaire du clip, et inversion de phase -- les deux réglages sans
    /// lesquels on ne peut pas monter deux prises l'une après l'autre.
    float gain = 1.0f;
    bool invertPhase = false;
};

/// LE MATÉRIAU D'UNE PISTE AUDIO : un fichier, et ce qu'il faut en savoir pour
/// l'afficher et le placer sans avoir à l'ouvrir.
///
/// Le chemin est RELATIF au dossier de projet, comme les presets et les
/// échantillons -- c'est ce qui permet à un projet de s'ouvrir sur une autre
/// machine. Un chemin absolu est refusé par le format, pas silencieusement
/// réécrit.
///
/// `sampleRate` et `frames` décrivent le fichier TEL QU'IL EST sur le disque,
/// pas tel que la session le joue : c'est la seule façon de savoir qu'un
/// rééchantillonnage est nécessaire, et de dessiner une forme d'onde à la
/// bonne largeur avant que le fichier ne soit lu.
struct AudioSource {
    std::string path;
    double sampleRate = 0.0;   ///< 0 = pas encore lue
    int64_t frames = 0;        ///< longueur du fichier, en trames
    int channels = 0;

    bool empty() const { return path.empty(); }
    double durationSeconds() const {
        return sampleRate > 0.0 ? static_cast<double>(frames) / sampleRate : 0.0;
    }
};

/// Un repère nommé sur la ligne de temps (couplet, refrain, « ici ça coince »).
///
/// Le format MIDI en porte depuis toujours (méta-événements Marker et
/// CuePoint) et ce projet les CONSERVAIT en octets opaques pour un export
/// fidèle, sans jamais les montrer ni les écrire dans `project.json` : ils
/// traversaient le logiciel sans exister pour lui.
struct Marker {
    Tick tick = 0;
    std::string name;
};

/// Un effet d'insert, DÉCRIT et non instancié : un identifiant de fabrique et
/// des valeurs de paramètres. `core/` ne connaît pas `audio/` et ne peut donc
/// pas porter un `IAudioEffect` ; il porte ce qu'il faut pour en fabriquer un,
/// ce qui est exactement ce que le format de projet écrit sur le disque.
///
/// POURQUOI C'EST DANS LA PISTE, ET PAS À CÔTÉ. La première version rangeait
/// les chaînes dans une `std::map<int, Chain>` **indexée par numéro de piste**,
/// interne au composant d'interface. Supprimer une piste décalait toutes les
/// suivantes et réaffectait les effets aux mauvaises pistes -- en silence, et
/// sans que rien ne soit jamais sauvegardé. Rangée DANS la piste, la chaîne
/// suit la piste : la suppression n'a plus rien à recalculer, et le bug ne
/// peut pas revenir par inadvertance.
struct TrackEffect {
    std::string type;                        ///< identifiant EffectFactory ("reverb"...)
    std::map<std::string, float> parameters; ///< nom de paramètre -> valeur en unités réelles
};

/// Un point d'automation. `value` est en UNITÉS RÉELLES (Hz, secondes), jamais
/// en normalisé -- la règle de tout le projet. `step` dit que le segment
/// PARTANT de ce point est un palier et non une rampe.
struct AutomationPoint {
    Tick tick = 0;
    float value = 0.0f;
    bool step = false;
};

/// Une courbe d'automation, ciblant un paramètre par son identité SÉMANTIQUE
/// (« filter.1.cutoff ») : c'est ce qui permet à la courbe de survivre à un
/// changement de machine, et à la chaîne d'analyse de l'écrire sans connaître
/// le code du DAW. Rangée dans la piste pour la même raison que `TrackEffect`.
struct AutomationCurve {
    std::string parameter;
    std::vector<AutomationPoint> points;
};

/// Une piste MIDI éditable : notes + lanes de contrôleurs, plus les
/// attributs de mixage/routing exposés par le Track Editor (section 4 du
/// cahier des charges). Le routing vers un synthé virtuel (`instrumentId`)
/// est préparé ici mais activé en Phase 2 (Synth Rack).
class Track {
public:
    /// Ce que la piste PORTE. Une piste MIDI joue des notes par un instrument ;
    /// une piste audio joue un fichier. Le reste -- volume, panoramique, muet,
    /// solo, départs, inserts, automation, clips -- leur est commun, et c'est
    /// pourquoi il n'y a qu'une classe : une piste audio n'est pas une autre
    /// espèce d'objet, c'est une piste dont le matériau n'est pas des notes.
    enum class Kind { Midi, Audio };
    Kind kind = Kind::Midi;

    std::string name;
    uint32_t colorRgba = 0xFF6B9BFFu; // ARGB, couleur par défaut (bleu doux)
    uint8_t channel = 0;              // 0-15

    /// Le MATÉRIAU de la piste : toutes ses notes, sur une seule ligne de
    /// temps, en ticks absolus. Les clips ci-dessous sont des fenêtres
    /// dessus ; ils ne l'emportent pas.
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

    /// Le fichier que joue une piste audio. Vide sur une piste MIDI.
    AudioSource audio;

    /// Les clips de la piste.
    ///
    /// **VIDE SIGNIFIE « AUCUNE DÉCOUPE »**, c'est-à-dire que la piste joue
    /// tout son matériau à sa place -- exactement ce qu'elle faisait avant
    /// que les clips existent. Ce n'est pas un cas particulier honteux : c'est
    /// le sens littéral du mot. Une piste qu'on n'a pas découpée n'a pas de
    /// clip, et un test vérifie que son rendu est identique au bit près à
    /// celui d'avant.
    std::vector<Clip> clips;

    /// Chaîne d'inserts et courbes d'automation de CETTE piste. Voir
    /// `TrackEffect` pour la raison -- non décorative -- de les ranger ici.
    std::vector<TrackEffect> effects;
    std::vector<AutomationCurve> automation;

    /// Trie toutes les lanes par tick croissant. À appeler après toute
    /// édition manuelle en dehors des méthodes utilitaires ci-dessous.
    void sortEvents();

    /// Ajoute une note et lui assigne un id unique via le compteur fourni.
    Note& addNote(Tick start, Tick end, uint8_t number, uint8_t velocity,
                   uint8_t channelOverride, uint64_t& idCounter);
};

} // namespace vsm::sequencer
