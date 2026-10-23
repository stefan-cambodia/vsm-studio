#!/usr/bin/env bash
# D577.1 — LE QUINTOLET DANS LA GRILLE (la barre du piano roll : Droit, Triolet, Pointé, Quintolet ; le *Quintuplet* de
# la grille de quantification de Cubase), MESURÉ PAR LE MIDI EXPORTÉ APRÈS « Quantifier (100 %) ».
#
# LES RÈGLES GARDÉES. La grille à la double-croche, son défaut ; quatre notes à 100, 190, 300 et 380. L'ATTENDU ÉCRIT
# AVANT LA MESURE (ROADMAP-daw, D577) :
#   (1) le TÉMOIN, droit (120 ticks) : 120, 240, 360, 360 ;
#   (2) le quintolet choisi dans la liste de la barre (96 ticks, cinq cases dans l'espace de quatre) : 96, 192, 288, 384 ;
#       la barre relue le dit (`modificateur=quintuplet`) ;
#   (3) la vue du projet le garde : enregistré (`pianoRollGridModifier : quintuplet`), rouvert et quantifié sans y
#       toucher : (2) ;
#   (4) un préréglage de quantification le garde : enregistré en quintolet, rappelé dans un projet neuf : (2) ;
#   (5) en anglais, la liste dit « Quintuplet ».
#
#   tools/quintolet-grille.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-quintolet.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(100, 140, 60), (190, 230, 62), (300, 340, 64), (380, 420, 65)]
ev = []
for d, f, h in notes:
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME ; $3 = projet ; le reste : variables
    local nom="$1" maison="$2" projet="$3"
    shift 3
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT_MIDI|OPTIONS|LISTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
debuts() {   # les débuts des notes d'un .mid, triés, ou « ABSENT »
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
        if x.type == "note_on" and x.velocity > 0: out.append(t)
print(" ".join(str(d) for d in sorted(out)) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
TOUT="menu:Édition > Sélection > Tout sélectionner"
QUANT="menu:Édition > Temps et durée > Quantifier (100 %)"
QUINT="liste:pianoroll.grille.modificateur=Quintolet"
PRE="menu:Édition > Préréglages de quantification"

echo "=== D577.1 : le quintolet dans la grille ==="
h() { mktemp -d "$brouillon/home.XXXX"; }
cp -r "$brouillon/base" "$brouillon/garde"
course temoin "$(h)" "$brouillon/base" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:exporter-midi:$brouillon/temoin.mid"
course quintolet "$(h)" "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="900:$QUINT;1100:relever-grille;1300:$TOUT;1600:$QUANT;2000:exporter-midi:$brouillon/quintolet.mid"
course garder "$(h)" "$brouillon/garde" VSM_DELAI=2200 VSM_GESTE_APRES="900:$QUINT;1400:enregistrer:$brouillon/garde"
course rouvert "$(h)" "$brouillon/garde" VSM_DELAI=2600 \
    VSM_GESTE_APRES="900:relever-grille;1100:$TOUT;1400:$QUANT;1800:exporter-midi:$brouillon/rouvert.mid"
maison="$(h)"
course prereglage "$maison" "$brouillon/base" VSM_DELAI=2400 VSM_OPTIONS="nom=Cinq" \
    VSM_GESTE_APRES="900:$QUINT;1300:$PRE > Enregistrer les réglages de quantification…"
course rappel "$maison" "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="900:$PRE > Cinq;1200:$TOUT;1500:$QUANT;1900:exporter-midi:$brouillon/rappel.mid"
course anglais "$(h)" "$brouillon/base" VSM_DELAI=1800 VSM_LANGUE=en VSM_TEXTES_LISTE=1 \
    VSM_GESTE_APRES="900:liste:pianoroll.grille.modificateur=Quintuplet;1200:relever-grille"

dt="$(debuts "$brouillon/temoin.mid")"; dq="$(debuts "$brouillon/quintolet.mid")"
do_="$(debuts "$brouillon/rouvert.mid")"; dp="$(debuts "$brouillon/rappel.mid")"
vue="$(python3 -c "
import json, sys
try: d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception: print('ABSENT'); sys.exit(0)
print(d.get('view', {}).get('pianoRollGridModifier', '-'))" "$brouillon/garde")"
barre() { grep -h "^VSM_PIANOROLL_GRILLE" "$1" | tail -1 | sed 's/.*modificateur=\([a-z]*\).*/\1/'; }
bq="$(barre "$brouillon/quintolet.txt")"; bo="$(barre "$brouillon/rouvert.txt")"; ba="$(barre "$brouillon/anglais.txt")"
grep -h "^VSM_QUANTIFICATION\|^VSM_LISTE" "$brouillon/quintolet.txt" "$brouillon/prereglage.txt" "$brouillon/rappel.txt" "$brouillon/anglais.txt" | sed 's/^/       /'
echo "       débuts : témoin [$dt] ; quintolet [$dq] (barre : $bq)"
echo "       vue enregistrée [$vue] ; rouvert [$do_] (barre : $bo) ; préréglage rappelé [$dp] ; en anglais, barre : $ba"

verdict "(1) le témoin droit : 120 240 360 360" "$([ "$dt" = "120 240 360 360" ] && echo 1 || echo 0)"
verdict "(2) le quintolet : 96 192 288 384, la barre le dit" \
    "$([ "$dq" = "96 192 288 384" ] && [ "$bq" = quintuplet ] && echo 1 || echo 0)"
verdict "(3) la vue du projet le garde : écrit quintuplet, rouvert, (2)" \
    "$([ "$vue" = quintuplet ] && [ "$bo" = quintuplet ] && [ "$do_" = "96 192 288 384" ] && echo 1 || echo 0)"
verdict "(4) un préréglage le garde : rappelé, (2)" "$([ "$dp" = "96 192 288 384" ] && echo 1 || echo 0)"
verdict "(5) en anglais, la liste dit « Quintuplet »" "$([ "$ba" = quintuplet ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "QUINTOLET-GRILLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "QUINTOLET-GRILLE : cinq cases dans l'espace de quatre, et le morceau s'en souvient"
