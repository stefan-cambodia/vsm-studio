#!/usr/bin/env bash
# La garde de D327 et D345 : L'ÉCHELLE D'UNE LANE D'AUTOMATION SUIT SON UNITÉ.
#
# RÈGLE GARDÉE (18/09/2026). Une lane passe en échelle logarithmique quand son
# paramètre porte une **unité multiplicative** (Hz, s, ms — y doubler la valeur a
# partout le même sens), que sa plage couvre **au moins une décade** et que son
# **minimum est strictement positif**. Tout le reste reste linéaire : le décibel
# est déjà un logarithme, le pour-cent et les demi-tons sont additifs, et une
# plage qui touche zéro n'a pas de logarithme.
#
# CE QUE LA RÈGLE COÛTE QUAND ELLE MANQUE. 223 paramètres du parc sont déclarés
# en secondes (les enveloppes), souvent de 0,001 à 8 s : quatre décades, dont
# trois sous un dixième de seconde. Trente points posés GÉOMÉTRIQUEMENT entre
# 3 ms et 4 s — c'est ainsi qu'on règle une enveloppe, en doublant — tenaient sur
# une ligne au ras du bas de la lane : médiane à **3,3 %** de sa hauteur.
#
# LE TÉMOIN EST DANS LE MÊME PROJET, et il ne change qu'UNE variable :
#   piste 0  envelope.1.decay   unité « s », 0,001..8   -> LOG
#   piste 1  filter.1.drive     SANS unité,  0,1..8     -> linéaire (même forme de plage)
#   piste 2  voice.glideTime    unité « s », 0..3       -> linéaire (minimum nul)
# Les trois portent les MÊMES trente points géométriques, mis à l'échelle.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/automation-echelle.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-echelle.XXXXXX")"
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
def piste(evs):
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
nom = lambda s: b"\xff\x03" + vlq(len(s)) + s.encode()
tempo = b"\xff\x51\x03\x07\xa1\x20"
sig = b"\xff\x58\x04\x04\x02\x18\x08"
pistes = []
for i, n in enumerate(("Log", "SansUnite", "MinimumNul")):
    evs = [(0, nom(n))] + ([(0, tempo), (0, sig)] if i == 0 else [])
    for k in range(8):
        h = 48 + k
        evs += [(0, bytes([0x90 | i, h, 100])), (480, bytes([0x80 | i, h, 0]))]
    pistes.append(piste(evs))
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + b"".join(pistes))

# LES MÊMES TRENTE POINTS GÉOMÉTRIQUES, mis à l'échelle de chaque plage.
def points(bas, haut):
    return [{"tick": i * 240, "value": round(bas * (haut / bas) ** (i / 29.0), 5)} for i in range(30)]
cibles = [("envelope.1.decay", points(0.003, 4.0)),
          ("filter.1.drive", points(0.12, 6.0)),
          ("voice.glideTime", points(0.003, 2.4))]
tracks = []
for i, (param, pts) in enumerate(cibles):
    tracks.append({"channel": i, "color": "#FF6B9BFF", "effects": [],
                   "instrument": {"preferredPlugin": "vsm.minimoog"},
                   "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                   "name": ("Log", "SansUnite", "MinimumNul")[i],
                   "automation": [{"parameter": param, "points": pts}]})
json.dump({"format": "vsm-project", "version": 1, "title": "echelles",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": tracks}, open(d + "/project.json", "w"), indent=1)
PY

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
lancer() {   # $1 index de piste -> journal + photo
    local maison; maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_AUTOMATION=1 VSM_DELAI=3500 \
        VSM_VUE="sans-rapport,arrangement,automation" VSM_GESTE_PISTE="choisir:$1" \
        VSM_CAPTURE="$brouillon/piste$1.png" \
        timeout 45 "$BIN" > "$brouillon/piste$1.txt" 2>&1
}

echo "=== D327/D345 : l'échelle d'une lane d'automation ==="
for i in 0 1 2; do lancer "$i"; done

ligne0="$(grep -m1 '^VSM_AUTOMATION : ' "$brouillon/piste0.txt")"
ligne1="$(grep -m1 '^VSM_AUTOMATION : ' "$brouillon/piste1.txt")"
ligne2="$(grep -m1 '^VSM_AUTOMATION : ' "$brouillon/piste2.txt")"
echo "       $ligne0"
echo "       $ligne1"
echo "       $ligne2"
verdict "« Amp Decay » (s, 0,001..8) : échelle LOG" \
        "$(grep -c 'unité « s ».*échelle log' <<<"$ligne0")"
verdict "témoin sans unité (0,1..8, même forme de plage) : LINÉAIRE" \
        "$(grep -c 'unité «  ».*échelle linéaire' <<<"$ligne1")"
verdict "témoin en secondes mais de minimum NUL (0..3) : LINÉAIRE" \
        "$(grep -c 'unité « s ».*échelle linéaire' <<<"$ligne2")"

# ET LA COURBE OCCUPE SA LANE, mesurée sur la photo : le relevé dit la règle
# appliquée, la photo dit ce que l'utilisateur voit.
etal="$(python3 tools/etalement-automation.py "$brouillon/piste0.png" 1070,1310 2>/dev/null)"
echo "       $etal"
med="$(sed -n 's/.*hauteur médiane *\([0-9.]*\) %.*/\1/p' <<<"$etal")"
spread="$(sed -n 's/.*étalement *\([0-9.]*\) %.*/\1/p' <<<"$etal")"
verdict "les points occupent la lane : médiane ${med:-?} % (règle : >= 40) et étalement ${spread:-?} % (règle : >= 40)" \
        "$(awk -v m="${med:-0}" -v s="${spread:-0}" 'BEGIN { print (m >= 40 && s >= 40) ? 1 : 0 }')"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
