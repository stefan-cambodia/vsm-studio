#!/usr/bin/env bash
# D612 — ALLER À UN ÉLÉMENT DE L'ORDRE DE JEU (le clic de l'*Arranger Editor* de Cubase), mesuré dans l'application :
# l'ordre [B, A, A] posé, la case cochée, la lecture lancée par le bouton ; un double-clic sur la 3ᵉ ligne (le verbe
# `ordre-aller:3`, par la même fonction que la souris) ; où va la tête, quel élément joue, et la fin de la chaîne.
#
# LE JEU : celui de `ordre-suivi.sh` (D606) — « A » au tick 0, « B » au tick 1 920, une mesure chacune (2 s).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D612, attendu (b)) :
#   après le double-clic, élément 3/3, la tête entre 0 et 2 s ; ~2,5 s plus tard, arrêté ; le témoin dans le même
#   binaire, case décochée : la tête au début de la section (dans A, à moins de 0,5 s) et pas de chaîne ; le témoin
#   (D611) : le verbe inconnu ; l'anglais (l'infobulle de la liste) ; la photo.
#
#   tools/ordre-aller.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D611/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ordre-aller.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for debut, hauteur in ((0, 57), (1920, 64)):
    corps += vlq(debut - t) + bytes([0x90, hauteur, 100]) + vlq(960) + bytes([0x80, hauteur, 0]); t = debut + 960
corps += vlq(3840 - t) + b"\xff\x2f\x00"
os.makedirs(f"{b}/projet/midi", exist_ok=True)
open(f"{b}/projet/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "ordre", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "A"}, {"tick": 1920, "name": "B"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/projet/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = délai de la photo (ms) ; $4 = gestes ; le reste : variables
    local bin="$1" nom="$2" delai="$3" gestes="$4" n
    shift 4
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI="$delai" VSM_GESTE_APRES="$gestes" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
releve() { grep "^VSM_ORDRE : relev" "$brouillon/$1.txt" | sed -n "${2}p"; }
tete_de() { echo "$1" | sed -n 's/.* ; tête \(-\?[0-9.]*\) s ; .*/\1/p'; }
element_de() { echo "$1" | sed -n 's/.* ; élément joué \([0-9]*\/[0-9]*\) ; .*/\1/p'; }
dans() { python3 -c "import sys; v = float(sys.argv[1]); sys.exit(0 if float(sys.argv[2]) <= v < float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
releve_n() { grep "^VSM_ORDRE : relev" "$brouillon/$1.txt" | sed -n "${2}p"; }

echo "=== D612 : aller à un élément de l'ordre de jeu ==="
ALLER="800:ordre:1,0,0;900:ordre-suit:oui;1200:cliquer:Play;1700:ordre-aller:3;1900:relever-ordre;4600:relever-ordre"
course "$BIN" aller 5000 "$ALLER"
course "$BIN" droit 2300 "800:ordre:1,0,0;1200:cliquer:Play;1700:ordre-aller:3;1900:relever-ordre"
course "$BIN" en 1500 "800:ordre:1,0,0;900:ordre-suit:oui;1000:relever-ordre" VSM_LANGUE=en

G="$(grep "^VSM_ORDRE : allé" "$brouillon/aller.txt" | head -1)"; L1="$(releve_n aller 1)"; L2="$(releve_n aller 2)"
D1="$(releve_n droit 1)"; E1="$(releve_n en 1)"
echo "       le geste    : $G"
echo "       ~0,2 s après : $L1"
echo "       ~2,9 s après : $L2"
echo "       décoché     : $D1"
echo "       en anglais  : $E1"
verdict "(b) le double-clic dit : « allé à l'élément », l'ordre [B, A, A], une chaîne de 3" \
    "$(echo "$G" | grep -q "suit oui ; ordre \[B, A, A\] ; chaîne de 3 ; " && echo 1 || echo 0)"
verdict "(b) ~0,2 s après : élément 3/3, la tête dans A (0 à 2 s), en lecture" \
    "$([ "$(element_de "$L1")" = "3/3" ] && dans "$(tete_de "$L1")" 0 2 && echo "$L1" | grep -q "en lecture" && echo 1 || echo 0)"
verdict "(b) ~2,9 s après : la chaîne finie, le transport arrêté" "$(echo "$L2" | grep -q "arrêté ; " && echo 1 || echo 0)"
verdict "(b) témoin décoché : pas de chaîne, la tête au début de la section A (moins de 0,5 s), en lecture" \
    "$(echo "$D1" | grep -q "suit non ; .* ; chaîne de 0 ; " && dans "$(tete_de "$D1")" 0 0.5 && echo "$D1" | grep -q "en lecture" && echo 1 || echo 0)"
verdict "(b) en anglais : l'infobulle « Double-click: playback goes to this element. »" \
    "$(echo "$E1" | grep -q "infobulle « Double-click: playback goes to this element. »" && echo 1 || echo 0)"
PHOTO="$(ls "$brouillon"/aller-*-Ordre*.png 2>/dev/null | head -1)"
verdict "(b) la fenêtre photographiée ($( [ -n "$PHOTO" ] && basename "$PHOTO"))" "$([ -n "$PHOTO" ] && [ -s "$PHOTO" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-aller 2300 "800:ordre:1,0,0;900:ordre-suit:oui;1200:cliquer:Play;1700:ordre-aller:3;1900:relever-ordre"
    ti="$(grep -c "ordre-aller:3 .*geste inconnu" "$brouillon/t-aller.txt")"; ta="$(grep -c "^VSM_ORDRE : allé" "$brouillon/t-aller.txt")"
    echo "       témoin : $ta saut(s) ; $ti « geste inconnu » pour ordre-aller"
    verdict "(b) témoin (D611) : le verbe inconnu, aucun saut" "$([ "$ta" = 0 ] && [ "$ti" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ORDRE ALLER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ORDRE ALLER : un double-clic mène la lecture à CET élément, et la chaîne reprend de là"
