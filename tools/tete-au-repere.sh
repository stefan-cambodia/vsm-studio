#!/usr/bin/env bash
# D565.1 — LA TÊTE AU REPÈRE 1 À 9 (Transport ▸ Tête au repère ▸ N, le pavé numérique ; le *To Marker 1–9* de Cubase),
# MESURÉE PAR LE JOURNAL DE LA TÊTE.
#
# LES RÈGLES GARDÉES. Trois repères, écrits DANS LE DÉSORDRE au projet : « Refrain » à 3 840, « Intro » à 960,
# « Couplet » à 1 920.
#   (1) le pavé 2 : la tête au DEUXIÈME dans l'ordre du temps — « Couplet », 1 920 ; le pavé 1 : 960 ;
#   (2) par le menu, « 3 — Refrain » : 3 840 ; le sous-menu nomme chaque repère, et grise 4 à 9 ;
#   (3) le pavé 5 : « pas de repère 5 (3 repère(s)) », la tête reste.
#
#   tools/tete-au-repere.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-repere.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 3840, "name": "Refrain"}, {"tick": 960, "name": "Intro"}, {"tick": 1920, "name": "Couplet"}],
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/P" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
sauts() { grep -h "^VSM_TETE_REPERE : " "$brouillon/$1.txt" | sed -n -e 's/.* → \([0-9]*\)$/\1/p' -e 's/^VSM_TETE_REPERE : pas de .*/aucun/p' | tr '\n' ' ' | sed 's/ $//'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D565.1 : la tête au repère 1 à 9 ==="
course pave VSM_DELAI=2400 VSM_GESTE_APRES="1000:touche:numpad 2;1400:touche:numpad 1;1800:touche:numpad 5;2100:relever-tete"
course menu VSM_DELAI=2200 VSM_GESTE_APRES="800:lister-menus;1200:menu:Transport > Tête au repère > 3 — Refrain"

sp="$(sauts pave)"; sm="$(sauts menu)"
mapfile -t sous < <(grep -h "^VSM_MENU_LISTE : Transport > Tête au repère > " "$brouillon/menu.txt" | sed 's/^VSM_MENU_LISTE : Transport > Tête au repère > //')
grep -h "^VSM_TETE_REPERE" "$brouillon/pave.txt" | sed 's/^/       /'
echo "       pavé 2, 1, 5 : [$sp] ; menu « 3 — Refrain » : [$sm]"
echo "       sous-menu : ${sous[*]:0:4}"

verdict "(1) le pavé 2 : « Couplet », 1 920 ; le pavé 1 : 960" "$(echo "$sp" | grep -q "^1920 960" && grep -q "^VSM_TETE_REPERE : repère 2 « Couplet » → 1920" "$brouillon/pave.txt" && echo 1 || echo 0)"
verdict "(2) par le menu : 3 840 ; les repères nommés, 4 à 9 grisés" \
    "$([ "$sm" = 3840 ] && echo "${sous[0]:-}" | grep -q "^1 — Intro" && echo "${sous[2]:-}" | grep -q "^3 — Refrain" \
        && echo "${sous[3]:-}" | grep -q "\[grisée\]" && echo 1 || echo 0)"
verdict "(3) le pavé 5 : dit, la tête reste à 960" \
    "$([ "$sp" = "1920 960 aucun" ] && grep -q "^VSM_TETE_REPERE : pas de repère 5 (3 repère(s)), la tête reste à 960" "$brouillon/pave.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-AU-REPERE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-AU-REPERE : la tête va au N-ième repère dans l'ordre du temps, d'une touche du pavé"
