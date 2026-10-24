#!/usr/bin/env bash
# D586.2 — L'ÉDITION SUSPEND LE SUIVI DE LA TÊTE (Affichage ▸ L'édition suspend le suivi de la tête ; le *Suspend
# Auto-Scroll when Editing* de Cubase), MESURÉ PAR LE DÉFILEMENT DE L'ARRANGEMENT RELEVÉ APRÈS QUE LA TÊTE A QUITTÉ LA
# FENÊTRE, LE TÉMOIN DÉCOCHÉ À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Une piste, un clip de 32 mesures ; la vue enregistrée au
# projet (`view.pixelsPerTick` 0,2) n'en montre que deux : la tête en sort après ~3,5 s de lecture, et le suivi (coché
# d'origine) tourne la page. L'appui dans la zone des clips passe par `clic:arrangement:fx,fy` — sur le clip de la piste
# 0 ; le défilement par `relever-defilement`.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D586) :
#   (1) cochée, la lecture lancée à la mesure 1, un appui à ~1 s, le relevé à ~6 s : le défilement 0 ;
#   (2) le TÉMOIN décoché : la page a tourné (> 0) ;
#   (3) cochée, arrêt puis relance : la page tourne de nouveau (> 0) ;
#   (4) relancé dans le même HOME : cochée ; en anglais, « Editing suspends playhead following ».
#
#   tools/suivi-suspendu.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

SUR_CLIP="${VSM_SUR_CLIP:-0.400,0.087}"   # le clip de la piste 0 (y 50 sur 574, comme tete-posee.sh), loin de ses bords

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-suivi.XXXXXX")"
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
corps += vlq(0) + b"\x90\x3c\x5a" + vlq(480) + b"\x80\x3c\x00"
corps += vlq(60480) + b"\x90\x3e\x5a" + vlq(480) + b"\x80\x3e\x00"   # la dernière noire finit au tick 61 440 : 32 mesures
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 61920, "start": 0, "length": 61920, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "suivi", "midi": {"file": "midi/arrangement.mid"},
           "view": {"pixelsPerTick": 0.2, "scrollTick": 0},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "clips": [clip], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|CLIC_POSITION|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
defilement() { grep -h "^VSM_DEFILEMENT : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_DEFILEMENT : arrangement \([0-9-]*\), .* tete \([0-9-]*\).*/\1 \2/p'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Affichage > L'édition suspend le suivi de la tête"

echo "=== D586.2 : l'édition suspend le suivi de la tête ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course cochee "$maison" VSM_DELAI=7600 \
    VSM_GESTE_APRES="300:$OPTION;600:cliquer:Play;1600:clic:arrangement:$SUR_CLIP;6600:relever-defilement;6700:cliquer:Stop"
course temoin "" VSM_DELAI=7600 \
    VSM_GESTE_APRES="600:cliquer:Play;1600:clic:arrangement:$SUR_CLIP;6600:relever-defilement;6700:cliquer:Stop"
course reprise "" VSM_DELAI=9600 \
    VSM_GESTE_APRES="300:$OPTION;600:cliquer:Play;1600:clic:arrangement:$SUR_CLIP;2600:cliquer:Stop;3100:cliquer:Play;8600:relever-defilement;8700:cliquer:Stop"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

read -r dc tc <<< "$(defilement cochee)"; read -r dt tt <<< "$(defilement temoin)"; read -r dr tr <<< "$(defilement reprise)"
grep -h "^VSM_SUIVI" "$brouillon"/cochee.txt "$brouillon"/reprise.txt | sed 's/^/       /'
echo "       défilements (tête) : cochée ${dc:-?} (${tc:-?}) ; témoin ${dt:-?} (${tt:-?}) ; reprise ${dr:-?} (${tr:-?})"
nc="$(grep -c "^VSM_SUIVI : suspendu" "$brouillon/cochee.txt")"; nt="$(grep -c "^VSM_SUIVI : suspendu" "$brouillon/temoin.txt")"
nr="$(grep -c "^VSM_SUIVI : repris" "$brouillon/reprise.txt")"
coche="$(grep -h "^VSM_MENU_LISTE : Affichage > L'édition suspend le suivi de la tête" "$brouillon/retenu.txt" | grep -c "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : View > Editing suspends playhead following" "$brouillon/anglais.txt")"
echo "       relancé : $coche entrée cochée ; en anglais : $en entrée(s)"
loin() { [ -n "${1:-}" ] && [ "$1" -gt 3840 ]; }   # la tête au-delà des deux mesures montrées

verdict "(1) cochée : le défilement 0, la tête au-delà de la fenêtre, le suivi dit suspendu" \
    "$([ "${dc:-x}" = 0 ] && loin "${tc:-}" && [ "$nc" = 1 ] && echo 1 || echo 0)"
verdict "(2) le témoin décoché : la page a tourné" \
    "$([ -n "${dt:-}" ] && [ "$dt" -gt 0 ] && loin "${tt:-}" && [ "$nt" = 0 ] && echo 1 || echo 0)"
verdict "(3) arrêt puis relance : la page tourne de nouveau" \
    "$([ -n "${dr:-}" ] && [ "$dr" -gt 0 ] && [ "$nr" = 1 ] && echo 1 || echo 0)"
verdict "(4) retenu au poste ; en anglais" "$([ "$coche" = 1 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "SUIVI-SUSPENDU : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SUIVI-SUSPENDU : un appui en lecture laisse la vue où elle est, jusqu'à l'arrêt"
