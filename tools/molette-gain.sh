#!/usr/bin/env bash
# D582.3 — LA MOLETTE RÈGLE LE GAIN DES CLIPS AUDIO (Édition ▸ La molette règle le gain et les fondus des clips audio ; l'*Use Mouse Wheel
# for Event Volume* de Cubase), MESURÉE PAR LE GAIN DU CLIP RELU DANS LE PROJET ENREGISTRÉ, LE TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES. Une piste MIDI et son clip (piste 0), puis une piste AUDIO importée (piste 1, un la à 1 kHz de 4 s).
# Les crans passent par `molette:arrangement:fx,fy:crans` (D525) ; les points se placent par `relever-clips` — le
# centre de chaque clip lu dans l'arrangement même, jamais deviné sur une photo (la leçon de D580).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D582) :
#   (1) l'option cochée, trois crans vers le haut au-dessus du clip audio : +3,0 dB ;
#   (2) puis deux vers le bas, une autre rafale : +1,0 dB ;
#   (3) Ctrl+Z après une rafale de trois : 0 dB, en UN pas ;
#   (4) le TÉMOIN sans l'option, trois crans : 0 dB ;
#   (5) au-dessus du clip MIDI, l'option cochée : son gain inchangé (1) ;
#   (6) relancé : l'entrée cochée ; en anglais, « The mouse wheel sets the gain and fades of audio clips ».
#
#   tools/molette-gain.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-molette.XXXXXX")"
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
json.dump({"format": "vsm-project", "version": 1, "title": "molette", "midi": {"file": "midi/arrangement.mid"},
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
gains() {   # « midi audio » : le gain des clips des pistes 0 et 1 d'un projet enregistré, ou ABSENT
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
    g = [t['clips'][0].get('gain', 1.0) for t in p['tracks'][:2]]
    print(' '.join(f'{v:.4f}' for v in g))
except Exception:
    print('ABSENT')" "$1/project.json"
}
db() { python3 -c "import math,sys; print(f'{20*math.log10(float(sys.argv[1])):+.1f}')" "$1" 2>/dev/null || echo "?"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Édition > La molette règle le gain et les fondus des clips audio"

echo "=== D582.3 : la molette règle le gain des clips audio ==="
# LE PROJET DE DÉPART : la piste MIDI et son clip, puis l'import — vérifié avant tout (la leçon de courbe-de-gain).
course import "" "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la1k.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
[ "$(gains "$brouillon/base")" = "1.0000 1.0000" ] \
    || { echo "  RATÉ le départ : deux clips à gain 1 attendus, lu [$(gains "$brouillon/base")] — rien d'autre n'est mesuré"; exit 1; }
# LES POINTS : le centre de chaque clip, en fractions de l'arrangement, lus par `relever-clips`.
course reperage "" "$brouillon/base" VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-clips"
lire() {   # $1 = piste → « fx,fy » du centre de son premier clip
    grep -h "^VSM_CLIP_ECRAN : piste $1 " "$brouillon/reperage.txt" | head -1 | python3 -c "
import re, sys
m = re.search(r'x ([0-9.]+)\.\.([0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.stdin.read())
if not m: print('ABSENT'); sys.exit(0)
x1, x2, y1, y2, w, h = map(float, m.groups())
print(f'{(x1 + x2) / 2 / w:.4f},{(y1 + y2) / 2 / h:.4f}')"
}
SUR_MIDI="$(lire 0)"; SUR_AUDIO="$(lire 1)"
echo "       points : clip MIDI [$SUR_MIDI] ; clip audio [$SUR_AUDIO]"
case "$SUR_AUDIO$SUR_MIDI" in *ABSENT*) echo "  RATÉ le repérage : un clip n'a pas été lu à l'écran"; exit 1 ;; esac

maison="$(mktemp -d "$brouillon/home.XXXX")"
course monte "$maison" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="500:$OPTION;900:molette:arrangement:$SUR_AUDIO:3;1500:enregistrer:$brouillon/monte"
course descend "" "$brouillon/base" VSM_DELAI=2600 \
    VSM_GESTE_APRES="500:$OPTION;900:molette:arrangement:$SUR_AUDIO:3;1700:molette:arrangement:$SUR_AUDIO:-2;2100:enregistrer:$brouillon/descend"
course defait "" "$brouillon/base" VSM_DELAI=2400 \
    VSM_GESTE_APRES="500:$OPTION;900:molette:arrangement:$SUR_AUDIO:3;1300:touche:ctrl + Z;1700:enregistrer:$brouillon/defait"
course temoin "" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="900:molette:arrangement:$SUR_AUDIO:3;1500:enregistrer:$brouillon/temoin"
course midi "" "$brouillon/base" VSM_DELAI=2200 \
    VSM_GESTE_APRES="500:$OPTION;900:molette:arrangement:$SUR_MIDI:3;1500:enregistrer:$brouillon/midi"
course retenu "$maison" "$brouillon/base" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" "$brouillon/base" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

gm="$(gains "$brouillon/monte")"; gd="$(gains "$brouillon/descend")"; gz="$(gains "$brouillon/defait")"
gt="$(gains "$brouillon/temoin")"; gi="$(gains "$brouillon/midi")"
grep -h "^VSM_MOLETTE_GAIN" "$brouillon"/monte.txt "$brouillon"/descend.txt | sed 's/^/       /'
echo "       gains (MIDI audio) : trois crans [$gm] ; puis deux en bas [$gd] ; Ctrl+Z [$gz] ; témoin [$gt] ; sur le MIDI [$gi]"
coche="$(grep -h "^VSM_MENU_LISTE : Édition > La molette règle le gain et les fondus des clips audio" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Edit > The mouse wheel sets the gain and fades of audio clips" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"
audio() { echo "$1" | awk '{print $2}'; }
midi_g() { echo "$1" | awk '{print $1}'; }

verdict "(1) trois crans : +3,0 dB" "$([ "$(db "$(audio "$gm")")" = "+3.0" ] && echo 1 || echo 0)"
verdict "(2) puis deux vers le bas : +1,0 dB" "$([ "$(db "$(audio "$gd")")" = "+1.0" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z après une rafale : 0 dB, en un pas" "$([ "$(audio "$gz")" = "1.0000" ] && echo 1 || echo 0)"
verdict "(4) le témoin sans l'option : 0 dB" "$([ "$(audio "$gt")" = "1.0000" ] && echo 1 || echo 0)"
verdict "(5) au-dessus du clip MIDI : inchangé" "$([ "$(midi_g "$gi")" = "1.0000" ] && [ "$(audio "$gi")" = "1.0000" ] && echo 1 || echo 0)"
verdict "(6) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MOLETTE-GAIN : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MOLETTE-GAIN : la molette monte et baisse le gain d'un clip audio, une rafale par pas"
