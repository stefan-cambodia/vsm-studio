#!/usr/bin/env bash
# D567.3 — CONTOURNER LES DÉPARTS DES TRANCHES CHOISIES (Mixage ▸ Contourner les départs des tranches choisies (N) ; le
# *Bypass Sends* de Cubase), MESURÉ PAR L'EXPORT (D332 : une règle du son se pose dans `interchange`, et c'est l'export
# qui le prouve) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. « Voix » joue un do de deux secondes, fader au silence, un départ PRÉ-FADER à 1 vers une
# réverbération : on ne l'entend QUE par le retour du bus. « Témoin » a un départ à 0,5 et n'est pas choisie.
#   (1) le témoin : l'export du projet d'origine SONNE (le retour) — sans quoi « silence » ne prouverait rien ;
#   (2) le menu dit « Contourner … (1) » avant, « Rétablir … (1) » après ; le projet relu : Voix contournée, son niveau
#       GARDÉ à 1, Témoin intacte ;
#   (3) l'export du projet contourné : le PLANCHER de ce projet — celui de la même Voix au départ à ZÉRO, à 1 dB près.
#       Le plancher n'est pas un zéro numérique (−138,5 dBFS mesuré le 22/10) : on le mesure plutôt que de le supposer ;
#   (4) rouvert, la tranche le dit (le relevé `VSM_MIXEUR` : « contournés ») ;
#   (5) un pas : Ctrl+Z rend des départs non contournés, après un geste qui a eu lieu.
#
#   tools/departs-contournes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-departs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, notes, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    if notes:
        corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x8f, 0x00]) + bytes([0x80, 60, 0])   # do4 de 0 à 1 920 ticks (2 s)
    return b"MTrk" + struct.pack(">I", len(corps + b"\x00\xff\x2f\x00")) + corps + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Voix", True, tempo=True) + piste("Temoin", False))
def p(nom, canal, volume, depart):
    return {"channel": canal, "color": "#FF6B9BFF", "effects": [], "name": nom,
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": volume, "sends": [depart]}}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "sends": [{"name": "Reverb", "effect": "reverb", "returnGain": 1.0, "preFader": True, "parameters": {}}],
           "tracks": [p("Voix", 0, 0.0, 1.0), p("Temoin", 1, 1.0, 0.5)]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
departs() {   # « nom:niveaux:contourné » de chaque piste, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{t['name']}:{','.join(format(s, 'g') for s in t['mix'].get('sends', []))}:{'oui' if t['mix'].get('sendsBypassed') else 'non'}\" for t in d['tracks']))" "$1"
}
crete() {   # crête de l'export en dBFS, « -inf » s'il est muet, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
c = float(np.max(np.abs(x))) if x.size else 0.0
print("-inf" if c <= 1e-9 else f"{20 * np.log10(c):.1f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Mixage > Contourner les départs des tranches choisies (1)"

echo "=== D567.3 : contourner les départs des tranches choisies ==="
for x in contourne annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
cp -r "$brouillon/base" "$brouillon/zero"
python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p))
d['tracks'][0]['mix']['sends'] = [0.0]
json.dump(d, open(p, 'w'), indent=1)" "$brouillon/zero"
course contourner "$brouillon/contourne" VSM_DELAI=2500 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:$GESTE;1500:lister-menus;1800:enregistrer:$brouillon/contourne"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"
course export-base "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base.wav"
course export-contourne "$brouillon/contourne" VSM_DELAI=1500 VSM_MIXEUR=1 VSM_EXPORT="$brouillon/contourne.wav"
course export-zero "$brouillon/zero" VSM_DELAI=1500 VSM_EXPORT="$brouillon/zero.wav"

db="$(departs "$brouillon/base")"; dc="$(departs "$brouillon/contourne")"; dz="$(departs "$brouillon/annule")"
cb="$(crete "$brouillon/base.wav")"; cc="$(crete "$brouillon/contourne.wav")"; c0="$(crete "$brouillon/zero.wav")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Mixage > \(Contourner\|Rétablir\) les départs" "$brouillon/contourner.txt" | sed 's/^VSM_MENU_LISTE : Mixage > //')
for x in "${m[@]}"; do echo "       menu : $x"; done
grep -h "^VSM_CONSOLE : d" "$brouillon/contourner.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
tranche="$(grep -h "^VSM_MIXEUR : piste 0 " "$brouillon/export-contourne.txt" | head -1)"
echo "       départs : base [$db] ; contournés [$dc] ; Ctrl+Z [$dz]"
echo "       export : témoin $cb dBFS, contourné $cc dBFS, plancher (départ à zéro) $c0 dBFS"

verdict "(1) le témoin : l'export du projet d'origine sonne par le retour du bus" \
    "$(awk -v c="$cb" 'BEGIN { print (c != "ABSENT" && c != "-inf" && c + 0 > -60) ? 1 : 0 }')"
verdict "(2) « Contourner … (1) », puis « Rétablir … (1) » ; Voix contournée, niveau gardé ; Témoin intacte" \
    "$([ "${#m[@]}" = 2 ] && [ "${m[0]}" = "Contourner les départs des tranches choisies (1)" ] \
       && [ "${m[1]}" = "Rétablir les départs des tranches choisies (1)" ] \
       && [ "$dc" = "Voix:1:oui Temoin:0.5:non" ] && echo 1 || echo 0)"
verdict "(3) l'export du projet contourné : le plancher du départ à zéro, à 1 dB près" \
    "$(awk -v c="$cc" -v z="$c0" 'BEGIN { if (c == "ABSENT" || z == "ABSENT") { print 0; exit }
        if (c == "-inf" || z == "-inf") { print (c == z) ? 1 : 0; exit }
        d = c - z; print (d < 1 && d > -1 && z + 0 < -60) ? 1 : 0 }')"
verdict "(4) rouvert, la tranche le dit" "$(echo "$tranche" | grep -q ", contournés" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend les départs, après un geste qui a eu lieu" \
    "$([ "$dz" = "$db" ] && grep -q "^VSM_CONSOLE : départs contournés sur 1 tranche(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DEPARTS-CONTOURNES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DEPARTS-CONTOURNES : la piste contournée ne verse plus rien, ses niveaux gardés, et l'export le dit"
