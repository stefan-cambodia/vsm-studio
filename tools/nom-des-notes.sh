#!/usr/bin/env bash
# D586.3 — LE NOM DES NOTES (Affichage ▸ Nom des notes ; le *Note Name Style* de Cubase), MESURÉ PAR LES NOMS QUE LE
# CLAVIER DU PIANO ROLL ÉCRIT ET PAR LES LIGNES DU MONITEUR MIDI, DANS LES TROIS RÉGLAGES.
#
# LES RÈGLES GARDÉES. Une piste MIDI, une note 60, la vue du piano roll. `relever-pianoroll` dit les noms que le clavier
# écrit pour 60, 61 et 69, par la fonction même de la peinture (`noms=`) ; le moniteur MIDI ouvert, une note 69 entrée au
# point d'entrée du matériel (`note-entrante:`), `relever-moniteur-midi` dit sa ligne telle qu'elle est peinte.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D586) :
#   (1) d'origine : 60 → C4, 61 → C#4, 69 → A4 ; le moniteur « A4 (69) » ;
#   (2) *Lettres (do central C3)* : C3, C#3, A3 ; « A3 (69) » ;
#   (3) *Solfège (do central Do3)* : Do3, Do#3, La3 ; « La3 (69) » ;
#   (4) relancé dans le même HOME : *Solfège* coché, et le clavier écrit Do3 ;
#   (5) en anglais : *Note names*, *Letters (middle C = C4)* ; le solfège reste « Do3 » (un nom de note, pas une phrase).
#
#   tools/nom-des-notes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-nom-notes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + b"\x00\x90\x3c\x64\x83\x60\x80\x3c\x00" + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "nom-des-notes", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
noms() { grep -h "^VSM_PIANOROLL_RANG : " "$brouillon/$1.txt" | tail -1 | sed -n 's/.* noms=\([^ ]*\).*/\1/p'; }
moniteur() { grep -h "^VSM_MONITEUR_MIDI_LIGNE : " "$brouillon/$1.txt" | grep -o "[A-Za-zé#]*-\{0,1\}[0-9]* (69)" | head -1; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
JOUER="1100:menu:Moniteur MIDI;1300:note-entrante:69:100:100;1700:relever-moniteur-midi"

echo "=== D586.3 : le nom des notes ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course c4 "" VSM_DELAI=2400 VSM_GESTE_APRES="900:relever-pianoroll;$JOUER"
course c3 "" VSM_DELAI=2400 VSM_GESTE_APRES="500:menu:Affichage > Nom des notes > Lettres (do central C3);900:relever-pianoroll;$JOUER"
course solfege "$maison" VSM_DELAI=2400 \
    VSM_GESTE_APRES="500:menu:Affichage > Nom des notes > Solfège (do central Do3);900:relever-pianoroll;$JOUER"
course retenu "$maison" VSM_DELAI=1800 VSM_GESTE_APRES="900:relever-pianoroll;1100:lister-menus"
course anglais "$maison" VSM_LANGUE=en VSM_DELAI=1800 VSM_GESTE_APRES="900:relever-pianoroll;1100:lister-menus"
course anglais-c4 "" VSM_LANGUE=en VSM_DELAI=1800 VSM_GESTE_APRES="1100:lister-menus"

for n in c4 c3 solfege retenu anglais; do echo "       $n : clavier [$(noms $n)] ; moniteur [$(moniteur $n)]"; done
grep -h "^VSM_REGLAGE_POSTE : affichageNomDesNotes" "$brouillon"/c3.txt "$brouillon"/solfege.txt | sed 's/^/       /'
coche="$(grep -h "^VSM_MENU_LISTE : Affichage > Nom des notes > Solfège (do central Do3)" "$brouillon/retenu.txt" | grep -c "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : View > Note names > Letters (middle C = C4)" "$brouillon/anglais-c4.txt")"
echo "       relancé : $coche entrée cochée ; en anglais : $en entrée(s)"

verdict "(1) d'origine : C4, C#4, A4 ; le moniteur A4 (69)" \
    "$([ "$(noms c4)" = "60:C4,61:C#4,69:A4" ] && [ "$(moniteur c4)" = "A4 (69)" ] && echo 1 || echo 0)"
verdict "(2) do central C3 : C3, C#3, A3 ; A3 (69)" \
    "$([ "$(noms c3)" = "60:C3,61:C#3,69:A3" ] && [ "$(moniteur c3)" = "A3 (69)" ] && echo 1 || echo 0)"
verdict "(3) solfège : Do3, Do#3, La3 ; La3 (69)" \
    "$([ "$(noms solfege)" = "60:Do3,61:Do#3,69:La3" ] && [ "$(moniteur solfege)" = "La3 (69)" ] && echo 1 || echo 0)"
verdict "(4) relancé : Solfège coché, le clavier écrit Do3" \
    "$([ "$coche" = 1 ] && [ "$(noms retenu)" = "60:Do3,61:Do#3,69:La3" ] && echo 1 || echo 0)"
verdict "(5) en anglais : l'entrée traduite ; le solfège reste Do3" \
    "$([ "$en" = 1 ] && [ "$(noms anglais)" = "60:Do3,61:Do#3,69:La3" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NOM-DES-NOTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NOM-DES-NOTES : le clavier et le moniteur écrivent le nom choisi — C4, C3 ou Do3"
