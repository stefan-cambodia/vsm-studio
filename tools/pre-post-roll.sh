#!/usr/bin/env bash
# D578.1, D578.2 — LE PRÉ-ROLL ET LE POST-ROLL (Transport ▸ Pré-roll, Transport ▸ Post-roll ; les *Pre-roll* et
# *Post-roll* de Cubase), MESURÉS PAR LE RELEVÉ DU TRANSPORT PENDANT ET APRÈS, LE TÉMOIN SANS EUX À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM : une mesure = 2 s). Une piste, A [1 920, 3 840) — de 2 à 4 s —
# et B [11 520, 13 440) — de 12 à 14 s, la fin du morceau. L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D578) :
#   (1) le TÉMOIN : la tête à la mesure 5 (8 s), Espace ; 0,3 s après, en lecture, la tête entre 8,0 et 8,6 s ;
#   (2) pré-roll d'1 mesure : la même course, la tête entre 6,0 et 6,6 s ;
#   (3) la tête à 0 et 1 mesure de pré-roll : entre 0,0 et 0,6 s — le départ rabattu au début du morceau ;
#   (4) arrêté avec « Revenir au point de départ » : la tête à 8,000 s, et non 6 — le point posé par l'utilisateur ;
#   (5) le BOUTON Play suit le pré-roll comme Espace : entre 6,0 et 6,6 s ;
#   (6) retenu au poste : relancé dans le même HOME, « Pré-roll : 1 mesure » est cochée ;
#   (7) le TÉMOIN du post-roll : A choisi, « Lire la sélection » s'arrête à 4,000 s ;
#   (8) post-roll d'1 mesure : à 3,2 s du départ, en lecture au-delà de 4 s ; à l'arrêt, la tête à 6,000 s ;
#   (9) 1 mesure de pré-roll ET 1 de post-roll : 0,3 s après, en lecture entre 0,0 et 0,6 s ; l'arrêt à 6,000 s ;
#  (10) en anglais : « Pre-roll: 1 bar », « Post-roll: 1 bar ».
#
#   tools/pre-post-roll.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pre-post-roll.XXXXXX")"
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
for debut in (1920, 2880, 11520, 12480):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip("A", 1920), clip("B", 11520)]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
# LES HOMES : les préférences posées dans le fichier de réglages, comme D372 — le retour au départ, le pré-roll, les deux.
def home(nom, valeurs):
    os.makedirs(f"{b}/{nom}/VintageSynthMidiStudio", exist_ok=True)
    open(f"{b}/{nom}/VintageSynthMidiStudio/VintageSynthMidiStudio.settings", "w").write(
        '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n'
        + "".join(f'  <VALUE name="{k}" val="{v}"/>\n' for k, v in valeurs) + "</PROPERTIES>\n")
home("home-retour", [("retourAuDepartALArret", 1), ("transportPreRollBars", 1)])
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releves() { grep -h "^VSM_DECOMPTE : " "$brouillon/$1.txt" | sed 's/^VSM_DECOMPTE : //'; }
tete() { echo "$1" | sed -n 's/.*tête \([-0-9.]*\) s.*/\1/p'; }
etat() { echo "$1" | sed -n 's/.*transport \(.*\)$/\1/p'; }
dans() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }
lecture_dans() {   # $1 = relevé ; $2 $3 = bornes : en lecture, la tête entre les deux
    [ "$(etat "$1")" = "en lecture" ] && dans "$(tete "$1")" "$2" "$3"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
PRE1="menu:Transport > Pré-roll > Pré-roll : 1 mesure"
POST1="menu:Transport > Post-roll > Post-roll : 1 mesure"

echo "=== D578.1, D578.2 : le pré-roll et le post-roll ==="
h() { mktemp -d "$brouillon/home.XXXX"; }
course temoin "" VSM_POSITION=5 VSM_DELAI=2000 VSM_VUE="$VUE" VSM_GESTE_APRES="1000:touche:spacebar;1300:relever-decompte"
maison="$(h)"
course pre "$maison" VSM_POSITION=5 VSM_DELAI=2200 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="800:$PRE1;1000:touche:spacebar;1300:relever-decompte"
course retenu "$maison" VSM_DELAI=1600 VSM_VUE="$VUE" VSM_GESTE_APRES="900:lister-menus"
course zero "" VSM_POSITION=1 VSM_DELAI=2200 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="800:$PRE1;1000:touche:spacebar;1300:relever-decompte"
course retour "$brouillon/home-retour" VSM_POSITION=5 VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1000:touche:spacebar;1300:relever-decompte;1700:touche:spacebar;2100:relever-decompte"
course bouton "" VSM_POSITION=5 VSM_DELAI=2200 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="800:$PRE1;1000:cliquer:Play;1300:relever-decompte"
course sel-temoin "" VSM_DELAI=5200 VSM_VUE="$VUE,premier-clip:0" \
    VSM_GESTE_APRES="1200:touche:shift + spacebar;4600:relever-decompte"
course sel-post "" VSM_DELAI=7400 VSM_VUE="$VUE,premier-clip:0" \
    VSM_GESTE_APRES="900:$POST1;1200:touche:shift + spacebar;4400:relever-decompte;6800:relever-decompte"
course sel-deux "" VSM_DELAI=8400 VSM_VUE="$VUE,premier-clip:0" \
    VSM_GESTE_APRES="700:$PRE1;900:$POST1;1200:touche:shift + spacebar;1500:relever-decompte;7800:relever-decompte"
course anglais "" VSM_LANGUE=en VSM_DELAI=1800 VSM_VUE="$VUE" VSM_GESTE_APRES="900:lister-menus"

mapfile -t rt < <(releves temoin); mapfile -t rp < <(releves pre); mapfile -t rz < <(releves zero)
mapfile -t rr < <(releves retour); mapfile -t rb < <(releves bouton); mapfile -t st < <(releves sel-temoin)
mapfile -t sp < <(releves sel-post); mapfile -t sd < <(releves sel-deux)
for c in temoin pre zero retour bouton sel-temoin sel-post sel-deux; do
    releves "$c" | sed "s/^/       $c : /"
done
grep -h "^VSM_PRE_ROLL\|^VSM_LIRE_SELECTION\|^VSM_TRANSPORT_ROLL" "$brouillon"/pre.txt "$brouillon"/zero.txt "$brouillon"/sel-post.txt "$brouillon"/sel-deux.txt | sed 's/^/       /'
coche="$(grep -h "^VSM_MENU_LISTE : Transport > Pré-roll > Pré-roll : 1 mesure" "$brouillon/retenu.txt")"
en_pre="$(grep -ch "^VSM_MENU_LISTE : Transport > Pre-roll > Pre-roll: 1 bar" "$brouillon/anglais.txt")"
en_post="$(grep -ch "^VSM_MENU_LISTE : Transport > Post-roll > Post-roll: 1 bar" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en_pre et $en_post entrée(s)"

verdict "(1) le témoin : en lecture, la tête entre 8,0 et 8,6 s" \
    "$([ "${#rt[@]}" = 1 ] && lecture_dans "${rt[0]}" 8.0 8.6 && echo 1 || echo 0)"
verdict "(2) 1 mesure de pré-roll : entre 6,0 et 6,6 s" \
    "$([ "${#rp[@]}" = 1 ] && lecture_dans "${rp[0]}" 6.0 6.6 && echo 1 || echo 0)"
verdict "(3) depuis 0 : entre 0,0 et 0,6 s" "$([ "${#rz[@]}" = 1 ] && lecture_dans "${rz[0]}" 0.0 0.6 && echo 1 || echo 0)"
verdict "(4) le retour au départ ramène à 8,000 s, et non 6 (parti entre 6,0 et 6,6)" \
    "$([ "${#rr[@]}" = 2 ] && lecture_dans "${rr[0]}" 6.0 6.6 && [ "$(etat "${rr[1]}")" = "arrêté" ] \
        && [ "$(tete "${rr[1]}")" = "8.000" ] && echo 1 || echo 0)"
verdict "(5) le bouton Play suit le pré-roll : entre 6,0 et 6,6 s" \
    "$([ "${#rb[@]}" = 1 ] && lecture_dans "${rb[0]}" 6.0 6.6 && echo 1 || echo 0)"
verdict "(6) retenu au poste : cochée au lancement suivant" "$(echo "$coche" | grep -q "\[cochée\]" && echo 1 || echo 0)"
verdict "(7) le témoin du post-roll : Lire la sélection s'arrête à 4,000 s" \
    "$([ "${#st[@]}" = 1 ] && [ "$(etat "${st[0]}")" = "arrêté" ] && [ "$(tete "${st[0]}")" = "4.000" ] && echo 1 || echo 0)"
verdict "(8) 1 mesure de post-roll : en lecture au-delà de 4 s, l'arrêt à 6,000 s" \
    "$([ "${#sp[@]}" = 2 ] && lecture_dans "${sp[0]}" 4.05 6.0 && [ "$(etat "${sp[1]}")" = "arrêté" ] \
        && [ "$(tete "${sp[1]}")" = "6.000" ] && echo 1 || echo 0)"
verdict "(9) les deux : parti entre 0,0 et 0,6 s, arrêté à 6,000 s" \
    "$([ "${#sd[@]}" = 2 ] && lecture_dans "${sd[0]}" 0.0 0.6 && [ "$(etat "${sd[1]}")" = "arrêté" ] \
        && [ "$(tete "${sd[1]}")" = "6.000" ] && echo 1 || echo 0)"
verdict "(10) en anglais : « Pre-roll: 1 bar », « Post-roll: 1 bar »" "$([ "$en_pre" = 1 ] && [ "$en_post" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PRE-POST-ROLL : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PRE-POST-ROLL : la lecture prend son élan avant la tête et s'éteint après la sélection"
