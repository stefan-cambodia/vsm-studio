#pragma once
#include <array>
#include <cmath>
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
    /// dans le piano roll (affichée hachurée) mais n'est pas jouée, et
    /// n'apparaît donc pas dans le flux de notes exporté.
    /// C'est un concept d'ÉDITEUR : le format SMF n'a rien pour le
    /// représenter. Depuis D6.3, elle survit tout de même à un aller-retour
    /// par un fichier .mid, écrite dans un bloc privé 0x7F que les autres
    /// logiciels ignorent (voir Project.cpp) -- sans jamais réintégrer le flux
    /// joué, sans quoi le fichier jouerait autre chose que ce qu'on entend.
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
    /// représenter ; depuis D6.3 elle voyage, comme `muted`, dans le bloc privé
    /// 0x7F -- de sorte qu'exporter puis réimporter une transcription ne perde
    /// plus le travail de vérification déjà fait.
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
/// LA FORME D'UN FONDU (D17.1).
///
/// POURQUOI CE N'EST PAS UN DÉTAIL D'ESTHÉTIQUE. Un fondu enchaîné entre deux
/// clips CORRÉLÉS -- deux prises du même passage, ou le même matériau coupé et
/// recollé, c'est-à-dire le cas courant d'un montage -- se somme en
/// AMPLITUDE : au point de croisement, deux moitiés linéaires font 0,5 + 0,5,
/// donc le niveau de départ, et tout va bien. Entre deux clips DÉCORRÉLÉS, la
/// somme se fait en PUISSANCE : 0,5² + 0,5² = 0,5, soit −3 dB. Le raccord se
/// creuse, et rien ne le dit.
///
/// Les deux cas existent et demandent deux formes, c'est pourquoi Cubase en
/// propose sept et Live deux (« Constant Gain » et « Constant Power »). Ici :
///
/// `Linear` : le gain suit une droite. Juste pour du matériau corrélé, et
/// c'est la forme qu'avaient tous les fondus jusqu'ici -- donc le défaut, pour
/// que rien de ce qui existe ne change de son.
/// `EqualPower` : le gain suit un quart de sinusoïde, dont le carré s'ajoute à
/// un avec celui du fondu complémentaire. Juste pour du matériau décorrélé.
/// `Slow` : le fondu démarre doucement et finit vite (le carré de la droite) --
/// une entrée qui « arrive » plutôt qu'elle ne surgit.
/// `Fast` : l'inverse (la racine) -- une sortie qui tient avant de lâcher.
enum class FadeShape : uint8_t { Linear = 0, EqualPower = 1, Slow = 2, Fast = 3 };

/// LE GAIN D'UN FONDU à l'avancement `x` (0 au début du fondu, 1 à sa fin).
///
/// ÉCRITE ICI, DANS LE MODÈLE, ET NON DANS LE MOTEUR : le dessin du clip et le
/// son qu'il rend doivent venir de la MÊME formule, sans quoi on dessine une
/// courbe et on en entend une autre -- c'est déjà la règle des courbes
/// d'automation (§ 6), et elle vaut ici pour la même raison.
inline float fadeShapeGain(FadeShape shape, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    switch (shape) {
        case FadeShape::EqualPower: return std::sin(x * 1.57079632679489662f);
        case FadeShape::Slow:       return x * x;
        case FadeShape::Fast:       return std::sqrt(x);
        case FadeShape::Linear:
        default:                    return x;
    }
}

/// LE SUIVI DE TEMPO D'UN CLIP AUDIO (D12, `docs/CDC-etirement-temporel.md`).
///
/// `Off` : le contenu est du temps réel, comme depuis D2. `KeepPitch` : la
/// durée suit le tempo et la hauteur ne bouge pas -- par le VOCODEUR DE
/// PHASE depuis D12.8 (banc 8 : −2 ms sur huit mesures de *Sky and Sand*,
/// contre −8 au WSOLA). `Repitch` : la durée suit le tempo et la hauteur suit
/// avec, comme un vinyle qu'on ralentit (rééchantillonnage). `KeepPitchWsola` :
/// la hauteur conservée par le WSOLA, gardé comme TÉMOIN et comme repli --
/// une option de clip, écrite dans le projet, pour qu'un A/B se lise dans le
/// fichier et non dans une variable d'environnement.
enum class WarpMode : uint8_t { Off = 0, KeepPitch = 1, Repitch = 2, KeepPitchWsola = 3 };

/// UN MARQUEUR DE WARP : une paire (position dans le FICHIER, en secondes ;
/// position MUSICALE, en ticks depuis le début du clip). Entre deux
/// marqueurs, la relation est linéaire ; avant le premier et après le dernier,
/// le rapport du segment voisin se prolonge. Un clip étiré en a au moins
/// deux : son début (tick 0) et sa fin.
struct WarpMarker {
    double sourceSeconds = 0.0;
    Tick tick = 0;
};

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

    /// LE SUIVI DE TEMPO (D12). `Off` par défaut : un projet existant s'ouvre
    /// identique, et la chaîne d'analyse continue d'écrire des reports d'audio
    /// en temps réel. Les marqueurs ne comptent que si le mode est allumé, et
    /// il en faut deux au moins (`setClipWarpMode` pose la paire neutre).
    WarpMode warpMode = WarpMode::Off;
    std::vector<WarpMarker> warpMarkers;
    /// À L'ENVERS (D13.4) : le clip lit sa fenêtre du fichier à rebours -- une
    /// cymbale inversée, une traîne qui monte. La fenêtre reste la même ; seul
    /// le sens change, et le moteur lit un miroir du fichier.
    bool reversed = false;

    /// IDENTIFIANT STABLE, pour que la sélection de la vue d'arrangement
    /// survive aux gestes (D5.1).
    ///
    /// Un index ne le pourrait pas : couper un clip en insère un, et la
    /// sélection désignerait alors le voisin. C'est exactement la raison qui
    /// avait fait donner un `id` aux notes, et elle vaut ici pour les mêmes
    /// gestes -- déplacer, redimensionner, couper.
    ///
    /// PLACÉ EN DERNIER, volontairement : tout le code existant construit un
    /// Clip par agrégat positionnel (`{sourceStart, sourceLength, startTick,
    /// length, muted, name, color, sourceStartSeconds, fadeIn, fadeOut, gain,
    /// invertPhase}`), qui reste donc valide tel quel.
    ///
    /// PAS SAUVEGARDÉ, et c'est délibéré : rien d'autre ne référence un clip,
    /// donc l'identifiant n'a besoin d'être unique que pendant la session. Il
    /// est attribué au chargement, comme celui des notes venues d'un fichier
    /// MIDI. L'écrire ferait grossir le format d'une donnée que personne ne
    /// relit.
    uint64_t id = 0;

    /// LA FORME DES FONDUS (D17.1). `Linear` par défaut : les projets
    /// existants sonnent au bit près comme avant.
    ///
    /// APRÈS `id`, ET C'EST LA RÈGLE DE CE STRUCT, écrite plus haut : tout le
    /// code existant construit un `Clip` par agrégat POSITIONNEL. Glissé entre
    /// `fadeOutSeconds` et `gain`, ce champ décalait tout ce qui suit — la
    /// première écriture de D17.1 l'a fait, et le compilateur l'a rattrapée.
    FadeShape fadeShape = FadeShape::Linear;
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

/// UNE PRISE CONSERVÉE : ce qu'une passe d'enregistrement a produit, gardé
/// entier à côté des autres (D3.5).
///
/// LE CHOIX DE CONCEPTION, ET CELUI QUI A ÉTÉ ÉCARTÉ. Une piste pourrait porter
/// N matériaux en permanence et n'en jouer qu'un : le planning, le piano roll,
/// le rendu et l'export devraient alors tous savoir lequel, c'est-à-dire que
/// chacun des quatre-vingts endroits qui lisent `Track::notes` devrait poser la
/// question. On a retenu l'autre modèle, celui du RANGEMENT : la piste garde
/// UN seul matériau courant -- exactement celui qu'elle a toujours eu -- et les
/// prises inactives attendent à côté. Choisir une prise, c'est ranger le
/// matériau courant dans la prise à laquelle il appartient et sortir celui de
/// la prise voulue.
///
/// Trois raisons, dans cet ordre :
///
///  1. **Rien de ce qui lit une piste n'a besoin de changer.** Le planning,
///     l'export MIDI, le rendu hors ligne et le piano roll voient le matériau
///     courant et ne savent même pas que des prises existent. C'est la même
///     règle qui avait fait choisir le modèle de la RÉGION pour les clips.
///  2. **Une piste SANS prise se comporte exactement comme avant** (voir
///     `Track::takes`) : `takes` vide veut dire « aucune prise empilée », pas
///     « prise vide ». La migration des projets existants est donc vide.
///  3. **Ce qui était là avant le premier enregistrement n'est jamais
///     perdu** : il devient la prise n° 0. Une pile de prises dans laquelle le
///     matériau d'origine aurait disparu serait un piège, pas une commodité.
///
/// Ce que ce modèle coûte, et qui est assumé : éditer des notes modifie la
/// prise ACTIVE, et seulement elle. C'est le comportement attendu -- on corrige
/// la prise qu'on écoute -- mais il faut le savoir avant d'aller chercher une
/// correction dans une autre.
struct Take {
    std::string name;
    /// Les bornes de la passe sur la ligne de temps. Elles servent à nommer et
    /// à situer la prise ; le matériau, lui, est en ticks absolus comme
    /// partout ailleurs.
    Tick startTick = 0;
    Tick endTick = 0;

    /// Le matériau de la prise, dans les mêmes unités et le même format que
    /// celui d'une piste -- puisque c'est exactement ce qu'il vient d'être, ou
    /// ce qu'il redeviendra.
    std::vector<Note> notes;
    AudioSource audio;
    std::vector<Clip> clips;
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
    /// Identifiant `EffectFactory` : « reverb » pour un effet interne, ou
    /// `clap:<chemin>#<id>` / `vst3:<chemin>#<id>` pour un effet qu'on n'a pas
    /// écrit (D7.3). Le champ ne change pas de nature : c'est toujours une
    /// chaîne que la fabrique sait lire, et `core/` continue de n'y voir qu'un
    /// nom.
    std::string type;
    std::map<std::string, float> parameters; ///< nom de paramètre -> valeur en unités réelles

    /// ÉTAT NATIF D'UN EFFET TIERS (D7.3), tel que
    /// `IAudioEffect::saveNativeState()` le rend -- du texte, en pratique du
    /// base64. VIDE pour les treize effets internes, dont le son EST leur table
    /// de paramètres.
    ///
    /// Même raison que pour les instruments (voir `SynthPreset::nativeState`) :
    /// un effet tiers porte des réponses impulsionnelles chargées, des courbes
    /// dessinées, des tables apprises, que rien dans le vocabulaire sémantique
    /// ne désigne. Sans ce champ, rouvrir un morceau rendrait une
    /// réverbération à convolution muette, ou une autre pièce.
    std::string nativeState;

    /// ACTIF OU CONTOURNÉ (D15.1). Contourné, l'insert tourne encore et garde
    /// sa latence ; seule sa sortie est remplacée par le signal sec retardé
    /// d'autant (voir `BypassableEffect`). Absent du fichier quand vrai : les
    /// projets écrits avant ne changent pas d'un octet.
    bool enabled = true;
};

/// Un point d'automation. `value` est en UNITÉS RÉELLES (Hz, secondes), jamais
/// en normalisé -- la règle de tout le projet. `step` dit que le segment
/// PARTANT de ce point est un palier et non une rampe.
struct AutomationPoint {
    Tick tick = 0;
    float value = 0.0f;
    bool step = false;
    /// LA COURBURE DU SEGMENT QUI PART DE CE POINT (D17.7), de -1 à +1.
    ///
    /// Zéro est la droite, et c'est le défaut : un projet d'avant D17.7 sonne
    /// et se dessine exactement comme avant. Positif, le segment monte VITE
    /// puis s'aplatit ; négatif, il traîne puis se précipite.
    ///
    /// POURQUOI IL EN FALLAIT UNE. Un fondu de volume DROIT EN GAIN n'est pas
    /// un fondu droit à l'oreille : l'oreille entend des décibels, et une
    /// droite en gain passe la moitié de sa course dans les six derniers
    /// décibels -- elle s'entend comme une chute brutale à la fin. C'est le
    /// geste d'automation le plus courant qui soit, et il ne se dessinait pas.
    ///
    /// APRÈS `step`, et c'est la règle de ce struct comme de `Clip` : le code
    /// existant construit des `AutomationPoint` par agrégat POSITIONNEL, et un
    /// champ glissé au milieu décalerait tout ce qui suit (piège payé deux
    /// fois à D17.1).
    float curve = 0.0f;
};

/// L'AVANCEMENT COURBÉ (D17.7) : où en est un segment de courbure `curve`
/// quand on a parcouru la fraction `x` de sa durée.
///
/// ÉCRITE ICI, DANS LE MODÈLE, et appelée par l'éditeur COMME par le moteur.
/// Deux formules qui divergeraient feraient dessiner une courbe et en entendre
/// une autre -- c'est déjà l'invariant du § 6 de la feuille de route, et la
/// même raison qui a fait mettre `fadeShapeGain` ici à D17.1.
///
/// La forme est `x` élevé à la puissance `2^(-2·courbure)` : continue,
/// strictement croissante, égale à l'identité à courbure nulle, et symétrique
/// (la courbure opposée donne la fonction réciproque). Une puissance plutôt
/// qu'une Bézier parce qu'elle s'inverse et se compose sans résoudre quoi que
/// ce soit -- et que la poignée du milieu de segment suffit à la régler.
inline float automationCurveEase(float curve, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    if (curve == 0.0f) return x;
    const float c = curve < -1.0f ? -1.0f : (curve > 1.0f ? 1.0f : curve);
    return std::pow(x, std::pow(2.0f, -2.0f * c));
}

/// Une courbe d'automation, ciblant un paramètre par son identité SÉMANTIQUE
/// (« filter.1.cutoff ») : c'est ce qui permet à la courbe de survivre à un
/// changement de machine, et à la chaîne d'analyse de l'écrire sans connaître
/// le code du DAW. Rangée dans la piste pour la même raison que `TrackEffect`.
struct AutomationCurve {
    /// L'IDENTITÉ DE CE QUI EST PILOTÉ, et depuis D4.6 elle ne désigne plus
    /// seulement un réglage de machine. Les conventions, toutes préfixées pour
    /// qu'un nom ne puisse pas en désigner deux :
    ///
    ///  - `filter.1.cutoff`, `envelope.1.attack`... : un réglage de la MACHINE
    ///    de la piste. C'est le cas historique, et il reste écrit tel quel --
    ///    les projets existants se relisent sans rien remarquer.
    ///  - `mix.volume` : le fader de la piste, en gain linéaire.
    ///  - `mix.pan` : son panoramique, de -1 à +1.
    ///  - `mix.send.1` .. `mix.send.8` : son niveau vers le bus de départ.
    ///  - `insert.1.<identité>` : un réglage du PREMIER insert de la piste,
    ///    nommé par sa propre identité sémantique -- par exemple
    ///    `insert.1.effect.reverb.mix`. Le numéro est la position dans la
    ///    chaîne, parce que rien d'autre ne distingue deux réverbérations sur
    ///    la même piste.
    ///  - `master.<nom>` : un réglage de la tranche master. La courbe est alors
    ///    rangée dans la piste 0 faute d'endroit qui soit à personne, et le
    ///    préfixe suffit à dire qu'elle ne la concerne pas.
    ///
    /// POURQUOI DES PRÉFIXES ET NON UN CHAMP « genre ». Un champ de plus
    /// obligerait le format, le lecteur, l'écrivain et la chaîne d'analyse à
    /// s'accorder sur une énumération ; un préfixe se lit, s'écrit et se
    /// diagnostique à l'œil dans le fichier. C'est la même raison qui avait fait
    /// choisir des identités sémantiques plutôt que des numéros.
    std::string parameter;
    std::vector<AutomationPoint> points;
};

/// LE MODE D'ÉCRITURE DE L'AUTOMATION (D16.8) — le W/R par tranche de
/// Cubase, l'armement d'automation de Live.
///
/// `Off` : la courbe ne s'obtient qu'au dessin, comme depuis D5.4 ; c'est le
/// défaut, et une piste qui n'a jamais rien armé s'écrit dans le fichier
/// exactement comme avant que ce réglage existe.
///
/// `Touch` : la main sur un fader écrit TANT QU'ON LE TIENT, et la courbe
/// reprend ce qu'elle disait dès qu'on lâche. C'est le mode des retouches --
/// corriger deux mesures sans effacer le reste.
///
/// `Latch` : la main sur un fader écrit à partir du moment où on l'a touché
/// et JUSQU'À L'ARRÊT du transport, même après avoir lâché. C'est le mode des
/// premiers jets -- poser une courbe entière en un passage.
///
/// Les deux se distinguent SEULEMENT par ce qui se passe au relâchement, et
/// c'est pourquoi ils ne sont pas deux mécanismes : le même enregistrement
/// tourne, et seul l'instant où il s'arrête change.
enum class AutomationMode : uint8_t { Off = 0, Touch = 1, Latch = 2 };

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
    /// Une piste MIDI joue des notes, une piste audio joue un fichier, et une
    /// piste de GROUPE ne joue rien du tout : elle reçoit d'autres pistes
    /// (D4.2). C'est ce qui permet de traiter huit micros de batterie comme un
    /// seul instrument -- un compresseur sur le groupe, un fader pour toute la
    /// batterie -- au lieu de refaire huit fois le même geste et d'espérer
    /// qu'ils restent d'accord.
    ///
    /// UN GROUPE EST UNE PISTE, et ce n'est pas un raccourci : il a un nom, un
    /// volume, un panoramique, un muet, un solo, une chaîne d'inserts, des
    /// départs et une courbe d'automation, c'est-à-dire exactement ce qu'a une
    /// piste. En faire un troisième objet aurait obligé le mixeur, l'éditeur
    /// d'effets, l'automation et le format à connaître deux choses là où une
    /// seule suffit.
    enum class Kind { Midi, Audio, Group };
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
    /// La piste ARMÉE reçoit le clavier MIDI, à l'écoute comme à
    /// l'enregistrement (D3.3). C'est un état de SESSION et non de morceau :
    /// il n'est délibérément pas écrit dans `project.json`. Rouvrir un projet
    /// avec une piste silencieusement armée ferait écrire la prise suivante à
    /// un endroit qu'on n'a pas désigné, et c'est le genre de surprise qu'un
    /// enregistrement ne pardonne pas.
    ///
    /// Un champ `monitoring` l'accompagnait, écrit par personne et lu par
    /// personne. Il est parti : l'armement dit déjà « c'est cette piste qui
    /// écoute mon clavier », et deux champs pour une seule idée finissent
    /// toujours par se contredire.
    bool armed = false;
    float volume = 1.0f;  // gain linéaire, 1.0 = 0 dB
    float pan = 0.0f;      // -1 (gauche) .. +1 (droite)
    /// Niveaux d'envoi vers les bus auxiliaires (sends). 0 = pas d'envoi.
    /// Portés par Track (donnée de mixage) comme volume/pan.
    ///
    /// UN VECTEUR ET NON PLUS UN TABLEAU DE DEUX (D4.2). Le nombre de départs
    /// était une constante du code : deux, figés sur une réverbération et un
    /// delay. Une piste peut en avoir moins que le projet n'en déclare -- un
    /// vecteur court vaut « pas d'envoi » sur les bus suivants, ce qui rend la
    /// migration des projets existants vide et évite d'avoir à retailler
    /// chaque piste quand on ajoute un bus.
    std::vector<float> sendLevels;

    /// Niveau d'envoi vers le bus `index`, ou zéro si la piste n'en déclare
    /// pas autant. À employer partout plutôt qu'un accès direct : c'est ce qui
    /// rend le vecteur court inoffensif.
    float sendLevel(size_t index) const {
        return index < sendLevels.size() ? sendLevels[index] : 0.0f;
    }
    /// Règle un niveau d'envoi, en allongeant le vecteur si besoin.
    void setSendLevel(size_t index, float value) {
        if (sendLevels.size() <= index) sendLevels.resize(index + 1, 0.0f);
        sendLevels[index] = value;
    }

    /// OÙ VA LA SORTIE DE CETTE PISTE : l'index de la piste de GROUPE qui la
    /// reçoit, ou -1 pour aller directement au master (D4.2).
    ///
    /// UN INDEX, ET LE PIÈGE QU'IL PORTE. Supprimer une piste décale toutes les
    /// suivantes, et un routage qui visait la piste 5 viserait la 4 : le
    /// mixage partirait ailleurs sans qu'aucun réglage n'ait bougé. C'est
    /// exactement le défaut qui avait fait ranger les chaînes d'effets DANS la
    /// piste (voir `TrackEffect`). Ici on ne peut pas l'éviter -- c'est une
    /// référence d'une piste vers une autre --, alors on la répare : celui qui
    /// supprime une piste renumérote les routages, et un test le vérifie.
    ///
    /// UN GROUPE NE VA JAMAIS DANS UN GROUPE. Les groupes imbriqués
    /// demanderaient un ordre topologique et une détection de cycle pour un
    /// besoin que rien n'a exprimé ; un seul niveau couvre l'usage réel
    /// (batterie, claviers, voix). Le moteur ignore donc le routage d'une piste
    /// de groupe et l'envoie au master.
    int outputGroup = -1;

    /// Identifiant du plugin instrument assigné (vide = aucun). Résolu par
    /// le Synth Rack en Phase 2 via ISynthPlugin / PluginRegistry.
    std::string instrumentId;
    std::string presetId;

    /// Le fichier que joue une piste audio. Vide sur une piste MIDI.
    AudioSource audio;

    /// LA PISTE EST GELÉE (D5.5) : son instrument et ses inserts ne tournent
    /// plus, et c'est `frozenAudio` qui est joué à leur place.
    ///
    /// CE QUI RESTE VIVANT, et c'est toute la différence entre geler et
    /// reporter : le volume, le panoramique, les départs, le muet et le solo.
    /// Une piste gelée se mixe exactement comme avant -- on a seulement cessé
    /// de recalculer ce qui ne changeait plus. Reporter (*bounce*), lui,
    /// remplace le matériau : c'est une décision, geler n'en est pas une.
    ///
    /// LE MATÉRIAU N'EST PAS DÉTRUIT : les notes, l'instrument et les inserts
    /// restent dans la piste et reviennent au dégel. Un gel qui effacerait ce
    /// qu'il remplace serait un report qui n'ose pas dire son nom.
    bool frozen = false;
    AudioSource frozenAudio;

    /// PISTE VERROUILLÉE (D16.5) : le cadenas par piste de Cubase.
    ///
    /// Elle se joue, s'entend, se mixe et se règle exactement comme avant --
    /// verrouiller n'est PAS taire. Ce qui est refusé, c'est le MONTAGE :
    /// déplacer, redimensionner, étirer, couper, joindre, créer, changer de
    /// piste, et l'édition de ses notes. Une piste de référence finie partait
    /// d'un coup de flèche depuis que les flèches déplacent les clips
    /// (D15.2), et rien ne la protégeait.
    ///
    /// LE REFUS EST DANS `ClipEdit` ET DANS L'ÉDITEUR DE NOTES, PAS DANS LA
    /// VUE : quarante gestes de deux composants toucheraient sinon quarante
    /// fois au même `if`, et le quarante-et-unième l'oublierait.
    ///
    /// Rien à voir avec `frozen`, qui est une affaire de CPU : une piste
    /// gelée s'édite encore (le gel se défait), une piste verrouillée
    /// calcule encore.
    bool locked = false;

    /// PISTE MASQUÉE (D17.4) : la Visibility de Cubase, le repliement de Live.
    ///
    /// Une reconstruction à soixante pistes se parcourt en entier ou pas du
    /// tout ; masquer est ce qui permet de travailler sur cinq d'entre elles.
    ///
    /// MASQUER N'EST PAS COUPER, et c'est la seule chose à retenir : une piste
    /// masquée sonne, se mixe et s'exporte EXACTEMENT comme avant (un test le
    /// vérifie au planificateur). Aucun calcul ne lit ce drapeau -- il est lu
    /// par les trois vues qui dessinent des pistes, et par rien d'autre. Un
    /// « masquer » qui ferait taire serait la pire des pannes muettes : on
    /// chercherait pendant une heure pourquoi la basse a disparu du mixage.
    bool hidden = false;

    /// LA TRANSPOSITION DE PISTE (D17.5), en demi-tons — le Transpose de
    /// l'inspecteur de Cubase.
    ///
    /// APPLIQUÉE À LA LECTURE, PAS AU MATÉRIAU, et c'est tout ce qui la
    /// distingue d'un « transposer la sélection » du piano roll. Le matériau
    /// ne bouge pas : on l'annule en remettant zéro, pas en défaisant un
    /// historique, on l'essaie à l'oreille en tournant un chiffre, et deux
    /// transpositions successives ne s'accumulent pas en erreurs d'arrondi
    /// parce qu'il n'y a aucun arrondi.
    ///
    /// LE PRIX, ASSUMÉ : le piano roll montre le matériau, donc les notes
    /// écrites, et non les notes entendues. C'est le comportement de Cubase et
    /// il se comprend dès qu'on sait que le réglage existe -- la console
    /// l'affiche en clair à côté du fader.
    ///
    /// LES NOTES QUI SORTENT DE 0..127 SONT ÉCARTÉES, jamais repliées à
    /// l'octave : replier ferait sonner une note à une hauteur que personne
    /// n'a demandée, ce qui est pire que de ne pas la jouer. Elles sont
    /// COMPTÉES (`transposeDroppedNotes`), et l'application le dit.
    int transposeSemitones = 0;

    /// LE GROUPE D'ÉDITION (D18.3) — les Edit Groups de Cubase. 0 = aucun.
    ///
    /// Deux pistes du même groupe se COUPENT, se déplacent et se joignent
    /// ensemble, au même tick. Couper une reconstruction multipiste à la
    /// mesure 33 demandait douze gestes, et un tick d'écart entre deux micros
    /// d'une même batterie casse leur phase — c'est-à-dire le son.
    ///
    /// RIEN À VOIR AVEC `outputGroup`, qui est un bus de MIXAGE : celui-ci ne
    /// touche à aucun signal, il ne fait que lier des gestes d'édition. Deux
    /// pistes peuvent être dans le même groupe d'édition et sortir sur des bus
    /// différents, et l'inverse.
    int editGroup = 0;

    /// LE DÉCALAGE DE PISTE (D16.7), en MILLISECONDES, négatif pour sonner
    /// plus tôt. Le Delay de l'inspecteur de Cubase, le Track Delay de Live.
    ///
    /// EN MILLISECONDES ET NON EN TICKS, et c'est tout le point : ce réglage
    /// sert à corriger le temps de réaction d'un joueur, la latence d'un
    /// appareil, ou à poser une caisse claire trois millisecondes en retard
    /// pour qu'elle « traîne ». Aucune de ces trois choses ne suit le tempo,
    /// et l'exprimer en ticks les ferait toutes changer au premier
    /// ritardando. Même raison que `Clip::sourceStartSeconds`.
    ///
    /// IL NE TOUCHE PAS À LA COMPENSATION DE LATENCE, qui corrige ce que les
    /// inserts retardent : celle-là remet les pistes ENSEMBLE, celui-ci les
    /// décale exprès. La latence déclarée du graphe ne change pas d'un
    /// échantillon (testé) -- sans quoi régler un décalage déplacerait tout
    /// le reste du morceau.
    double delayMs = 0.0;

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
    /// D16.8 : `Off` par défaut, et absent du fichier dans ce cas.
    AutomationMode automationMode = AutomationMode::Off;

    /// Les prises empilées de la piste (D3.5).
    ///
    /// **VIDE SIGNIFIE « AUCUNE PRISE EMPILÉE »**, c'est-à-dire que la piste
    /// n'a qu'un matériau et se comporte exactement comme avant que les prises
    /// existent. Ce n'est pas un cas particulier : c'est le sens littéral du
    /// mot. Voir `Take` pour le modèle retenu et celui qui a été écarté.
    std::vector<Take> takes;

    /// HAUTEUR DE LA PISTE dans la vue d'arrangement, en pixels, et si elle est
    /// PLIÉE (D5.3).
    ///
    /// SAUVEGARDÉES AVEC LE MORCEAU, et c'est un choix. Ce ne sont pas des
    /// propriétés du son : ce sont des propriétés de la façon dont on REGARDE
    /// ce morceau-là -- on déplie la basse pendant qu'on travaille dessus, on
    /// replie les huit micros de batterie qu'on a fini de régler. Rouvrir un
    /// projet en ayant perdu cette disposition obligerait à la refaire à chaque
    /// fois, et sur un morceau à seize pistes ce n'est pas un détail.
    ///
    /// La hauteur est celle de la piste DÉPLIÉE : plier n'écrase pas le
    /// réglage, il le met de côté. Sans quoi déplier rendrait une hauteur
    /// standard, et le travail de mise en page serait perdu au premier pli.
    int arrangementHeight = 56;
    bool folded = false;

    /// La prise dont le matériau est ACTUELLEMENT dans la piste, ou -1 si le
    /// matériau courant n'appartient à aucune prise (le cas de toute piste qui
    /// n'a jamais servi à un enregistrement empilé).
    ///
    /// L'invariant qui compte : quand `activeTake` désigne une prise, le
    /// contenu de `takes[activeTake]` est PÉRIMÉ -- la vérité est dans
    /// `notes`/`audio`/`clips`, et elle y sera rangée au prochain changement de
    /// prise. Dupliquer le matériau actif dans sa prise obligerait à tenir deux
    /// copies d'accord à chaque note déplacée.
    int activeTake = -1;

    /// Trie toutes les lanes par tick croissant. À appeler après toute
    /// édition manuelle en dehors des méthodes utilitaires ci-dessous.
    void sortEvents();

    /// Ajoute une note et lui assigne un id unique via le compteur fourni.
    Note& addNote(Tick start, Tick end, uint8_t number, uint8_t velocity,
                   uint8_t channelOverride, uint64_t& idCounter);
};

/// Empile une prise sur la piste et la rend active.
///
/// SI LA PISTE AVAIT DÉJÀ UN MATÉRIAU ET AUCUNE PRISE, ce matériau devient la
/// prise n° 0 sous le nom donné par `nomDeLOrigine`. C'est la seule façon de
/// ne rien perdre : sans cela, le premier enregistrement empilé effacerait ce
/// qui était là -- typiquement une partie reconstruite, c'est-à-dire ce qu'on
/// avait de plus précieux.
void pushTake(Track& track, Take take, const std::string& nomDeLOrigine = "Origine");

/// Rend active la prise `index`, en rangeant d'abord le matériau courant dans
/// la prise à laquelle il appartient. Sans effet si l'index est hors bornes ou
/// désigne déjà la prise active.
void selectTake(Track& track, int index);

/// ASSEMBLER LES PRISES (D18.2) — les lanes de Cubase, les take lanes de Live.
///
/// `Track::takes` conserve chaque passe depuis D3.5, et l'on ne pouvait que
/// CHOISIR la meilleure : impossible de prendre le couplet de la deuxième et
/// le refrain de la quatrième. Or c'est le geste pour lequel on enregistre
/// plusieurs passes.
///
/// UN TRONÇON dit « de tel tick à tel tick, prends telle prise ». La prise
/// composite est la suite de ses tronçons, et rien d'autre : elle ne se
/// recopie pas à la main, elle se RECALCULE — c'est ce qui permet de corriger
/// une frontière sans avoir à tout refaire.
struct CompSegment {
    int takeIndex = 0;
    Tick fromTick = 0;
    Tick toTick = 0;      ///< exclu
};

/// Les notes que ces tronçons décrivent, prises dans leurs prises
/// respectives, avec des identifiants neufs.
///
/// LE PIÈGE QUE CETTE FONCTION DOIT CONNAÎTRE, et qui est écrit plus haut :
/// quand `activeTake` désigne une prise, le contenu de `takes[activeTake]` est
/// PÉRIMÉ — la vérité est dans `notes`. Lire aveuglément `takes[i].notes`
/// rendrait donc l'état d'AVANT pour la prise qu'on est en train d'écouter,
/// c'est-à-dire précisément celle qu'on vient de juger bonne.
///
/// Une note est prise si son DÉBUT tombe dans le tronçon ; elle est coupée à
/// la fin de celui-ci, comme au bord d'une section (D18.4) et d'un clip.
std::vector<Note> buildCompositeTake(const Track& track,
                                      const std::vector<CompSegment>& segments,
                                      uint64_t& idCounter);

/// Pose la composite comme matériau courant.
///
/// Le matériau courant est d'abord RANGÉ dans sa prise, sinon la passe qu'on
/// écoutait serait perdue en la choisissant. Ensuite `activeTake` devient -1 :
/// une composite n'appartient à aucune prise, et prétendre le contraire ferait
/// écraser une passe au prochain changement.
///
/// Rend faux si les tronçons ne décrivent rien : la piste n'est alors pas
/// touchée.
bool applyCompositeTake(Track& track, const std::vector<CompSegment>& segments,
                         uint64_t& idCounter);

} // namespace vsm::sequencer
