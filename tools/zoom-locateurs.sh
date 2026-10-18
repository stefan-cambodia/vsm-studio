#!/usr/bin/env bash
# D565.2 — LE ZOOM SUR LES LOCATEURS (Affichage ▸ Zoom : entre les locateurs ; le *Zoom to Locators* de Cubase),
# MESURÉ PAR LA FENÊTRE RELEVÉE.
#
# LES RÈGLES GARDÉES. Un morceau de seize mesures, les locateurs sur les mesures 3 à 5 [3 840, 7 680).
#   (1) la fenêtre commence un vingtième de la plage avant le locateur gauche (3 648) et finit après le droit, à moins
#       de 15 % de la plage au-delà — la règle du zoom sur la sélection ;
#   (2) « Annuler le zoom » rend la vue d'avant (la largeur relevée d'avant) ;
#   (3) sans locateurs : l'entrée GRISÉE.
#
#   tools/zoom-locateurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zoom-loc.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for debut in range(0, 16 * 1920, 1920):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]) + vlq(480) + bytes([0x80, 60, 0])
    t = debut + 480
corps += b"\x00\xff\x2f\x00"
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
for nom, boucle in (("P", True), ("sans", False)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                       + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                 "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
    if boucle: transport["loop"] = {"enabled": False, "startTick": 3840, "endTick": 7680}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": transport,
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.juno106"},
                           "clips": [{"start": 0, "length": 16 * 1920, "sourceStart": 0, "sourceLength": 16 * 1920, "name": "A"}]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D565.2 : le zoom sur les locateurs ==="
course zoom "$brouillon/P" VSM_DELAI=2600 \
    VSM_GESTE_APRES="800:relever-arrangement;1100:menu:Affichage > Zoom : entre les locateurs;1400:relever-arrangement;1700:menu:Affichage > Annuler le zoom;2000:relever-arrangement"
course sans "$brouillon/sans" VSM_DELAI=1800 VSM_GESTE_APRES="900:lister-menus"

ligne="$(grep -h "^VSM_ZOOM_LOCATEURS" "$brouillon/zoom.txt" | head -1)"
mapfile -t fen < <(grep -h "^VSM_ARRANGEMENT : fenêtre" "$brouillon/zoom.txt" | sed 's/^VSM_ARRANGEMENT : fenêtre \([0-9.]*\) mesure.*/\1/')
grise="$(grep -h "^VSM_MENU_LISTE : Affichage > Zoom : entre les locateurs" "$brouillon/sans.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Affichage > //')"
echo "       $ligne"
echo "       fenêtres (mesures) : avant ${fen[0]:-?}, après ${fen[1]:-?}, annulé ${fen[2]:-?} ; sans locateurs : [$grise]"
de="$(echo "$ligne" | sed -n 's/.*fenêtre de \([0-9]*\) à \([0-9]*\) ticks/\1/p')"; a="$(echo "$ligne" | sed -n 's/.*fenêtre de \([0-9]*\) à \([0-9]*\) ticks/\2/p')"

verdict "(1) de 3 648 à un peu plus de 7 680" \
    "$([ "$de" = 3648 ] && [ -n "$a" ] && [ "$a" -ge 7680 ] && [ "$a" -le $((7680 + 576)) ] && echo 1 || echo 0)"
verdict "(2) « Annuler le zoom » rend la largeur d'avant" \
    "$([ -n "${fen[2]:-}" ] && [ "${fen[2]:-}" = "${fen[0]:-x}" ] && [ "${fen[1]:-}" != "${fen[0]:-}" ] && echo 1 || echo 0)"
verdict "(3) sans locateurs : grisée" "$([ "$grise" = "Zoom : entre les locateurs [grisée]" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZOOM-LOCATEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZOOM-LOCATEURS : l'arrangement cadre la plage des locateurs comme une sélection, et le zoom s'annule"
