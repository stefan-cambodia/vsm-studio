#!/usr/bin/env bash
# D643 — UN FICHIER MIDI DE FORMAT 0, OUVERT PUIS RÉÉCRIT. Mesure de l'hypothèse écrite avant elle (ROADMAP-daw, D643) :
#   (H1) exporté (*Exporter MIDI…*) : quel en-tête (format, nombre de pistes) ?
#   (H2) enregistré comme projet : l'en-tête de son `midi/arrangement.mid` ; rouvert : ses pistes (noms, canaux, notes) ;
#   (H3) le témoin : le même contenu en format 1.
# Le premier tour MESURE et écrit des faits ; les verdicts s'écrivent avec l'attendu du remède.
#
#   tools/format-zero.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-format-zero.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp -r "$brouillon"/*.txt "$brouillon"/*.mid "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

# Deux fichiers, le même contenu : une mélodie au canal 1 (60, 64, 67) et une batterie au canal 10 (36, 38).
python3 - "$brouillon" <<'PY'
import struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(evts, nom=b""):
    corps = b"\x00\xff\x51\x03\x07\xa1\x20" if nom == b"tempo" else b""
    if nom and nom != b"tempo": corps += b"\x00\xff\x03" + bytes([len(nom)]) + nom
    t = 0
    for tick, e in sorted(evts, key=lambda x: (x[0], x[1][0] & 0xF0 == 0x90)):
        corps += vlq(tick - t) + e; t = tick
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
melodie = []
for i, h in enumerate([60, 64, 67]):
    melodie += [(480 * i, bytes([0x90, h, 100])), (480 * i + 400, bytes([0x80, h, 0]))]
batterie = []
for i, h in enumerate([36, 38]):
    batterie += [(960 * i, bytes([0x99, h, 110])), (960 * i + 120, bytes([0x89, h, 0]))]
# Format 0 : UNE piste, tempo compris.
seule = piste(melodie + batterie + [(0, b"\xff\x51\x03\x07\xa1\x20")])
open(f"{b}/zero.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + seule)
# Format 1 : le tempo, puis les deux parties.
un = piste([], b"tempo") + piste(melodie, b"Melodie") + piste(batterie, b"Batterie")
open(f"{b}/un.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + un)
PY

entete() {   # « format F, N piste(s), M note(s) par piste »
    python3 - "$1" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
fmt, n = int.from_bytes(d[8:10], "big"), int.from_bytes(d[10:12], "big")
i, notes = 14, []
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; statut = 0; c = []
    while j < fin:
        while d[j] & 0x80: j += 1
        j += 1
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); m = 0
            while True:
                x = d[k]; k += 1; m = (m << 7) | (x & 0x7F)
                if not x & 0x80: break
            j = k + m; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        if statut & 0xF0 == 0x90 and d[j + 1] > 0: c.append(f"{(statut & 0x0F) + 1}:{d[j]}")
        j += taille
    notes.append("[" + " ".join(c) + "]"); i = fin
print(f"format {fmt}, {n} piste(s) déclarée(s), {len(notes)} écrite(s) ; notes par piste (canal:hauteur) : {' '.join(notes)}")
PY2
}
pistes() {   # les pistes d'un projet enregistré : nom, canal, machine
    python3 -c "import json, sys
t = json.load(open(sys.argv[1] + '/project.json'))['tracks']
print(' | '.join(f\"{x.get('name')} (canal {x.get('channel', 0) + 1}, {x.get('instrument', {}).get('preferredPlugin', '-')})\" for x in t))" "$1" 2>/dev/null || echo ABSENT
}
course() {   # $1 = nom ; $2 = gestes ; le reste : variables. Un HOME neuf.
    local nom="$1" gestes="$2"; shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_DELAI=4500 \
        VSM_GESTE_APRES="$gestes" "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|refus|inconnu|ATTENTION)" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /"
}

echo "=== D643 : un fichier MIDI de format 0, ouvert puis réécrit (mesure) ==="
for src in zero un; do
    course "$src" "800:ouvrir-midi:$brouillon/$src.mid;1800:exporter-midi:$brouillon/$src-export.mid;2400:enregistrer:$brouillon/$src-projet"
    course "$src-rouvert" "1500:enregistrer:$brouillon/$src-projet2" VSM_PROJET="$brouillon/$src-projet"
    echo "--- source $src.mid : $(entete "$brouillon/$src.mid")"
    echo "    (H1) l'export        : $(entete "$brouillon/$src-export.mid")"
    echo "    (H2) le projet       : $(entete "$brouillon/$src-projet/midi/arrangement.mid")"
    echo "         ses pistes      : $(pistes "$brouillon/$src-projet")"
    echo "         rouvert, réenregistré : $(entete "$brouillon/$src-projet2/midi/arrangement.mid")"
    echo "         ses pistes      : $(pistes "$brouillon/$src-projet2")"
done
