#!/usr/bin/env bash
# D580.2 — DÉSACTIVER LE PUNCH À L'ARRÊT (Enregistrement ▸ Désactiver le punch à l'arrêt ; le *Deactivate Punch In on
# Stop* de Cubase), MESURÉ PAR L'ÉTAT DE LA RÉGION RELEVÉ APRÈS L'ARRÊT, LE TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM ; la région de punch [1920, 3840) — [2 s, 4 s) — active). Une
# piste MIDI armée par son bouton R (`armer:0`), *Sans décompte* ; l'état se lit par `relever-punch`.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D580) :
#   (1) l'option cochée, F9, F9 — la seconde pression clôt la prise, la lecture CONTINUE —, relevé en lecture, puis
#       Stop : la région INACTIVE, ses bornes [1920, 3840) gardées ;
#   (2) l'option cochée, une LECTURE (le bouton Play) puis Stop : inactive ;
#   (3) le TÉMOIN sans l'option, la même suite : active ;
#   (4) l'option et « Arrêter après la sortie de punch » (D579.3), F9 seul : arrêté à 4,000 s, la région inactive ;
#   (5) relancé dans le même HOME : l'entrée cochée ; en anglais, « Deactivate punch on stop ».
#
#   tools/punch-a-l-arret.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-punch-arret.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
# Une piste MIDI et UNE note à 10 s : un morceau vide est fini à 0, et une LECTURE s'y arrête aussitôt (D579, cas 10).
corps = (b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + bytes([0xcb, 0x00, 0x90, 48, 80, 0x83, 0x60, 0x80, 48, 0])
         + b"\x00\xff\x2f\x00")   # 9 600 ticks (10 s), puis 480
os.makedirs(f"{b}/punch/midi", exist_ok=True)
open(f"{b}/punch/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "punch", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "punch": {"enabled": True, "startTick": 1920, "endTick": 3840}},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/punch/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/punch" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
etat() { grep -h "^VSM_PUNCH_ETAT : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_PUNCH_ETAT : //'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OFF="menu:Enregistrement > Désactiver le punch à l'arrêt"
ARRET="menu:Enregistrement > Arrêter après la sortie de punch"
SANS="menu:Enregistrement > Sans décompte"

echo "=== D580.2 : désactiver le punch à l'arrêt ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course prise "$maison" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$OFF;800:relever-punch;1000:touche:F9;2600:touche:F9;2900:relever-decompte;3000:relever-punch;3100:cliquer:Stop;3400:relever-punch"
course lecture "" VSM_DELAI=3200 \
    VSM_GESTE_APRES="500:$OFF;800:cliquer:Play;2000:cliquer:Stop;2600:relever-punch"
course temoin "" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:relever-punch;1000:touche:F9;2600:touche:F9;3100:cliquer:Stop;3400:relever-punch"
course sortie "" VSM_DELAI=6400 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;450:$ARRET;500:$OFF;800:touche:F9;5600:relever-punch;5700:relever-decompte"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

ep="$(etat prise)"; el="$(etat lecture)"; et="$(etat temoin)"; es="$(etat sortie)"
avant="$(grep -h "^VSM_PUNCH_ETAT : " "$brouillon/prise.txt" | head -1 | sed 's/^VSM_PUNCH_ETAT : //')"
apres_f9="$(grep -h "^VSM_PUNCH_ETAT : " "$brouillon/prise.txt" | sed -n 2p | sed 's/^VSM_PUNCH_ETAT : //')"
joue="$(grep -h "^VSM_DECOMPTE : " "$brouillon/prise.txt" | head -1)"
ds="$(grep -h "^VSM_DECOMPTE : " "$brouillon/sortie.txt" | tail -1)"
grep -h "^VSM_PUNCH\|^VSM_REGLAGE_POSTE" "$brouillon"/prise.txt "$brouillon"/lecture.txt "$brouillon"/sortie.txt | sed 's/^/       /'
echo "       avant : [$avant] ; prise : [$ep] ; lecture : [$el] ; témoin : [$et] ; sortie : [$es]"
echo "       $ds"
echo "       après F9 F9 : [$apres_f9] — $joue"
coche="$(grep -h "^VSM_MENU_LISTE : Enregistrement > Désactiver le punch à l'arrêt" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Recording > Deactivate punch on stop" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"

verdict "(1) F9, F9 (en lecture, active), Stop : inactive [1920, 3840)" \
    "$([ "$avant" = "active [1920, 3840)" ] && [ "$apres_f9" = "active [1920, 3840)" ] && echo "$joue" | grep -q "transport en lecture" \
       && [ "$ep" = "inactive [1920, 3840)" ] && echo 1 || echo 0)"
verdict "(2) une lecture puis Stop : inactive" "$([ "$el" = "inactive [1920, 3840)" ] && echo 1 || echo 0)"
verdict "(3) le témoin sans l'option : active" "$([ "$et" = "active [1920, 3840)" ] && echo 1 || echo 0)"
verdict "(4) avec l'arrêt après la sortie : arrêté à 4,000 s, inactive" \
    "$(echo "$ds" | grep -q "tête 4.000 s, transport arrêté" && [ "$es" = "inactive [1920, 3840)" ] && echo 1 || echo 0)"
verdict "(5) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PUNCH-A-L-ARRET : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PUNCH-A-L-ARRET : l'arrêt désactive la région de punch, ses bornes gardées"
