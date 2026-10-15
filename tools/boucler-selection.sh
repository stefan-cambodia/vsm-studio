#!/usr/bin/env bash
# D554.1 — BOUCLER LA SÉLECTION (Transport ▸ Boucler la sélection, Maj+G ; le *Loop Selection* de Cubase), MESURÉ PAR
# LE RELEVÉ DE LA BOUCLE (projet, moteur, bouton) ET DU TRANSPORT.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM : une mesure = 2 s). Une piste, A [1 920, 3 840) — de 2 à 4 s — et
# B [5 760, 7 680) ; A choisi.
#   (1) Maj+G : 0,6 s plus tard, la boucle est [1920,3840] au projet, [2,000 ; 4,000] s au moteur, allumée partout, la
#       lecture court et la tête est dans A ;
#   (2) 2,6 s après le départ — 0,6 s APRÈS la fin de A —, la lecture court toujours et la tête est revenue dans A
#       (entre 2 et 3 s), là où une lecture sans boucle serait à 4,6 s ;
#   (3) un pas, « Boucler la sélection », dans l'historique ;
#   (4) rien de choisi, par le MENU : « Boucler la sélection : Rien n'est choisi… » dit, le transport arrêté, la boucle
#       intacte (éteinte, [0,0]) ;
#   (5) P (« Locateurs sur la sélection ») sans rien de choisi : DIT aussi — il ne disait rien jusque-là, et ne
#       faisait rien.
#
#   tools/boucler-selection.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-boucler-selection.XXXXXX")"
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
for debut in (1920, 2880, 5760, 6720):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip("A", 1920), clip("B", 5760)]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releves() { grep -h "^VSM_DECOMPTE : " "$brouillon/$1.txt" | sed 's/^VSM_DECOMPTE : //'; }
boucle() { grep -h "^VSM_BOUCLE : " "$brouillon/$1.txt" | sed 's/^VSM_BOUCLE : //' | sed 's/ ; clic .*//'; }
tete() { echo "$1" | sed -n 's/.*tête \([-0-9.]*\) s.*/\1/p'; }
etat() { echo "$1" | sed -n 's/.*transport \(.*\)$/\1/p'; }
dans() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
ALLUMEE="projet oui [1920,3840] ; moteur oui [2.000,4.000 s] ; bouton oui"

echo "=== D554.1 : boucler la sélection ==="
course boucler VSM_DELAI=5000 VSM_VUE="sans-rapport,arrangement,premier-clip:0" \
    VSM_GESTE_APRES="1200:touche:shift + G;1800:relever-boucle;1800:relever-decompte;3800:relever-decompte;4200:relever-historique"
course rien VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:menu:Boucler la sélection;1700:relever-boucle;1700:relever-decompte"
course p-rien VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:touche:P;1700:relever-boucle"

mapfile -t l < <(releves boucler); b="$(boucle boucler)"; mapfile -t n < <(releves rien); bn="$(boucle rien)"
bp="$(boucle p-rien)"; hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/boucler.txt" | tail -1)"
for x in "${l[@]}"; do echo "       boucler : $x"; done
echo "       boucle : [$b] ; rien de choisi [$bn] ; P sans choix [$bp] ; ${hist:-historique non relevé}"
grep -h "^VSM_BOUCLER_SELECTION\|^VSM_BOITE" "$brouillon/boucler.txt" "$brouillon/rien.txt" "$brouillon/p-rien.txt" | sed 's/^/       /'

verdict "(1) la boucle sur A, partout, et la lecture dans A" \
    "$([ "$b" = "$ALLUMEE" ] && [ "${#l[@]}" = 2 ] && [ "$(etat "${l[0]}")" = "en lecture" ] && dans "$(tete "${l[0]}")" 2.0 4.0 && echo 1 || echo 0)"
verdict "(2) passé la fin de A, la tête est revenue dans A" \
    "$([ "${#l[@]}" = 2 ] && [ "$(etat "${l[1]}")" = "en lecture" ] && dans "$(tete "${l[1]}")" 2.0 3.0 && echo 1 || echo 0)"
verdict "(3) un pas « Boucler la sélection »" "$(echo "$hist" | grep -q "Boucler la sélection" && echo 1 || echo 0)"
verdict "(4) rien de choisi : dit, rien ne part, la boucle intacte" \
    "$([ "${#n[@]}" = 1 ] && [ "$(etat "${n[0]}")" = "arrêté" ] && echo "$bn" | grep -q "^projet non \[0,0\]" \
       && grep -q "^VSM_BOITE : Boucler la sélection : Rien n'est choisi" "$brouillon/rien.txt" && echo 1 || echo 0)"
verdict "(5) P sans rien de choisi : dit" \
    "$(echo "$bp" | grep -q "^projet non \[0,0\]" && grep -q "^VSM_BOITE : Locateurs sur la sélection : Rien n'est choisi" "$brouillon/p-rien.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "BOUCLER-SELECTION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "BOUCLER-SELECTION : la sélection devient la boucle, et la lecture y tourne"
