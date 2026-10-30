#!/usr/bin/env bash
# D608 — RELIER UN FICHIER MANQUANT (le *Find Missing Files ▸ Locate* de Cubase), mesuré dans l'application : la ligne
# MANQUANT de la réserve choisie, « Relier... » pressé, le fichier désigné (par `VSM_FICHIER`, qui saute le seul
# sélecteur, D102) ; ce que la réserve lit ensuite, ce que le dossier contient, et si la piste SONNE.
#
# LE JEU : celui de D607 — « Voix » joue `audio/voix.wav`, garde la prise `audio/prise2.wav` ; « Basse » désigne
# `audio/absent.wav`, absent ; `audio/orphelin.wav` traîne. Hors du dossier du projet : `basse-retrouvee.wav` (2 s,
# stéréo, un sinus à −10 dBFS de crête), et `texte.wav`, un texte qui n'a d'audio que le nom.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D608, attendu (b)) :
#   relié → la ligne `audio/basse-retrouvee.wav` « joué », 0 manquant, le fichier copié dans `audio/` ; en lecture, le
#   vumètre de « Basse » au-dessus de −40 dBFS, quand le témoin non relié lit −inf ; Ctrl+Z → `audio/absent.wav`
#   MANQUANT revient ; un fichier illisible → refusé et dit, rien de relié ; « Relier... » grisé sur une ligne qui ne
#   manque pas ; le témoin (D607) : le verbe inconnu ; l'anglais.
#
#   tools/relier-fichier.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D607/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-relier.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
racine = sys.argv[1]
b = racine + "/projet"
os.makedirs(f"{b}/midi", exist_ok=True)
os.makedirs(f"{b}/audio", exist_ok=True)
os.makedirs(f"{racine}/ailleurs", exist_ok=True)
def mtrk(corps):
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
tempo = mtrk(b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00")
vide = mtrk(b"\x00\xff\x2f\x00")
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + tempo + vide)
open(f"{b}/midi/prises.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + vide)
sr = 48000
def son(chemin, secondes, canaux=1, crete=3000):
    with wave.open(chemin, "wb") as w:
        w.setnchannels(canaux); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<" + "h" * canaux, *([int(crete * math.sin(2 * math.pi * 220 * n / sr))] * canaux))
                               for n in range(int(secondes * sr))))
son(f"{b}/audio/voix.wav", 1.0); son(f"{b}/audio/prise2.wav", 0.5); son(f"{b}/audio/orphelin.wav", 0.25)
son(f"{racine}/ailleurs/basse-retrouvee.wav", 2.0, canaux=2, crete=10362)   # −10 dBFS de crête
open(f"{racine}/ailleurs/texte.wav", "w").write("ceci n'est pas un fichier audio\n")
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
def clip(debut, longueur):
    return {"start": debut, "length": longueur, "sourceStart": debut, "sourceLength": longueur, "color": "#FF6B9BFF"}
def source(nom, frames, canaux=1):
    return {"file": f"audio/{nom}", "sampleRate": sr, "frames": frames, "channels": canaux}
json.dump({"format": "vsm-project", "version": 1, "title": "relier", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voix", "mix": mix,
                       "audio": source("voix.wav", sr), "clips": [clip(0, 480), clip(960, 480)],
                       "takes": [{"name": "Prise 1", "startTick": 0, "endTick": 1920, "midiTrack": -1,
                                  "audio": source("voix.wav", sr)},
                                 {"name": "Prise 2", "startTick": 0, "endTick": 1920, "midiTrack": -1,
                                  "audio": source("prise2.wav", sr // 2)}],
                       "activeTake": 0},
                      {"kind": "audio", "channel": 1, "color": "#FF6B9BFF", "effects": [], "name": "Basse", "mix": mix,
                       "audio": source("absent.wav", 2 * sr, 2)}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3400 VSM_GESTE_APRES="$gestes" VSM_VUMETRES=1 VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*|illisible) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# LA FENÊTRE DÉGÉNÉRÉE (D607 : 128 × 128, la fenêtre redimensionnée APRÈS avoir été montrée, écran éteint et session
# verrouillée — non reproduit à la demande) : son bouton n'a plus de surface, et le clic est refusé. La course est
# REJOUÉE une fois, et c'est DIT — la règle des boîtes absentes de D72 : relancer avant de conclure, jamais en silence.
rejouees=0
course_sure() {
    course "$@"
    if grep -q "fenêtre 1[0-9][0-9]x1[0-9][0-9]$" "$brouillon/$2.txt"; then
        echo "       course « $2 » REJOUÉE : $(grep -o "fenêtre [0-9]*x[0-9]*" "$brouillon/$2.txt" | head -1) au premier essai"
        rejouees=$((rejouees + 1))
        rm -rf "$brouillon/p-$2"
        course "$@"
    fi
}
ligne() { grep "^VSM_RESERVE : $2 ; " "$brouillon/$1.txt" | tail -1; }
etat_de() { echo "$1" | awk -F' ; ' '{print $NF}'; }
vumetre_basse() { grep "^VSM_VUMETRE : tranche 1 : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_VUMETRE : tranche 1 : \([-0-9.inf]*\) dBFS.*/\1/p'; }
au_dessus() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v not in ('', '-inf') and float(v) > float(sys.argv[2]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

F="Réserve des fichiers"
# LA BOÎTE D'OUVERTURE D'ABORD, comme l'utilisateur — CORRIGÉ APRÈS LE PREMIER ESSAI : le fichier absent de « Basse »
# fait ouvrir le projet sur une boîte MODALE « Audio non chargé », qui bloquait TOUT clic (« Relier... » comme Play) ;
# les clics étaient journalisés « cliqués » et ne faisaient rien. Elle se ferme par son bouton OK.
OK="900:cliquer:fenetre:Audio non chargé:OK"
OK_EN="900:cliquer:fenetre:Audio not loaded:OK"
CHOISIR="$OK;1200:reserve-choisir:audio/absent.wav;1400:cliquer:fenetre:$F:Relier..."
echo "=== D608 : relier un fichier manquant ==="
# LE TÉMOIN NON RELIÉ D'ABORD : le premier lancement d'une rafale, session verrouillée, peut rétrécir les fenêtres
# flottantes (D607) — il n'a, lui, aucun bouton à presser.
course_sure "$BIN" nonrelie "$OK;1800:cliquer:Play;2000:relever-reserve;3000:relever-ordre"
course_sure "$BIN" relie "$CHOISIR;1700:relever-reserve;1800:cliquer:Play;3000:relever-ordre" VSM_FICHIER="$brouillon/ailleurs/basse-retrouvee.wav"
course_sure "$BIN" annule "$CHOISIR;1800:touche:ctrl + Z;2200:relever-reserve" VSM_FICHIER="$brouillon/ailleurs/basse-retrouvee.wav"
course_sure "$BIN" illisible "$CHOISIR;1900:relever-reserve" VSM_FICHIER="$brouillon/ailleurs/texte.wav"
course_sure "$BIN" grise "$OK;1200:reserve-choisir:audio/voix.wav"
course_sure "$BIN" en "$OK_EN;1200:reserve-choisir:audio/absent.wav;1400:cliquer:fenetre:Pool:Relink...;1700:relever-reserve" \
    VSM_LANGUE=en VSM_FICHIER="$brouillon/ailleurs/basse-retrouvee.wav"

C="$(grep "^VSM_RESERVE : choisie audio/absent.wav" "$brouillon/relie.txt" | head -1)"
R="$(grep "^VSM_RESERVE : reli" "$brouillon/relie.txt" | head -1)"
L="$(ligne relie audio/basse-retrouvee.wav)"; T="$(ligne relie total)"
echo "       choisie   : $C"
echo "       relié     : $R"
echo "       ligne     : $L"
echo "       total     : $T"
echo "       vumètre « Basse » : relié $(vumetre_basse relie) dBFS ; non relié $(vumetre_basse nonrelie) dBFS"
verdict "(b) la ligne MANQUANT choisie : « Relier... » actif" "$(echo "$C" | grep -q "Relier actif" && echo 1 || echo 0)"
verdict "(b) relié : absent.wav → audio/basse-retrouvee.wav, 1 désignation, copié" \
    "$(echo "$R" | grep -q "relié audio/absent.wav → audio/basse-retrouvee.wav (1 désignation(s), copié)" && echo 1 || echo 0)"
verdict "(b) la réserve : basse-retrouvee.wav « joué », 2.00 s stéréo, désigné par « Basse »" \
    "$([ "$(etat_de "$L")" = "joué" ] && echo "$L" | grep -q " ; 2.00 s ; 48 kHz stéréo ; .* ; piste « Basse »" && echo 1 || echo 0)"
verdict "(b) le total : 0 manquant(s)" "$(echo "$T" | grep -q " · 0 manquant(s) · " && echo 1 || echo 0)"
verdict "(b) le fichier copié dans audio/ du projet" "$([ -s "$brouillon/p-relie/audio/basse-retrouvee.wav" ] && echo 1 || echo 0)"
verdict "(b) en lecture, « Basse » sonne : au-dessus de −40 dBFS" "$(au_dessus "$(vumetre_basse relie)" -40 && echo 1 || echo 0)"
# LE TÉMOIN POSITIF DE LA LECTURE : « −inf » ne vaut que si le transport jouait (la leçon de D145 — une valeur qui ne
# bouge pas ne prouve rien sans le témoin qui montre qu'elle aurait pu bouger).
verdict "(b) témoin non relié : en lecture, « Basse » muette (−inf), sa ligne MANQUANT" \
    "$(grep -q "^VSM_ORDRE : relevé .* ; en lecture" "$brouillon/nonrelie.txt" && [ "$(vumetre_basse nonrelie)" = "-inf" ] \
       && [ "$(etat_de "$(ligne nonrelie audio/absent.wav)")" = "MANQUANT" ] && echo 1 || echo 0)"
A="$(ligne annule audio/absent.wav)"; AB="$(ligne annule audio/basse-retrouvee.wav)"
echo "       Ctrl+Z    : $A"
echo "       Ctrl+Z    : $AB"
verdict "(b) Ctrl+Z : absent.wav MANQUANT revient ; la copie « gardée pour l'historique »" \
    "$([ "$(etat_de "$A")" = "MANQUANT" ] && [ "$(etat_de "$AB")" = "gardé pour l'historique" ] && echo 1 || echo 0)"
I="$(grep "^VSM_RESERVE : relier REFUS" "$brouillon/illisible.txt" | head -1)"
echo "       illisible : $I"
verdict "(b) un fichier illisible : refusé et dit, absent.wav toujours MANQUANT" \
    "$(echo "$I" | grep -q "texte.wav illisible" && [ "$(etat_de "$(ligne illisible audio/absent.wav)")" = "MANQUANT" ] && echo 1 || echo 0)"
G="$(grep "^VSM_RESERVE : choisie" "$brouillon/grise.txt" | head -1)"
verdict "(b) une ligne qui ne manque pas : « Relier... » grisé ($G)" "$(echo "$G" | grep -q "Relier grisé" && echo 1 || echo 0)"
E="$(ligne en audio/basse-retrouvee.wav)"
echo "       anglais   : $E"
verdict "(b) en anglais : « Relink... » pressé dans « Pool », la ligne « playing »" "$([ "$(etat_de "$E")" = "playing" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-relie "$CHOISIR;1700:relever-reserve" VSM_FICHIER="$brouillon/ailleurs/basse-retrouvee.wav"
    ti="$(grep -c "geste inconnu" "$brouillon/t-relie.txt")"; tr_="$(grep -c "^VSM_RESERVE : reli" "$brouillon/t-relie.txt")"
    echo "       témoin : $tr_ liaison(s) ; $ti « geste inconnu »"
    verdict "(b) témoin (D607) : aucune liaison, le verbe inconnu" "$([ "$tr_" = 0 ] && [ "$ti" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "RELIER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RELIER : le fichier manquant se relie, la piste sonne, et Ctrl+Z le défait"
