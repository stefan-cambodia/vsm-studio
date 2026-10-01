#!/usr/bin/env bash
# D529 — UN LASSO TENU CONTRE LE BORD CHOISIT AU-DELÀ DE LA VUE.
#
# LA RÈGLE GARDÉE.
#   (1) l'arrangement, quarante pistes à un clip d'une mesure : un lasso parti du vide à
#       côté du clip de P02, tiré contre le bord bas et TENU 30 répétitions, choisit plus
#       de clips que lâché tout de suite — et le témoin lâché en choisit six (P02 à P07) ;
#   (2) le piano roll (le rectangle veut Ctrl : sans lui, l'appui dans le vide DESSINE une
#       note — vu à la première course, 02/10 03 h 18), huit notes descendant de do3 (60) à si♭1 (46) par tons, une par
#       temps, la vue ouverte en haut à 62 : un rectangle tenu contre le bord bas choisit
#       plus de notes que lâché — et le témoin lâché en choisit deux (60 et 58), ou trois
#       quand le pas du glissé lui-même découvre 56.
# Le défilement au bord ne se juge que par l'écart avec le témoin lâché (D527).
#
# POURQUOI. Le lasso et le rectangle partaient d'un point de la FENÊTRE : la vue qui
# défile sous eux laissait l'origine sur place à l'écran, et le rectangle ne couvrait
# jamais plus qu'une fenêtre. Ils n'étaient donc pas des gestes qui défilent (D527, D528).
#
#   tools/selection-au-bord.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-selection-bord.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/pistes/midi" "$brouillon/notes/midi"
python3 - "$brouillon" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, notes, tempo=False):
    evs = ([(0, b"\xff\x51\x03\x07\xa1\x20")] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for debut, hauteur, duree in notes:
        evs += [(debut - t, bytes([0x90, hauteur, 100])), (duree, bytes([0x80, hauteur, 0]))]
        t = debut + duree
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def projet(dossier, pistes, vue=None):
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480)
    for i, (nom, notes) in enumerate(pistes):
        data += piste(nom, notes, tempo=(i == 0))
    open(f"{dossier}/midi/arrangement.mid", "wb").write(data)
    doc = {"format": "vsm-project", "version": 1, "title": "selection", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": nom} for nom, _ in pistes]}
    if vue:
        doc["view"] = vue
    json.dump(doc, open(f"{dossier}/project.json", "w"), indent=1)
# Quarante pistes à UNE note en mesure 1 (un clip d'une mesure) ; P40 en a une de plus en
# mesure 8, pour que le cadrage d'ouverture montre huit mesures et laisse du vide à droite.
pistes = [(f"P{i + 1:02d}", [(0, 60, 480)] + ([(7 * 1920, 60, 480)] if i == 39 else [])) for i in range(40)]
projet(d + "/pistes", pistes)
notes = [(k * 480, 60 - 2 * k, 240) for k in range(8)]
projet(d + "/notes", [("notes", notes)],
       {"selectedTrack": 0, "pianoRollPixelsPerTick": 0.08, "pianoRollScrollTick": 0,
        "pianoRollTopNote": 62, "pianoRollNoteHeight": 16})
PY

course() {   # $1 = nom ; $2 = projet ; $3 = VSM_VUE ; $4 = VSM_GESTE_APRES
    local maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/$2" VSM_TAILLE="1280x800" VSM_DELAI=3000 \
        VSM_VUE="$3" VSM_GESTE_APRES="$4" \
        VSM_CAPTURE="$brouillon/$1.png" timeout 60 "$BIN" > "$brouillon/$1.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|GLISSE_TENU) : .*(aucun|AUCUN|inconnu|refus|ATTENTION)" "$brouillon/$1.txt" \
        | sed "s/^/        journal ($1) : /" >&2
}
clips() { grep "VSM_ARRANGEMENT : pistes visibles" "$brouillon/$1.txt" | tail -1 | grep -o "[0-9]* clip(s) choisi(s)" | cut -d' ' -f1; }
notes_choisies() { grep "VSM_SELECTION" "$brouillon/$1.txt" | tail -1 | grep -o "[0-9]* note(s)" | cut -d' ' -f1; }
champ() { grep "VSM_PIANOROLL_RANG" "$brouillon/$1.txt" | tail -1 | grep -o " $2=[^ ]*" | cut -d= -f2; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
vrai() { python3 -c "import sys; print(1 if ($1) else 0)" 2>/dev/null || echo 0; }

echo "=== D529 : un lasso tenu contre le bord choisit au-delà de la vue ==="
# L'arrangement : 374 px de haut (22 de règle, pistes de 56 px), P02 à fy 0,283 ; le lasso
# part du vide à fx 0,85 (mesure ~7) et va au bord bas, dans la colonne de la mesure 1.
for cas in tenu:30 lache:0; do
    nom="lasso-${cas%%:*}"
    course "$nom" pistes "sans-rapport,arrangement" \
        "1200:glisser-tenir:arrangement:0.85,0.283:0.32,0.985:${cas##*:};1800:relever-arrangement"
done
c1="$(clips lasso-tenu)"; c0="$(clips lasso-lache)"
echo "       arrangement : tenu → ${c1:-?} clip(s) choisi(s) ; lâché → ${c0:-?}"
verdict "(1) tenu au bord bas, le lasso choisit plus de clips que lâché" \
    "$(vrai "'${c1:-}' != '' and '${c0:-}' != '' and ${c1:-0} > ${c0:-0}")"
verdict "(1 bis) le témoin lâché : les six clips de P02 à P07" "$(vrai "'${c0:-}' == '6'")"

course geometrie notes "sans-rapport" "1200:relever-pianoroll"
taille="$(champ geometrie taille)"; clavier="$(champ geometrie clavier)"; haut="$(champ geometrie haut)"
if [ -z "$taille" ] || [ -z "$clavier" ] || [ "${haut:-}" != 62 ]; then
    echo "  RATÉ la géométrie du piano roll n'a pas pu être relue (ou la vue ne s'ouvre pas à 62)"; rates=$((rates + 1))
else
    read -r fx0 fy0 fx1 fy1 < <(python3 - "$taille" "$clavier" <<'PY'
import sys
l, h = (int(v) for v in sys.argv[1].split("x")); k = int(sys.argv[2])
# du vide à la rangée de 62 (au début du morceau) jusqu'au bord bas, après la dernière note
print(f"{(k + 6) / l:.4f} {8 / h:.4f} {(k + 3600 * 0.08) / l:.4f} {(h - 12 - 6) / h:.4f}")
PY
)
    for cas in tenu:30 lache:0; do
        nom="rectangle-${cas%%:*}"
        course "$nom" notes "sans-rapport" "1200:glisser-tenir:pianoroll:$fx0,$fy0:$fx1,$fy1:${cas##*:}:ctrl;1800:relever-pianoroll"
    done
    n1="$(notes_choisies rectangle-tenu)"; n0="$(notes_choisies rectangle-lache)"
    echo "       piano roll ${taille} : tenu → ${n1:-?} note(s) choisie(s) ; lâché → ${n0:-?}"
    verdict "(2) tenu au bord bas, le rectangle choisit plus de notes que lâché" \
        "$(vrai "'${n1:-}' != '' and '${n0:-}' != '' and ${n1:-0} > ${n0:-0}")"
    # Le glissé lui-même ENTRE dans la bande et fait un pas (62 → 61), qui découvre 56 :
    # écrit « deux » avant la mesure du binaire corrigé (02/10, 03 h 25), à tort — la même
    # omission qu'aux témoins de D527 et de D528.
    verdict "(2 bis) le témoin lâché : les notes visibles, à un pas près — 60 et 58, ou avec 56" "$(vrai "'${n0:-}' in ('2', '3')")"
fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
