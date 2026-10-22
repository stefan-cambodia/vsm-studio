#!/usr/bin/env bash
# D576.2, D576.3 — LES RÉGLAGES DU LEGATO (Édition ▸ Temps et durée ▸ Chevauchement du legato ▸ … ; ▸ Legato entre les
# notes choisies seulement ; le *Legato Overlap* et le *Legato Mode* des préférences de Cubase), MESURÉS PAR LE MIDI
# EXPORTÉ APRÈS « Legato ».
#
# LES RÈGLES GARDÉES. Quatre notes de 100 ticks à 0, 480, 960 et 1 440, de vélocités 40, 100, 40, 100. L'ATTENDU ÉCRIT
# AVANT LA MESURE (ROADMAP-daw, D576) :
#   (1) le TÉMOIN, toutes les notes, chevauchement 0 : fins à 480, 960, 1 440, et 1 540 (la dernière garde sa durée) ;
#   (2) recouvrement de 10 ticks : 490, 970, 1 450, 1 540 ; (3) blanc de 10 ticks : 470, 950, 1 430, 1 540 ;
#   (4) les notes faibles seules choisies (« Notes plus faibles que 64 ») : sans le mode, 480 et 1 440 — avec le mode
#       « entre les notes choisies », 960 et 1 060 ; les deux autres intactes (580, 1 540) ;
#   (5) retenus : relancé dans le même HOME, le menu dit le recouvrement et le mode cochés.
#
#   tools/legato-reglages.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-legato.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 100, 60, 40), (480, 580, 62, 100), (960, 1060, 64, 40), (1440, 1540, 65, 100)]
ev = []
for d, f, h, v in notes:
    ev.append((d, 1, bytes([0x90, h, v]))); ev.append((f, 0, bytes([0x80, h, 0])))
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

course() {   # $1 = nom ; $2 = HOME ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fins() {   # les fins des notes d'un .mid, dans l'ordre des départs, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            out.append((ouvertes[x.note].pop(0), t))
print(" ".join(str(f) for d, f in sorted(out)) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
TOUT="menu:Édition > Sélection > Tout sélectionner"
LEGATO="menu:Édition > Temps et durée > Legato"
PLUS="menu:Édition > Temps et durée > Chevauchement du legato > Recouvrement de 10 ticks"
MOINS="menu:Édition > Temps et durée > Chevauchement du legato > Blanc de 10 ticks"
MODE="menu:Édition > Temps et durée > Legato entre les notes choisies seulement"
FAIBLES="menu:Édition > Sélection > Notes plus faibles que 64 (2)"

echo "=== D576.2, D576.3 : les réglages du legato ==="
h() { mktemp -d "$brouillon/home.XXXX"; }
course temoin "$(h)" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$LEGATO;1700:exporter-midi:$brouillon/temoin.mid"
maison="$(h)"
course plus "$maison" VSM_DELAI=2600 VSM_GESTE_APRES="900:$PLUS;1100:$TOUT;1400:$LEGATO;1800:exporter-midi:$brouillon/plus.mid"
course moins "$(h)" VSM_DELAI=2600 VSM_GESTE_APRES="900:$MOINS;1100:$TOUT;1400:$LEGATO;1800:exporter-midi:$brouillon/moins.mid"
course faibles "$(h)" VSM_DELAI=2600 VSM_GESTE_APRES="1100:$FAIBLES;1400:$LEGATO;1800:exporter-midi:$brouillon/faibles.mid"
course mode "$maison" VSM_DELAI=2800 \
    VSM_GESTE_APRES="800:$MODE;1000:menu:Édition > Temps et durée > Chevauchement du legato > Fin sur le départ suivant;1200:$FAIBLES;1500:$LEGATO;1900:exporter-midi:$brouillon/mode.mid"
course retenu "$maison" VSM_DELAI=1800 VSM_GESTE_APRES="1000:lister-menus"

ft="$(fins "$brouillon/temoin.mid")"; fp="$(fins "$brouillon/plus.mid")"; fm="$(fins "$brouillon/moins.mid")"
ff="$(fins "$brouillon/faibles.mid")"; fo="$(fins "$brouillon/mode.mid")"
retenus="$(grep -h "^VSM_MENU_LISTE : Édition > Temps et durée > \(Chevauchement du legato > \|Legato entre\)" "$brouillon/retenu.txt" \
           | grep "\[cochée\]" | sed 's/^VSM_MENU_LISTE : Édition > Temps et durée > //')"
grep -h "^VSM_LEGATO" "$brouillon/plus.txt" "$brouillon/mode.txt" | sed 's/^/       /'
echo "       fins : témoin [$ft] ; +10 [$fp] ; −10 [$fm] ; faibles sans le mode [$ff] ; avec [$fo]"
echo "$retenus" | sed 's/^/       retenu : /'

verdict "(1) le témoin : 480 960 1440 1540" "$([ "$ft" = "480 960 1440 1540" ] && echo 1 || echo 0)"
verdict "(2) recouvrement de 10 : 490 970 1450 1540" "$([ "$fp" = "490 970 1450 1540" ] && echo 1 || echo 0)"
verdict "(3) blanc de 10 : 470 950 1430 1540" "$([ "$fm" = "470 950 1430 1540" ] && echo 1 || echo 0)"
verdict "(4) les faibles choisies : 480/1 440 sans le mode, 960/1 060 avec, les autres intactes" \
    "$([ "$ff" = "480 580 1440 1540" ] && [ "$fo" = "960 580 1060 1540" ] && echo 1 || echo 0)"
verdict "(5) retenus au relancement : « Fin sur le départ suivant » (posé en dernier) et le mode cochés" \
    "$(echo "$retenus" | grep -q "^Chevauchement du legato > Fin sur le départ suivant" \
       && echo "$retenus" | grep -q "^Legato entre les notes choisies seulement" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LEGATO-REGLAGES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LEGATO-REGLAGES : le legato laisse un blanc ou recouvre, prend les seules notes choisies, et s'en souvient"
