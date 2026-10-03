#!/usr/bin/env bash
# D538 — INSÉRER OU SUPPRIMER DU TEMPS AU MILIEU D'UN CLIP MIDI, MESURÉ PAR L'EXPORT MIDI.
#
# LA RÈGLE GARDÉE. Une piste, UN clip identité [0, 3840) — celui que pose l'ouverture d'un MIDI
# (D333) —, do à 0 et sol à 2 880 ; les locateurs sur [960, 1920), au milieu du clip.
#   (1) le témoin : le .mid exporté joue do à 0, sol à 2 880 ;
#   (2) « Insérer du silence entre les locateurs » : sol à 3 840 (2 880 + 960). Avant D538, la
#       seconde moitié du clip gardait sa fenêtre sur l'endroit d'où le sol était parti : il
#       DISPARAISSAIT ;
#   (3) « Supprimer le temps entre les locateurs » : sol à 1 920. Avant D538, il sonnait une
#       plage trop tôt.
#   (4) D541 : sur une piste à clip OUVERT (celui que pose l'import FL Studio ou Ableton) dont le
#       matériau finit à 1 200, insérer du temps à 1 920 laisse UN clip, toujours ouvert — avant
#       D541, une écharde d'un tick naissait au-delà de la coupe (`project.json` relu).
#
#   tools/temps-dans-un-clip.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-temps-clip.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/clip/midi"
python3 - "$brouillon/clip" <<'PY2'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Piano"),
       (0, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0])),
       (2400, bytes([0x90, 67, 100])), (480, bytes([0x80, 67, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "loop": {"enabled": False, "startTick": 960, "endTick": 1920}},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "clips": [{"sourceStart": 0, "sourceLength": 3840, "start": 0, "length": 3840, "color": "#FF6B9BFF"}],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
# D541 : la même piste, son clip OUVERT (tout à zéro), le matériau finissant à 1 200 ; locateurs plus loin.
o = d.replace("/clip", "/ouvert")
import os, shutil
shutil.copytree(d, o)
j = json.load(open(f"{o}/project.json"))
j["tracks"][0]["clips"] = [{"sourceStart": 0, "sourceLength": 0, "start": 0, "length": 0, "color": "#FF6B9BFF"}]
j["transport"]["loop"] = {"enabled": False, "startTick": 1920, "endTick": 2880}
evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Piano"),
       (0, bytes([0x90, 60, 100])), (240, bytes([0x80, 60, 0])),
       (720, bytes([0x90, 64, 100])), (240, bytes([0x80, 64, 0]))]
corps = b"".join(vlq(dt) + o2 for dt, o2 in evs) + b"\x00\xff\x2f\x00"
open(f"{o}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump(j, open(f"{o}/project.json", "w"), indent=1)
PY2

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES ; $3 = projet (« clip » par défaut)
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/${3:-clip}" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=3000 VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|grisée)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « canal:tick:hauteur » de chaque note d'un .mid, triées — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
i, jouees = 14, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st, t = 0, 0, 0
    while j < len(corps):
        dt = 0
        while corps[j] & 0x80: dt = (dt << 7) | (corps[j] & 0x7F); j += 1
        dt = (dt << 7) | corps[j]; j += 1; t += dt
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: jouees.append((st & 0x0F, t, corps[j]))
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(" ".join(f"{c + 1}:{t}:{h}" for c, t, h in sorted(jouees)) or "AUCUNE")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D538 : insérer ou supprimer du temps au milieu d'un clip MIDI ==="
course temoin "1500:exporter-midi:$brouillon/temoin.mid"
course inserer "1200:menu:Insérer du silence entre les locateurs;1600:exporter-midi:$brouillon/insere.mid"
course supprimer "1200:menu:Supprimer le temps entre les locateurs;1600:exporter-midi:$brouillon/supprime.mid"
t="$(notes "$brouillon/temoin.mid")"; i="$(notes "$brouillon/insere.mid")"; s="$(notes "$brouillon/supprime.mid")"
echo "       témoin {$t} ; inséré {$i} ; supprimé {$s}"
verdict "(1) le témoin : do à 0, sol à 2 880" "$([ "$t" = "1:0:60 1:2880:67" ] && echo 1 || echo 0)"
verdict "(2) insérer 960 ticks à 960 : le sol s'entend, à 3 840" "$([ "$i" = "1:0:60 1:3840:67" ] && echo 1 || echo 0)"
verdict "(3) supprimer [960, 1 920) : le sol à 1 920" "$([ "$s" = "1:0:60 1:1920:67" ] && echo 1 || echo 0)"

course ouvert "1200:menu:Insérer du silence entre les locateurs;1600:enregistrer:$brouillon/ouvert-insere" ouvert
clips="$(python3 -c "
import json, sys
try:
    j = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{c['sourceStart']}+{c['sourceLength']}@{c['start']}x{c['length']}\" for c in j['tracks'][0].get('clips', [])))
" "$brouillon/ouvert-insere/project.json")"
echo "       clip ouvert, temps inséré à 1 920 : {$clips}"
verdict "(4) D541 : un seul clip, toujours ouvert, sans écharde" "$([ "$clips" = "0+0@0x0" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TEMPS DANS UN CLIP : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TEMPS DANS UN CLIP : la seconde moitié du clip lit encore ses notes, à sa nouvelle place"
