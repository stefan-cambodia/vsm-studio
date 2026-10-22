#!/usr/bin/env bash
# D575.1, D575.2 — LA ZONE DE NON-QUANTIFICATION ET LE RAYON DE CAPTURE (Édition ▸ Temps et durée ▸ Zone de
# non-quantification ▸ … ; ▸ Rayon de capture ▸ … ; le *Non Quantize* et le *Catch Range* du panneau de quantification
# de Cubase), MESURÉS PAR LE MIDI EXPORTÉ APRÈS « Quantifier (100 %) ».
#
# LES RÈGLES GARDÉES. La grille à la double-croche (120 ticks), son défaut ; quatre notes à 243, 370, 512 et 650 (+3, +10,
# +32, +50 de leurs cibles). L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D575) :
#   (1) le TÉMOIN, sans zone ni rayon : 240, 360, 480, 600 ;
#   (2) zone 10 % (12 ticks) : 243, 370, 480, 600 — ce qui est près reste ;
#   (3) rayon 25 % (30 ticks) : 240, 360, 512, 650 — ce qui est loin reste ;
#   (4) zone 10 % et rayon 40 % (48 ticks) : 243, 370, 480, 650 ;
#   (5) la vue du projet les garde : enregistré avec la zone de 10 %, rouvert et quantifié sans y toucher : (2) ;
#   (6) un préréglage de quantification les garde : enregistré avec (4), rappelé dans un projet neuf : (4).
#
#   tools/zone-quantification.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zone-quantif.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(243, 303, 60), (370, 430, 62), (512, 572, 64), (650, 710, 65)]
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
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT_MIDI|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
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
ZONE10="menu:Édition > Temps et durée > Zone de non-quantification > Zone de 10 % du pas"
RAYON25="menu:Édition > Temps et durée > Rayon de capture > Capturer à 25 % du pas"
RAYON40="menu:Édition > Temps et durée > Rayon de capture > Capturer à 40 % du pas"
PRE="menu:Édition > Préréglages de quantification"

echo "=== D575.1, D575.2 : la zone de non-quantification, le rayon de capture ==="
h() { mktemp -d "$brouillon/home.XXXX"; }
cp -r "$brouillon/base" "$brouillon/garde"
course temoin "$(h)" "$brouillon/base" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:exporter-midi:$brouillon/temoin.mid"
course zone "$(h)" "$brouillon/base" VSM_DELAI=2600 \
    VSM_GESTE_APRES="900:$ZONE10;1100:$TOUT;1400:$QUANT;1800:exporter-midi:$brouillon/zone.mid"
course rayon "$(h)" "$brouillon/base" VSM_DELAI=2600 \
    VSM_GESTE_APRES="900:$RAYON25;1100:$TOUT;1400:$QUANT;1800:exporter-midi:$brouillon/rayon.mid"
course deux "$(h)" "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="800:$ZONE10;1000:$RAYON40;1200:$TOUT;1500:$QUANT;1900:exporter-midi:$brouillon/deux.mid"
course garder "$(h)" "$brouillon/garde" VSM_DELAI=2200 VSM_GESTE_APRES="900:$ZONE10;1400:enregistrer:$brouillon/garde"
course rouvert "$(h)" "$brouillon/garde" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:exporter-midi:$brouillon/rouvert.mid"
maison="$(h)"
course prereglage "$maison" "$brouillon/base" VSM_DELAI=2600 VSM_OPTIONS="nom=Humain" \
    VSM_GESTE_APRES="800:$ZONE10;1000:$RAYON40;1300:$PRE > Enregistrer les réglages de quantification…"
course rappel "$maison" "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="900:$PRE > Humain;1200:$TOUT;1500:$QUANT;1900:exporter-midi:$brouillon/rappel.mid"

dt="$(debuts "$brouillon/temoin.mid")"; dz="$(debuts "$brouillon/zone.mid")"; dr="$(debuts "$brouillon/rayon.mid")"
dd="$(debuts "$brouillon/deux.mid")"; do_="$(debuts "$brouillon/rouvert.mid")"; dp="$(debuts "$brouillon/rappel.mid")"
vue="$(python3 -c "
import json, sys
try: d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception: print('ABSENT'); sys.exit(0)
v = d.get('view', {}); print(v.get('quantizeZone', '-'), v.get('quantizeCatch', '-'))" "$brouillon/garde")"
grep -h "^VSM_QUANTIFIER : zone\|^VSM_QUANTIFIER : rayon\|^VSM_QUANTIFICATION" "$brouillon/deux.txt" "$brouillon/prereglage.txt" "$brouillon/rappel.txt" | sed 's/^/       /'
echo "       débuts : témoin [$dt] ; zone 10 % [$dz] ; rayon 25 % [$dr] ; les deux [$dd]"
echo "       vue enregistrée (zone rayon) [$vue] ; rouvert [$do_] ; préréglage rappelé [$dp]"

verdict "(1) le témoin : 240 360 480 600" "$([ "$dt" = "240 360 480 600" ] && echo 1 || echo 0)"
verdict "(2) zone 10 % : 243 370 480 600" "$([ "$dz" = "243 370 480 600" ] && echo 1 || echo 0)"
verdict "(3) rayon 25 % : 240 360 512 650" "$([ "$dr" = "240 360 512 650" ] && echo 1 || echo 0)"
verdict "(4) zone 10 % et rayon 40 % : 243 370 480 650" "$([ "$dd" = "243 370 480 650" ] && echo 1 || echo 0)"
verdict "(5) la vue du projet garde la zone : rouvert, (2)" \
    "$(python3 -c "import sys; z = sys.argv[1].split()[0]; sys.exit(0 if z != '-' and abs(float(z) - 0.1) < 1e-6 else 1)" "$vue" 2>/dev/null \
       && [ "$do_" = "243 370 480 600" ] && echo 1 || echo 0)"
verdict "(6) un préréglage les garde : rappelé, (4)" "$([ "$dp" = "243 370 480 650" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZONE-QUANTIFICATION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZONE-QUANTIFICATION : la quantification laisse ce qui est près et ce qui est loin, et s'en souvient"
