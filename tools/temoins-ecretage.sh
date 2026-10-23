#!/usr/bin/env bash
# D577.3 — REMETTRE LES TÉMOINS D'ÉCRÊTAGE À ZÉRO (Mixage ▸ Remettre les témoins d'écrêtage à zéro (N), le *Reset
# Meters* de la MixConsole de Cubase), MESURÉ PAR LE RELEVÉ DES VUMÈTRES, LE TÉMOIN D'UN CLIC SUR UNE SEULE À CÔTÉ.
#
# LES RÈGLES GARDÉES. Projet de trois pistes ; des crêtes d'un seul buffer (`VSM_MIXEUR_NIVEAU=…!`, la forme qui ne
# réarme pas) à +0,5 dBFS. L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D577) :
#   (1) deux tranches armées : le menu dit « (2) » ; après le geste, 0 témoin allumé (le relevé `VSM_VUMETRES`) et
#       l'entrée grisée « (0) » ;
#   (2) aucun pas : l'historique reste à 0 (un état de l'écran, D344, pas du morceau) ;
#   (3) le TÉMOIN, un clic sur le premier mètre seul : 1 allumé reste — la tranche 1 —, et le menu dit « (1) » ;
#   (4) le master compte : armé seul, le menu dit « (1) » ; après le geste, son témoin éteint.
#
#   tools/temoins-ecretage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-temoins-ecretage.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    corps += b"\x00" + bytes([0x90 | canal, 60, 100]) + vlq(480) + bytes([0x80 | canal, 60, 0]) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
noms = ["Un", "Deux", "Trois"]
os.makedirs(f"{b}/P/midi", exist_ok=True)
pistes = [piste(n, i, tempo=(i == 0)) for i, n in enumerate(noms)]
open(f"{b}/P/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + b"".join(pistes))
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": i, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": dict(mix),
                       "instrument": {"preferredPlugin": "vsm.minimoog"}} for i, n in enumerate(noms)]},
          open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/P" VSM_TAILLE="1280x742" VSM_VUMETRES=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|CLIC|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
entrees() { grep -h "^VSM_MENU_LISTE : Mixage > Remettre les témoins d'écrêtage à zéro" "$1" | sed 's/^VSM_MENU_LISTE : Mixage > //'; }
allumes() {   # « tranche:témoin » relevés à la photo, master compris
    grep -h "^VSM_VUMETRE : " "$1" | sed -n 's/^VSM_VUMETRE : \(tranche \)\?\([a-z0-9]*\) : .*écrête \([01]\).*/\2:\3/p' | tr '\n' ' ' | sed 's/ $//'
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REMETTRE="menu:Remettre les témoins d'écrêtage à zéro"

echo "=== D577.3 : remettre les témoins d'écrêtage à zéro ==="
course remise VSM_DELAI=2200 VSM_MIXEUR_NIVEAU="0:0.5!;1:0.5!" \
    VSM_GESTE_APRES="900:lister-menus;1100:$REMETTRE;1300:lister-menus;1500:relever-historique"
course clic VSM_DELAI=2000 VSM_MIXEUR_NIVEAU="0:0.5!;1:0.5!" \
    VSM_GESTE_APRES="1000:cliquer:mixeur.vumetre;1300:lister-menus"
course master VSM_DELAI=2200 VSM_MIXEUR_NIVEAU="master:0.5!" \
    VSM_GESTE_APRES="900:lister-menus;1100:$REMETTRE;1300:lister-menus"

e_r="$(entrees "$brouillon/remise.txt" | tr '\n' ';')"; e_c="$(entrees "$brouillon/clic.txt" | tr '\n' ';')"
e_m="$(entrees "$brouillon/master.txt" | tr '\n' ';')"
v_r="$(allumes "$brouillon/remise.txt")"; v_c="$(allumes "$brouillon/clic.txt")"; v_m="$(allumes "$brouillon/master.txt")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/remise.txt" | tail -1)"
grep -h "^VSM_TEMOINS_ECRETAGE\|^VSM_MIXEUR_NIVEAU" "$brouillon/remise.txt" "$brouillon/master.txt" | sed 's/^/       /'
echo "       deux tranches : menu [$e_r] ; témoins à la photo [$v_r] ; ${hist:-historique non relevé}"
echo "       un clic sur la première : menu [$e_c] ; témoins [$v_c]"
echo "       le master seul : menu [$e_m] ; témoins [$v_m]"

verdict "(1) deux tranches : (2), puis 0 allumé et (0) grisée" \
    "$([ "$e_r" = "Remettre les témoins d'écrêtage à zéro (2);Remettre les témoins d'écrêtage à zéro (0) [grisée];" ] \
        && [ "$v_r" = "0:0 1:0 2:0 master:0" ] \
        && grep -q "^VSM_TEMOINS_ECRETAGE : 2 éteint(s), 0 allumé(s)" "$brouillon/remise.txt" && echo 1 || echo 0)"
verdict "(2) aucun pas : l'historique reste à 0" "$([ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] && echo 1 || echo 0)"
verdict "(3) le témoin d'un clic sur la première : 1 allumé (la tranche 1), le menu dit (1)" \
    "$([ "$v_c" = "0:0 1:1 2:0 master:0" ] && [ "$e_c" = "Remettre les témoins d'écrêtage à zéro (1);" ] && echo 1 || echo 0)"
verdict "(4) le master compte : (1), puis éteint et (0) grisée" \
    "$([ "$e_m" = "Remettre les témoins d'écrêtage à zéro (1);Remettre les témoins d'écrêtage à zéro (0) [grisée];" ] \
        && [ "$v_m" = "0:0 1:0 2:0 master:0" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TEMOINS-ECRETAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TEMOINS-ECRETAGE : tous les témoins s'éteignent d'un geste, et le morceau n'en sait rien"
