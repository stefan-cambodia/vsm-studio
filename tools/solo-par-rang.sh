#!/usr/bin/env bash
# D640 — LE SOLO D'UN RANG DE LA CARTE (le *Solo Instrument* de l'éditeur de batterie de Cubase) : la case « S » d'une ligne
# cochée, seuls les rangs en solo de la piste se jouent — lecture, chasse, export audio (le planning commun, D21) ; le muet
# l'emporte ; les autres pistes ne sont pas touchées ; le piano roll pâlit ce qui se tait (D638).
#
# LES RÈGLES GARDÉES (une piste « Batterie », TR-909, canal 10 : 36@0, 42@480, 36@960, 42@1440). L'ATTENDU ÉCRIT AVANT LE
# CODE (ROADMAP-daw, D640, (c) à (e)) :
#   (c) l'export d'un seul rang en solo (`carte-solo:42`) a la crête de l'export où l'on a rendu 36 muet (`carte-muet:36`), à
#       0,5 dB près, et DIFFÈRE de l'export sans geste (sinon la mesure ne discrimine pas, et c'est dit) ; la carte relue dit
#       « solo 42 » ; "drumSolo": [42] ; un pas « Solo de la carte » ; la 36 « muette » au relevé des losanges, la 42 non ;
#   (d) le témoin (le binaire d'avant D640) : le verbe inconnu ;
#   (e) l'export MIDI nomme le solo ; l'infobulle en anglais.
#
#   tools/solo-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D637/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-solo-par-rang.XXXXXX")"
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
for debut, h in [(0, 36), (480, 42), (960, 36), (1440, 42)]:   # grosse caisse et charleston, en alternance
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
ecart() { python3 -c "import sys; a, b = sys.argv[1:3]; print('NA' if 'ABSENT' in (a, b) or '' in (a, b) or '-inf' in (a, b) else f'{abs(float(a) - float(b)):.2f}')" "$1" "$2"; }
inferieur() { python3 -c "import sys; v, s = sys.argv[1:3]; sys.exit(0 if v != 'NA' and float(v) <= float(s) else 1)" "$1" "$2" 2>/dev/null && echo 1 || echo 0; }
superieur() { python3 -c "import sys; v, s = sys.argv[1:3]; sys.exit(0 if v != 'NA' and float(v) > float(s) else 1)" "$1" "$2" 2>/dev/null && echo 1 || echo 0; }
solos_projet() { python3 -c "import json, sys; t = json.load(open(sys.argv[1] + '/project.json'))['tracks'][0]; print(' '.join(map(str, t.get('drumSolo', []))) or 'aucun')" "$1" 2>/dev/null || echo ABSENT; }

echo "=== D640 : le solo d'un rang de la carte ==="
course "$BIN" sans "1500:exporter-audio:$brouillon/sans.wav"
course "$BIN" muet36 "700:carte-muet:36;2000:exporter-audio:$brouillon/muet36.wav"
course "$BIN" solo "700:carte-solo:42;1200:relever-carte;1400:relever-losanges;1600:relever-historique;1800:enregistrer:$brouillon/solo-projet;2100:exporter-audio:$brouillon/solo.wav;4500:exporter-midi:$brouillon/solo.mid"
course "$BIN" en "700:carte-solo:42;1200:relever-carte" VSM_LANGUE=en

CS="$(crete "$brouillon/sans.wav")"; CM="$(crete "$brouillon/muet36.wav")"; CO="$(crete "$brouillon/solo.wav")"
E_SM="$(ecart "$CO" "$CM")"; E_SS="$(ecart "$CO" "$CS")"
PS="$(solos_projet "$brouillon/solo-projet")"
H="$(grep -h "^VSM_HISTORIQUE_PAS" "$brouillon/solo.txt" | tail -1 | grep -o "Solo de la carte" | wc -l)"
MUETTES="$(grep "^VSM_LOSANGE : note .* ; muette$" "$brouillon/solo.txt" | sed -E 's/^VSM_LOSANGE : note ([0-9]+) .*/\1/' | sort -u | tr '\n' ' ' | sed 's/ $//')"
echo "       crêtes : sans geste $CS ; 36 muet $CM ; 42 en solo $CO dBFS (écart solo/muet $E_SM dB, solo/sans $E_SS dB)"
echo "       carte : $(grep -h "^VSM_CARTE : solo" "$brouillon/solo.txt" | tr '\n' ' ') ; projet drumSolo $PS ; $H pas ; muettes [$MUETTES] ; rang 36 « $(grep -h "^VSM_LOSANGE : rang 36 : " "$brouillon/solo.txt" | sed 's/^VSM_LOSANGE : rang 36 : //') »"

verdict "(c) un rang en solo sonne comme l'autre rendu muet (0,5 dB près)" "$(inferieur "$E_SM" 0.5)"
if [ "$(superieur "$E_SS" 0.5)" = 1 ]; then
    verdict "(c) et diffère de l'export sans geste (la mesure discrimine)" 1
else
    verdict "(c) et diffère de l'export sans geste — NON : la crête ne discrimine pas, la mesure ne conclut pas" 0
fi
verdict "(c) la carte relue dit « solo 42 »" "$(grep -q "^VSM_CARTE : solo 42$" "$brouillon/solo.txt" && echo 1 || echo 0)"
verdict "(c) le projet porte drumSolo [42], un pas « Solo de la carte »" "$( [ "$PS" = "42" ] && [ "$H" = 1 ] && echo 1 || echo 0)"
verdict "(c) la 36 pâlie (« muette » au relevé), la 42 non" "$( [ "$MUETTES" = "36" ] && echo 1 || echo 0)"
verdict "(e) l'export MIDI nomme le solo (2 frappes hors du solo)" "$(grep -q "solo de la carte (2 frappe(s) hors du solo)" "$brouillon/solo.txt" && echo 1 || echo 0)"
verdict "(e) l'infobulle de la case en anglais" "$(grep -q "^VSM_CARTE : infobulle S : Soloed row: only the soloed rows" "$brouillon/en.txt" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-solo "700:carte-solo:42;1200:relever-carte"
    echo "       témoin : $(grep -h "carte-solo" "$brouillon/t-solo.txt" | head -1)"
    verdict "(d) témoin (avant D640) : le verbe inconnu" "$(grep -q "VSM_GESTE_APRES : carte-solo:42 .*refus" "$brouillon/t-solo.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "SOLO-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SOLO-PAR-RANG : seul le rang en solo joue, le muet l'emporte, et c'est dit"
