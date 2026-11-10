#!/usr/bin/env bash
# D623 — À TICK ÉGAL, LA NOTE QUI FINIT SE FERME AVANT QUE LA SUIVANTE S'OUVRE, mesuré dans l'application : le geste de
# D622 (la grille du rang 38 à 1/4, une frappe posée par un clic au tick ~400 — elle dure de 480 à 960 et TOUCHE la frappe
# de repère posée à 960), puis le projet enregistré (`enregistrer:`) et l'export MIDI (`exporter-midi:`, D356) ; l'ordre
# des événements lu tel que le fichier l'écrit, sans appariement.
#
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D623, (b)) : au tick 960, le note-off de 38 AVANT son note-on, dans le projet
# et dans l'export ; le témoin (D622) : le note-on avant ; relu par l'application, deux frappes de 38.
#
#   tools/ordre-midi.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D622/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ordre-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
for tick, e in ((960, b"\x99\x26\x6e"), (1080, b"\x89\x26\x00"), (1440, b"\x99\x24\x6e"), (1560, b"\x89\x24\x00")):
    corps += vlq(tick - t) + e; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "ordre", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; $4 = projet (défaut : le jeu). Un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" p="${4:-}" n
    if [ -z "$p" ]; then cp -r "$brouillon/projet" "$brouillon/p-$nom"; p="$brouillon/p-$nom"; fi
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$p" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=2600 VSM_GESTE_APRES="$gestes" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CLIC_POSITION|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# L'ordre des note-on et note-off de 38 au tick 960, tel que le fichier l'écrit : « off on », « on off », ou « ABSENT ».
ordre_a_960() {
    python3 - "$1" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, i = [], 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; st = 0
    while j < fin:
        v = 0
        while True:
            c = d[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); n = 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: st = d[j]; j += 1
        tl = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
        if st & 0xF0 in (0x80, 0x90) and t == 960 and d[j] == 38:
            out.append("on" if st & 0xF0 == 0x90 and d[j + 1] > 0 else "off")
        j += tl
    i = fin
print(" ".join(out) if out else "ABSENT")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D623 : à tick égal, la note qui finit se ferme d'abord ==="
course "$BIN" geometrie "1200:relever-losanges"
centre() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1 \2/p'; }
read -r X960 Y38 <<< "$(centre 38 960)"; read -r X1440 _ <<< "$(centre 36 1440)"
[ -n "${X960:-}" ] && [ -n "${X1440:-}" ] || { echo "REFUS : la géométrie des frappes de repère n'a pas été relevée"; exit 1; }
X400="$(python3 -c "import sys; a, b = float(sys.argv[1]), float(sys.argv[2]); print(f'{a + (400 - 960) * (b - a) / 480:.4f}')" "$X960" "$X1440")"
GESTE="900:carte-grille:38>1/4;1300:clic:pianoroll:$X400,$Y38"
course "$BIN" pose "$GESTE;1700:enregistrer:$brouillon/pose-projet;1900:exporter-midi:$brouillon/pose-export.mid"
course "$BIN" relu "1200:relever-losanges" "$brouillon/pose-projet"
P="$(ordre_a_960 "$brouillon/pose-projet/midi/arrangement.mid")"; E="$(ordre_a_960 "$brouillon/pose-export.mid")"
R="$(grep -c "^VSM_LOSANGE : note 38 " "$brouillon/relu.txt")"
echo "       au tick 960 : projet « $P » ; export « $E » ; relu par l'application : $R frappe(s) de 38"
verdict "(b) le projet enregistré : le note-off de 38 avant son note-on ($P)" "$([ "$P" = "off on" ] && echo 1 || echo 0)"
verdict "(b) l'export MIDI : de même ($E)" "$([ "$E" = "off on" ] && echo 1 || echo 0)"
verdict "(b) relu par l'application : deux frappes de 38 ($R)" "$([ "$R" = 2 ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-pose "$GESTE;1700:enregistrer:$brouillon/t-pose-projet"
    T="$(ordre_a_960 "$brouillon/t-pose-projet/midi/arrangement.mid")"
    echo "       témoin (D622) : au tick 960, « $T »"
    verdict "(b) témoin (D622) : le note-on avant le note-off ($T)" "$([ "$T" = "on off" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ORDRE-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ORDRE-MIDI : la frappe qui finit se ferme avant que la suivante s'ouvre, au projet comme à l'export"
