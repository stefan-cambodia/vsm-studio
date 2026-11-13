#!/usr/bin/env bash
# D637 — LE MUET D'UN RANG DE LA CARTE (la colonne *M* de la *Drum Map* de Cubase) : la case « M » d'une ligne cochée, les
# frappes de cette note ÉCRITE ne se jouent plus — ni à la lecture, ni à l'export audio (le planning est commun aux deux
# chemins, D21) ; le matériau ne change pas ; l'export MIDI garde les frappes et NOMME le muet parmi ce qu'il ne porte pas.
#
# LES RÈGLES GARDÉES (une piste « Batterie » sur la TR-909, canal 10, qui ne joue QUE des frappes 42 — quatre, une par
# temps). L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D637, (c) à (e)) :
#   (c) `carte-muet:42` puis l'export audio : la crête du WAV sous −90 dBFS ; le même export sans le geste : au-dessus de
#       −40 dBFS (le témoin de la règle) ; la carte relue dit « muet 42 » ; le projet porte "drumMuted": [42] ; un pas
#       « Muet de la carte » ; le projet garde ses quatre frappes ;
#   (d) le témoin (le binaire d'avant D637) : le verbe inconnu, l'export sonne ;
#   (e) l'export MIDI nomme les rangs muets ; l'infobulle de la case en anglais.
#
#   tools/muet-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D635/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-muet-par-rang.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
b = f"{racine}/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
evts = []
for debut, h in [(0, 42), (480, 42), (960, 42), (1440, 42)]:   # QUE des frappes 42 (charleston fermé)
    evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 120, 0, bytes([0x89, h, 0]))]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
    corps += vlq(tick - t) + e; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "projet", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; $4 = une variable de plus (VSM_LANGUE=en). Une copie neuve, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" plus="${4:-VSM_RIEN=1}" n
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=6000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" "$plus" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE|CLIC_POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les frappes d'un projet enregistré : « hauteur@début+durée », triées (le lecteur de `longueur-insertion.sh`).
frappes() {
    python3 - "$1/midi/arrangement.mid" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, ouvertes, i = [], {}, 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; statut = 0
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
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        h, vel = d[j], (d[j + 1] if taille == 2 else 0)
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY2
}
crete() {
    python3 - "$1" <<'PY2'
import math, os, struct, sys
p = sys.argv[1]
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None; ext = b""
while i + 8 <= len(d):
    nom, t = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24]); ext = d[i + 8 + 24:i + 8 + 26]
    if nom == b"data": data = d[i + 8:i + 8 + t]
    i += 8 + t + (t & 1)
if fmt is None or data is None: print("ABSENT"); sys.exit(0)
tag, bits = fmt[0], fmt[5]
if tag == 0xFFFE: tag = int.from_bytes(ext, "little")
m = 0.0
if tag == 3:
    for (v,) in struct.iter_unpack("<f", data[: len(data) // 4 * 4]): m = max(m, abs(v))
else:
    n = bits // 8; plein = float(1 << (bits - 1))
    for k in range(0, len(data) - n + 1, n): m = max(m, abs(int.from_bytes(data[k:k + n], "little", signed=True)) / plein)
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.2f}")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
sous() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v == '-inf' or (v not in ('', 'ABSENT') and float(v) < float(sys.argv[2])) else 1)" "$1" "$2" 2>/dev/null && echo 1 || echo 0; }
dessus() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v not in ('', '-inf', 'ABSENT') and float(v) > float(sys.argv[2]) else 1)" "$1" "$2" 2>/dev/null && echo 1 || echo 0; }
muets_projet() { python3 -c "import json, sys; t = json.load(open(sys.argv[1] + '/project.json'))['tracks'][0]; print(' '.join(map(str, t.get('drumMuted', []))) or 'aucun')" "$1" 2>/dev/null || echo ABSENT; }

echo "=== D637 : le muet d'un rang de la carte ==="
course "$BIN" temoin-regle "1500:exporter-audio:$brouillon/temoin-regle.wav"
course "$BIN" muet "700:carte-muet:42;1200:relever-carte;1400:relever-historique;1700:enregistrer:$brouillon/muet-projet;2000:exporter-audio:$brouillon/muet.wav;4500:exporter-midi:$brouillon/muet.mid"
course "$BIN" en "700:carte-muet:42;1200:relever-carte" VSM_LANGUE=en

CT="$(crete "$brouillon/temoin-regle.wav")"; CM="$(crete "$brouillon/muet.wav")"
PM="$(muets_projet "$brouillon/muet-projet")"; FM="$(frappes "$brouillon/muet-projet")"
H="$(grep -h "^VSM_HISTORIQUE" "$brouillon/muet.txt" | grep -o "Muet de la carte" | wc -l)"
echo "       export sans geste : $CT dBFS ; avec le rang 42 muet : $CM dBFS"
echo "       carte : $(grep -h "^VSM_CARTE : muet" "$brouillon/muet.txt" | tr '\n' ' ') ; projet : drumMuted $PM ; frappes $FM ; $H pas"
echo "       export MIDI : $(grep -h "rangs muets de la carte" "$brouillon/muet.txt" | head -1 | cut -c1-140)"
EN="$(grep -h "^VSM_CARTE : infobulle" "$brouillon/en.txt" | head -1)"

verdict "(c) le témoin de la règle : l'export sans geste sonne (au-dessus de -40 dBFS)" "$(dessus "$CT" -40)"
verdict "(c) le rang 42 muet : l'export se tait (sous -90 dBFS)" "$(sous "$CM" -90)"
verdict "(c) la carte relue dit « muet 42 »" "$(grep -q "^VSM_CARTE : muet 42$" "$brouillon/muet.txt" && echo 1 || echo 0)"
verdict "(c) le projet porte drumMuted [42] et garde ses quatre frappes" \
    "$( [ "$PM" = "42" ] && [ "$FM" = "42@0+120 42@480+120 42@960+120 42@1440+120" ] && echo 1 || echo 0)"
verdict "(c) un pas « Muet de la carte »" "$( [ "$H" = 1 ] && echo 1 || echo 0)"
verdict "(e) l'export MIDI nomme les rangs muets (4 frappes)" "$(grep -q "rangs muets de la carte (4 frappe(s))" "$brouillon/muet.txt" && echo 1 || echo 0)"
verdict "(e) l'infobulle de la case en anglais" "$(grep -q "^VSM_CARTE : infobulle M : Muted row: its hits no longer play" "$brouillon/en.txt" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    BINAIRE_T="$TEMOIN"
    course "$TEMOIN" t-muet "700:carte-muet:42;2000:exporter-audio:$brouillon/t-muet.wav"
    CTM="$(crete "$brouillon/t-muet.wav")"
    echo "       témoin : export $CTM dBFS ; $(grep -h "carte-muet" "$brouillon/t-muet.txt" | head -1)"
    verdict "(d) témoin (avant D637) : le verbe inconnu, l'export sonne" \
        "$( [ "$(dessus "$CTM" -40)" = 1 ] && grep -q "VSM_GESTE_APRES : carte-muet:42 .*refus" "$brouillon/t-muet.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "MUET-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MUET-PAR-RANG : un rang muet se tait à la lecture et à l'export, le fichier garde ses frappes"
