#!/usr/bin/env bash
# D581.3 — LES CONTRÔLEURS SUIVENT LES NOTES (Édition ▸ Les contrôleurs suivent les notes, le menu du piano roll ; l'*Auto
# Select Controllers* de l'éditeur de clavier de Cubase), MESURÉS PAR LES INSTANTS DU MIDI EXPORTÉ APRÈS UN DÉCALAGE, LE
# TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la grille à la double-croche : 120 ticks). Deux notes de vélocité 50, [480,
# 720) et [960, 1200) ; une troisième, à 2400 et de vélocité 100, qui tient le morceau ouvert — l'export finit où finit
# ce qu'on entend, et le premier jet, sans elle, perdait le CC à 2000 aux trois exports ; des CC 1 à 480, 600, 960, 1100
# et 2000 ; un pitch bend à 700. Les deux notes choisies par « Notes plus faibles que 64 (2) » — « Tout sélectionner »
# étendrait la plage jusqu'à 2640 —, le décalage d'un pas de grille par la flèche du piano roll
# (`touche:pianoroll:cursor right`, `nudgeSelection`).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D581) :
#   (1) avec l'option : les notes à 600 et 1080, les CC à 600, 720, 1080, 1220 et 2000, le pitch bend à 820 ;
#   (2) le TÉMOIN sans elle : les notes à 600 et 1080, les CC à 480, 600, 960, 1100, 2000, le pitch bend à 700 ;
#   (3) Ctrl+Z après le décalage avec l'option : les notes ET les contrôleurs à leurs instants d'avant, en un pas ;
#   (4) relancé dans le même HOME : l'entrée cochée ; en anglais, « Controllers follow the notes ».
#
#   tools/controleurs-suivent.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-controleurs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = [(480, 1, bytes([0x90, 60, 50])), (720, 0, bytes([0x80, 60, 0])),
       (960, 1, bytes([0x90, 64, 50])), (1200, 0, bytes([0x80, 64, 0])),
       (2400, 1, bytes([0x90, 67, 100])), (2640, 0, bytes([0x80, 67, 0])),
       (700, 2, bytes([0xE0, 0x00, 0x50]))]
for t, v in ((480, 20), (600, 40), (960, 60), (1100, 80), (2000, 100)):
    evs.append((t, 2, bytes([0xB0, 1, v])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "controleurs", "midi": {"file": "midi/arrangement.mid"},
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
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GESTE|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
instants() {   # « notes … | cc … | pb … » d'un .mid, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
notes, cc, pb = [], [], []
for tr in m.tracks:
    t = 0
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0: notes.append(t)
        elif x.type == "control_change" and x.control == 1: cc.append(t)
        elif x.type == "pitchwheel": pb.append(t)
j = lambda l: " ".join(str(v) for v in sorted(l))
print(f"notes {j(notes)} | cc {j(cc)} | pb {j(pb)}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SUIVENT="menu:Édition > Les contrôleurs suivent les notes"
TOUT="menu:Édition > Sélection > Notes plus faibles que 64 (2)"

echo "=== D581.3 : les contrôleurs suivent les notes ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course suivent "$maison" VSM_DELAI=2400 \
    VSM_GESTE_APRES="500:choisir:0;700:$SUIVENT;900:$TOUT;1100:touche:pianoroll:cursor right;1600:exporter-midi:$brouillon/suivent.mid"
course temoin "" VSM_DELAI=2400 \
    VSM_GESTE_APRES="500:choisir:0;900:$TOUT;1100:touche:pianoroll:cursor right;1600:exporter-midi:$brouillon/temoin.mid"
course defait "" VSM_DELAI=2600 \
    VSM_GESTE_APRES="500:choisir:0;700:$SUIVENT;900:$TOUT;1100:touche:pianoroll:cursor right;1400:touche:ctrl + Z;1800:exporter-midi:$brouillon/defait.mid"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

is="$(instants "$brouillon/suivent.mid")"; it="$(instants "$brouillon/temoin.mid")"; iz="$(instants "$brouillon/defait.mid")"
grep -h "^VSM_CONTROLEURS_SUIVENT\|^VSM_TOUCHE" "$brouillon"/suivent.txt "$brouillon"/defait.txt | sed 's/^/       /'
echo "       avec l'option : [$is]"
echo "       témoin        : [$it]"
echo "       après Ctrl+Z  : [$iz]"
coche="$(grep -h "^VSM_MENU_LISTE : Édition > Les contrôleurs suivent les notes" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Edit > Controllers follow the notes" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"

verdict "(1) avec l'option : CC 600 720 1080 1220 2000, pitch bend 820" \
    "$([ "$is" = "notes 600 1080 2400 | cc 600 720 1080 1220 2000 | pb 820" ] && echo 1 || echo 0)"
verdict "(2) le témoin : CC 480 600 960 1100 2000, pitch bend 700" \
    "$([ "$it" = "notes 600 1080 2400 | cc 480 600 960 1100 2000 | pb 700" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : tout revient, en un pas" \
    "$([ "$iz" = "notes 480 960 2400 | cc 480 600 960 1100 2000 | pb 700" ] && echo 1 || echo 0)"
verdict "(4) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CONTROLEURS-SUIVENT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CONTROLEURS-SUIVENT : les contrôleurs sous les notes partent avec elles, du même écart, dans le même pas"
