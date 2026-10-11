#!/usr/bin/env bash
# D552.3 — GLISSER LE CONTENU D'UN CLIP (Ctrl+Alt + glisser dans son corps, le *Slip* de Cubase), MESURÉ PAR LE PROJET
# RELU ET PAR LE .mid DE CE QUI EST JOUÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, l'aimant à la mesure). Une piste, sa matière : do4 (60) à 0, ré4 (62) à
# 960, mi4 (64) à 1 920, fa4 (65) à 2 880 ; un clip A [1 920, 3 840) qui en montre [1 920, 3 840) — il joue 64, 65.
#   (1) Ctrl+Alt + glisser A d'une mesure vers la droite : la fenêtre ne bouge pas (A à 1 920, longueur 1 920), sa
#       matière recule d'une mesure (`sourceStart` 0) — A joue désormais 60 à 1 920 et 62 à 2 880 ;
#   (2) un pas : Ctrl+Z → `sourceStart` 1 920 ;
#   (3) A VERROUILLÉ : rien ne bouge, « 1 clip verrouillé est resté en place » ;
#   (4) le TÉMOIN, le même glisser SANS les touches : c'est un déplacement — A va à 3 840, `sourceStart` 1 920 ;
#   (5) H17 — le témoin puis Ctrl+Z rend A tel qu'il était (1 920, 1 920, 1 920). Trouvé par (2) : l'annulation passait
#       par la couverture des notes « écrites » hors clip (D337), et ÉTENDAIT A jusqu'aux notes de 0 et 960, que
#       l'ouverture laisse dehors.
#
#   tools/glisser-contenu.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-glisser-contenu.XXXXXX")"
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
for debut, h in ((0, 60), (960, 62), (1920, 64), (2880, 65)):
    corps += vlq(debut - t) + bytes([0x90, h, 100]); corps += vlq(480) + bytes([0x80, h, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
def ecrire(nom, verrou):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    clip = {"start": 1920, "length": 1920, "sourceStart": 1920, "sourceLength": 1920, "name": "A"}
    if verrou:
        clip["locked"] = True
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
ecrire("base", False)
ecrire("verrou", True)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GLISSE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clipA() {   # « début:longueur:sourceStart » de A, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [x for t in p['tracks'] for x in t.get('clips', []) if x.get('name') == 'A']
print(f\"{int(c[0]['start'])}:{int(c[0]['length'])}:{int(c[0].get('sourceStart', 0))}\" if c else 'ABSENT')
" "$1"
}
joue() {   # « tick:hauteur » des notes jouées, d'après un .mid exporté, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t = 0
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            out.append(f"{t}:{x.note}")
print(" ".join(out) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REFUS="^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place"

echo "=== D552.3 : glisser le contenu d'un clip ==="
course geometrie "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"
ligne="$(grep '^VSM_CLIP_ECRAN : piste 0 "A"' "$brouillon/geometrie.txt" | tail -1)"
geste="$(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
if not m: print('AUCUN'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
saisi = (x1 + x2) / 2                     # le milieu du clip
pas = (x2 - x1) * 1.05                    # une mesure et un peu : l'aimant ramène à une mesure
print(f'arrangement:{saisi / w:.5f},{(y1 + y2) / 2 / h:.5f}:{(saisi + pas) / w:.5f}')
" "$ligne")"
echo "       A à l'écran : ${ligne#VSM_CLIP_ECRAN : }"
for x in glisse annule temoin depl-annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
if [ "$geste" != AUCUN ]; then
    course glisser "$brouillon/glisse" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste:ctrl+alt;1700:enregistrer:$brouillon/glisse;2000:exporter-midi:$brouillon/glisse.mid"
    course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste:ctrl+alt;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"
    course verrou "$brouillon/verrou" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste:ctrl+alt;1700:enregistrer:$brouillon/verrou"
    course temoin "$brouillon/temoin" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste;1700:enregistrer:$brouillon/temoin"
    course depl-annule "$brouillon/depl-annule" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/depl-annule"
fi

g="$(clipA "$brouillon/glisse/project.json")"; j="$(joue "$brouillon/glisse.mid")"
z="$(clipA "$brouillon/annule/project.json")"; v="$(clipA "$brouillon/verrou/project.json")"; t="$(clipA "$brouillon/temoin/project.json")"; da="$(clipA "$brouillon/depl-annule/project.json")"
echo "       glissé [$g], joué [$j] ; Ctrl+Z [$z] ; verrouillé [$v] ; témoin sans touches [$t], puis Ctrl+Z [$da]"
grep -h "^D337" "$brouillon/annule.txt" "$brouillon/depl-annule.txt" | sed 's/^/       journal : /'

verdict "(1) la fenêtre reste, la matière recule d'une mesure : A joue 60 et 62" \
    "$([ "$g" = "1920:1920:0" ] && [ "$j" = "1920:60 2880:62" ] && echo 1 || echo 0)"
verdict "(2) un pas : Ctrl+Z rend la matière d'avant" "$([ "$z" = "1920:1920:1920" ] && echo 1 || echo 0)"
verdict "(3) verrouillé : rien ne bouge, et c'est dit" "$([ "$v" = "1920:1920:1920" ] && grep -q "$REFUS" "$brouillon/verrou.txt" && echo 1 || echo 0)"
verdict "(4) le témoin sans les touches est un déplacement" "$([ "$t" = "3840:1920:1920" ] && echo 1 || echo 0)"
verdict "(5) H17 : un déplacement puis Ctrl+Z rend le clip tel qu'il était, sans rien couvrir" \
    "$([ "$da" = "1920:1920:1920" ] && ! grep -q "^D337" "$brouillon/depl-annule.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "GLISSER-CONTENU : $rates contrôle(s) raté(s)"; exit 1; fi
echo "GLISSER-CONTENU : la matière glisse sous la fenêtre, qui ne bouge pas"
