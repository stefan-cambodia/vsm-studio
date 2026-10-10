#!/usr/bin/env bash
# D546.2 — LE DÉCOMPTE ET LA PRISE, MESURÉS DANS L'APPLICATION : trois défauts trouvés en une phase.
#
# LES RÈGLES GARDÉES. Le moteur force le clic tant que le bloc commence avant la FIN DU DÉCOMPTE (les tests
# `audio/` de `test_count_in.cpp` le mesurent au son : huit clics de 12 à 16 s, métronome éteint, et rien sans
# elle) ; ce banc mesure que l'APPLICATION la pose et la retire. Une piste MIDI armée, 120 BPM, une seule noire à
# 0 (la fin du morceau est à 1 s), un décompte d'une mesure (la préférence par défaut), métronome éteint ; F9.
#   (1) punch à la mesure 9 (16 s), pendant le décompte : le moteur a reçu 16,000 s, la tête est entre 14 et
#       16 s — H6 ; et le décompte n'est plus arrêté ni rembobiné par la fin du morceau, passée depuis 13 s ;
#   (2) la prise commencée : la fin du décompte retirée (0,000), la phase « prise », la tête au-delà de 16 s ;
#   (3) l'arrêt (F9) : 0,000 ;
#   (4) le décompte INTERROMPU (F9 pendant qu'il compte), sur un projet de 12 mesures : 16,000 puis 0,000 —
#       sans quoi la lecture suivante, partie d'avant la mesure 9, cliquerait métronome éteint — ; et le
#       TRANSPORT arrêté, la tête au point d'entrée, encore une seconde plus tard (H9 : il se disait « en
#       lecture », la tête figée) ;
#   (5) sans punch, la tête à 0 : 50 ms après F9, la phase est « décompte » et la tête NÉGATIVE (H8 : la durée
#       du décompte valait zéro au début du morceau) ;
#   (6) la même prise, 2,9 s plus tard : toujours « prise », la tête au-delà de 0,5 s (H7 : la fin du morceau
#       l'arrêtait).
#
#   tools/decompte-punch.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-decompte.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "punch": {"enabled": True, "startTick": 8 * 1920, "endTick": 10 * 1920}},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Partie"}]}, open(f"{b}/base/project.json", "w"), indent=1)
# Le même, qui dure 12 mesures (une seconde noire à la mesure 12) : le point d'entrée est AVANT la fin.
os.makedirs(f"{b}/long/midi", exist_ok=True)
corps = (b"\x00\xff\x51\x03\x07\xa1\x20\x00\x90\x3c\x64\x83\x60\x80\x3c\x00"
         + b"\x81\xa1\x20\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00")   # 11 x 1920 - 480 = 20640
open(f"{b}/long/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
p = json.load(open(f"{b}/base/project.json"))
json.dump(p, open(f"{b}/long/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,piste:0" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|TOUCHE|BOITE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS|carte son|armée)" "$brouillon/$nom.txt" \
        | grep -v "VSM_GESTE : armer" | sed "s/^/        journal ($nom) : /" >&2
}
releves() { grep "^VSM_DECOMPTE : " "$1" | sed 's/^VSM_DECOMPTE : //'; }
champ() {   # $1 = relevé ; $2 = phase|fin|tete|transport
    case "$2" in
        phase) echo "$1" | sed -n 's/^phase \([^,]*\),.*/\1/p' ;;
        fin)   echo "$1" | sed -n 's/.*fin du décompte \([-0-9.]*\) s.*/\1/p' ;;
        tete)  echo "$1" | sed -n 's/.*tête \([-0-9.]*\) s.*/\1/p' ;;
        transport) echo "$1" | sed -n 's/.*, transport \(.*\)$/\1/p' ;;
    esac
}
entre() { python3 -c "import sys; sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)" "$1" "$2" "$3" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D546.2 : le décompte d'un punch à la mesure 9 ==="
course prise VSM_DELAI=6500 \
    VSM_GESTE_APRES="300:armer;600:touche:F9;1200:relever-decompte;3600:relever-decompte;4200:touche:F9;4700:relever-decompte"
course interrompu VSM_DELAI=3500 VSM_PROJET="$brouillon/long" \
    VSM_GESTE_APRES="300:armer;600:touche:F9;1200:relever-decompte;1600:touche:F9;1700:relever-decompte;2700:relever-decompte"
cp -r "$brouillon/base" "$brouillon/debut"
python3 -c "
import json, sys
p = json.load(open(sys.argv[1])); del p['transport']['punch']; json.dump(p, open(sys.argv[1], 'w'), indent=1)
" "$brouillon/debut/project.json"
course debut VSM_DELAI=4500 VSM_PROJET="$brouillon/debut" \
    VSM_GESTE_APRES="300:armer;600:touche:F9;650:relever-decompte;3500:relever-decompte"

mapfile -t p < <(releves "$brouillon/prise.txt")
mapfile -t q < <(releves "$brouillon/interrompu.txt")
mapfile -t d < <(releves "$brouillon/debut.txt")
for r in "${p[@]}"; do echo "       prise      : $r"; done
for r in "${q[@]}"; do echo "       interrompu : $r"; done
for r in "${d[@]}"; do echo "       début      : $r"; done

verdict "(1) pendant le décompte : fin à 16,000 s, tête entre 14 et 16 s" \
    "$([ "${#p[@]}" = 3 ] && [ "$(champ "${p[0]}" phase)" = "décompte" ] && [ "$(champ "${p[0]}" fin)" = "16.000" ] \
        && entre "$(champ "${p[0]}" tete)" 14 16 && echo 1 || echo 0)"
verdict "(2) la prise commencée : la fin du décompte retirée, la tête au-delà de 16 s" \
    "$([ "${#p[@]}" = 3 ] && [ "$(champ "${p[1]}" phase)" = "prise" ] && [ "$(champ "${p[1]}" fin)" = "0.000" ] \
        && entre "$(champ "${p[1]}" tete)" 16 30 && echo 1 || echo 0)"
verdict "(3) l'arrêt : 0,000" \
    "$([ "${#p[@]}" = 3 ] && [ "$(champ "${p[2]}" phase)" = "arrêt" ] && [ "$(champ "${p[2]}" fin)" = "0.000" ] && echo 1 || echo 0)"
verdict "(4) le décompte interrompu : 16,000 puis 0,000, le transport arrêté au point d'entrée" \
    "$([ "${#q[@]}" = 3 ] && [ "$(champ "${q[0]}" fin)" = "16.000" ] && [ "$(champ "${q[1]}" phase)" = "arrêt" ] \
        && [ "$(champ "${q[1]}" fin)" = "0.000" ] && [ "$(champ "${q[2]}" tete)" = "16.000" ] \
        && [ "$(champ "${q[1]}" transport)" = "arrêté" ] && [ "$(champ "${q[2]}" transport)" = "arrêté" ] && echo 1 || echo 0)"
verdict "(5) au début du morceau : un décompte, la tête négative" \
    "$([ "${#d[@]}" = 2 ] && [ "$(champ "${d[0]}" phase)" = "décompte" ] && entre "$(champ "${d[0]}" tete)" -2 -0.001 \
        && echo 1 || echo 0)"
verdict "(6) la prise court au-delà de la fin du morceau" \
    "$([ "${#d[@]}" = 2 ] && [ "$(champ "${d[1]}" phase)" = "prise" ] && entre "$(champ "${d[1]}" tete)" 0.5 30 \
        && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DECOMPTE-PUNCH : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DECOMPTE-PUNCH : le décompte compte partout, et la prise court au-delà de la fin du morceau"
