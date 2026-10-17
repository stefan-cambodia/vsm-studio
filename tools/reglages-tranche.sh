#!/usr/bin/env bash
# D562.3 — COPIER ET COLLER LES RÉGLAGES D'UNE TRANCHE (Piste ▸ Copier les réglages de la tranche, puis Coller les réglages
# sur les pistes choisies (N) ; le *Copy / Paste Channel Settings* de Cubase), MESURÉ PAR LE PROJET RELU ET LES STEMS.
#
# LES RÈGLES GARDÉES. Trois pistes : « Source » (trim +6 dB, phase inversée, départs 0,3 / 0, une réverbération
# CONTOURNÉE, volume 0,5, panoramique −0,3), « B » et « C » (neutres, volume 0,2 et 0,25, panoramique 0,4 et −0,2,
# chacune une note).
#   (1) collé sur B et C : le menu disait « (2) » ; leur trim, leur phase, leurs départs et leur chaîne sont ceux de la
#       Source ; leur volume et leur panoramique les leurs ;
#   (2) le stem de B monte de 6,02 dB (à 0,1) : le trim collé s'entend (la phase ne change pas une valeur efficace, la
#       réverbération est contournée) ;
#   (3) un pas : Ctrl+Z rend B et C neutres, après un geste qui a eu lieu ;
#   (4) rien de copié : « Coller les réglages (rien de copié) » GRISÉE ; et la Source seule choisie, après copie :
#       « (0) » grisée — elle a déjà ses réglages.
#
#   tools/reglages-tranche.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tranche.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, canal, note, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    if note:
        corps += b"\x00" + bytes([0x90 | canal, note, 100]) + b"\x8f\x00" + bytes([0x80 | canal, note, 0])
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + piste("Source", 0, 0, tempo=True) + piste("B", 1, 57) + piste("C", 2, 64))
def mix(volume, pan, **x):
    return dict({"muted": False, "pan": pan, "sends": [0.0, 0.0], "solo": False, "volume": volume}, **x)
def entree(nom, canal, m, effets):
    return {"channel": canal, "color": "#FF6B9BFF", "effects": effets, "name": nom, "mix": m,
            "instrument": {"preferredPlugin": "vsm.minimoog"}}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Source", 0, mix(0.5, -0.3, trimDb=6.0, invertPhase=True, sends=[0.3, 0.0]),
                             [{"type": "reverb", "parameters": {}, "enabled": False}]),
                      entree("B", 1, mix(0.2, 0.4), []), entree("C", 2, mix(0.25, -0.2), [])]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tranches() {   # « nom:trim/phase/départs/inserts/volume/pan » de chaque piste, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
out = []
for t in d['tracks']:
    m = t.get('mix', {})
    fx = ','.join(e['type'] + ('-' if e.get('enabled') is False else '') for e in t.get('effects', []))
    dep = ','.join(f'{v:g}' for v in m.get('sends', []))
    out.append(f\"{t['name']}:{m.get('trimDb', 0):g}/{'inv' if m.get('invertPhase') else 'nor'}/{dep}/[{fx}]/{m.get('volume'):g}/{m.get('pan'):g}\")
print(' '.join(out))" "$1"
}
stem() {   # la valeur efficace (dBFS) du stem dont le nom FINIT par $2, ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY2'
import glob, sys
import numpy as np
import soundfile as sf
f = [x for x in glob.glob(sys.argv[1] + "/*.wav") if x.split("/")[-1][:-4].endswith(sys.argv[2])]   # « … B.wav », pas un bus
if len(f) != 1:
    print("ABSENT"); sys.exit(0)
x, _ = sf.read(f[0], dtype="float64", always_2d=True)
print(f"{20 * np.log10(max(float(np.sqrt(np.mean(x ** 2))), 1e-12)):.2f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
COPIER="menu:Piste > Copier les réglages de la tranche"
COLLER="menu:Piste > Coller les réglages sur les pistes choisies (2)"
CHOISIR="1000:touche:alt + cursor down;1200:touche:shift + alt + cursor down"

echo "=== D562.3 : copier et coller les réglages d'une tranche ==="
for x in c z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course coller "$brouillon/c" VSM_DELAI=3200 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="700:$COPIER;$CHOISIR;1500:lister-menus;1800:$COLLER;2300:enregistrer:$brouillon/c"
course annuler "$brouillon/z" VSM_DELAI=3200 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="700:$COPIER;$CHOISIR;1800:$COLLER;2200:touche:ctrl + Z;2700:enregistrer:$brouillon/z"
course rien "$brouillon/base" VSM_DELAI=2200 VSM_VUE="sans-rapport,piste:0" VSM_GESTE_APRES="900:lister-menus;1200:$COPIER;1500:lister-menus"
mkdir -p "$brouillon/stems-temoin" "$brouillon/stems-c"
course stems-temoin "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT_STEMS="$brouillon/stems-temoin"
course stems-c "$brouillon/c" VSM_DELAI=1500 VSM_EXPORT_STEMS="$brouillon/stems-c"

tb="$(tranches "$brouillon/base")"; tc="$(tranches "$brouillon/c")"; tz="$(tranches "$brouillon/z")"
avant="$(grep -h "^VSM_MENU_LISTE : Piste > Coller les réglages" "$brouillon/coller.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Piste > //')"
mapfile -t rien < <(grep -h "^VSM_MENU_LISTE : Piste > Coller les réglages" "$brouillon/rien.txt" | sed 's/^VSM_MENU_LISTE : Piste > //')
sb="$(stem "$brouillon/stems-temoin" B)"; sc="$(stem "$brouillon/stems-c" B)"
grep -h "^VSM_TRANCHE" "$brouillon/coller.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       départ : [$tb]"
echo "       collé  : [$tc]"
echo "       Ctrl+Z : [$tz]"
echo "       menu : [$avant] ; rien de copié [${rien[0]:-}] ; Source seule après copie [${rien[1]:-}] ; stem B : $sb → $sc dBFS"

verdict "(1) B et C reçoivent trim, phase, départs, inserts ; gardent volume et panoramique" \
    "$([ "$avant" = "Coller les réglages sur les pistes choisies (2)" ] \
        && [ "$tc" = "Source:6/inv/0.3,0/[reverb-]/0.5/-0.3 B:6/inv/0.3,0/[reverb-]/0.2/0.4 C:6/inv/0.3,0/[reverb-]/0.25/-0.2" ] && echo 1 || echo 0)"
verdict "(2) le stem de B monte de 6,02 dB" \
    "$(python3 -c "
import sys
try:
    sys.exit(0 if abs(float(sys.argv[2]) - float(sys.argv[1]) - 6.02) <= 0.1 else 1)
except ValueError:
    sys.exit(1)" "$sb" "$sc" && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend B et C neutres, après un geste qui a eu lieu" \
    "$([ "$tz" = "$tb" ] && grep -q "^VSM_TRANCHE : r.glages coll.s sur 2 piste" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) rien de copié, puis la Source seule : grisées" \
    "$(echo "${rien[0]:-}" | grep -q "^Coller les réglages (rien de copié) \[grisée\]" \
        && echo "${rien[1]:-}" | grep -q "^Coller les réglages sur les pistes choisies (0) \[grisée\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REGLAGES-TRANCHE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REGLAGES-TRANCHE : le son d'une tranche se copie sur d'autres, leur balance reste la leur"
