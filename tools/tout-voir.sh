#!/usr/bin/env bash
# LA GARDE DE D502 : « ZOOM : TOUT VOIR (LES DEUX VUES) » CADRE LES DEUX VUES, QUELLE
# QUE SOIT LA PORTE.
#
# RÈGLE GARDÉE (29/09/2026). Ctrl+0 porte dans la table le nom « Zoom : tout voir
# (les deux vues) », et `executerCommande` cadre l'arrangement ET le piano roll
# (D14.2). Avant D502, le piano roll, qui reçoit la touche d'abord quand il a le
# clavier (D492), la traitait en ne cadrant que lui — et son entrée du menu
# Édition, Ctrl+0 en regard, aussi.
#
# COMMENT. Un projet d'une piste ; les deux vues sont zoomées au démarrage
# (`VSM_VUE=zoom-arrangement:4,zoom-piano:4`), puis « tout voir » est demandé par
# chaque porte : la touche piano roll au clavier (`focus:pianoroll:`), arrangement
# au clavier, application au clavier, et l'entrée du menu Édition. L'arrangement
# se relit sur `VSM_ARRANGEMENT` (« soit N % »), le piano roll sur
# `VSM_PIANOROLL_RANG` (`zoom=`). TÉMOIN : la même course sans geste — les deux
# doivent en différer.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/tout-voir.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tout-voir.XXXXXX")"
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
evs = [(0, b"\xff\x03\x03une")]
for i in range(16):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60 + i % 12, 100])), (480, bytes([0x80, 60 + i % 12, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "tout-voir", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY

course() {   # $1 nom ; $2... variables du geste -> « arr=N pr=Z »
    local nom="$1" h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$h" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_DELAI=800 \
        VSM_VUE="sans-rapport,arrangement,zoom-arrangement:4,zoom-piano:4" \
        VSM_ARRANGEMENT=1 VSM_PIANOROLL_ZONES=1 VSM_CAPTURE="$brouillon/$nom.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(MENU|TOUCHE) : .*(aucune|AUCUNE|grisée|illisible)" "$brouillon/$nom.txt" | sed 's/^/        journal : /' >&2
    echo "arr=$(grep 'VSM_ARRANGEMENT : fen' "$brouillon/$nom.txt" | tail -1 | grep -o 'soit [0-9.]* %' | tr -d 'soit %') pr=$(grep VSM_PIANOROLL_RANG "$brouillon/$nom.txt" | tail -1 | grep -o ' zoom=[^ ]*' | cut -d= -f2)"
}

rates=0
temoin="$(course temoin)"
t_arr="${temoin%% *}"; t_pr="${temoin##* }"
juger() {   # $1 nom ; $2 résultat
    local arr="${2%% *}" pr="${2##* }"
    if [ "$arr" != "$t_arr" ] && [ "$pr" != "$t_pr" ] && [ "$arr" != "arr=" ] && [ "$pr" != "pr=" ]; then
        printf '  OK   %-26s %s (témoin %s)\n' "$1" "$2" "$temoin"
    else
        printf '  RATÉ %-26s %s (témoin %s ; les DEUX vues doivent avoir bougé)\n' "$1" "$2" "$temoin"
        rates=$((rates + 1))
    fi
}

echo "=== D502 : « tout voir » cadre les deux vues, par chaque porte ==="
juger "touche, piano roll"   "$(course pr VSM_TOUCHE='focus:pianoroll:ctrl + 0')"
juger "touche, arrangement"  "$(course arr VSM_TOUCHE='focus:arrangement:ctrl + 0')"
juger "touche, application"  "$(course app VSM_TOUCHE='ctrl + 0')"
juger "menu Édition"         "$(course menu VSM_MENU='Zoom : tout voir')"
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
