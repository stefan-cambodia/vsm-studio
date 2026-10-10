#!/usr/bin/env bash
# D547.2 — LE MONTAGE EN CHAÎNE (Shuffle), MESURÉ PAR LE PROJET RELU ET PAR L'EXPORT.
#
# LA RÈGLE GARDÉE. Une piste MIDI, trois clips d'une mesure séparés d'une mesure (480 ticks par noire) :
# A [0, 1920) · B [3840, 5760) · C [7680, 9600). B est écrit le premier au projet, pour que `premier-clip:0`
# le choisisse. La bascule : Affichage ▸ Montage en chaîne dans l'arrangement.
#   (1) en chaîne, Suppr sur B : C recule de la longueur de B (5760), A reste ; le TÉMOIN, hors chaîne : C
#       reste à 7680 ;
#   (2) un pas : en chaîne, Suppr puis Ctrl+Z → A · B · C ;
#   (3) en chaîne, le bord droit de B tiré d'une mesure (à la souris, `glisser:arrangement:…`, la géométrie
#       relevée par `relever-clips`) : C avance de ce que la fin de B a parcouru, au tick près ; hors chaîne : C
#       reste à 7680 ;
#   (4) C verrouillé, en chaîne, Suppr sur B : rien ne bouge, rien ne part, « Montage en chaîne refusé » dit ;
#   (5) la bascule retenue : relancée dans le même HOME, l'arrangement se dit « en chaîne » ;
#   (6) l'EXPORT : le projet monté en chaîne dure 2,0 s (une mesure à 120 BPM) de moins que le témoin ;
#   (7) la pastille photographiée en français et en anglais (regardée à la main).
#
#   tools/montage-en-chaine.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
MENU="Montage en chaîne dans l'arrangement"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-chaine.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import copy, json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20", 0
for debut in (0, 3840, 7680):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(1440) + bytes([0x80, 60, 0]); t = debut + 1440
corps += b"\x00\xff\x2f\x00"
mid = b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps
clip = lambda nom, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": nom}
projet = {"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
          "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                      "instrument": {"preferredPlugin": "vsm.minimoog"},
                      "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                      "clips": [clip("B", 3840), clip("A", 0), clip("C", 7680)]}]}
verrou = copy.deepcopy(projet)
verrou["tracks"][0]["clips"][2]["locked"] = True
for nom, p in (("base", projet), ("verrou", verrou)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(mid)
    json.dump(p, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables (HOME compris s'il faut le garder)
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|TOUCHE|MENU|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:longueur » des clips d'un project.json, triés par nom, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = sorted((x.get('name', '?'), int(x.get('start', 0)), int(x.get('length', 0))) for t in p['tracks'] for x in t.get('clips', []))
print(' '.join(f'{n}:{d}:{l}' for n, d, l in c) or 'AUCUN')
" "$1"
}
duree() {   # la durée d'un .wav en secondes, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import soundfile as sf
try:
    i = sf.info(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
print(f"{i.frames / i.samplerate:.3f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D547.2 : le montage en chaîne ==="
for x in ch-suppr t-suppr ch-annule ch-bord t-bord ch-verrou; do
    [ "$x" = ch-verrou ] && cp -r "$brouillon/verrou" "$brouillon/$x" || cp -r "$brouillon/base" "$brouillon/$x"
done
VUE="sans-rapport,arrangement,premier-clip:0"
course chaine-suppr "$brouillon/ch-suppr" VSM_DELAI=2500 VSM_MENU="$MENU" VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:enregistrer:$brouillon/ch-suppr"
course temoin-suppr "$brouillon/t-suppr" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:enregistrer:$brouillon/t-suppr"
course chaine-annule "$brouillon/ch-annule" VSM_DELAI=3000 VSM_MENU="$MENU" VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:touche:ctrl + Z;2200:enregistrer:$brouillon/ch-annule"
course chaine-verrou "$brouillon/ch-verrou" VSM_DELAI=2500 VSM_MENU="$MENU" VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:enregistrer:$brouillon/ch-verrou"

# Le bord droit de B : sa place à l'écran d'abord, puis le geste, tiré de la largeur de B (une mesure).
course geometrie "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"
ligne="$(grep '^VSM_CLIP_ECRAN : piste 0 "B"' "$brouillon/geometrie.txt" | tail -1)"
glisse="$(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
if not m: print('AUCUNE'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
print(f'arrangement:{(x2 - 2) / w:.5f},{(y1 + y2) / 2 / h:.5f}:{(x2 - 2 + (x2 - x1)) / w:.5f}')
" "$ligne")"
echo "       B à l'écran : ${ligne#VSM_CLIP_ECRAN : } → glisser:$glisse"
if [ "$glisse" != AUCUNE ]; then
    course chaine-bord "$brouillon/ch-bord" VSM_DELAI=2500 VSM_MENU="$MENU" VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$glisse;1700:enregistrer:$brouillon/ch-bord"
    course temoin-bord "$brouillon/t-bord" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$glisse;1700:enregistrer:$brouillon/t-bord"
fi

cs="$(clips "$brouillon/ch-suppr/project.json")"; ts="$(clips "$brouillon/t-suppr/project.json")"
ca="$(clips "$brouillon/ch-annule/project.json")"; cb="$(clips "$brouillon/ch-bord/project.json")"
tb="$(clips "$brouillon/t-bord/project.json")"; cv="$(clips "$brouillon/ch-verrou/project.json")"
echo "       Suppr sur B : en chaîne [$cs] ; témoin [$ts] ; en chaîne puis Ctrl+Z [$ca]"
echo "       bord de B tiré d'une mesure : en chaîne [$cb] ; témoin [$tb]"
echo "       C verrouillé, en chaîne, Suppr sur B : [$cv]"
verdict "(1) en chaîne, C recule de la longueur de B ; le témoin le laisse" \
    "$([ "$cs" = "A:0:1920 C:5760:1920" ] && [ "$ts" = "A:0:1920 C:7680:1920" ] && echo 1 || echo 0)"
verdict "(2) un pas : Ctrl+Z rend A · B · C" "$([ "$ca" = "A:0:1920 B:3840:1920 C:7680:1920" ] && echo 1 || echo 0)"
# La chaîne se juge sur ce que la fin de B a RÉELLEMENT parcouru : C avance d'autant, au tick près. (La fin
# elle-même tombe hors de la grille du décalage entre le point saisi et le bord : c'est D547.4, à part.)
verdict "(3) le bord de B tiré : C avance d'autant que la fin de B en chaîne, reste hors chaîne" \
    "$(python3 -c "
import sys
def lire(t):
    return {n: (int(d), int(l)) for n, d, l in (x.split(':') for x in t.split())}
try:
    c, t = lire(sys.argv[1]), lire(sys.argv[2])
except Exception:
    sys.exit(1)
pousse = c['B'][1] - 1920
ok = (pousse >= 1900 and c['C'][0] - 7680 == pousse and c['A'] == (0, 1920)
      and t['B'] == c['B'] and t['C'] == (7680, 1920))
sys.exit(0 if ok else 1)" "$cb" "$tb" && echo 1 || echo 0)"
verdict "(4) C verrouillé : rien ne bouge, le refus est dit" \
    "$([ "$cv" = "A:0:1920 B:3840:1920 C:7680:1920" ] \
        && grep -q "^VSM_BOITE : Montage en chaîne refusé : Sur 1 piste, un clip verrouillé suit le geste" "$brouillon/chaine-verrou.txt" \
        && echo 1 || echo 0)"

# (5) La bascule retenue : un lancement la coche, le suivant (même HOME) la relit.
maison="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_MENU="$MENU" VSM_DELAI=1500 \
    VSM_CAPTURE="$brouillon/retenue-1.png" timeout 60 "$BIN" > "$brouillon/retenue-1.txt" 2>&1
env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" VSM_DELAI=2000 \
    VSM_GESTE_APRES="1200:relever-arrangement" VSM_CAPTURE="$brouillon/retenue-2.png" timeout 60 "$BIN" > "$brouillon/retenue-2.txt" 2>&1
bascules="$(grep "^VSM_ARRANGEMENT : aimant" "$brouillon/retenue-2.txt" | tail -1)"
echo "       relancé : ${bascules:-aucun relevé}"
verdict "(5) la bascule retenue d'un lancement à l'autre" "$(echo "$bascules" | grep -q ", en chaîne$" && echo 1 || echo 0)"

# (6) L'export : une mesure de moins.
for x in ch-suppr t-suppr; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done
dc="$(duree "$brouillon/ch-suppr.wav")"; dt="$(duree "$brouillon/t-suppr.wav")"
echo "       export : en chaîne $dc s ; témoin $dt s"
verdict "(6) l'export monté en chaîne dure 2,0 s de moins" \
    "$(python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[2]) - float(sys.argv[1]) - 2.0) <= 0.05 else 1)" "$dc" "$dt" 2>/dev/null \
        && echo 1 || echo 0)"

# (7) La pastille, en français et en anglais.
# Le libellé du menu est celui de LA LANGUE : le premier banc passait le libellé français à l'interface
# anglaise, le menu n'était pas joué, et la photo vide de pastille comptait quand même. Le contrôle lit donc
# l'EFFET (« en chaîne » au relevé), pas l'existence du fichier.
photos=0
for langue in fr en; do
    entree="$MENU"; [ "$langue" = en ] && entree="Shuffle editing in the arrangement"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" \
        VSM_MENU="$entree" VSM_VUE="sans-rapport,arrangement" VSM_DELAI=2000 VSM_GESTE_APRES="1200:relever-arrangement" \
        VSM_CAPTURE="$brouillon/pastille-$langue.png" timeout 60 "$BIN" > "$brouillon/pastille-$langue.txt" 2>&1
    grep -E "VSM_MENU : .*(aucune|AUCUNE|grisée|ambigu)" "$brouillon/pastille-$langue.txt" | sed "s/^/        journal (pastille-$langue) : /" >&2
    [ -s "$brouillon/pastille-$langue.png" ] && grep -q "^VSM_ARRANGEMENT : aimant.*, en chaîne$" "$brouillon/pastille-$langue.txt" \
        && photos=$((photos + 1))
    sleep 3
done
echo "       pastille : $brouillon/pastille-fr.png, pastille-en.png (VSM_GARDER pour les garder)"
verdict "(7) la pastille photographiée en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MONTAGE-EN-CHAINE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MONTAGE-EN-CHAINE : la suite de la piste suit le geste, en chaîne et seulement en chaîne"
