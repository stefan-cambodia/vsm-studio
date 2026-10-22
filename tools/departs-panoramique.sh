#!/usr/bin/env bash
# D572.2 — LES DÉPARTS QUI SUIVENT LE PANORAMIQUE (Mixage ▸ Les départs suivent le panoramique (tranches choisies) (N) ;
# les panoramiques de départ liés de Cubase), MESURÉS PAR L'EXPORT (D332) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. « Voix » joue un do de deux secondes, tournée À FOND À GAUCHE, fader au silence, un départ
# PRÉ-FADER à 1 vers un égaliseur neutre : on n'entend que le retour du bus.
#   (1) le témoin : l'export du projet d'origine rend les deux canaux du retour ÉGAUX (le départ prend le signal d'avant
#       le panoramique) — sans quoi « le droit tombe » ne prouverait rien ;
#   (2) le menu dit « … suivent … (1) » avant, « … ignorent … (1) » après ; le projet relu porte `sendsFollowPan` ;
#   (3) l'export du projet lié : le canal droit tombe au plancher (sous −100 dBFS), le gauche garde la crête du témoin
#       à 0,01 dB près ;
#   (4) rouvert, la tranche le dit (`VSM_MIXEUR` : « suivent le panoramique ») ;
#   (5) un pas : Ctrl+Z rend des départs qui ignorent le panoramique, après un geste qui a eu lieu.
#
#   tools/departs-panoramique.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-departs-pan.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x04Voix"
corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x8f, 0x00]) + bytes([0x80, 60, 0])   # do4 de 0 à 1 920 ticks (2 s)
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "sends": [{"name": "Neutre", "effect": "eq", "returnGain": 1.0, "preFader": True, "parameters": {}}],
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voix",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": -1.0, "solo": False, "volume": 0.0, "sends": [1.0]}}]},
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
lie() {   # « oui » si la piste suit le panoramique, « non », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print('oui' if d['tracks'][0]['mix'].get('sendsFollowPan') else 'non')" "$1"
}
cretes() {   # « gauche droite » en dBFS (« -inf » pour un canal muet), ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if x.size == 0 or x.shape[1] < 2:
    print("ABSENT"); sys.exit(0)
def db(c):
    c = float(np.max(np.abs(c)))
    return "-inf" if c <= 1e-12 else f"{20 * np.log10(c):.3f}"
print(db(x[:, 0]), db(x[:, 1]))
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Mixage > Les départs suivent le panoramique (tranches choisies) (1)"

echo "=== D572.2 : les départs qui suivent le panoramique ==="
for x in lie annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course lier "$brouillon/lie" VSM_DELAI=2500 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:$GESTE;1500:lister-menus;1800:enregistrer:$brouillon/lie"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"
course export-base "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base.wav"
course export-lie "$brouillon/lie" VSM_DELAI=1500 VSM_MIXEUR=1 VSM_EXPORT="$brouillon/lie.wav"

lb="$(lie "$brouillon/base")"; ll="$(lie "$brouillon/lie")"; la="$(lie "$brouillon/annule")"
cb="$(cretes "$brouillon/base.wav")"; cl="$(cretes "$brouillon/lie.wav")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Mixage > Les départs \(suivent\|ignorent\) le panoramique" "$brouillon/lier.txt" | sed 's/^VSM_MENU_LISTE : Mixage > //')
for x in "${m[@]}"; do echo "       menu : $x"; done
grep -h "^VSM_CONSOLE : d.parts li" "$brouillon/lier.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
tranche="$(grep -h "^VSM_MIXEUR : piste 0 " "$brouillon/export-lie.txt" | head -1)"
echo "       lié : base [$lb] ; après le menu [$ll] ; Ctrl+Z [$la]"
echo "       export (gauche droite, dBFS) : témoin [$cb] ; lié [$cl]"

verdict "(1) le témoin : le retour rend les deux canaux égaux, le panoramique n'y est pas" \
    "$(awk -v c="$cb" 'BEGIN { if (c == "ABSENT") { print 0; exit } split(c, C, " ")
        print (C[1] != "-inf" && C[1] + 0 > -60 && C[1] == C[2]) ? 1 : 0 }')"
verdict "(2) « … suivent … (1) », puis « … ignorent … (1) » ; le projet relu le porte" \
    "$([ "${#m[@]}" = 2 ] && [ "${m[0]}" = "Les départs suivent le panoramique (tranches choisies) (1)" ] \
       && [ "${m[1]}" = "Les départs ignorent le panoramique (tranches choisies) (1)" ] && [ "$ll" = oui ] && [ "$lb" = non ] && echo 1 || echo 0)"
verdict "(3) l'export lié : le droit au plancher, le gauche à la crête du témoin (0,01 dB)" \
    "$(awk -v c="$cl" -v b="$cb" 'BEGIN { if (c == "ABSENT" || b == "ABSENT") { print 0; exit }
        split(c, C, " "); split(b, B, " "); d = C[1] - B[1]
        print ((C[2] == "-inf" || C[2] + 0 < -100) && d < 0.01 && d > -0.01) ? 1 : 0 }')"
verdict "(4) rouvert, la tranche le dit" "$(echo "$tranche" | grep -q ", suivent le panoramique" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend des départs qui ignorent le panoramique, après un geste qui a eu lieu" \
    "$([ "$la" = non ] && grep -q "^VSM_CONSOLE : départs liés au panoramique sur 1 tranche(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DEPARTS-PANORAMIQUE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DEPARTS-PANORAMIQUE : liés, les départs portent le panoramique dans le bus, et l'export le dit"
