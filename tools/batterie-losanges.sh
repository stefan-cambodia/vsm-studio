#!/usr/bin/env bash
# D615 — LES FRAPPES DE BATTERIE EN LOSANGES (l'affichage du *Drum Editor* de Cubase), mesurées dans l'application : la
# géométrie des losanges (`relever-losanges`), puis des clics du piano roll (`clic:pianoroll:fx,fy`) dans le losange et
# dans l'ancienne barre ; le nombre de notes choisies (`VSM_SELECTION`, la dernière ligne vaut le compte courant).
#
# LE JEU : une piste « Batterie » sur la TR-909, canal 10 : une grosse caisse au temps 1 et une caisse claire au temps 2,
# d'une NOIRE chacune. Les clics visent la caisse claire (le losange de la grosse caisse, sur le tick 0, est à moitié
# sous la colonne du clavier).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D615) :
#   CORRIGÉ APRÈS LE PREMIER ESSAI (ROADMAP-daw, D615) : l'outil de sélection POSE une note quand on clique dans le vide,
#   et la choisit — le compte des notes de la piste départage, pas celui des choisies :
#   (a) un clic à gauche de l'attaque, dans le losange (à 0,6 demi-largeur) → 2 notes (la frappe saisie), 1 choisie ;
#   (b) un clic au milieu de la durée, à droite du losange (dans l'ancienne barre) → 3 notes (une frappe neuve posée) ;
#   (c) la case décochée (les barres) : (a) → 3, (b) → 2 ; le témoin (D614) : comme (c) ;
#   (d) l'entrée du menu en anglais, retenue au poste ; la photo regardée.
#
#   tools/batterie-losanges.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D614/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-losanges.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"
t = 0
for debut, h in ((0, 36), (480, 38)):
    corps += vlq(debut - t) + bytes([0x99, h, 110]) + vlq(480) + bytes([0x89, h, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "losanges", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Un HOME neuf, gardé pour les préférences.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    mkdir -p "$brouillon/home-$nom"
    env HOME="$brouillon/home-$nom" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=2200 VSM_GESTE_APRES="$gestes" "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CLIC_POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
choisies() { grep "^VSM_SELECTION : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_SELECTION : \([0-9]*\) .*/\1/p'; }
# Le nombre de notes de la piste, au relevé qui suit le clic (une ligne par note).
notes() { grep -c "^VSM_LOSANGE : note " "$brouillon/$1.txt"; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D615 : les frappes de batterie en losanges ==="
course "$BIN" geometrie "1200:relever-losanges"
G="$(grep "^VSM_LOSANGE : note 38 " "$brouillon/geometrie.txt" | head -1)"
MODE="$(grep "^VSM_LOSANGE : [lb]" "$brouillon/geometrie.txt" | head -1)"
echo "       $MODE"
echo "       $G"
read -r CX CY HW FIN <<< "$(echo "$G" | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ; demi-largeur \([0-9.]*\) ; fin \([0-9.]*\)$/\1 \2 \3 \4/p')"
[ -n "${CX:-}" ] || { echo "REFUS : la géométrie de la caisse claire n'a pas été relevée"; exit 1; }
DANS="$(python3 -c "import sys; print(f'{float(sys.argv[1]) - 0.6 * float(sys.argv[2]):.4f},{sys.argv[3]}')" "$CX" "$HW" "$CY")"
BARRE="$(python3 -c "import sys; print(f'{float(sys.argv[1]) + 4.0 * float(sys.argv[2]):.4f},{sys.argv[3]}')" "$CX" "$HW" "$CY")"
python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) + 4 * float(sys.argv[2]) < float(sys.argv[3]) else 1)" "$CX" "$HW" "$FIN" \
    || echo "       ATTENTION : le point (b) n'est pas dans l'ancienne barre — le contrôle (b) ne vaudrait rien"
echo "       clics : dans le losange [$DANS] ; dans l'ancienne barre [$BARRE]"
MENU="menu:Affichage > Batterie en losanges (canal 10)"
course "$BIN" dans "1400:clic:pianoroll:$DANS;1700:relever-losanges"
course "$BIN" barre "1400:clic:pianoroll:$BARRE;1700:relever-losanges"
course "$BIN" barres-dans "900:$MENU;1400:clic:pianoroll:$DANS;1700:relever-losanges"
course "$BIN" barres-barre "900:$MENU;1400:clic:pianoroll:$BARRE;1700:relever-losanges"
course "$BIN" en "900:lister-menus" VSM_LANGUE=en

verdict "(a) le mode : « losanges » sur la piste du canal 10" "$(echo "$MODE" | grep -q "losanges ; canal 10" && echo 1 || echo 0)"
verdict "(a) un clic dans le losange, à gauche de l'attaque : la frappe saisie — 2 notes ($(notes dans)), 1 choisie" \
    "$([ "$(notes dans)" = 2 ] && [ "$(choisies dans)" = 1 ] && echo 1 || echo 0)"
verdict "(b) un clic dans l'ancienne barre, à droite du losange : une frappe neuve — 3 notes ($(notes barre))" "$([ "$(notes barre)" = 3 ] && echo 1 || echo 0)"
verdict "(c) décochée, les barres : à gauche de l'attaque 3 notes ($(notes barres-dans)), dans la barre 2 ($(notes barres-barre))" \
    "$([ "$(notes barres-dans)" = 3 ] && [ "$(notes barres-barre)" = 2 ] && grep -q "^VSM_LOSANGE : barres" "$brouillon/barres-barre.txt" && echo 1 || echo 0)"
RET="$(grep -o 'name="affichageBatterieEnLosanges" val="[^"]*"' "$brouillon/home-barres-dans/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" 2>/dev/null)"
verdict "(d) retenue au poste (décochée : « $RET »)" "$(echo "$RET" | grep -q 'val="0"' && echo 1 || echo 0)"
verdict "(d) en anglais : « Drums as diamonds (channel 10) »" "$(grep -q "Drums as diamonds (channel 10)" "$brouillon/en.txt" && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    # LE TÉMOIN NE CONNAÎT PAS `relever-losanges` : ses notes se comptent dans le projet qu'il enregistre.
    course "$TEMOIN" t-dans "1400:clic:pianoroll:$DANS;1800:enregistrer:$brouillon/t-dans-projet"
    course "$TEMOIN" t-barre "1400:clic:pianoroll:$BARRE;1800:enregistrer:$brouillon/t-barre-projet"
    compte_mid() { python3 - "$1/midi/arrangement.mid" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
print(sum(1 for i in range(len(d) - 2) if d[i] & 0xF0 == 0x90 and d[i + 2] > 0 and d[i + 1] < 128))
PY2
}
    TD="$(compte_mid "$brouillon/t-dans-projet")"; TB="$(compte_mid "$brouillon/t-barre-projet")"
    echo "       témoin : à gauche de l'attaque $TD note(s), dans la barre $TB"
    verdict "(c) témoin (D614) : les barres — à gauche de l'attaque 3 notes, dans la barre 2" "$([ "$TD" = 3 ] && [ "$TB" = 2 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "LOSANGES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LOSANGES : une frappe de batterie se dessine et se saisit en losange sur son attaque"
