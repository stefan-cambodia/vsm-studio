#!/usr/bin/env bash
# D559.2 — REMETTRE LA CONSOLE À ZÉRO (Mixage ▸ Remettre la console à zéro (N tranche(s))… ; le *Reset Mixer* de Cubase), MESURÉ PAR
# LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Deux pistes : « Basse » à 0,5 de volume, panoramique −0,5, un départ à 0,3, trim +6 dB, polarité
# inversée, et deux inserts dont un CONTOURNÉ ; « Nappe » muette et en solo.
#   (1) le menu dit « (2 tranche(s)) » ;
#   (2) confirmé (VSM_CONFIRMER=oui) : les deux tranches à l'état d'une piste neuve — volume 1, centre, ni muet ni solo,
#       pas de trim, polarité normale, départs à zéro —, les deux inserts GARDÉS et le contourné toujours contourné ;
#       la question posée dit « Les inserts restent (2) » ; dit « 2 tranche(s) remise(s) à zéro, 2 insert(s) laissé(s) » ;
#   (3) refusé (VSM_CONFIRMER=non) : rien ne change ;
#   (4) un pas : Ctrl+Z rend la console d'avant, après une remise qui a eu lieu ;
#   (5) le menu après : « (0 tranche(s)) [grisée] ».
#
#   tools/console-a-zero.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-console-zero.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
pistes = []
for canal, (nom, hauteur) in enumerate((("Basse", 40), ("Nappe", 64))):
    corps = b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += b"\x00" + bytes([0x90 | canal, hauteur, 100]) + b"\x8f\x00" + bytes([0x80 | canal, hauteur, 0]) + b"\x00\xff\x2f\x00"
    pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
tempo = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + b"MTrk" + struct.pack(">I", len(tempo)) + tempo + b"".join(pistes))
basse = {"muted": False, "pan": -0.5, "sends": [0.3, 0.0], "solo": False, "volume": 0.5, "trimDb": 6.0, "invertPhase": True}
nappe = {"muted": True, "pan": 0.0, "sends": [0.0, 0.0], "solo": True, "volume": 1.0}
effets = [{"type": "compressor", "parameters": {}}, {"type": "reverb", "parameters": {}, "enabled": False}]
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": effets, "name": "Basse", "mix": basse,
                       "instrument": {"preferredPlugin": "vsm.juno106"}},
                      {"channel": 1, "color": "#FF6B9BFF", "effects": [], "name": "Nappe", "mix": nappe,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
console() {   # la console de chaque piste d'un project.json relu, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
out = []
for t in d['tracks']:
    m = t.get('mix', {})
    eff = ','.join(e.get('type', '?') + ('' if e.get('enabled', True) else '(off)') for e in t.get('effects', []))
    out.append(f\"{t['name']}: v{m.get('volume', 1):g} p{m.get('pan', 0):g} {'M' if m.get('muted') else '-'}{'S' if m.get('solo') else '-'}\"
               f\" t{m.get('trimDb', 0):g} {'inv' if m.get('invertPhase') else 'nor'} d{'/'.join(f'{x:g}' for x in m.get('sends', []))} [{eff}]\")
print(' ; '.join(out))" "$1/project.json"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Mixage > Remettre la console à zéro (2 tranche(s))…"

echo "=== D559.2 : remettre la console à zéro ==="
for x in oui non z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course oui "$brouillon/oui" VSM_DELAI=2800 VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="800:lister-menus;1100:$GESTE;1500:lister-menus;1900:enregistrer:$brouillon/oui"
course non "$brouillon/non" VSM_DELAI=2400 VSM_CONFIRMER=non VSM_GESTE_APRES="1000:$GESTE;1500:enregistrer:$brouillon/non"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1000:$GESTE;1400:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
course temoin "$brouillon/base" VSM_DELAI=2000 VSM_GESTE_APRES="1200:enregistrer:$brouillon/base"

c0="$(console "$brouillon/base")"; co="$(console "$brouillon/oui")"; cn="$(console "$brouillon/non")"; cz="$(console "$brouillon/z")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Mixage > Remettre la console" "$brouillon/oui.txt" | sed 's/^VSM_MENU_LISTE : Mixage > //')
grep -h "^VSM_BOITE : Remettre\|^VSM_CONSOLE" "$brouillon/oui.txt" | sed 's/^/       /'
echo "       témoin  [$c0]"
echo "       oui     [$co]"
echo "       non     [$cn]"
echo "       Ctrl+Z  [$cz]"
for x in "${m[@]}"; do echo "       menu : $x"; done

NEUTRE="Basse: v1 p0 -- t0 nor d0/0 [compressor,reverb(off)] ; Nappe: v1 p0 -- t0 nor d0/0 []"
verdict "(1) le menu dit 2 tranches" "$(echo "${m[0]:-}" | grep -q "(2 tranche(s))…$" && echo 1 || echo 0)"
verdict "(2) confirmé : les tranches neuves, les inserts gardés (le contourné aussi), et c'est dit" \
    "$([ "$co" = "$NEUTRE" ] && grep -q "^VSM_CONSOLE : 2 tranche(s) remise(s) à zéro, 2 insert(s) laissé(s)" "$brouillon/oui.txt" \
       && grep -q "^VSM_BOITE : Remettre la console à zéro : .*Les inserts restent (2)" "$brouillon/oui.txt" && echo 1 || echo 0)"
verdict "(3) refusé : rien ne change" "$([ "$cn" = "$c0" ] && grep -q "^VSM_CONSOLE : remise à zéro refusée" "$brouillon/non.txt" && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend la console d'avant" \
    "$([ "$cz" = "$c0" ] && [ "$c0" != "$NEUTRE" ] && grep -q "^VSM_CONSOLE : 2 tranche(s) remise(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(5) le menu après : zéro, grisée" "$(echo "${m[1]:-}" | grep -q "(0 tranche(s))… \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CONSOLE-A-ZERO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CONSOLE-A-ZERO : la console revient à l'état neuf, les inserts restent, et un Ctrl+Z la rend"
