#!/usr/bin/env bash
# D558.1 — POSER UN REPÈRE À LA TÊTE (Transport ▸ Poser un repère à la tête, Inser ; l'*Insert Marker* de Cubase),
# MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Une piste MIDI ; le projet porte déjà « Repère 1 » (tick 0) et « Repère 3 » (tick 3 840).
#   (1) la tête à 960 (deux temps), Inser : un repère « Repère 2 » à 960 — le premier numéro LIBRE, pas le compte
#       (qui donnerait 3, déjà pris) —, sans boîte, dit au journal ; le projet relu en porte trois, dans l'ordre ;
#   (2) une seconde fois au même tick : refusé et dit, toujours trois repères ;
#   (3) par le MENU, la tête à 1 920, sur le même projet de départ : « Repère 2 » à 1 920 ;
#   (4) un pas : Ctrl+Z rend les deux repères d'origine, après un geste qui a eu lieu ;
#   (5) le TÉMOIN sans geste : les deux repères d'origine.
#
#   tools/repere-a-la-tete.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-repere-tete.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 0, "name": "Repère 1"}, {"tick": 3840, "name": "Repère 3"}],
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    mkdir -p "$brouillon/$nom-projet"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
reperes() {   # « tick:nom » des repères d'un project.json relu, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{m['tick']}:{m['name']}\" for m in d.get('markers', [])) or 'AUCUN')" "$1/project.json"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement,piste:0"

echo "=== D558.1 : poser un repère à la tête ==="
course inser VSM_DELAI=3000 VSM_VUE="$VUE,tete-temps:2" \
    VSM_GESTE_APRES="1000:touche:insert;1400:touche:insert;2000:enregistrer:$brouillon/inser-projet"
course menu VSM_DELAI=2600 VSM_VUE="$VUE,tete-temps:4" \
    VSM_GESTE_APRES="1000:menu:Transport > Poser un repère à la tête;1700:enregistrer:$brouillon/menu-projet"
course annuler VSM_DELAI=3000 VSM_VUE="$VUE,tete-temps:2" \
    VSM_GESTE_APRES="1000:touche:insert;1400:touche:ctrl + Z;2000:enregistrer:$brouillon/annuler-projet"
course temoin VSM_DELAI=2000 VSM_VUE="$VUE,tete-temps:2" VSM_GESTE_APRES="1200:enregistrer:$brouillon/temoin-projet"

i="$(reperes "$brouillon/inser-projet")"; m="$(reperes "$brouillon/menu-projet")"
z="$(reperes "$brouillon/annuler-projet")"; t="$(reperes "$brouillon/temoin-projet")"
grep -h "^VSM_REPERE_POSE" "$brouillon/inser.txt" "$brouillon/menu.txt" | sed 's/^/       /'
echo "       Inser deux fois : [$i]"
echo "       menu : [$m] ; Ctrl+Z : [$z] ; témoin : [$t]"

ORIGINE="0:Repère 1 3840:Repère 3"
verdict "(1) Inser : « Repère 2 » à 960, le premier numéro libre, et c'est dit" \
    "$(grep -q "^VSM_REPERE_POSE : « Repère 2 » à 960 ticks (3 repère(s))" "$brouillon/inser.txt" \
       && [ "$i" = "0:Repère 1 960:Repère 2 3840:Repère 3" ] && echo 1 || echo 0)"
verdict "(2) au même tick : refusé et dit, rien de plus" \
    "$(grep -q "^VSM_REPERE_POSE : refusé, un repère est déjà à 960 ticks (« Repère 2 »)" "$brouillon/inser.txt" && echo 1 || echo 0)"
verdict "(3) par le menu, à 1 920 : « Repère 2 », le numéro libre de ce projet-là" \
    "$([ "$m" = "0:Repère 1 1920:Repère 2 3840:Repère 3" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les repères d'origine, après un geste qui a eu lieu" \
    "$([ "$z" = "$ORIGINE" ] && grep -q "^VSM_REPERE_POSE : « Repère 2 »" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(5) le témoin sans geste : les repères d'origine" "$([ "$t" = "$ORIGINE" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REPERE-A-LA-TETE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REPERE-A-LA-TETE : un repère se pose à la tête d'une touche, au premier nom libre"
