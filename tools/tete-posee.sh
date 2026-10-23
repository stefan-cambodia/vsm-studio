#!/usr/bin/env bash
# D580.1, D580.3 — UN CLIC DANS LE VIDE POSE LA TÊTE, ET LA SÉLECTION SUIT LA TÊTE (Transport ▸ Un clic dans le vide pose
# la tête, Édition ▸ La sélection suit la tête ; le *Locate When Clicking in Empty Space* et l'*Auto Select Events under
# Cursor* de Cubase), MESURÉS PAR LA TÊTE ET LA SÉLECTION RELEVÉES APRÈS CHAQUE GESTE, LES TÉMOINS SANS LES OPTIONS.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Piste 0 « Voie_A » : A [0, 1920), B [3840, 5760) ; piste 1
# « Voie_B » : C [0, 7680) ; une note à 10 s sur la piste 1 (une lecture ne s'arrête pas à la fin du morceau) ; trois
# repères, à 960, 2880 et 4000 — la tête s'y pose par `repere-aller:`, le chemin de la règle (`onPlayheadRequested`).
# Les clics passent par `clic:arrangement:fx,fy` (l'appui et le relâché au même point), le glissé par `glisser:`.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D580) :
#   (1) l'option cochée, un clic sur le fond de la piste 0, loin des clips : la tête > 0, au tick que l'arrangement dit
#       avoir lu (`VSM_CLIC_VIDE`) ;
#   (2) le TÉMOIN sans l'option, le même clic : la tête à 0, et le clic dit « ne bouge pas » ;
#   (3) l'option cochée, un GLISSÉ sur le fond : la tête à 0, aucun `VSM_CLIC_VIDE` ;
#   (4) l'option cochée, un clic sur le clip A : la tête à 0, A choisi ;
#   (5) la sélection qui suit la tête, la piste 0 choisie : la tête à 960 {A}, à 2880 {}, à 4000 {B} ;
#   (6) les pistes 0 et 1 choisies, la tête à 960 : {A, C} ;
#   (7) le TÉMOIN sans l'option, la piste 0 choisie, la tête à 960 : {} ;
#   (8) PAS EN LECTURE : {A} à 960, la lecture lancée, relevé à ~2,4 s (la tête entre A et B) : toujours {A} ; arrêté
#       dans le trou : {} ;
#   (9) relancé dans le même HOME : les deux entrées cochées ; en anglais, « A click on empty space sets the playhead »,
#       « Selection follows the playhead ».
#
#   tools/tete-posee.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

# LES POINTS DE L'ARRANGEMENT (fractions de sa largeur et de sa hauteur), lus par `relever-clips` à 1600 × 1000 : il
# fait 886 × 574, la piste 0 tient y 22..78, A x 150..327, B x 503..680. Un premier jet lu sur la photo, à 0,30 de haut,
# tombait SOUS les pistes — un vide qui compte aussi, mais pas celui que le cas nomme : le clic « sur A » a posé la tête.
VIDE="${VSM_VIDE:-0.880,0.087}"       # le fond de la piste 0, à droite de B (x 780, y 50)
VIDE_FIN="${VSM_VIDE_FIN:-0.948,0.113}" # la fin du glissé, toujours sur la piste 0 (x 840, y 65)
SUR_A="${VSM_SUR_A:-0.271,0.087}"     # le clip A (x 240, y 50)

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-posee.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(i, notes):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if i == 0 else b"") + b"\x00\xff\x03\x06Voie_" + bytes([0x41 + i])
    t = 0
    for debut, duree, h in notes:
        corps += vlq(debut - t) + bytes([0x90 | i, h, 90]) + vlq(duree) + bytes([0x80 | i, h, 0])
        t = debut + duree
    return b"MTrk" + struct.pack(">I", len(corps + b"\x00\xff\x2f\x00")) + corps + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480)
    + piste(0, [(0, 480, 60), (3840, 480, 62)]) + piste(1, [(0, 480, 48), (9600, 480, 50)]))
clip = lambda s, l: {"sourceStart": s, "sourceLength": l, "start": s, "length": l, "color": "#FF6B9BFF"}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "tete", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 960, "name": "dans A"}, {"tick": 2880, "name": "trou"}, {"tick": 4000, "name": "dans B"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voie_A", "mix": mix,
                       "clips": [clip(0, 1920), clip(3840, 1920)], "instrument": {"preferredPlugin": "vsm.minimoog"}},
                      {"channel": 1, "color": "#FF6B9BFF", "effects": [], "name": "Voie_B", "mix": mix,
                       "clips": [clip(0, 7680)], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|CLIC_POSITION|GLISSER|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tete() { grep -h "^VSM_TETE : tick " "$brouillon/$1.txt" | sed -n "${2:-1}p" | sed 's/^VSM_TETE : tick \([0-9-]*\).*/\1/'; }
choix() { grep -h "^VSM_SELECTION_CLIPS : " "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/^VSM_SELECTION_CLIPS : [0-9]* :\{0,1\} *//'; }
clic_lu() { grep -h "^VSM_CLIC_VIDE : " "$brouillon/$1.txt" | head -1 | sed 's/^VSM_CLIC_VIDE : \([0-9]*\) ticks.*/\1/'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
CLIC="menu:Transport > Un clic dans le vide pose la tête"
SUIT="menu:Édition > La sélection suit la tête"
ETENDRE="menu:Piste > Étendre le choix vers le bas"

echo "=== D580.1 : un clic dans le vide pose la tête ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course clic "$maison" VSM_DELAI=2000 VSM_GESTE_APRES="500:$CLIC;800:clic:arrangement:$VIDE;1100:relever-tete"
course clic-temoin "" VSM_DELAI=2000 VSM_GESTE_APRES="800:clic:arrangement:$VIDE;1100:relever-tete"
course lasso "" VSM_DELAI=2000 VSM_GESTE_APRES="500:$CLIC;800:glisser:arrangement:$VIDE:$VIDE_FIN;1100:relever-tete"
course sur-a "" VSM_DELAI=2000 VSM_GESTE_APRES="500:$CLIC;800:clic:arrangement:$SUR_A;1100:relever-tete;1200:relever-selection"

echo "=== D580.3 : la sélection suit la tête ==="
course suit "$maison" VSM_DELAI=3000 \
    VSM_GESTE_APRES="500:$SUIT;700:choisir:0;900:repere-aller:0;1200:relever-selection;1300:repere-aller:1;1600:relever-selection;1700:repere-aller:2;2000:relever-selection;2100:$ETENDRE;2200:repere-aller:0;2500:relever-selection"
course suit-temoin "" VSM_DELAI=1800 VSM_GESTE_APRES="700:choisir:0;900:repere-aller:0;1200:relever-selection"
course lecture "" VSM_DELAI=4200 \
    VSM_GESTE_APRES="500:$SUIT;700:choisir:0;900:repere-aller:0;1200:relever-selection;1300:cliquer:Play;2700:relever-selection;2800:relever-tete;2900:cliquer:Stop;3300:relever-tete;3400:relever-selection"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

lu="$(clic_lu clic)"; t1="$(tete clic)"; t2="$(tete clic-temoin)"; t3="$(tete lasso)"; t4="$(tete sur-a)"
n3="$(grep -c "^VSM_CLIC_VIDE" "$brouillon/lasso.txt")"; dit2="$(grep -c "^VSM_CLIC_VIDE : .*ne bouge pas" "$brouillon/clic-temoin.txt")"
s4="$(choix sur-a 1)"
s5a="$(choix suit 1)"; s5b="$(choix suit 2)"; s5c="$(choix suit 3)"; s6="$(choix suit 4)"; s7="$(choix suit-temoin 1)"
l1="$(choix lecture 1)"; l2="$(choix lecture 2)"; l3="$(choix lecture 3)"; lt="$(tete lecture 1)"; la="$(tete lecture 2)"
grep -h "^VSM_CLIC_VIDE\|^VSM_SOUS_LA_TETE\|^VSM_REGLAGE_POSTE" "$brouillon"/clic.txt "$brouillon"/clic-temoin.txt "$brouillon"/suit.txt \
    "$brouillon"/lecture.txt | sed 's/^/       /'
echo "       têtes : clic $t1 (lu $lu) ; témoin $t2 ; lasso $t3 ($n3 clic(s) dans le vide) ; sur A $t4 [$s4]"
echo "       sélections : 960 [$s5a] ; 2880 [$s5b] ; 4000 [$s5c] ; deux pistes à 960 [$s6] ; témoin [$s7]"
echo "       lecture : à 960 [$l1] ; en lecture, tête $lt [$l2] ; arrêtée à $la [$l3]"
coches="$(grep -h "^VSM_MENU_LISTE : \(Transport > Un clic dans le vide pose la tête\|Édition > La sélection suit la tête\)" "$brouillon/retenu.txt" | grep -c "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : \(Transport > A click on empty space sets the playhead\|Edit > Selection follows the playhead\)" "$brouillon/anglais.txt")"
echo "       relancé : $coches entrée(s) cochée(s) ; en anglais : $en entrée(s)"

verdict "(1) l'option : la tête posée au tick lu (> 0)" "$([ -n "$lu" ] && [ "$lu" -gt 0 ] && [ "$t1" = "$lu" ] && echo 1 || echo 0)"
verdict "(2) le témoin : la tête à 0, le clic dit « ne bouge pas »" "$([ "$t2" = 0 ] && [ "$dit2" = 1 ] && echo 1 || echo 0)"
verdict "(3) un glissé reste un lasso : la tête à 0, aucun clic dans le vide" "$([ "$t3" = 0 ] && [ "$n3" = 0 ] && echo 1 || echo 0)"
verdict "(4) un clic sur A : la tête à 0, A choisi" "$([ "$t4" = 0 ] && [ "$s4" = "Voie_A:0" ] && echo 1 || echo 0)"
verdict "(5) la piste 0 : {A} à 960, {} à 2880, {B} à 4000" \
    "$([ "$s5a" = "Voie_A:0" ] && [ -z "$s5b" ] && [ "$s5c" = "Voie_A:3840" ] && echo 1 || echo 0)"
verdict "(6) les pistes 0 et 1 à 960 : {A, C}" "$([ "$s6" = "Voie_A:0 Voie_B:0" ] && echo 1 || echo 0)"
verdict "(7) le témoin sans l'option : {}" "$([ -z "$s7" ] && [ "$(grep -c '^VSM_SELECTION_CLIPS : 0' "$brouillon/suit-temoin.txt")" = 1 ] && echo 1 || echo 0)"
verdict "(8) pas en lecture : {A} gardé dans le trou, {} à l'arrêt dans le trou" \
    "$([ "$l1" = "Voie_A:0" ] && [ "$l2" = "Voie_A:0" ] && [ -n "$lt" ] && [ "$lt" -ge 1920 ] && [ "$lt" -lt 3840 ] \
       && [ -n "$la" ] && [ "$la" -ge 1920 ] && [ "$la" -lt 3840 ] && [ -z "$l3" ] && echo 1 || echo 0)"
verdict "(9) retenu au poste (2 cochées) ; en anglais (2 entrées)" "$([ "$coches" = 2 ] && [ "$en" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-POSEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-POSEE : un clic dans le vide pose la tête, et la sélection suit la tête posée, pas la lecture"
