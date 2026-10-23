#!/usr/bin/env bash
# D582.1 — LA POSITION EN TIMECODE ET EN ÉCHANTILLONS (Transport ▸ Position affichée ▸ Secondes, Timecode (24, 25, 30 i/s),
# Échantillons ; les formats d'affichage du transport de Cubase), MESURÉE PAR LE LIBELLÉ RELEVÉ À UNE POSITION CONNUE,
# LE TÉMOIN EN SECONDES À CÔTÉ.
#
# LES RÈGLES GARDÉES (120 BPM, 4/4). La tête posée à la mesure 2, temps 2 (`VSM_POSITION=2.2`) : 2,5 s. Le libellé se
# lit par `relever-position`, celui que la barre peint, avec la fréquence qu'elle affiche (0 : jamais posée).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D582) :
#   (1) le TÉMOIN en secondes : 00:02,500 ;
#   (2) 24 i/s : 00:00:02:12 ; (3) 25 i/s : 00:00:02:12 ; (4) 30 i/s : 00:00:02:15 ;
#   (5) échantillons : 2,5 × la fréquence affichée ;
#   (6) la partie en mesures, « mes. 2 · 2 », la même dans les cinq ;
#   (7) relancé dans le même HOME : le format coché ; en anglais, « Timecode (25 fps) ».
#
#   tools/position-affichee.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-position.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\xcb\x00\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "position", "midi": {"file": "midi/arrangement.mid"},
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
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_POSITION=2.2 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GESTE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
affichee() { grep -h "^VSM_POSITION_AFFICHEE : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_POSITION_AFFICHEE : //; s/ ; fréquence .*//'; }
frequence() { grep -h "^VSM_POSITION_AFFICHEE : " "$brouillon/$1.txt" | tail -1 | sed -n 's/.* ; fréquence \([0-9]*\)$/\1/p'; }
temps() { echo "$1" | sed 's/ *|.*//'; }
mesures() { echo "$1" | sed 's/.*| *//'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
P="menu:Transport > Position affichée"

echo "=== D582.1 : la position en timecode et en échantillons ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course secondes "" VSM_DELAI=1600 VSM_GESTE_APRES="1000:relever-position"
course tc24 "" VSM_DELAI=1600 VSM_GESTE_APRES="700:$P > Timecode (24 i/s);1000:relever-position"
course tc25 "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="700:$P > Timecode (25 i/s);1000:relever-position"
course tc30 "" VSM_DELAI=1600 VSM_GESTE_APRES="700:$P > Timecode (30 i/s);1000:relever-position"
course echant "" VSM_DELAI=1600 VSM_GESTE_APRES="700:$P > Échantillons;1000:relever-position"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus;1000:relever-position"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

s="$(affichee secondes)"; a="$(affichee tc24)"; b="$(affichee tc25)"; c="$(affichee tc30)"; e="$(affichee echant)"; r="$(affichee retenu)"
hz="$(frequence echant)"
attendu_e="$(python3 -c "import sys; f=int(sys.argv[1]); print(round(2.5*f) if f > 0 else 'INCONNU')" "${hz:-0}" 2>/dev/null)"
for x in secondes tc24 tc25 tc30 echant retenu; do echo "       $x : [$(affichee $x)]"; done
echo "       fréquence affichée : ${hz:-non relevée} Hz → attendu $attendu_e échantillons"
coche="$(grep -h "^VSM_MENU_LISTE : Transport > Position affichée > Timecode (25 i/s)" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Transport > Position display > Timecode (25 fps)" "$brouillon/anglais.txt")"

verdict "(1) le témoin en secondes : 00:02,500" "$([ "$(temps "$s")" = "00:02,500" ] && echo 1 || echo 0)"
verdict "(2) 24 i/s : 00:00:02:12" "$([ "$(temps "$a")" = "00:00:02:12" ] && echo 1 || echo 0)"
verdict "(3) 25 i/s : 00:00:02:12" "$([ "$(temps "$b")" = "00:00:02:12" ] && echo 1 || echo 0)"
verdict "(4) 30 i/s : 00:00:02:15" "$([ "$(temps "$c")" = "00:00:02:15" ] && echo 1 || echo 0)"
verdict "(5) échantillons : 2,5 × la fréquence affichée" "$([ "$(temps "$e")" = "$attendu_e" ] && echo 1 || echo 0)"
verdict "(6) la partie en mesures, la même dans les cinq" \
    "$(m="$(mesures "$s")"; [ -n "$m" ] && [ "$(mesures "$a")" = "$m" ] && [ "$(mesures "$b")" = "$m" ] && [ "$(mesures "$c")" = "$m" ] && [ "$(mesures "$e")" = "$m" ] && echo 1 || echo 0)"
verdict "(7) retenu (coché, et le libellé relancé en 25 i/s) ; en anglais" \
    "$(echo "$coche" | grep -q "\[cochée\]" && [ "$(temps "$r")" = "00:00:02:12" ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "POSITION-AFFICHEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "POSITION-AFFICHEE : la position se lit en secondes, en timecode et en échantillons, les mesures à côté"
