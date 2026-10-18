#!/usr/bin/env bash
# D564.2 — REMETTRE À ZÉRO LES TRANCHES CHOISIES (Mixage ▸ Remettre les tranches choisies à zéro (N)… ; le *Reset
# Selected Channels* de Cubase), MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Trois pistes réglées pareil (volume 0,3, panoramique 0,5, trim +2, un départ à 0,5, une
# réverbération en insert). B et C choisies (B, puis Maj+Alt+↓).
#   (1) le menu dit « (2) » ; confirmé : B et C à l'état d'une piste neuve, leur insert gardé ; A n'a pas bougé ;
#   (2) refusé : rien ne change ;
#   (3) un pas : Ctrl+Z rend B et C, après un geste qui a eu lieu.
#
#   tools/tranches-choisies.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tranches.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + piste("A", tempo=True) + piste("B") + piste("C"))
mix = {"muted": False, "pan": 0.5, "sends": [0.5, 0.0], "solo": False, "volume": 0.3, "trimDb": 2.0}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "effects": [{"type": "reverb", "parameters": {}}], "name": n,
                       "mix": dict(mix), "instrument": {"preferredPlugin": "vsm.minimoog"}} for c, n in enumerate("ABC")]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tranches() {   # « nom:volume/pan/trim/départ0/inserts » de chaque piste, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
out = []
for t in d['tracks']:
    m = t.get('mix', {})
    out.append(f\"{t['name']}:{m.get('volume'):g}/{m.get('pan'):g}/{m.get('trimDb', 0):g}/{(m.get('sends') or [0])[0]:g}/{len(t.get('effects', []))}\")
print(' '.join(out))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REMETTRE="menu:Mixage > Remettre les tranches choisies à zéro (2)…"
CHOISIR="700:touche:shift + alt + cursor down"

echo "=== D564.2 : remettre à zéro les tranches choisies ==="
for x in oui non z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course oui "$brouillon/oui" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:1" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="$CHOISIR;1000:lister-menus;1300:$REMETTRE;1900:enregistrer:$brouillon/oui"
course non "$brouillon/non" VSM_DELAI=2400 VSM_VUE="sans-rapport,piste:1" VSM_CONFIRMER=non \
    VSM_GESTE_APRES="$CHOISIR;1200:$REMETTRE;1700:enregistrer:$brouillon/non"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:1" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="$CHOISIR;1200:$REMETTRE;1700:touche:ctrl + Z;2200:enregistrer:$brouillon/z"

tb="$(tranches "$brouillon/base")"; to="$(tranches "$brouillon/oui")"; tn="$(tranches "$brouillon/non")"; tz="$(tranches "$brouillon/z")"
entree="$(grep -h "^VSM_MENU_LISTE : Mixage > Remettre les tranches choisies" "$brouillon/oui.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Mixage > //')"
grep -h "^VSM_CONSOLE" "$brouillon/oui.txt" "$brouillon/non.txt" | sed 's/^/       /'
echo "       départ : [$tb]"
echo "       oui    : [$to]  — menu [$entree]"
echo "       non    : [$tn] ; Ctrl+Z : [$tz]"

verdict "(1) « (2) » ; B et C neuves, leur insert gardé ; A intacte" \
    "$([ "$entree" = "Remettre les tranches choisies à zéro (2)…" ] && [ "$to" = "A:0.3/0.5/2/0.5/1 B:1/0/0/0/1 C:1/0/0/0/1" ] && echo 1 || echo 0)"
verdict "(2) refusé : rien ne change" "$([ "$tn" = "$tb" ] && grep -q "^VSM_CONSOLE : remise des tranches choisies refus" "$brouillon/non.txt" && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend B et C, après un geste qui a eu lieu" \
    "$([ "$tz" = "$tb" ] && grep -q "^VSM_CONSOLE : 2 tranche(s) choisie(s) remise(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TRANCHES-CHOISIES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TRANCHES-CHOISIES : seules les tranches choisies reviennent à l'état d'une piste neuve"
