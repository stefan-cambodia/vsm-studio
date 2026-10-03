#!/usr/bin/env bash
# D543.2 — LA FENÊTRE DES REPÈRES : SES LIGNES, ET SES GESTES PAR LES FONCTIONS DES RÈGLES.
#
# LA RÈGLE GARDÉE. Trois repères : Intro à 0, Couplet à 1 920, Refrain à 5 760.
#   (1) la fenêtre ouverte (`VSM_VUE=reperes`) relève « mes. 1 · 1 Intro | mes. 2 · 1 Couplet |
#       mes. 4 · 1 Refrain » (`VSM_REPERES`, les lignes telles qu'elles sont peintes) ;
#   (2) « aller » au troisième : la tête à 5 760 ;
#   (3) « renommer » le deuxième en « Pont » : le project.json le porte ;
#   (4) « retirer » le premier : deux repères ; Ctrl+Z : trois ;
#   (5) la fenêtre SUIT : après le renommage et le retrait, le relevé dit les lignes nouvelles.
#
#   tools/fenetre-reperes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fenetre-reperes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/trois/midi"
python3 - "$brouillon/trois" <<'PY'
import json, struct, sys
d = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "Intro"}, {"tick": 1920, "name": "Couplet"}, {"tick": 5760, "name": "Refrain"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
PY

maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
env HOME="$maison" VSM_PROJET="$brouillon/trois" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,reperes" VSM_DELAI=4500 \
    VSM_OPTIONS="nom=Pont" VSM_GESTE_APRES="1200:relever-reperes;1500:repere-aller:2;1700:relever-tete;\
2000:repere-renommer:1;2300:repere-retirer:0;2600:relever-reperes;2800:enregistrer:$brouillon/apres;\
3100:touche:ctrl + Z;3400:enregistrer:$brouillon/annule" \
    VSM_CAPTURE="$brouillon/fenetre.png" timeout 60 "$BIN" > "$brouillon/journal.txt" 2>&1
grep -E "VSM_(GESTE_APRES|VUE|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION)" "$brouillon/journal.txt" \
    | sed "s/^/        journal : /" >&2
reperes() {   # « tick:nom … » d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    print(' '.join(f\"{m['tick']}:{m['name']}\" for m in json.load(open(sys.argv[1])).get('markers', [])) or '(aucun)')
except Exception:
    print('ABSENT')
" "$1"
}
premier="$(grep "^VSM_REPERES" "$brouillon/journal.txt" | head -1)"
second="$(grep "^VSM_REPERES" "$brouillon/journal.txt" | sed -n 2p)"
tete="$(grep "^VSM_TETE" "$brouillon/journal.txt" | tail -1)"
a="$(reperes "$brouillon/apres/project.json")"; u="$(reperes "$brouillon/annule/project.json")"
echo "       $premier"
echo "       $tete"
echo "       $second"
echo "       après {$a} ; Ctrl+Z {$u}"

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
verdict "(1) la fenêtre relève les trois lignes" \
    "$([ "$premier" = "VSM_REPERES : mes. 1 · 1 Intro | mes. 2 · 1 Couplet | mes. 4 · 1 Refrain" ] && echo 1 || echo 0)"
verdict "(2) aller au troisième : la tête à 5 760" "$(echo "$tete" | grep -q "5760" && echo 1 || echo 0)"
verdict "(3) et (4) renommé « Pont », le premier retiré" "$([ "$a" = "1920:Pont 5760:Refrain" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z : le premier revient" "$([ "$u" = "0:Intro 1920:Pont 5760:Refrain" ] && echo 1 || echo 0)"
verdict "(5) la fenêtre suit" "$([ "$second" = "VSM_REPERES : mes. 2 · 1 Pont | mes. 4 · 1 Refrain" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FENÊTRE DES REPÈRES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FENÊTRE DES REPÈRES : les repères se lisent, se rejoignent, se renomment et se retirent"
