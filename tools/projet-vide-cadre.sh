#!/usr/bin/env bash
# D566.4 — UN PROJET VIDE S'OUVRAIT À 8 PX PAR TICK : le cadrage de l'ouverture (D362) cadrait
# `max(1, lastSoundingTick())`, UN tick sur toute la largeur (une mesure de 15 360 px), vue qui s'enregistre ensuite
# avec le projet (D363) — un fichier importé après restait hors de l'écran. Le cadrage ne descend plus sous QUATRE
# MESURES. (Lancée SANS projet, l'application garde son zoom d'usine et n'était pas touchée : c'est l'OUVERTURE d'un
# projet sans rien qui sonne — sans piste — qui l'était.)
#
# LES RÈGLES GARDÉES (relevé `VSM_ARRANGEMENT=1`, lu APRÈS les gestes) :
#   (1) un projet sans piste ouvert : la fenêtre montre de 3,5 à 5 mesures (4 / 0,96), « MORCEAU VIDE » ;
#   (2) un fichier de 6 s importé ensuite (trois mesures à 120 BPM) : ENTIER à l'écran (au moins 100 %) ;
#   (3) le projet enregistré porte une vue sous le plafond du zoom à la main (2,0 px par tick), et non 8 ;
#   (4) le morceau de quatre mesures de D362 garde son cadrage au tick près : la même fenêtre que le projet vide.
#
#   tools/projet-vide-cadre.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-vide.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
with wave.open(f"{b}/son.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
    w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 220 * i / 44100))) for i in range(6 * 44100)))
# Un projet SANS PISTE, celui que le banc de D566.2 importait (et dont il retirait la vue enregistrée).
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/vide/midi", exist_ok=True)
open(f"{b}/vide/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                   + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "vide", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/vide/project.json", "w"), indent=1)
# Le morceau « court » de D362 : une note par mesure, quatre mesures.
os.makedirs(f"{b}/court/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n:
        o.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(o))
evs = [(0, b"\xff\x03\x05Court"), (0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
for i in range(4):
    evs += [(0 if i == 0 else 1920 - 240, bytes([0x90, 60, 100])), (240, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(f"{b}/court/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "court", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "name": "Court", "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0}}]},
          open(f"{b}/court/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_ARRANGEMENT=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|IMPORT[A-Z_]*) : .*(inconnu|introuvable|aucun|refus|REFUS|ÉCHEC)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fenetre() { grep "^VSM_ARRANGEMENT : fen" "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_ARRANGEMENT : //'; }
mesures() { fenetre "$1" | sed -n 's/^fenêtre \([0-9.]*\) mesure.*/\1/p'; }
part() { fenetre "$1" | sed -n 's/.*soit \([0-9.]*\) %.*/\1/p'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D566.4 : un projet vide ne s'ouvre plus à 8 px par tick ==="
cp -r "$brouillon/vide" "$brouillon/importe"
course vide VSM_DELAI=1500 VSM_VUE="sans-rapport,arrangement" VSM_PROJET="$brouillon/vide"
course importe VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" VSM_PROJET="$brouillon/importe" \
    VSM_IMPORT_AUDIO="$brouillon/son.wav" VSM_GESTE_APRES="1200:relever-arrangement;1500:enregistrer:$brouillon/importe"
course court VSM_DELAI=1500 VSM_VUE="sans-rapport,arrangement" VSM_PROJET="$brouillon/court"
zoom="$(python3 -c "
import json, sys
try:
    print(json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))['view']['pixelsPerTick'])
except Exception as e:
    print('ABSENT')" "$brouillon/importe")"
echo "       vide    : [$(fenetre vide)]"
echo "       importé : [$(fenetre importe)] — vue enregistrée : $zoom px par tick"
echo "       court   : [$(fenetre court)]"

v1="$(awk -v m="$(mesures vide)" 'BEGIN { print (m != "" && m >= 3.5 && m <= 5.0) ? 1 : 0 }')"
fenetre vide | grep -q "MORCEAU VIDE" || v1=0
verdict "(1) un projet sans piste ouvert : de 3,5 à 5 mesures à l'écran, le morceau dit vide" "$v1"
verdict "(2) un fichier de 6 s importé ensuite : entier à l'écran" \
    "$(awk -v p="$(part importe)" 'BEGIN { print (p != "" && p >= 100.0) ? 1 : 0 }')"
verdict "(3) la vue enregistrée est sous le plafond du zoom à la main (2,0), et non 8" \
    "$(awk -v z="$zoom" 'BEGIN { print (z != "ABSENT" && z + 0 > 0 && z + 0 <= 2.0) ? 1 : 0 }')"
verdict "(4) le morceau de quatre mesures : la même fenêtre que le projet vide (le cadrage de D362 inchangé)" \
    "$([ -n "$(mesures court)" ] && [ "$(mesures court)" = "$(mesures vide)" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PROJET-VIDE-CADRE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PROJET-VIDE-CADRE : un projet vide s'ouvre sur quatre mesures, et ce qu'on y importe se voit"
