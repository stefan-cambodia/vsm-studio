#!/usr/bin/env bash
# D610 — ADAPTER UNE PARTIE GENERAL MIDI À LA MACHINE, mesuré dans l'application : le bouton de la fenêtre de la carte
# pressé (`cliquer:`, le vrai bouton) sur une partie General MIDI jouée par la TR-909 ; les entrées posées, le compte et
# ce qui reste sans équivalent ; Ctrl+Z ; l'anglais ; le témoin.
#
# LE JEU : une piste « Batterie » sur la TR-909, canal 10 : grosse caisse 36, toms 41 et 43, charleston pédale 44, ride
# 51, crash 2 57 — une noire chacun. La 909 nomme 36 ; elle n'a ni tom en 41/43, ni pédale, ni crash 2, ni ride.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D610, attendu (c)) :
#   le bouton pressé → les entrées 41 → 45, 43 → 45, 44 → 42, 57 → 49 ; « 4 note(s) adaptée(s), 1 sans équivalent » et
#   le ride (51) nommé ; Ctrl+Z les retire d'un coup ; l'anglais ; le témoin (D609) : le bouton absent.
#
#   tools/adapter-batterie.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D609/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-adapter.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"
os.makedirs(f"{b}/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"
t = 0
for i, h in enumerate((36, 41, 43, 44, 51, 57)):
    corps += vlq(i * 480 - t) + bytes([0x99, h, 110]) + vlq(240) + bytes([0x89, h, 0]); t = i * 480 + 240
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "adapter", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=2800 VSM_GESTE_APRES="$gestes" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# LA FENÊTRE DÉGÉNÉRÉE (128 × 128) : écran ÉTEINT, les fenêtres flottantes neuves sortent ainsi (D607, D608 ; jamais
# pendant les 207 bancs de la série de D605-D609, écran allumé) — le bouton n'a plus de surface. La course est REJOUÉE
# une fois, et c'est DIT (la règle des boîtes de D72).
rejouees=0
course_sure() {
    course "$@"
    local photo
    photo="$(ls "$brouillon/$2"-*-Carte*.png "$brouillon/$2"-*-Drum*.png 2>/dev/null | head -1)"
    if [ -n "$photo" ] && python3 -c "import struct,sys; d=open(sys.argv[1],'rb').read(24); w,h=struct.unpack('>II',d[16:24]); sys.exit(0 if w < 200 or h < 200 else 1)" "$photo"; then
        echo "       course « $2 » REJOUÉE : fenêtre dégénérée au premier essai ($(basename "$photo"))"
        rejouees=$((rejouees + 1))
        rm -rf "$brouillon/p-$2" "$photo"
        course "$@"
    fi
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
sortie() { grep "^VSM_CARTE : $2 " "$brouillon/$1.txt" | tail -1 | sed 's/.* → //'; }

echo "=== D610 : adapter une partie General MIDI à la machine ==="
ADAPTER="1200:relever-carte;1400:cliquer:fenetre:Carte de batterie:Adapter General MIDI à la machine;1800:relever-carte"
course_sure "$BIN" adapte "$ADAPTER"
course_sure "$BIN" annule "$ADAPTER;2100:touche:ctrl + Z;2400:relever-carte"
course_sure "$BIN" en "1200:relever-carte;1400:cliquer:fenetre:Drum map:Adapt General MIDI to the machine;1800:relever-carte" VSM_LANGUE=en

A="$(grep "^VSM_CARTE : adapt" "$brouillon/adapte.txt" | head -1)"
echo "       $A"
grep "^VSM_CARTE : [0-9]\|^VSM_CARTE : remarque" "$brouillon/adapte.txt" | tail -7 | sed 's/^/       /'
verdict "(c) le compte : 4 note(s) adaptée(s), 1 sans équivalent, le ride (51) nommé" \
    "$(echo "$A" | grep -q " : 4 note(s) adaptée(s), 1 sans équivalent : ride (51)$" && echo 1 || echo 0)"
verdict "(c) les sorties : 41 → 45, 43 → 45, 44 → 42, 57 → 49 ; 36 et 51 elles-mêmes" \
    "$([ "$(sortie adapte 41)" = "tom grave (45)" ] && [ "$(sortie adapte 43)" = "tom grave (45)" ] \
       && [ "$(sortie adapte 44)" = "charleston fermé (42)" ] && [ "$(sortie adapte 57)" = "crash (49)" ] \
       && [ "$(sortie adapte 36)" = "elle-même" ] && [ "$(sortie adapte 51)" = "elle-même" ] && echo 1 || echo 0)"
verdict "(c) la fenêtre le dit : « Sans équivalent sur cette machine : ride (51) »" \
    "$(grep -q "^VSM_CARTE : remarque : Sans équivalent sur cette machine : ride (51)$" "$brouillon/adapte.txt" && echo 1 || echo 0)"
U="$(grep "^VSM_CARTE : piste" "$brouillon/annule.txt" | tail -1)"
echo "       Ctrl+Z : $U"
verdict "(c) Ctrl+Z retire les quatre d'un coup : 0 note déplacée, 41 → elle-même" \
    "$(echo "$U" | grep -q " ; 0 note(s) déplacée(s)$" && [ "$(sortie annule 41)" = "elle-même" ] && echo 1 || echo 0)"
E="$(grep "^VSM_CARTE : adapt" "$brouillon/en.txt" | head -1)"; ER="$(grep "^VSM_CARTE : remarque" "$brouillon/en.txt" | tail -1)"
echo "       en anglais : $E ; $ER"
verdict "(c) en anglais : le bouton « Adapt General MIDI to the machine », « No equivalent on this machine: ride (51) »" \
    "$(echo "$E" | grep -q "4 note(s) adaptée(s)" && echo "$ER" | grep -q "No equivalent on this machine: ride (51)" && echo 1 || echo 0)"
PHOTO="$(ls "$brouillon"/adapte-*-Carte*.png 2>/dev/null | head -1)"
verdict "(c) la fenêtre photographiée ($( [ -n "$PHOTO" ] && basename "$PHOTO"))" "$([ -n "$PHOTO" ] && [ -s "$PHOTO" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-adapte "$ADAPTER"
    tc="$(grep -c "Adapter General MIDI à la machine — cliqué" "$brouillon/t-adapte.txt")"
    ta="$(grep -c "^VSM_CARTE : adapt" "$brouillon/t-adapte.txt")"
    echo "       témoin : $tc clic(s) sur le bouton ; $ta adaptation(s)"
    verdict "(c) témoin (D609) : pas de bouton, aucune adaptation" "$([ "$tc" = 0 ] && [ "$ta" = 0 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ADAPTER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ADAPTER : chaque pièce General MIDI va à sa pareille sur la machine, et ce qui manque est dit"
