#!/usr/bin/env bash
# D525 — L'ARRANGEMENT DÉFILE VERTICALEMENT, ET LA MOLETTE Y FAIT QUELQUE CHOSE.
#
# LA RÈGLE GARDÉE. Un projet de 40 pistes dans une fenêtre de 1 280 × 800 : la vue
# d'arrangement n'en montre qu'une partie, et
#   (1) à l'ouverture, elle montre le HAUT (piste 1, décalage nul) et dit qu'il en
#       reste dessous ;
#   (2) la molette (5 crans vers le bas) fait défiler les PISTES — décalage non
#       nul, première piste visible au-delà de la 1 — sans toucher au temps ;
#   (3) 5 crans vers le haut ramènent au décalage nul — et (2) est le témoin qu'il
#       en était parti (une valeur qui revient ne prouve rien seule, 12/09) ;
#   (4) Maj + molette fait défiler le TEMPS, pas les pistes ;
#   (5) Ctrl + molette zoome (moins de mesures dans la fenêtre), pas les pistes ;
#   (6) choisir la piste 35 la fait voir ;
#   (7) puis choisir la piste 1 fait remonter jusqu'à elle.
#
# POURQUOI CE BANC. Avant D525, `trackTop` partait de la règle sans aucun décalage
# et aucun composant de l'arrangement ne recevait la molette : au-delà de la
# hauteur de la fenêtre, les pistes étaient inatteignables, et rien ne le relevait
# — les en-têtes de piste sont PEINTS, invisibles au relevé de textes.
#
#   tools/arrangement-defile.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-defile.XXXXXX")"
GARDER="${VSM_GARDER:-}"   # un dossier où recopier les photos, pour les regarder
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, hauteur, tempo=b""):
    # le tempo dans la PREMIÈRE piste (portes-des-pistes.sh : une piste de tempo à
    # part compte pour une piste de plus)
    evs = ([(0, tempo)] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    for i in range(8):
        evs += [(0 if i == 0 else 1440, bytes([0x90, hauteur, 100])), (480, bytes([0x80, hauteur, 0]))]
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
noms = [f"P{i + 1:02d}" for i in range(40)]
data = b"MThd" + struct.pack(">IHHH", 6, 1, len(noms), 480)
for i, nom in enumerate(noms):
    data += piste(nom, 36 + i, tempo=b"\xff\x51\x03\x07\xa1\x20" if i == 0 else b"")
open(d + "/midi/arrangement.mid", "wb").write(data)
def t(nom):
    return {"channel": 0, "color": "#FF6B9BFF", "effects": [],
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
            "name": nom}
json.dump({"format": "vsm-project", "version": 1, "title": "defile",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t(n) for n in noms]},
          open(d + "/project.json", "w"), indent=1)
PY

n=0
course() {   # $1 = nom ; $2 = VSM_GESTE_APRES -> le journal dans $brouillon/$1.txt
    n=$((n + 1))
    local maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x800" VSM_DELAI=3000 \
        VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="$2" \
        VSM_CAPTURE="$brouillon/$1.png" timeout 60 "$BIN" > "$brouillon/$1.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147) : un verbe mal adressé se dit
    # là, et c'est la seule trace qu'il laisse.
    grep -E "VSM_(GESTE_APRES|GESTE|MOLETTE) : .*(aucun|AUCUN|inconnu|refus|ATTENTION)" "$brouillon/$1.txt" \
        | sed "s/^/        journal ($1) : /" >&2
}
# Le N-ième relevé « pistes visibles » d'une course : « a b vues decalage possibles »
vertical() {
    grep "VSM_ARRANGEMENT : pistes visibles" "$brouillon/$1.txt" | sed -n "${2}p" \
        | sed -E 's/.*visibles ([0-9]+)\.\.([0-9]+) sur [0-9]+ \(([0-9]+) .*vertical ([0-9]+) px sur ([0-9]+) possibles.*/\1 \2 \3 \4 \5/'
}
fenetre() { grep "VSM_ARRANGEMENT : fen" "$brouillon/$1.txt" | sed -n "${2}p" | sed -E 's/.*fen[^ ]* ([0-9.]+) mesure.*/\1/'; }
horizontal() { grep "VSM_DEFILEMENT : arrangement" "$brouillon/$1.txt" | sed -n "${2}p" | sed -E 's/.*arrangement ([0-9]+),.*/\1/'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
champ() { echo "$1" | cut -d' ' -f"$2"; }
vrai() { python3 -c "import sys; print(1 if ($1) else 0)" 2>/dev/null || echo 0; }

echo "=== D525 : l'arrangement défile (40 pistes, 1 280 × 800) ==="

course ouverture "1200:relever-arrangement"
o="$(vertical ouverture 1)"
echo "       ouverture : pistes ${o:-?} (première dernière vues décalage possibles)"
verdict "(1) à l'ouverture : la piste 1 en haut, décalage nul, et des pistes dessous" \
    "$(vrai "'$o' != '' and $(champ "$o" 1) == 1 and $(champ "$o" 4) == 0 and $(champ "$o" 2) < 40 and $(champ "$o" 5) > 0")"

course molette "1200:relever-arrangement;1300:relever-defilement;1500:molette:arrangement:0.6,0.5:-5;1800:relever-arrangement;1900:relever-defilement"
a="$(vertical molette 1)"; b="$(vertical molette 2)"; h1="$(horizontal molette 1)"; h2="$(horizontal molette 2)"
echo "       molette −5 : ${a:-?} → ${b:-?} ; temps ${h1:-?} → ${h2:-?}"
verdict "(2) la molette fait défiler les pistes, pas le temps" \
    "$(vrai "'$b' != '' and $(champ "$b" 4) > 0 and $(champ "$b" 1) > 1 and '${h1:-x}' == '${h2:-y}'")"

course aller-retour "1500:molette:arrangement:0.6,0.5:-5;1700:relever-arrangement;1900:molette:arrangement:0.6,0.5:5;2200:relever-arrangement"
a="$(vertical aller-retour 1)"; b="$(vertical aller-retour 2)"
echo "       aller-retour : ${a:-?} → ${b:-?}"
verdict "(3) 5 crans vers le haut ramènent au haut — après en être parti" \
    "$(vrai "'$a' != '' and '$b' != '' and $(champ "$a" 4) > 0 and $(champ "$b" 4) == 0 and $(champ "$b" 1) == 1")"

course maj "1200:relever-defilement;1300:relever-arrangement;1500:molette:arrangement:0.6,0.5:-3:maj;1800:relever-defilement;1900:relever-arrangement"
h1="$(horizontal maj 1)"; h2="$(horizontal maj 2)"; a="$(vertical maj 1)"; b="$(vertical maj 2)"
echo "       Maj : temps ${h1:-?} → ${h2:-?} ; pistes ${a:-?} → ${b:-?}"
verdict "(4) Maj + molette fait défiler le temps, pas les pistes" \
    "$(vrai "'${h1:-}' != '' and '${h2:-}' != '' and ${h2:-0} > ${h1:-0} and '$a' == '$b'")"

course ctrl "1200:relever-arrangement;1500:molette:arrangement:0.6,0.5:3:ctrl;1800:relever-arrangement"
f1="$(fenetre ctrl 1)"; f2="$(fenetre ctrl 2)"; a="$(vertical ctrl 1)"; b="$(vertical ctrl 2)"
echo "       Ctrl : fenêtre ${f1:-?} → ${f2:-?} mesure(s) ; pistes ${a:-?} → ${b:-?}"
verdict "(5) Ctrl + molette zoome, et ne fait pas défiler les pistes" \
    "$(vrai "'${f1:-}' != '' and '${f2:-}' != '' and float('${f2:-0}') < float('${f1:-0}') and '$a' == '$b'")"

course choisir35 "1200:choisir:34;1600:relever-arrangement"
c="$(vertical choisir35 1)"
echo "       choisir la 35 : ${c:-?}"
verdict "(6) choisir la piste 35 la fait voir" \
    "$(vrai "'$c' != '' and $(champ "$c" 1) <= 35 <= $(champ "$c" 2)")"

course retour1 "1200:choisir:34;1500:choisir:0;1900:relever-arrangement"
c="$(vertical retour1 1)"
echo "       puis la 1 : ${c:-?}"
verdict "(7) puis choisir la piste 1 fait remonter jusqu'à elle" \
    "$(vrai "'$c' != '' and $(champ "$c" 1) == 1 and $(champ "$c" 4) == 0")"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
