#!/usr/bin/env bash
# LA GARDE DE D499 : UNE TOUCHE RÉASSIGNÉE NE FAIT PLUS RIEN DANS L'ARRANGEMENT.
#
# RÈGLE GARDÉE (29/09/2026). Le zoom de l'arrangement passe par la table des
# raccourcis ; son repli « + = - » (lu par le CARACTÈRE de la touche) ne vaut que
# sans table. Avant D499, il valait toujours : le zoom réassigné à Ctrl+= et
# Ctrl+-, « = » et « - » zoomaient ENCORE l'arrangement — hors de la table, hors de
# la fenêtre des raccourcis.
#
# COMMENT. Chaque course a un HOME neuf dont le fichier de préférences réassigne
# (ou non) les deux zooms ; les touches sont jouées par `VSM_TOUCHE=<vue>:x11:<t>`
# (D499), qui les fabrique AVEC leur caractère, comme X11 les livre — sans quoi
# le repli, qui lit le caractère, n'était jamais atteint au banc. L'état se lit
# sur `VSM_ARRANGEMENT` (« soit N % » : la part du morceau montrée) et, pour le
# piano roll, sur `VSM_PIANOROLL_RANG` (`zoom=`).
#
# CHAQUE CAS SE JUGE CONTRE SON TÉMOIN (D145) : la même course sans touche.
# Le contrôle « sans réassignation, = zoome » garde que la correction n'a pas
# éteint la touche elle-même.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/zoom-reassigne.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zoom-reassigne.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x03une")]
for i in range(32):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "zoom", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY

# LES PRÉFÉRENCES D'UN HOME DE BANC : le zoom réassigné (ou non), au format que
# `loadShortcuts` relit (clé « raccourcis », « vsm.raccourcis.v1 »).
maison() {   # $1 = reassigne|defaut -> le chemin du HOME
    local h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    if [ "$1" = "reassigne" ]; then
        mkdir -p "$h/VintageSynthMidiStudio"
        python3 - "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" <<'PY'
import json, sys
from xml.sax.saxutils import quoteattr
val = json.dumps({"format": "vsm.raccourcis.v1",
                  "overrides": {"view.zoomIn": "ctrl + =", "view.zoomOut": "ctrl + -"}})
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="raccourcis" val=' + quoteattr(val) + '/>\n</PROPERTIES>\n')
PY
    fi
    echo "$h"
}

course() {   # $1 nom ; $2 reassigne|defaut ; $3 clé (arr|pr) ; $4 touches (vide = témoin) -> valeur relue
    local nom="$1" h
    h="$(maison "$2")"
    local touches=()
    [ -n "$4" ] && touches=(VSM_TOUCHE="$4")
    env HOME="$h" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_DELAI=800 \
        VSM_VUE="sans-rapport,arrangement" VSM_ARRANGEMENT=1 VSM_PIANOROLL_ZONES=1 \
        VSM_CAPTURE="$brouillon/$nom.png" "${touches[@]}" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_TOUCHE : .*(illisible)|Raccourcis illisibles" "$brouillon/$nom.txt" | sed 's/^/        journal : /' >&2
    if [ "$3" = "arr" ]; then
        grep "VSM_ARRANGEMENT : fen" "$brouillon/$nom.txt" | tail -1 | grep -o "soit [0-9.]* %" | tr -d 'soit %'
    else
        grep "VSM_PIANOROLL_RANG" "$brouillon/$nom.txt" | tail -1 | grep -o " zoom=[^ ]*" | cut -d= -f2
    fi
}

rates=0
juger() {   # $1 nom ; $2 témoin ; $3 valeur ; $4 change|immobile
    local verdict=RATÉ
    if [ -n "$2" ] && [ -n "$3" ]; then
        { [ "$4" = "change" ] && [ "$2" != "$3" ]; } && verdict=OK
        { [ "$4" = "immobile" ] && [ "$2" = "$3" ]; } && verdict=OK
    fi
    printf '  %-4s %-34s témoin %s → %s (attendu : %s)\n' "$verdict" "$1" "$2" "$3" "$4"
    [ "$verdict" = OK ] || rates=$((rates + 1))
}

echo "=== D499 : une touche de zoom réassignée ne zoome plus l'arrangement ==="
t_re="$(course temoin-reassigne reassigne arr "")"
t_de="$(course temoin-defaut defaut arr "")"
t_pr="$(course temoin-pianoroll reassigne pr "")"
juger "réassigné, arrangement:x11:="        "$t_re" "$(course a-egal reassigne arr "arrangement:x11:=")" immobile
juger "réassigné, arrangement:x11:-"        "$t_re" "$(course a-moins reassigne arr "arrangement:x11:-")" immobile
juger "réassigné, arrangement:x11:ctrl + =" "$t_re" "$(course a-ctrl reassigne arr "arrangement:x11:ctrl + =")" change
juger "sans réassignation, x11:= (contrôle)" "$t_de" "$(course d-egal defaut arr "arrangement:x11:=")" change
juger "réassigné, pianoroll:x11:= (contrôle)" "$t_pr" "$(course p-egal reassigne pr "pianoroll:x11:=")" immobile
grep -h "VSM_TOUCHE : " "$brouillon"/a-egal.txt "$brouillon"/a-ctrl.txt | sed 's/^/        /'
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
