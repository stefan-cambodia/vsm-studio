#!/usr/bin/env bash
# D559.1 — LEVER TOUS LES SOLOS, TOUS LES MUETS (Mixage ▸ Lever tous les solos (N), Lever tous les muets (N) ; le
# *Deactivate All Solo / Mute* de Cubase), MESURÉ PAR LE PROJET RELU ET PAR L'EXPORT.
#
# LES RÈGLES GARDÉES. Quatre pistes, une note chacune, à des hauteurs différentes : A et B en solo, C muette, D muette
# et en solo PROTÉGÉ.
#   (1) le menu dit « (2) » solos et « (2) » muets ;
#   (2) « Lever tous les solos » : plus aucun solo au projet relu, C et D toujours muettes, D toujours protégée — dit
#       « 2 solo(s) levé(s) » ; et l'export NE CHANGE PAS (à 0,1 dB) : avant comme après, A et B sonnent, C et D se
#       taisent — le témoin qui dit que lever les solos n'a pas levé les muets ;
#   (3) « Lever tous les muets » ensuite : plus aucun muet, et l'export sonne plus fort que celui d'après (2) — les
#       deux muettes entrent ;
#   (4) un pas chacun : Ctrl+Z après (2) rend les solos ;
#   (5) le menu après les deux : « (0) [grisée] » l'un et l'autre.
#
#   tools/lever-solos-muets.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-lever.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
pistes = []
for canal, (nom, hauteur) in enumerate((("A", 60), ("B", 64), ("C", 67), ("D", 72))):
    corps = b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += b"\x00" + bytes([0x90 | canal, hauteur, 100]) + vlq(1920) + bytes([0x80 | canal, hauteur, 0]) + b"\x00\xff\x2f\x00"
    pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
tempo = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 5, 480) + b"MTrk" + struct.pack(">I", len(tempo)) + tempo + b"".join(pistes))
def mix(muted=False, solo=False, safe=False):
    m = {"muted": muted, "pan": 0.0, "sends": [0.0, 0.0], "solo": solo, "volume": 0.8}
    if safe: m["soloSafe"] = True
    return m
reglages = {"A": mix(solo=True), "B": mix(solo=True), "C": mix(muted=True), "D": mix(muted=True, safe=True)}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": reglages[n],
                       "instrument": {"preferredPlugin": "vsm.juno106"}} for c, n in enumerate("ABCD")]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
etats() {   # « nom:S/M/P » de chaque piste d'un project.json (S solo, M muet, P protégée), ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
out = []
for t in d['tracks']:
    m = t.get('mix', {})
    out.append(t['name'] + ':' + ('S' if m.get('solo') else '') + ('M' if m.get('muted') else '') + ('P' if m.get('soloSafe') else ''))
print(' '.join(out))" "$1/project.json"
}
rms() {   # la valeur efficace d'un export, en dBFS, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
r = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
print(f"{20 * np.log10(max(r, 1e-12)):.2f}")
PY2
}
plus_fort() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[1]) > float(sys.argv[2]) + 1.0 else 1)
except ValueError:
    sys.exit(1)" "$1" "$2"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SOLOS="menu:Mixage > Lever tous les solos (2)"
MUETS="menu:Mixage > Lever tous les muets (2)"

echo "=== D559.1 : lever tous les solos, tous les muets ==="
for x in s sm z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course solos "$brouillon/s" VSM_DELAI=2600 \
    VSM_GESTE_APRES="800:lister-menus;1100:$SOLOS;1500:enregistrer:$brouillon/s"
course les-deux "$brouillon/sm" VSM_DELAI=3000 \
    VSM_GESTE_APRES="1000:$SOLOS;1300:$MUETS;1700:lister-menus;2100:enregistrer:$brouillon/sm"
course annuler "$brouillon/z" VSM_DELAI=2800 \
    VSM_GESTE_APRES="1000:$SOLOS;1400:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
for x in base s sm; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done

e0="$(etats "$brouillon/base")"; es="$(etats "$brouillon/s")"; esm="$(etats "$brouillon/sm")"; ez="$(etats "$brouillon/z")"
r0="$(rms "$brouillon/base.wav")"; rs="$(rms "$brouillon/s.wav")"; rsm="$(rms "$brouillon/sm.wav")"
mapfile -t avant < <(grep -h "^VSM_MENU_LISTE : Mixage > Lever tous les" "$brouillon/solos.txt" | head -2 | sed 's/^VSM_MENU_LISTE : Mixage > //')
mapfile -t apres < <(grep -h "^VSM_MENU_LISTE : Mixage > Lever tous les" "$brouillon/les-deux.txt" | sed 's/^VSM_MENU_LISTE : Mixage > //')
grep -h "^VSM_CONSOLE" "$brouillon/solos.txt" "$brouillon/les-deux.txt" | sed 's/^/       /'
echo "       états : départ [$e0] ; solos levés [$es] ; les deux [$esm] ; Ctrl+Z [$ez]"
echo "       export (dBFS) : départ $r0 ; solos levés $rs ; les deux $rsm"
echo "       menu avant : ${avant[*]:-}"
echo "       menu après : ${apres[*]:-}"

verdict "(1) le menu dit 2 solos et 2 muets" \
    "$(echo "${avant[0]:-}" | grep -q "^Lever tous les solos (2)$" && echo "${avant[1]:-}" | grep -q "^Lever tous les muets (2)$" && echo 1 || echo 0)"
verdict "(2) les solos levés, les muets et la protection gardés, et c'est dit" \
    "$([ "$es" = "A: B: C:M D:MP" ] && grep -q "^VSM_CONSOLE : 2 solo(s) levé(s)" "$brouillon/solos.txt" \
       && python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) < 0.1 else 1)" "$r0" "$rs" 2>/dev/null \
       && echo 1 || echo 0)"
verdict "(3) puis les muets : plus rien, et l'export sonne plus fort (les deux muettes entrent)" \
    "$([ "$esm" = "A: B: C: D:P" ] && plus_fort "$rsm" "$rs" && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les solos" "$([ "$ez" = "$e0" ] && [ "$e0" = "A:S B:S C:M D:MP" ] && echo 1 || echo 0)"
verdict "(5) le menu après : zéro et grisés" \
    "$(echo "${apres[0]:-}" | grep -q "^Lever tous les solos (0) \[grisée\]$" && echo "${apres[1]:-}" | grep -q "^Lever tous les muets (0) \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LEVER-SOLOS-MUETS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LEVER-SOLOS-MUETS : tous les solos et tous les muets se lèvent d'un geste, la protection reste"
