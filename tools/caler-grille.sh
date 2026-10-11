#!/usr/bin/env bash
# D552.1 — CALER LES DÉBUTS DES CLIPS SUR LA GRILLE (Édition ▸ Caler les débuts des clips sur la grille), MESURÉ PAR LE
# PROJET RELU.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4 ; l'aimant de l'arrangement à la mesure, 1 920 ticks). Projet G : une
# piste, A à 300, B à 2 100, C à 5 000 VERROUILLÉ, D à 3 840 (déjà sur la grille), chacun d'une demi-mesure.
#   (1) tout choisi, le geste : A à 0, B à 1 920, D inchangé ; C reste à 5 000, et « 1 clip verrouillé est resté en
#       place » est dit ; les fenêtres suivent (un déplacement, pas un rognage : `sourceStart` inchangé) ;
#   (2) un pas : Ctrl+Z → A à 300, B à 2 100 ;
#   (3) tout déjà sur la grille (G-cale) : « Rien à caler » dit, aucun pas.
#
#   tools/caler-grille.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-caler-grille.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def ecrire(nom, clips):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\xff\x2f\x00"
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
clip = lambda n, d, **x: dict({"start": d, "length": 960, "sourceStart": d, "sourceLength": 960, "name": n}, **x)
ecrire("G", [clip("A", 300), clip("B", 2100), clip("D", 3840), clip("C", 5000, locked=True)])
ecrire("G-cale", [clip("A", 0), clip("B", 1920)])
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:sourceStart » des clips, triés par nom, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{c.get('name', '?')}:{int(c.get('start', 0))}:{int(c.get('sourceStart', 0))}\" for t in p['tracks'] for c in sorted(t.get('clips', []), key=lambda c: c.get('name', ''))) or 'AUCUN')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement,tout-choisir"
CALER="menu:Caler les débuts des clips sur la grille"
REFUS="^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place"

echo "=== D552.1 : caler les débuts des clips sur la grille ==="
for x in Gc Gz; do cp -r "$brouillon/G" "$brouillon/$x"; done
course caler "$brouillon/Gc" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$CALER;1700:enregistrer:$brouillon/Gc"
course annuler "$brouillon/Gz" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$CALER;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Gz"
course rien "$brouillon/G-cale" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$CALER;1500:relever-historique;1700:enregistrer:$brouillon/G-cale"

c="$(clips "$brouillon/Gc/project.json")"; z="$(clips "$brouillon/Gz/project.json")"; r="$(clips "$brouillon/G-cale/project.json")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/rien.txt" | tail -1)"
echo "       calé [$c] ; Ctrl+Z [$z] ; déjà calé [$r], ${hist:-historique non relevé}"
grep -h "^VSM_CALER_GRILLE" "$brouillon/caler.txt" "$brouillon/rien.txt" | sed 's/^/       /'

verdict "(1) chaque début à la mesure la plus proche, la fenêtre suit, le verrouillé reste et c'est dit" \
    "$([ "$c" = "A:0:300 B:1920:2100 C:5000:5000 D:3840:3840" ] && grep -q "$REFUS" "$brouillon/caler.txt" && echo 1 || echo 0)"
verdict "(2) un pas : Ctrl+Z rend les places d'avant" "$([ "$z" = "A:300:300 B:2100:2100 C:5000:5000 D:3840:3840" ] && echo 1 || echo 0)"
verdict "(3) déjà sur la grille : « Rien à caler » dit, aucun pas" \
    "$([ "$r" = "A:0:0 B:1920:1920" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] && grep -q "^VSM_BOITE : Rien à caler" "$brouillon/rien.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CALER-GRILLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CALER-GRILLE : chaque clip commence sur la grille, sa matière avec lui"
