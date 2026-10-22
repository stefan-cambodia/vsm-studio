#!/usr/bin/env bash
# D572.3 — LES PRÉRÉGLAGES DE ZOOM (Affichage ▸ Préréglages de zoom ; les *Zoom Presets* de Cubase), MESURÉS PAR LE
# RELEVÉ DE L'ARRANGEMENT (`VSM_ARRANGEMENT : fenêtre N mesure(s)`, D362).
#
# LES RÈGLES GARDÉES. Un projet d'une piste, les locateurs sur [0, 3 840). Premier lancement : « Zoom : entre les
# locateurs » (D565.2), puis « Enregistrer le zoom de l'arrangement… » sous le nom « Serre ». Second lancement, même HOME :
#   (1) le témoin : la fenêtre d'ouverture, plus large que celle qu'on a retenue ;
#   (2) « Serre » rappelé : la fenêtre retenue, au dixième de mesure ;
#   (3) un pas de la pile des zooms : « Annuler le zoom » rend la fenêtre d'ouverture ;
#   (4) « Retirer un préréglage de zoom ▸ Serre » : le menu ne le propose plus ; un HOME neuf ne l'a jamais connu.
#
#   tools/zoom-prereglages.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zooms.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for i in range(64):   # seize mesures de noires
    corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x83, 0x60]) + bytes([0x80, 60, 0])
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": True, "startTick": 0, "endTick": 3840},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0},
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" \
        VSM_DELAI=2800 "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|VUE)[A-Z_]* : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fenetres() {   # les fenêtres relevées, dans l'ordre (« 17.4 2.2 … »)
    grep -h "^VSM_ARRANGEMENT : fenêtre " "$brouillon/$1.txt" | sed 's/^VSM_ARRANGEMENT : fenêtre \([0-9.]*\) mesure.*/\1/' | tr '\n' ' ' | sed 's/ $//'
}
MENU="Affichage > Préréglages de zoom"

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D572.3 : les préréglages de zoom ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course enregistrer "$maison" VSM_OPTIONS="nom=Serre" \
    VSM_GESTE_APRES="1000:menu:Affichage > Zoom : entre les locateurs;1300:relever-arrangement;1600:menu:$MENU > Enregistrer le zoom de l'arrangement…"
course rappeler "$maison" \
    VSM_GESTE_APRES="1000:relever-arrangement;1300:menu:$MENU > Serre;1700:menu:Affichage > Annuler le zoom;2000:relever-arrangement"
course retirer "$maison" VSM_GESTE_APRES="1200:menu:$MENU > Retirer un préréglage de zoom > Serre;1500:lister-menus"
course neuf "$(mktemp -d "$brouillon/home.XXXX")" VSM_GESTE_APRES="1000:lister-menus"

fe="$(fenetres enregistrer)"; fr="$(fenetres rappeler)"
grep -h "^VSM_ZOOM_PREREGLE\|^VSM_ZOOM : " "$brouillon"/{enregistrer,rappeler,retirer}.txt | sed 's/^/       /'
echo "       fenêtres (mesures) : à l'enregistrement [$fe] ; au rappel [$fr]"
aucun() { grep -h "^VSM_MENU_LISTE : $MENU > " "$brouillon/$1.txt" | grep -v "Enregistrer le zoom" | sed "s/^VSM_MENU_LISTE : $MENU > //" | tr '\n' ';'; }
echo "       menu après retrait : $(aucun retirer) ; HOME neuf : $(aucun neuf)"
read -r serre < <(echo "$fe" | awk '{print $NF}')
read -r f0 f1 f2 < <(echo "$fr" | awk '{print $1, $2, $NF}')

verdict "(1) le témoin : la fenêtre d'ouverture est plus large que celle retenue" \
    "$(awk -v a="${f0:-0}" -v s="${serre:-0}" 'BEGIN { print (s + 0 > 0 && a + 0 > s + 1) ? 1 : 0 }')"
verdict "(2) « Serre » rappelé : la fenêtre retenue, au dixième de mesure" \
    "$(awk -v r="${f1:-0}" -v s="${serre:-0}" 'BEGIN { d = r - s; print (s + 0 > 0 && d < 0.11 && d > -0.11) ? 1 : 0 }')"
verdict "(3) un pas de la pile : « Annuler le zoom » rend la fenêtre d'ouverture" \
    "$(awk -v a="${f0:-0}" -v z="${f2:-0}" 'BEGIN { d = a - z; print (a + 0 > 0 && d < 0.11 && d > -0.11) ? 1 : 0 }')"
verdict "(4) retiré : le menu ne le propose plus ; un HOME neuf ne l'a jamais connu" \
    "$([ "$(aucun retirer)" = "(aucun préréglage de zoom) [grisée];" ] && [ "$(aucun neuf)" = "(aucun préréglage de zoom) [grisée];" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZOOM-PREREGLAGES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZOOM-PREREGLAGES : le zoom retenu sous un nom se rappelle, s'annule d'un pas, se retire"
