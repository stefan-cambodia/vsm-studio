#!/usr/bin/env bash
# D528 — TENIR UNE NOTE CONTRE UN BORD DU PIANO ROLL FAIT DÉFILER LA VUE SOUS ELLE.
#
# LA RÈGLE GARDÉE. Une piste de huit notes à do3 (60), une par mesure, la vue ouverte en
# haut à 62 (seize pixels par hauteur).
# À 72 d'abord (02/10, 02 h 45) : la grille ne fait que 107 px à 1 280 × 800 (six
# rangées, les lanes du bas prennent le reste), do3 était HORS de la vue et la note se
# saisissait hors de la surface. À 62, do3 est la troisième rangée. La note de la mesure 2, tirée contre un bord et
# TENUE 30 répétitions (la répétition automatique de `mouseDrag`, `glisser-tenir:`) :
#   (1) au bord HAUT, elle monte plus haut que lâchée tout de suite, et la vue a défilé ;
#   (2) le témoin lâché tout de suite atterrit sur la plus haute hauteur visible (62, ou un
#       pas au-dessus : le glissé lui-même entre dans la bande) ;
#   (3) au bord DROIT, le temps défile et elle va plus loin que lâchée.
# Chaque cas contre son témoin lâché : le défilement au bord ne se juge que par l'écart
# (12/09 : une valeur qui revient ne prouve rien sans le témoin qui montre d'où elle part).
#
# LE BANC NE DEVINE PAS LA GÉOMÉTRIE : il lit d'abord la taille du piano roll et la
# largeur du clavier au relevé `VSM_PIANOROLL_RANG` (D528), puis vise la note en
# fraction de la surface. La note déplacée se relit dans le `.mid` enregistré (mido).
#
#   tools/pianoroll-bord.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
PY=analyse/.venv/bin/python
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
"$PY" -c "import mido" 2>/dev/null || { echo "REFUS : mido est requis pour relire le .mid"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pianoroll-bord.XXXXXX")"
GARDER="${VSM_GARDER:-}"
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
evs, t = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05notes")], 0
for m in range(1, 9):
    debut = (m - 1) * 1920
    evs += [(debut - t, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0]))]
    t = debut + 480
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "bord", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "notes"}],
           "view": {"selectedTrack": 0, "pianoRollPixelsPerTick": 0.08, "pianoRollScrollTick": 0,
                    "pianoRollTopNote": 62, "pianoRollNoteHeight": 16}},
          open(d + "/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES
    local maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x800" VSM_DELAI=3000 \
        VSM_VUE="sans-rapport" VSM_GESTE_APRES="$2" \
        VSM_CAPTURE="$brouillon/$1.png" timeout 60 "$BIN" > "$brouillon/$1.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|GLISSE_TENU) : .*(aucun|AUCUN|inconnu|refus|ATTENTION)" "$brouillon/$1.txt" \
        | sed "s/^/        journal ($1) : /" >&2
}
champ() { grep "VSM_PIANOROLL_RANG" "$brouillon/$1.txt" | tail -1 | grep -o " $2=[^ ]*" | cut -d= -f2; }

course geometrie "1200:relever-pianoroll"
taille="$(champ geometrie taille)"; clavier="$(champ geometrie clavier)"; haut="$(champ geometrie haut)"; rang="$(champ geometrie rang)"
echo "=== D528 : tenir une note contre un bord du piano roll ==="
echo "       piano roll ${taille:-?}, clavier ${clavier:-?} px, haut ${haut:-?}, ${rang:-?} px par hauteur"
if [ -z "$taille" ] || [ -z "$clavier" ] || [ "${haut:-}" != 62 ]; then
    echo "  RATÉ la géométrie n'a pas pu être relue (ou la vue ne s'ouvre pas à 62)"; echo "--- 1 raté(s)"; exit 1
fi
read -r f0x f0y fhy fdx < <(python3 - "$taille" "$clavier" "$rang" <<'PY'
import sys
l, h = (int(v) for v in sys.argv[1].split("x")); k, r = int(sys.argv[2]), int(sys.argv[3])
x = k + (1920 + 240) * 0.08          # le milieu de la note de la mesure 2
y = (62 - 60) * r + r / 2            # le milieu de la rangée de do3
print(f"{x / l:.4f} {y / h:.4f} {6 / h:.4f} {(l - 12 - 6) / l:.4f}")
PY
)

ou() {   # $1 = dossier enregistré -> « hauteur début » de la note qui n'est plus à sa place
    "$PY" - "$1" <<'PY'
import glob, sys
import mido
fichiers = glob.glob(sys.argv[1] + "/midi/*.mid")
if not fichiers:
    print("? ?"); sys.exit(0)
notes, t = [], 0
for piste in mido.MidiFile(fichiers[0]).tracks:
    t = 0
    for m in piste:
        t += m.time
        if m.type == "note_on" and m.velocity > 0:
            notes.append((m.note, t))
places = {(60, (k - 1) * 1920) for k in range(1, 9)}
deplacees = [n for n in notes if n not in places]
print(*(deplacees[0] if len(deplacees) == 1 else ("?", "?")))
PY
}
tenir() {   # $1 = nom ; $2 = fx1,fy1 ; $3 = répétitions
    course "$1" "1200:glisser-tenir:pianoroll:$f0x,$f0y:$2:$3;1700:relever-pianoroll;1800:enregistrer:$brouillon/$1-ecrit"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
vrai() { python3 -c "import sys; print(1 if ($1) else 0)" 2>/dev/null || echo 0; }

tenir haut-tenu "$f0x,$fhy" 30
tenir haut-lache "$f0x,$fhy" 0
read -r h1 d1 < <(ou "$brouillon/haut-tenu-ecrit"); read -r h0 d0 < <(ou "$brouillon/haut-lache-ecrit")
v1="$(champ haut-tenu haut)"; v0="$(champ haut-lache haut)"
echo "       bord haut : tenue → hauteur ${h1:-?} (vue en haut à ${v1:-?}) ; lâchée → hauteur ${h0:-?} (vue à ${v0:-?})"
verdict "(1) tenue au bord haut, la note monte plus haut que lâchée, et la vue a défilé" \
    "$(vrai "'${h1:-?}' != '?' and '${h0:-?}' != '?' and ${h1:-0} > ${h0:-0} and ${v1:-0} > 62")"
# Le glissé lui-même ENTRE dans la bande : cet appel-là fait déjà un pas (62 → 63), comme
# le témoin de D527 défilait de 13 px. Écrit « exactement 62 » avant la mesure du binaire
# corrigé (02/10, 02 h 48), à tort : la plus haute hauteur visible, à trois hauteurs au plus.
verdict "(2) le témoin lâché tout de suite : la plus haute hauteur visible, à un pas au plus de 62" \
    "$(vrai "'${h0:-?}' != '?' and '${h0:-?}' == '${v0:-?}' and ${v0:-0} - 62 <= 3")"

tenir droite-tenu "$fdx,$f0y" 30
tenir droite-lache "$fdx,$f0y" 0
read -r h1 d1 < <(ou "$brouillon/droite-tenu-ecrit"); read -r h0 d0 < <(ou "$brouillon/droite-lache-ecrit")
s1="$(champ droite-tenu defilement)"; s0="$(champ droite-lache defilement)"
echo "       bord droit : tenue → début ${d1:-?}, défilement ${s1:-?} ; lâchée → début ${d0:-?}, défilement ${s0:-?}"
verdict "(3) tenue au bord droit, le temps défile et la note va plus loin que lâchée" \
    "$(vrai "'${d1:-?}' != '?' and '${d0:-?}' != '?' and ${s1:-0} > ${s0:-0} and ${d1:-0} > ${d0:-0}")"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
