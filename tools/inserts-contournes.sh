#!/usr/bin/env bash
# D568.3 — CONTOURNER LES INSERTS DES TRANCHES CHOISIES (Mixage ▸ Contourner les inserts des tranches choisies (N) ; le
# *Bypass Inserts* de la MixConsole de Cubase), MESURÉ PAR L'EXPORT ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. « Voix » joue un do de deux secondes à travers une distorsion et une réverbération ; « Autre »,
# un mi à travers une réverbération, n'est pas choisie.
#   (1) le menu dit « Contourner … (1) » avant, « Rétablir … (1) » après ; le projet relu : les deux inserts de Voix
#       à `enabled` faux, celui d'Autre intact ;
#   (2) l'export du projet contourné par le menu est, au bit près, celui du même projet contourné À LA MAIN (`enabled`
#       faux écrit dans le fichier, le chemin de D15.1) — et il DIFFÈRE de l'export d'origine (le témoin) ;
#   (3) le MOTEUR : l'instance vivante de chaque insert (`relever-inserts`) — Voix « oo » avant le geste (le témoin),
#       « xx » après, Autre « o » ; l'export ne le prouve pas, il reconstruit sa chaîne depuis le fichier ;
#   (4) un pas : Ctrl+Z rend les inserts actifs, dans le fichier et dans le moteur, après un geste qui a eu lieu.
#
#   tools/inserts-contournes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-inserts.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, h, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += bytes([0x00, 0x90, h, 100, 0x8f, 0x00, 0x80, h, 0]) + b"\x00\xff\x2f\x00"   # 1 920 ticks
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def projet(dossier, contourne):
    os.makedirs(f"{dossier}/midi", exist_ok=True)
    open(f"{dossier}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Voix", 60, tempo=True) + piste("Autre", 64))
    fx = lambda t: dict({"type": t, "parameters": {}}, **({"enabled": False} if contourne else {}))
    def p(nom, canal, effets):
        return {"channel": canal, "color": "#FF6B9BFF", "name": nom, "effects": effets,
                "instrument": {"preferredPlugin": "vsm.minimoog"},
                "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 0.8}}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [p("Voix", 0, [fx("distortion"), fx("reverb")]),
                          p("Autre", 1, [{"type": "reverb", "parameters": {}}])]},
              open(f"{dossier}/project.json", "w"), indent=1)
projet(f"{b}/base", False)
projet(f"{b}/main", True)    # contourné À LA MAIN : `enabled` faux dans le fichier (D15.1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
inserts() {   # « nom:actif,actif » de chaque piste (o = actif, x = contourné), ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{t['name']}:{''.join('o' if e.get('enabled', True) else 'x' for e in t.get('effects', []))}\" for t in d['tracks']))" "$1"
}
ecart() {   # crête de la différence de deux exports en dBFS (« -inf » s'ils sont identiques), ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if a.size == 0 or b.size == 0:
    print("ABSENT"); sys.exit(0)
n = min(len(a), len(b))
d = float(np.max(np.abs(a[:n] - b[:n]))) if n else 0.0
print("-inf" if d == 0.0 and len(a) == len(b) else f"{20 * np.log10(max(d, 1e-300)):.1f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Mixage > Contourner les inserts des tranches choisies (1)"

echo "=== D568.3 : contourner les inserts des tranches choisies ==="
for x in contourne annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course contourner "$brouillon/contourne" VSM_DELAI=2500 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="800:relever-inserts;900:lister-menus;1200:$GESTE;1400:relever-inserts;1500:lister-menus;1800:enregistrer:$brouillon/contourne"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;1900:relever-inserts;2100:enregistrer:$brouillon/annule"
course export-base "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base.wav"
course export-contourne "$brouillon/contourne" VSM_DELAI=1500 VSM_EXPORT="$brouillon/contourne.wav"
course export-main "$brouillon/main" VSM_DELAI=1500 VSM_EXPORT="$brouillon/main.wav"

ic="$(inserts "$brouillon/contourne")"; iz="$(inserts "$brouillon/annule")"
e_main="$(ecart "$brouillon/contourne.wav" "$brouillon/main.wav")"; e_base="$(ecart "$brouillon/contourne.wav" "$brouillon/base.wav")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Mixage > \(Contourner\|Rétablir\) les inserts" "$brouillon/contourner.txt" | sed 's/^VSM_MENU_LISTE : Mixage > //')
for x in "${m[@]}"; do echo "       menu : $x"; done
grep -h "^VSM_CONSOLE : inserts\|^VSM_INSERTS" "$brouillon/contourner.txt" | sed 's/^/       /'
mapfile -t v < <(grep -h "^VSM_INSERTS : piste 0 " "$brouillon/contourner.txt" | sed 's/.*moteur //')
vz="$(grep -h "^VSM_INSERTS : piste 0 " "$brouillon/annuler.txt" | tail -1 | sed 's/.*description //')"
va="$(grep -h "^VSM_INSERTS : piste 1 " "$brouillon/contourner.txt" | tail -1 | sed 's/.*moteur //')"
echo "       inserts : contournés [$ic] ; Ctrl+Z [$iz]"
echo "       écart de l'export contourné : au contourné à la main $e_main dBFS, à l'origine $e_base dBFS"

verdict "(1) « Contourner … (1) », puis « Rétablir … (1) » ; Voix contournée, Autre intacte" \
    "$([ "${#m[@]}" = 2 ] && [ "${m[0]}" = "Contourner les inserts des tranches choisies (1)" ] \
       && [ "${m[1]}" = "Rétablir les inserts des tranches choisies (1)" ] && [ "$ic" = "Voix:xx Autre:o" ] && echo 1 || echo 0)"
verdict "(2) l'export contourné par le menu = contourné à la main, au bit près ; et il diffère de l'origine" \
    "$([ "$e_main" = "-inf" ] && awk -v e="$e_base" 'BEGIN { exit !(e != "ABSENT" && e != "-inf" && e + 0 > -60) }' && echo 1 || echo 0)"
verdict "(3) le moteur : Voix oo avant, xx après ; Autre o" \
    "$([ "${#v[@]}" = 2 ] && [ "${v[0]}" = "oo" ] && [ "${v[1]}" = "xx" ] && [ "$va" = "o" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les inserts actifs, fichier et moteur, après un geste qui a eu lieu" \
    "$([ "$iz" = "Voix:oo Autre:o" ] && [ "$vz" = "oo, moteur oo" ] && grep -q "^VSM_CONSOLE : inserts contournés sur 1 tranche(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "INSERTS-CONTOURNES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "INSERTS-CONTOURNES : les inserts des tranches choisies se contournent comme à la main, en un pas"
