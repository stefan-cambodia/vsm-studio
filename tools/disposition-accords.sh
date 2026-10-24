#!/usr/bin/env bash
# D592 — LA DISPOSITION DES ACCORDS (Édition ▸ Hauteur ▸ Disposition des accords ▸ Renverser vers le haut / vers le bas,
# Drop 2, Drop 3, Drop 2 et 4 ; les *Inversions* et *Drop Notes* du *Chord Editing* de Cubase), MESURÉE PAR LE MIDI
# EXPORTÉ APRÈS LE GESTE, LE TÉMOIN SANS GESTE À CÔTÉ.
#
# LE JEU (480 ticks par noire, notes de 240 ticks ; la fenêtre d'un accord, une quadruple-croche : 30 ticks) :
# A à 0 (60 64 67, serré) ; B à 480 (60 67 76, plus d'une octave) ; C à 960 (60 64 67 71, quatre sons) ; D JOUÉ à 1440,
# 1446, 1452 (62 65 69) ; une note seule à 1920 (48) ; E à 2400 (118 122 125, le suraigu) ; F à 2880 (2 5 9, le grave).
# L'export se relit dans l'ordre du temps puis des hauteurs : le geste ne touche pas au temps, et à un même instant
# c'est un multiensemble (la leçon du 13/09).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D592) :
#   (1) vers le haut : 64 67 72 | 67 76 84 | 64 67 71 72 | 74 65 69 | 48 | 118 122 125 | 5 9 14 ; 5 changés, 1 hors ;
#   (2) vers le bas  : 55 60 64 | 52 60 67 | 59 60 64 67 | 62 65 57 | 48 | 113 118 122 | 2 5 9 ; 5 changés, 1 hors ;
#   (3) Drop 2       : 52 60 67 | 55 60 76 | 55 60 64 71 | 62 53 69 | 48 | 110 118 125 | 2 5 9 ; 5 changés, 1 hors ;
#   (4) Drop 3       : 48 64 67 | 48 67 76 | 52 60 67 71 | 50 65 69 | 48 | 106 122 125 | 2 5 9 ; 5 changés, 1 hors ;
#   (5) Drop 2 et 4  : seul C bouge (48 55 64 71) ; 1 changé, 5 « trop peu de voix » ;
#   (6) Ctrl+Z après « vers le haut », la ligne VSM_DISPOSITION (5 changés) lue DANS LA MÊME COURSE : le témoin ;
#   (7) en anglais : Edit > Pitch > Chord voicing > Invert upward, Invert downward, Drop 2, Drop 3, Drop 2 + 4 ;
#   (8) le témoin sans geste : 20 notes, le jeu tel quel.
#
#   tools/disposition-accords.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-disposition.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 60), (0, 64), (0, 67), (480, 60), (480, 67), (480, 76), (960, 60), (960, 64), (960, 67), (960, 71),
         (1440, 62), (1446, 65), (1452, 69), (1920, 48), (2400, 118), (2400, 122), (2400, 125),
         (2880, 2), (2880, 5), (2880, 9)]
evs = []
for debut, h in notes:
    evs.append((debut, 1, bytes([0x90, h, 90])))
    evs.append((debut + 240, 0, bytes([0x80, h, 0])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "disposition", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison n
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    n=$(grep -cE "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # les hauteurs des notes d'un .mid, dans l'ordre du temps puis des hauteurs, ou « ABSENT »
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
        if x.type == "note_on" and x.velocity > 0: out.append((t, x.note))
print(" ".join(str(h) for _, h in sorted(out)) or "RIEN")
PY2
}
disposition() { grep -h "^VSM_DISPOSITION" "$brouillon/$1.txt" | head -1 | sed 's/^VSM_DISPOSITION : //'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

TOUT="600:choisir:0;900:menu:Édition > Sélection > Tout sélectionner"
D="Édition > Hauteur > Disposition des accords"
echo "=== D592 : renverser les accords, et les drops ==="
course haut VSM_DELAI=2200 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Renverser vers le haut;1600:exporter-midi:$brouillon/haut.mid"
course bas VSM_DELAI=2200 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Renverser vers le bas;1600:exporter-midi:$brouillon/bas.mid"
course drop2 VSM_DELAI=2200 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Drop 2;1600:exporter-midi:$brouillon/drop2.mid"
course drop3 VSM_DELAI=2200 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Drop 3;1600:exporter-midi:$brouillon/drop3.mid"
course drop24 VSM_DELAI=2200 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Drop 2 et 4;1600:exporter-midi:$brouillon/drop24.mid"
course annule VSM_DELAI=2400 VSM_GESTE_APRES="$TOUT;1200:menu:$D > Renverser vers le haut;1500:touche:ctrl + Z;1900:exporter-midi:$brouillon/annule.mid"
course temoin VSM_DELAI=2000 VSM_GESTE_APRES="600:choisir:0;1400:exporter-midi:$brouillon/temoin.mid"
course anglais VSM_LANGUE=en VSM_DELAI=1800 VSM_GESTE_APRES="600:choisir:0;1000:lister-menus"

for c in haut bas drop2 drop3 drop24 annule temoin; do
    printf '       %-7s [%s]\n' "$c" "$(hauteurs "$brouillon/$c.mid")"
    [ "$c" = temoin ] || printf '               %s\n' "$(disposition "$c")"
done
en="$(grep -cE "^VSM_MENU_LISTE : Edit > Pitch > Chord voicing > (Invert upward|Invert downward|Drop 2|Drop 3|Drop 2 \+ 4)( \[|$)" "$brouillon/anglais.txt")"
echo "       en anglais : $en entrée(s)"

TEMOIN="60 64 67 60 67 76 60 64 67 71 62 65 69 48 118 122 125 2 5 9"
egal() { [ "$(hauteurs "$brouillon/$1.mid")" = "$2" ] && echo 1 || echo 0; }
dit() { disposition "$1" | grep -q "$2" && echo 1 || echo 0; }
verdict "(1) vers le haut : B de deux octaves, E reste ; 5 changés, 1 hors" \
    "$(( $(egal haut "64 67 72 67 76 84 64 67 71 72 74 65 69 48 118 122 125 5 9 14") * $(dit haut "5 accord(s) changé(s), 1 hors de la tessiture") ))"
verdict "(2) vers le bas : F reste ; 5 changés, 1 hors" \
    "$(( $(egal bas "55 60 64 52 60 67 59 60 64 67 62 65 57 48 113 118 122 2 5 9") * $(dit bas "5 accord(s) changé(s), 1 hors de la tessiture") ))"
verdict "(3) Drop 2 ; 5 changés, 1 hors" \
    "$(( $(egal drop2 "52 60 67 55 60 76 55 60 64 71 62 53 69 48 110 118 125 2 5 9") * $(dit drop2 "5 accord(s) changé(s), 1 hors de la tessiture") ))"
verdict "(4) Drop 3 ; 5 changés, 1 hors" \
    "$(( $(egal drop3 "48 64 67 48 67 76 52 60 67 71 50 65 69 48 106 122 125 2 5 9") * $(dit drop3 "5 accord(s) changé(s), 1 hors de la tessiture") ))"
verdict "(5) Drop 2 et 4 : seul C ; 1 changé, 5 trop peu de voix" \
    "$(( $(egal drop24 "60 64 67 60 67 76 48 55 64 71 62 65 69 48 118 122 125 2 5 9") * $(dit drop24 "1 accord(s) changé(s), 0 hors de la tessiture, 5 trop peu de voix") ))"
verdict "(6) Ctrl+Z : le témoin, après 5 changés dans la même course" \
    "$(( $(egal annule "$TEMOIN") * $(dit annule "5 accord(s) changé(s)") ))"
verdict "(7) en anglais (5 entrées)" "$([ "$en" = 5 ] && echo 1 || echo 0)"
verdict "(8) le témoin sans geste : le jeu tel quel" "$(egal temoin "$TEMOIN")"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "DISPOSITION-ACCORDS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DISPOSITION-ACCORDS : chaque accord se renverse et se drope selon le rang de ses voix, et ce qui est laissé est dit"
