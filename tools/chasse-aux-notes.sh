#!/usr/bin/env bash
# D585.3 — LA CHASSE AUX NOTES COMMENCÉES (Transport ▸ Chasser les notes commencées ; le *Chase Events ▸ Note* de
# Cubase), MESURÉE PAR LES NOTES QUI SONNENT SUR LA PISTE ET SON NIVEAU, RELEVÉS PENDANT LA LECTURE, LE TÉMOIN DÉCOCHÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Une piste MIDI, Minimoog, une note 60 de 0 à 8 s
# [0, 7 680), et une note 72 à 10 s pour que la lecture ne s'arrête pas avant. La tête posée à 2 s (`VSM_POSITION=2.1`,
# mesure 2, temps 1), la lecture par le bouton Play ; `relever-sonnantes` dit les notes qui sonnent sur chaque piste (le
# masque des voyants du clavier, D32.3), le niveau efficace de la piste et le nombre de rebouclages.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D585) :
#   (1) la lecture lancée à 2 s, le relevé ~600 ms plus tard : la piste sonne [60], à plus de −60 dBFS ;
#   (2) le TÉMOIN décoché : [], sous −90 dBFS ;
#   (3) une boucle [2 s ; 3 s) allumée, la lecture lancée à 2 s, le relevé après le premier rebouclage (~1,6 s) : [60] ;
#   (4) son témoin décoché : [] ;
#   (5) la tête posée à 2 s, transport à l'arrêt : [] ;
#   (6) relancé dans le même HOME : décochée ; en anglais, « Chase notes already started ».
#
#   tools/chasse-aux-notes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-chasse-notes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Nappe"
corps += vlq(0) + b"\x90\x3c\x64" + vlq(7680) + b"\x80\x3c\x00"     # la nappe : 60, de 0 à 8 s
corps += vlq(1920) + b"\x90\x48\x64" + vlq(480) + b"\x80\x48\x00"   # 72 à 10 s : la lecture continue
corps += b"\x00\xff\x2f\x00"
for nom, boucle in (("base", False), ("boucle", True)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                 "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
    if boucle:
        transport["loop"] = {"enabled": True, "startTick": 1920, "endTick": 2880}   # [2 s ; 3 s)
    json.dump({"format": "vsm-project", "version": 1, "title": "chasse-" + nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": transport,
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Nappe", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; $3 = projet ; le reste : variables
    local nom="$1" maison="$2" projet="$3"
    shift 3
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|POSITION|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
sonne() { grep -h "^VSM_SONNANTES : piste 0 " "$brouillon/$1.txt" | tail -1; }
notes() { sonne "$1" | sed -n 's/^VSM_SONNANTES : piste 0 \[\([0-9 ]*\)\].*/\1/p'; }
db() { sonne "$1" | sed -n 's/.* RMS \([-0-9.]*\) dBFS.*/\1/p'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Transport > Chasser les notes commencées"

echo "=== D585.3 : la chasse aux notes commencées ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course depart "" base VSM_POSITION=2.1 VSM_DELAI=2200 VSM_GESTE_APRES="700:cliquer:Play;1300:relever-sonnantes;1400:cliquer:Stop"
course temoin "$maison" base VSM_POSITION=2.1 VSM_DELAI=2200 \
    VSM_GESTE_APRES="400:$OPTION;700:cliquer:Play;1300:relever-sonnantes;1400:cliquer:Stop"
course boucle "" boucle VSM_POSITION=2.1 VSM_DELAI=3000 VSM_GESTE_APRES="700:cliquer:Play;2300:relever-sonnantes;2400:cliquer:Stop"
course boucle-temoin "" boucle VSM_POSITION=2.1 VSM_DELAI=3000 \
    VSM_GESTE_APRES="400:$OPTION;700:cliquer:Play;2300:relever-sonnantes;2400:cliquer:Stop"
course arret "" base VSM_POSITION=2.1 VSM_DELAI=2000 VSM_GESTE_APRES="1300:relever-sonnantes"
course retenu "$maison" base VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" base VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

for n in depart temoin boucle boucle-temoin arret; do echo "       $n : $(sonne $n | sed 's/^VSM_SONNANTES : //')"; done
decoche="$(grep -h "^VSM_MENU_LISTE : Transport > Chasser les notes commencées" "$brouillon/retenu.txt" | grep -vc "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : Transport > Chase notes already started" "$brouillon/anglais.txt")"
echo "       relancé : $decoche entrée décochée ; en anglais : $en entrée(s)"
plus_que() { python3 -c "import sys; print(int(sys.argv[1] != '' and float(sys.argv[1]) > float(sys.argv[2])))" "$1" "$2"; }
moins_que() { python3 -c "import sys; print(int(sys.argv[1] != '' and float(sys.argv[1]) < float(sys.argv[2])))" "$1" "$2"; }
lecture() { sonne "$1" | grep -c "en lecture"; }
tours() { sonne "$1" | sed -n 's/.* \([0-9]*\) rebouclage(s).*/\1/p'; }

verdict "(1) départ à 2 s : [60], à plus de −60 dBFS" \
    "$([ "$(notes depart)" = 60 ] && [ "$(plus_que "$(db depart)" -60)" = 1 ] && [ "$(lecture depart)" = 1 ] && echo 1 || echo 0)"
verdict "(2) le témoin décoché : [], sous −90 dBFS" \
    "$([ -n "$(sonne temoin)" ] && [ -z "$(notes temoin)" ] && [ "$(moins_que "$(db temoin)" -90)" = 1 ] && [ "$(lecture temoin)" = 1 ] && echo 1 || echo 0)"
verdict "(3) après le premier rebouclage : [60]" \
    "$([ "$(notes boucle)" = 60 ] && [ "$(tours boucle)" = 1 ] && echo 1 || echo 0)"
verdict "(4) son témoin décoché : []" \
    "$([ -n "$(sonne boucle-temoin)" ] && [ -z "$(notes boucle-temoin)" ] && [ "$(tours boucle-temoin)" = 1 ] && echo 1 || echo 0)"
verdict "(5) la tête posée à l'arrêt : []" \
    "$([ -n "$(sonne arret)" ] && [ -z "$(notes arret)" ] && [ "$(lecture arret)" = 0 ] && echo 1 || echo 0)"
verdict "(6) retenu au poste ; en anglais" "$([ "$decoche" = 1 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CHASSE-AUX-NOTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CHASSE-AUX-NOTES : une note commencée avant la tête sonne au départ et à chaque tour de boucle, jamais à l'arrêt"
