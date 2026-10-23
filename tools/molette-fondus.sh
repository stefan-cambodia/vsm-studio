#!/usr/bin/env bash
# D583.1 — LES FONDUS À MAJ+MOLETTE (Édition ▸ La molette règle le gain et les fondus des clips audio ; le « … and Fades »
# de l'*Use Mouse Wheel for Event Volume and Fades* de Cubase), MESURÉS PAR LES FONDUS DU CLIP RELUS DANS LE PROJET
# ENREGISTRÉ, LE TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES (120 BPM : une noire, 0,5 s). Une piste MIDI (piste 0), puis une piste AUDIO importée (piste 1, un la
# à 1 kHz de 4 s, posé à 0, sans fondu). Les crans passent par `molette:arrangement:fx,fy:crans:maj` (D525) ; les points
# — un quart et trois quarts du clip — se placent par `relever-clips`, jamais devinés sur une photo.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D583) :
#   (1) l'option cochée, trois crans vers le haut à Maj sur la moitié gauche : entrée 1,5 s, sortie 0 ;
#   (2) puis un vers le bas : entrée 1,0 s ;
#   (3) trois crans vers le haut sur la moitié droite : sortie 1,5 s, entrée 0 ;
#   (4) Ctrl+Z après une rafale de trois à gauche : 0 et 0, en UN pas ;
#   (5) le TÉMOIN sans l'option, trois crans à gauche : 0 et 0 ;
#   (6) huit crans à gauche : 4,0 s, la borne du clip ;
#   (7) relancé : l'entrée cochée ; en anglais, « The mouse wheel sets the gain and fades of audio clips ».
#
#   tools/molette-fondus.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-molette-fondus.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = b"".join(struct.pack("<hh", v, v) for v in
                  (int(0.5 * 32767 * math.sin(2 * math.pi * 1000.0 * n / sr)) for n in range(4 * sr)))
with wave.open(f"{b}/la1k.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x8f\x00\x80\x3c\x00\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "fondus", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "clips": [{"sourceStart": 0, "sourceLength": 1920, "start": 0, "length": 1920}]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; $3 = projet ; le reste : variables
    local nom="$1" maison="$2" projet="$3"
    shift 3
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU|IMPORT|MOLETTE|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fondus() {   # « entrée sortie » du clip audio (piste 1) d'un projet enregistré, en secondes, ou ABSENT
    python3 -c "
import json, sys
try:
    c = json.load(open(sys.argv[1]))['tracks'][1]['clips'][0]
    print(f\"{c.get('fadeIn', 0.0):.3f} {c.get('fadeOut', 0.0):.3f}\")
except Exception:
    print('ABSENT')" "$1/project.json"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Édition > La molette règle le gain et les fondus des clips audio"
M="molette:arrangement"

echo "=== D583.1 : les fondus à Maj+molette ==="
# LE PROJET DE DÉPART : la piste MIDI et son clip, puis l'import — vérifié avant tout (la leçon de courbe-de-gain).
course import "" "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la1k.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
[ "$(fondus "$brouillon/base")" = "0.000 0.000" ] \
    || { echo "  RATÉ le départ : un clip audio sans fondu attendu, lu [$(fondus "$brouillon/base")] — rien d'autre n'est mesuré"; exit 1; }
# LES POINTS : un quart et trois quarts du clip audio, en fractions de l'arrangement, lus par `relever-clips`.
course reperage "" "$brouillon/base" VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-clips"
lire() {   # $1 = piste, $2 = la part de la largeur du clip → « fx,fy » d'un point de son premier clip
    grep -h "^VSM_CLIP_ECRAN : piste $1 " "$brouillon/reperage.txt" | head -1 | python3 -c "
import re, sys
m = re.search(r'x ([0-9.]+)\.\.([0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.stdin.read())
if not m: print('ABSENT'); sys.exit(0)
x1, x2, y1, y2, w, h = map(float, m.groups())
p = float(sys.argv[1])
print(f'{(x1 + p * (x2 - x1)) / w:.4f},{(y1 + y2) / 2 / h:.4f}')" "$2"
}
GAUCHE="$(lire 1 0.25)"; DROITE="$(lire 1 0.75)"
echo "       points : le clip audio, moitié gauche [$GAUCHE], moitié droite [$DROITE]"
case "$GAUCHE$DROITE" in *ABSENT*) echo "  RATÉ le repérage : le clip audio n'a pas été lu à l'écran"; exit 1 ;; esac

maison="$(mktemp -d "$brouillon/home.XXXX")"
course entree "$maison" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="500:$OPTION;900:$M:$GAUCHE:3:maj;1500:enregistrer:$brouillon/entree"
course moins "" "$brouillon/base" VSM_DELAI=2600 \
    VSM_GESTE_APRES="500:$OPTION;900:$M:$GAUCHE:3:maj;1700:$M:$GAUCHE:-1:maj;2100:enregistrer:$brouillon/moins"
course sortie "" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="500:$OPTION;900:$M:$DROITE:3:maj;1500:enregistrer:$brouillon/sortie"
course defait "" "$brouillon/base" VSM_DELAI=2400 \
    VSM_GESTE_APRES="500:$OPTION;900:$M:$GAUCHE:3:maj;1300:touche:ctrl + Z;1700:enregistrer:$brouillon/defait"
course temoin "" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="900:$M:$GAUCHE:3:maj;1500:enregistrer:$brouillon/temoin"
course borne "" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="500:$OPTION;900:$M:$GAUCHE:8:maj;1500:enregistrer:$brouillon/borne"
course retenu "$maison" "$brouillon/base" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" "$brouillon/base" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

fe="$(fondus "$brouillon/entree")"; fm="$(fondus "$brouillon/moins")"; fs="$(fondus "$brouillon/sortie")"
fz="$(fondus "$brouillon/defait")"; ft="$(fondus "$brouillon/temoin")"; fb="$(fondus "$brouillon/borne")"
grep -h "^VSM_MOLETTE_FONDU" "$brouillon"/entree.txt "$brouillon"/sortie.txt | sed 's/^/       /'
echo "       fondus (entrée sortie, s) : trois à gauche [$fe] ; puis un en bas [$fm] ; trois à droite [$fs] ; Ctrl+Z [$fz] ; témoin [$ft] ; huit à gauche [$fb]"
coche="$(grep -h "^VSM_MENU_LISTE : Édition > La molette règle le gain et les fondus des clips audio" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Edit > The mouse wheel sets the gain and fades of audio clips" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"

verdict "(1) trois crans à gauche : entrée 1,5 s, sortie 0" "$([ "$fe" = "1.500 0.000" ] && echo 1 || echo 0)"
verdict "(2) puis un vers le bas : entrée 1,0 s" "$([ "$fm" = "1.000 0.000" ] && echo 1 || echo 0)"
verdict "(3) trois crans à droite : sortie 1,5 s" "$([ "$fs" = "0.000 1.500" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z après une rafale : 0 et 0, en un pas" "$([ "$fz" = "0.000 0.000" ] && echo 1 || echo 0)"
verdict "(5) le témoin sans l'option : 0 et 0" "$([ "$ft" = "0.000 0.000" ] && echo 1 || echo 0)"
verdict "(6) huit crans à gauche : 4,0 s, la borne du clip" "$([ "$fb" = "4.000 0.000" ] && echo 1 || echo 0)"
verdict "(7) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MOLETTE-FONDUS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MOLETTE-FONDUS : Maj+molette allonge et raccourcit les fondus d'un clip audio, d'une noire par cran, une rafale par pas"
