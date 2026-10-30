#!/usr/bin/env bash
# D606 — LA LECTURE SUIT L'ORDRE DE JEU (le mode Arrangement de Cubase), mesurée dans l'application : l'ordre posé et la
# case cochée par des verbes, la lecture lancée par le bouton Play ; où est la tête, quel élément joue, et l'arrêt après
# le dernier. Le témoin dans le même binaire (la case décochée : la tête file droit) et le binaire de D605.
#
# LE JEU : 480 ticks à la noire, 120 BPM, deux repères — « A » au tick 0, « B » au tick 1 920 (une mesure, 2 s) —, une
# piste au Minimoog avec une note dans chaque mesure ; les sections : A de 0 à 2 s, B de 2 à 4 s ; l'ordre [B, A].
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D606, attendu (c)) :
#   à ~1 s de lecture, la tête dans B (élément 1/2, entre 2 et 4 s) ; à ~3 s, dans A (élément 2/2, entre 0 et 2 s) ; à
#   ~5,5 s, le transport arrêté ; le témoin décoché : la tête file droit (dans A à ~1 s) ; le témoin (D605) : le verbe
#   inconnu ; l'anglais ; la fenêtre photographiée, l'élément joué éclairé.
#
#   tools/ordre-suivi.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D605/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ordre-suivi.XXXXXX")"
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

# LA LECTURE PAR LE BOUTON, à 1 200 ms ; les relevés une, trois et cinq secondes et demie plus tard.
SUIT="800:ordre:1,0;900:ordre-suit:oui;1200:cliquer:Play;2200:relever-ordre;4200:relever-ordre;6700:relever-ordre"
DROIT="800:ordre:1,0;1200:cliquer:Play;2200:relever-ordre"
echo "=== D606 : la lecture suit l'ordre de jeu ==="
course "$BIN" suit 7200 "$SUIT"
course "$BIN" photo 4000 "800:ordre:1,0;900:ordre-suit:oui;1200:cliquer:Play"
course "$BIN" droit 2600 "$DROIT"
course "$BIN" en 1500 "800:ordre:1,0;900:ordre-suit:oui;1000:relever-ordre" VSM_LANGUE=en

# CORRIGÉ APRÈS LE PREMIER ESSAI : l'ordre posé AVANT la case cochée n'a pas de chaîne (« chaîne de 0 », juste) — le
# contrôle lit la ligne de la case, qui suit.
P="$(grep "^VSM_ORDRE : case" "$brouillon/suit.txt" | head -1)"
L1="$(releve suit 1)"; L2="$(releve suit 2)"; L3="$(releve suit 3)"; D1="$(releve droit 1)"; E1="$(releve en 1)"
echo "       cochée      : $P"
echo "       à ~1 s      : $L1"
echo "       à ~3 s      : $L2"
echo "       à ~5,5 s    : $L3"
echo "       décoché     : $D1"
echo "       en anglais  : $E1"
verdict "(c) l'ordre posé et la case cochée : [B, A], une chaîne de 2" \
    "$(echo "$P" | grep -q "suit oui ; ordre \[B, A\] ; chaîne de 2 ; " && echo 1 || echo 0)"
verdict "(c) à ~1 s : élément 1/2, la tête dans B (2 à 4 s), en lecture" \
    "$([ "$(element_de "$L1")" = "1/2" ] && dans "$(tete_de "$L1")" 2 4 && echo "$L1" | grep -q "en lecture" && echo 1 || echo 0)"
verdict "(c) à ~3 s : élément 2/2, la tête dans A (0 à 2 s), en lecture" \
    "$([ "$(element_de "$L2")" = "2/2" ] && dans "$(tete_de "$L2")" 0 2 && echo "$L2" | grep -q "en lecture" && echo 1 || echo 0)"
verdict "(c) à ~5,5 s : le transport arrêté après le dernier élément" "$(echo "$L3" | grep -q "arrêté ; " && echo 1 || echo 0)"
verdict "(c) témoin décoché : aucune chaîne, la tête file droit dans A" \
    "$(echo "$D1" | grep -q "suit non ; .* ; chaîne de 0 ; " && dans "$(tete_de "$D1")" 0 2 && echo 1 || echo 0)"
verdict "(c) en anglais : la case « Playback follows the order »" "$(echo "$E1" | grep -q "case « Playback follows the order »" && echo 1 || echo 0)"
PHOTO="$(ls "$brouillon"/photo-*-Ordre*.png 2>/dev/null | head -1)"
verdict "(c) la fenêtre de l'ordre photographiée en lecture ($( [ -n "$PHOTO" ] && basename "$PHOTO"))" \
    "$([ -n "$PHOTO" ] && [ -s "$PHOTO" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-suit 2600 "$DROIT"
    tl="$(grep -c "^VSM_ORDRE" "$brouillon/t-suit.txt")"; ti="$(grep -c "geste inconnu" "$brouillon/t-suit.txt")"
    echo "       témoin : $tl relevé(s) d'ordre ; $ti « geste inconnu »"
    verdict "(c) témoin (D605) : aucun relevé, les verbes inconnus" "$([ "$tl" = 0 ] && [ "$ti" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ORDRE SUIVI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ORDRE SUIVI : la lecture joue l'ordre sans toucher au matériau, et s'arrête après le dernier élément"
