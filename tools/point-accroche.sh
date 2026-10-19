#!/usr/bin/env bash
# D566.2 — LE POINT D'ACCROCHE D'UN CLIP AUDIO (clic droit ▸ Point d'accroche à la tête, Retirer le point d'accroche ;
# le *Snap Point to Cursor* de Cubase), MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM, 4/4 ; l'aimant de l'arrangement à la mesure, 1 920 ticks). Un
# fichier de 4 s importé, son clip posé à 300 (hors de la grille) : S, sans point ; P, le même avec un point à
# 1,1875 s du fichier — il sonne à 300 + 1 140 = 1 440.
#   (1) la tête à 1 440 (mes. 1 · 4), « Point d'accroche à la tête » : le point à 1,1875 s (± 1 ms) ; un pas, Ctrl+Z
#       le retire ;
#   (2) « Caler les débuts des clips sur la grille » : sur P, le POINT va à 1 920 et le clip commence à 780 ; le
#       TÉMOIN S, sans point, va à 0 ;
#   (3) glissé de 2 000 ticks à la souris, aimant allumé : sur P, le point tombe sur 3 840 et le clip commence à
#       2 700 ; le TÉMOIN S commence à 1 920 (± 2 ticks d'arrondi du pixel) ;
#   (4) « Retirer le point d'accroche » : `snapPoint` absent du projet, et le geste dit l'avoir retiré (un binaire qui
#       ignore le champ l'efface aussi à l'enregistrement : le projet seul ne prouve rien) ;
#   (5) la tête hors du clip : « Point d'accroche à la tête » grisé ; « Retirer » grisé sur S, offert sur P ;
#   (6) la photo de P : le trait du point est dessiné (pixels clairs dans la colonne que le relevé donne, absents de la
#       même colonne sur S), et le relevé le place à 300 + 1 140 ticks (à 1 px près).
#
#   tools/point-accroche.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-point-accroche.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
with wave.open(f"{b}/son.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<h", int(0.3 * 32767 * math.sin(2 * math.pi * 220 * n / sr))) for n in range(4 * sr)))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                   + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|POSITION|GLISSE|IMPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clip() {   # « début:point » du clip audio d'un project.json (point « - » s'il n'y en a pas), ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])]
if not c:
    print('ABSENT'); sys.exit(0)
p = c[0].get('snapPoint')
print(f\"{int(c[0]['start'])}:{'-' if p is None else format(p, '.4f')}\")" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
POSER="Point d'accroche à la tête"
RETIRER="Retirer le point d'accroche"
CALER="menu:Caler les débuts des clips sur la grille"

echo "=== D566.2 : le point d'accroche d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_VUE="sans-rapport" VSM_IMPORT_AUDIO="$brouillon/son.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
python3 - "$brouillon" <<'PY'
import json, shutil, sys
b = sys.argv[1]
for nom, point in (("S", None), ("P", 1.1875)):
    shutil.copytree(f"{b}/base", f"{b}/{nom}")
    d = json.load(open(f"{b}/{nom}/project.json", encoding="utf-8"))
    d.pop("view", None)   # la vue de l'import (un projet vide) ne sert pas : l'ouverture cadre le clip (D362)
    for t in d["tracks"]:
        for c in t.get("clips", []) if t.get("audio") else []:
            c["start"] = 300
            if point is not None: c["snapPoint"] = point
    json.dump(d, open(f"{b}/{nom}/project.json", "w", encoding="utf-8"), indent=1)
PY
for x in poser annuler caler caler-temoin glisser glisser-temoin retirer; do
    case $x in poser|annuler|caler-temoin|glisser-temoin) cp -r "$brouillon/S" "$brouillon/$x" ;; *) cp -r "$brouillon/P" "$brouillon/$x" ;; esac
done
course poser "$brouillon/poser" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" VSM_POSITION=1.4 \
    VSM_MENU_CONTEXTE="clip-audio-tous:$POSER" VSM_GESTE_APRES="1500:enregistrer:$brouillon/poser"
course annuler "$brouillon/annuler" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" VSM_POSITION=1.4 \
    VSM_MENU_CONTEXTE="clip-audio-tous:$POSER" VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/annuler"
course caler "$brouillon/caler" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir" \
    VSM_GESTE_APRES="1200:$CALER;1700:enregistrer:$brouillon/caler"
course caler-temoin "$brouillon/caler-temoin" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir" \
    VSM_GESTE_APRES="1200:$CALER;1700:enregistrer:$brouillon/caler-temoin"
course retirer "$brouillon/retirer" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_MENU_CONTEXTE="clip-audio-tous:$RETIRER" VSM_GESTE_APRES="1500:enregistrer:$brouillon/retirer"
course liste-s "$brouillon/S" VSM_DELAI=1800 VSM_VUE="sans-rapport,arrangement" VSM_POSITION=9.1 VSM_MENU_CONTEXTE="clip-audio:?"
course liste-p "$brouillon/P" VSM_DELAI=1800 VSM_VUE="sans-rapport,arrangement" VSM_POSITION=9.1 VSM_MENU_CONTEXTE="clip-audio:?"
course photo-p "$brouillon/P" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"
course photo-s "$brouillon/S" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"

# LE GLISSER : saisi au milieu du clip, tiré de 2 000 ticks -- un pas hors de la grille, sans quoi point et début
# donneraient la même chose.
ligne="$(grep '^VSM_CLIP_ECRAN : piste [0-9]* "son"' "$brouillon/photo-p.txt" | tail -1)"
read -r geste colonne < <(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
f = re.search(r'fenêtre ([0-9]+)x([0-9]+) à (-?[0-9]+),(-?[0-9]+)', sys.argv[1])
a = re.search(r'accroche x ([-0-9.]+)', sys.argv[1])
if not (m and f and a): print('AUCUN AUCUNE'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
fw, fh, ox, oy = (float(v) for v in f.groups())
parTick = (x2 - x1) / 3840.0
fy = (y1 + y2) / 2 / h
saisi = (x1 + x2) / 2
# la colonne du trait, en coordonnées de FENÊTRE (l'échelle de la photo se lit sur l'image) ; et sa place attendue
print(f'arrangement:{saisi / w:.5f},{fy:.5f}:{(saisi + 2000 * parTick) / w:.5f}',
      f'{ox + float(a.group(1)):.1f},{oy + y1:.0f},{oy + y2:.0f},{fw:.0f},{abs(float(a.group(1)) - (x1 + 1140 * parTick)):.1f}')
" "$ligne")
echo "       le clip à l'écran : ${ligne#VSM_CLIP_ECRAN : }"
if [ "$geste" != AUCUN ]; then
    course glisser "$brouillon/glisser" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste;1700:enregistrer:$brouillon/glisser"
    course glisser-temoin "$brouillon/glisser-temoin" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$geste;1700:enregistrer:$brouillon/glisser-temoin"
fi

po="$(clip "$brouillon/poser")"; an="$(clip "$brouillon/annuler")"; ca="$(clip "$brouillon/caler")"; ct="$(clip "$brouillon/caler-temoin")"
gl="$(clip "$brouillon/glisser")"; gt="$(clip "$brouillon/glisser-temoin")"; re="$(clip "$brouillon/retirer")"
entrees() { grep -h "^VSM_MENU_CONTEXTE : clip-audio = " "$1" | head -1 | tr '|' '\n' | grep -E "accroche" | sed 's/^ *//; s/ *$//' | tr '\n' ';'; }
ls_="$(entrees "$brouillon/liste-s.txt")"; lp="$(entrees "$brouillon/liste-p.txt")"
# LA PHOTO : des pixels clairs dans la colonne du point, sur P et pas sur S (le trait en tirets, et sa pointe).
trait() { "$PY" - "$1" "$colonne" <<'PY2'
import sys
try:
    from PIL import Image
    im = Image.open(sys.argv[1]).convert("RGB")
    x, y1, y2, fw, _ = (float(v) for v in sys.argv[2].split(","))
except Exception:
    print("ILLISIBLE"); sys.exit(0)
k = im.width / fw   # l'échelle de la photo
clairs = 0
for y in range(int(y1 * k) + 2, int(y2 * k) - 1):
    if any(sum(im.getpixel((int(round(x * k)) + dx, y))) > 600 for dx in (-1, 0, 1)): clairs += 1
print(clairs)
PY2
}
tp="$(trait "$brouillon/photo-p.png")"; ts="$(trait "$brouillon/photo-s.png")"
grep -h "^VSM_ACCROCHE : " "$brouillon"/poser.txt "$brouillon"/retirer.txt | sed 's/^/       /'
echo "       posé à la tête [$po] ; Ctrl+Z [$an] ; retiré [$re]"
echo "       calé : P [$ca], témoin S [$ct] ; glissé : P [$gl], témoin S [$gt]"
echo "       menu, tête hors du clip — S : $ls_  P : $lp"
echo "       photo, pixels clairs dans la colonne du point — P : $tp ; S : $ts (le relevé place le trait à ${colonne##*,} px de 300 + 1 140 ticks)"

proche() { python3 -c "
import sys
try:
    v = int(sys.argv[1].split(':')[0]); sys.exit(0 if abs(v - int(sys.argv[2])) <= 2 else 1)
except Exception:
    sys.exit(1)" "$1" "$2"; }
point_ok() { python3 -c "
import sys
try:
    sys.exit(0 if abs(float(sys.argv[1].split(':')[1]) - 1.1875) <= 0.001 else 1)
except Exception:
    sys.exit(1)" "$1"; }
verdict "(1) posé à la tête : 1,1875 s du fichier ; Ctrl+Z le retire" \
    "$(point_ok "$po" && [ "${po%%:*}" = 300 ] && [ "$an" = "300:-" ] && grep -q "^VSM_ACCROCHE : point posé à la tête (tick 1440) sur 1 clip" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(2) calé sur la grille : le point à 1 920, le clip à 780 ; le témoin sans point à 0" \
    "$([ "${ca%%:*}" = 780 ] && [ "${ct%%:*}" = 0 ] && echo 1 || echo 0)"
verdict "(3) glissé de 2 000 : le point sur 3 840, le clip à 2 700 ; le témoin à 1 920" \
    "$(proche "$gl" 2700 && proche "$gt" 1920 && echo 1 || echo 0)"
# LE GESTE DOIT DIRE QU'IL A RETIRÉ UN POINT : un binaire qui ignore le champ l'efface aussi en enregistrant.
verdict "(4) retiré : snapPoint absent du projet, et le geste l'a dit" \
    "$([ "$re" = "300:-" ] && grep -q "^VSM_ACCROCHE : point retiré sur 1 clip" "$brouillon/retirer.txt" && echo 1 || echo 0)"
verdict "(5) la tête hors du clip : poser grisé ; retirer grisé sur S, offert sur P" \
    "$([ "$ls_" = "Point d'accroche à la tête [grisee];Retirer le point d'accroche [grisee];" ] \
        && [ "$lp" = "Point d'accroche à la tête [grisee];Retirer le point d'accroche;" ] && echo 1 || echo 0)"
verdict "(6) la photo : le trait du point dessiné sur P, absent sur S" \
    "$(python3 -c "
import sys
try:
    p, s, e = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]); sys.exit(0 if p >= 8 and s <= 2 and e <= 1.0 else 1)
except ValueError:
    sys.exit(1)" "$tp" "$ts" "${colonne##*,}" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "POINT-ACCROCHE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "POINT-ACCROCHE : c'est le point d'accroche qui se glisse et se cale sur la grille, et non le début du clip"
