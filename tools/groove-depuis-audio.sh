#!/usr/bin/env bash
# D544.2 — LE GROOVE D'UN CLIP AUDIO, DONNÉ À UNE PARTIE MIDI, MESURÉ PAR LE .MID EXPORTÉ.
#
# LA RÈGLE GARDÉE. Un projet à deux pistes : « Partie », MIDI, quatre notes droites aux ticks 240 · 480 ·
# 720 · 960 (120 BPM, 480 ticks par noire) ; « clics », audio, le fichier de D543.3 (attaques à 0,26 ·
# 0,49 · 0,77 · 1,01 s, soit les ticks 250 · 470 · 739 · 970).
#   (1) « Extraire le groove de ce clip » : le journal dit 4 attaques, 4 pas sur 16, et les écarts
#       2:+0,0833 · 4:−0,0833 · 6:+0,1583 · 8:+0,0833 (10, −10, 19 et 10 ticks sur 120) ;
#   (2) la partie choisie, « Appliquer le groove « clics (attaques) » » : dans le .mid exporté, les notes
#       à 250 · 470 · 739 · 970 (±1) — le TÉMOIN, même course sans appliquer : 240 · 480 · 720 · 960 ;
#   (3) le clip CALÉ d'abord par « Quantifier l'audio » : son groove ne déplace rien — la boîte « Aucune
#       note n'a bougé » est dite, et le .mid garde 240 · 480 · 720 · 960 (la carte d'étirement est lue).
# Chaque fichier se lit AVANT de juger : un .mid absent rend « ABSENT », jamais « faux ».
#
#   tools/groove-depuis-audio.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-groove-audio.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.mid "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
echantillons = [0.0] * (2 * sr)
for instant in (0.260, 0.490, 0.770, 1.010):
    debut = round(instant * sr)
    for n in range(int(0.010 * sr)):
        echantillons[debut + n] += 0.8 * math.sin(2 * math.pi * 3000 * n / sr) * math.exp(-n / (0.0012 * sr))
trames = b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in echantillons)
with wave.open(f"{b}/clics.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
# La partie droite : quatre do de 60 ticks aux croches 240 · 480 · 720 · 960.
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20", 0
for debut in (240, 480, 720, 960):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(60) + bytes([0x80, 60, 0]); t = debut + 60
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Partie"}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU_CONTEXTE|MENU|OPTIONS|EXPORT|EXPORT_MIDI|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
debuts() {   # les ticks de début des notes d'un .mid, à 480 par noire — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
if data[:4] != b"MThd":
    print("ABSENT"); sys.exit(0)
division = int.from_bytes(data[12:14], "big")
i, debuts = 14, []
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
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: debuts.append(round(t * 480 / division))
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(" ".join(str(t) for t in sorted(debuts)) or "AUCUNE")
PY
}
proches() {   # $1 = ticks relevés ; $2 = ticks attendus : à ±1 tick, autant de notes
    python3 -c "
import sys
try:
    a = [int(x) for x in sys.argv[1].split()]; b = [int(x) for x in sys.argv[2].split()]
except ValueError:
    sys.exit(1)
sys.exit(0 if len(a) == len(b) and all(abs(x - y) <= 1 for x, y in zip(a, b)) else 1)" "$1" "$2"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D544.2 : le groove d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/clics.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
cp -r "$brouillon/base" "$brouillon/cale"
EXTRAIRE="clip-audio-tous:Extraire le groove de ce clip"
APPLIQUER="menu:Appliquer le groove « clics (attaques) »"
course applique "$brouillon/base" VSM_DELAI=3500 VSM_MENU_CONTEXTE="$EXTRAIRE" \
    VSM_GESTE_APRES="1000:choisir:0;1400:touche:pianoroll:ctrl + A;1800:$APPLIQUER;2400:exporter-midi:$brouillon/applique.mid"
course temoin "$brouillon/base" VSM_DELAI=3500 VSM_MENU_CONTEXTE="$EXTRAIRE" \
    VSM_GESTE_APRES="1000:choisir:0;1400:touche:pianoroll:ctrl + A;2400:exporter-midi:$brouillon/temoin.mid"
course caler "$brouillon/cale" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Quantifier l'audio…" \
    VSM_OPTIONS="grille=2" VSM_GESTE_APRES="1500:enregistrer:$brouillon/cale"
course applique-cale "$brouillon/cale" VSM_DELAI=3500 VSM_MENU_CONTEXTE="$EXTRAIRE" \
    VSM_GESTE_APRES="1000:choisir:0;1400:touche:pianoroll:ctrl + A;1800:$APPLIQUER;2400:exporter-midi:$brouillon/applique-cale.mid"

grep -h "^VSM_GROOVE_AUDIO\|^VSM_QUANTIFIER_AUDIO" "$brouillon/applique.txt" "$brouillon/caler.txt" "$brouillon/applique-cale.txt" | sed 's/^/       /'
da="$(debuts "$brouillon/applique.mid")"; dt="$(debuts "$brouillon/temoin.mid")"; dc="$(debuts "$brouillon/applique-cale.mid")"
echo "       débuts des notes : groove appliqué [$da] ; témoin [$dt] ; groove du clip calé [$dc]"
verdict "(1) 4 attaques, 4 pas sur 16, les écarts des attaques à leurs pas" \
    "$(grep -q "^VSM_GROOVE_AUDIO : 4 attaque(s), 4 pas sur 16 ; pas:écart 2:0.0833 4:-0.0833 6:0.1583 8:0.0833" "$brouillon/applique.txt" && echo 1 || echo 0)"
verdict "(2) la partie prend le placement de la prise : 250 · 470 · 739 · 970, le témoin droit" \
    "$(proches "$da" "250 470 739 970" && proches "$dt" "240 480 720 960" && echo 1 || echo 0)"
verdict "(3) le groove du clip calé ne déplace rien, et c'est dit" \
    "$(grep -q "^VSM_QUANTIFIER_AUDIO : grille 1/8, 4 attaque(s) à .* ; 4 calée(s)" "$brouillon/caler.txt" \
        && grep -q "^VSM_BOITE : Appliquer le groove : Aucune note n'a bougé" "$brouillon/applique-cale.txt" \
        && proches "$dc" "240 480 720 960" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "GROOVE-DEPUIS-AUDIO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "GROOVE-DEPUIS-AUDIO : une partie droite prend le placement d'une prise audio"
