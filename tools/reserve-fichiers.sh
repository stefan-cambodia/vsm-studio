#!/usr/bin/env bash
# D607 — LA RÉSERVE DES FICHIERS (le *Pool* de Cubase), mesurée dans l'application : ce que la fenêtre lit d'un dossier
# de projet à quatre fichiers et un absent, ligne par ligne, et son total ; l'anglais ; le témoin.
#
# LE JEU : un projet enregistré, deux pistes audio — « Voix » joue `audio/voix.wav` (1 s, deux clips) et garde deux
# prises, « Prise 1 » sur le même fichier (active) et « Prise 2 » sur `audio/prise2.wav` (0,5 s) ; « Basse » désigne
# `audio/absent.wav`, qui n'existe pas ; `audio/orphelin.wav` (0,25 s) est dans le dossier et rien ne le désigne.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D607, attendu (b)) :
#   quatre lignes — voix.wav « joué », prise2.wav « en réserve », absent.wav « MANQUANT », orphelin.wav « inutilisé » ;
#   le total « 4 fichier(s) · … · 1 manquant(s) · 1 inutilisé(s) » ; le témoin (D606) : l'entrée inconnue ; l'anglais ;
#   la photo.
#
#   tools/reserve-fichiers.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D606/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-reserve.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1] + "/projet"
os.makedirs(f"{b}/midi", exist_ok=True)
os.makedirs(f"{b}/audio", exist_ok=True)
# UNE PISTE MIDI PAR PISTE DU PROJET, même vide — CORRIGÉ APRÈS LE PREMIER ESSAI : avec une seule, l'application a dit
# « Le projet décrit 2 piste(s) mais le MIDI en contient 1 » et n'a configuré que « Voix » ; « Basse » et son fichier
# absent n'existaient plus. Et un `prises.mid` vide, pour les prises (sans lui : « prises introuvables »).
def mtrk(corps):
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
tempo = mtrk(b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00")
vide = mtrk(b"\x00\xff\x2f\x00")
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + tempo + vide)
open(f"{b}/midi/prises.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + vide)
sr = 48000
def son(nom, secondes):
    with wave.open(f"{b}/audio/{nom}", "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<h", int(3000 * math.sin(2 * math.pi * 440 * n / sr)))
                               for n in range(int(secondes * sr))))
son("voix.wav", 1.0); son("prise2.wav", 0.5); son("orphelin.wav", 0.25)
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
def clip(debut, longueur):
    return {"start": debut, "length": longueur, "sourceStart": debut, "sourceLength": longueur, "color": "#FF6B9BFF"}
def source(nom, frames, canaux=1):
    return {"file": f"audio/{nom}", "sampleRate": sr, "frames": frames, "channels": canaux}
json.dump({"format": "vsm-project", "version": 1, "title": "reserve", "midi": {"file": "midi/arrangement.mid"},
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
course() {   # $1 = binaire ; $2 = nom ; le reste : variables. Chaque course part d'une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" n
    shift 2
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=2200 VSM_GESTE_APRES="1500:relever-reserve" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
ligne() { grep "^VSM_RESERVE : $2 ; " "$brouillon/$1.txt" | head -1; }
etat_de() { echo "$1" | awk -F' ; ' '{print $NF}'; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D607 : la réserve des fichiers ==="
course "$BIN" fr
course "$BIN" en VSM_LANGUE=en
for f in audio/voix.wav audio/prise2.wav audio/absent.wav audio/orphelin.wav total; do echo "       $(ligne fr "$f")"; done
n="$(grep -c "^VSM_RESERVE : audio/" "$brouillon/fr.txt")"
verdict "(b) quatre lignes de fichier (lues : $n)" "$([ "$n" = 4 ] && echo 1 || echo 0)"
verdict "(b) voix.wav : joué, désigné par la piste et ses deux clips" \
    "$(L="$(ligne fr audio/voix.wav)"; [ "$(etat_de "$L")" = "joué" ] && echo "$L" | grep -q "piste « Voix » (2 clip(s))" && echo 1 || echo 0)"
verdict "(b) prise2.wav : en réserve, désigné par « Prise 2 »" \
    "$(L="$(ligne fr audio/prise2.wav)"; [ "$(etat_de "$L")" = "en réserve" ] && echo "$L" | grep -q "prise « Prise 2 » de « Voix »" && echo 1 || echo 0)"
verdict "(b) absent.wav : MANQUANT, sa taille « — »" \
    "$(L="$(ligne fr audio/absent.wav)"; [ "$(etat_de "$L")" = "MANQUANT" ] && echo "$L" | grep -q " ; — ; piste « Basse »" && echo 1 || echo 0)"
verdict "(b) orphelin.wav : inutilisé, désigné par rien, sa durée lue au fichier (0.25 s)" \
    "$(L="$(ligne fr audio/orphelin.wav)"; [ "$(etat_de "$L")" = "inutilisé" ] && echo "$L" | grep -q " ; 0.25 s ; 48 kHz mono ; " && echo 1 || echo 0)"
verdict "(b) le total : 4 fichier(s), 1 manquant(s), 1 inutilisé(s)" \
    "$(ligne fr total | grep -q "4 fichier(s) · .* · 1 manquant(s) · 1 inutilisé(s)" && echo 1 || echo 0)"
E="$(ligne en audio/absent.wav)"; ET="$(ligne en total)"
echo "       en anglais : $E"
echo "       en anglais : $ET"
verdict "(b) en anglais : « MISSING », « track “Basse” », le total en anglais" \
    "$([ "$(etat_de "$E")" = "MISSING" ] && echo "$E" | grep -q "track “Basse”" && echo "$ET" | grep -q "4 file(s) · .* · 1 missing · 1 unused" && echo 1 || echo 0)"
PHOTO="$(ls "$brouillon"/fr-*-R*serve*.png 2>/dev/null | head -1)"
verdict "(b) la fenêtre photographiée ($( [ -n "$PHOTO" ] && basename "$PHOTO"))" "$([ -n "$PHOTO" ] && [ -s "$PHOTO" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-fr
    tl="$(grep -c "^VSM_RESERVE" "$brouillon/t-fr.txt")"; ti="$(grep -c "geste inconnu" "$brouillon/t-fr.txt")"
    echo "       témoin : $tl relevé(s) de réserve ; $ti « geste inconnu »"
    verdict "(b) témoin (D606) : aucun relevé, le verbe inconnu" "$([ "$tl" = 0 ] && [ "$ti" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "RESERVE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RESERVE : la fenêtre lit ce que le projet emploie, ce qui manque et ce qui traîne"
