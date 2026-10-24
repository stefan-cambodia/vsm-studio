#!/usr/bin/env bash
# D594 — LES MACROS (Édition ▸ Macros ▸ Commencer l'enregistrement / Arrêter l'enregistrement (N commande(s)) /
# Macro N (k commande(s)) ; les *Macros* des *Key Commands* de Cubase), MESURÉES PAR LE MIDI EXPORTÉ : la macro
# rejouée contre les mêmes commandes jouées à la main.
#
# LE JEU : celui de D592 (sept accords, vingt notes, 480 ticks à la noire). LA MACRO : « Tout sélectionner »,
# « Transposer +1 demi-ton », « Drop 2 ».
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D594, corrigé avant le code à 04 h 42) :
#   (1) enregistrée puis arrêtée : « Macro 1 » rangée, 3 commandes, et le menu porte « Macro 1 (3 commande(s)) » ;
#   (2) relancé dans le même HOME (la macro est au poste), rejouée : l'export identique à celui des trois commandes
#       jouées à la main — 53 61 68 | 56 61 77 | 56 61 65 72 | 63 54 70 | 49 | 111 119 126 | 3 6 10 ;
#   (3) Ctrl+Z après le rejeu défait ses pas un à un (« Tout sélectionner » n'est pas une édition : deux pas) — un
#       Ctrl+Z rend la transposition seule, deux le jeu d'origine ;
#   (4) « Drop 2 » seul, enregistré notes choisies, rejoué sans sélection : arrêtée au pas 1, dit, l'export inchangé ;
#   (5) arrêter sans commande : rien de rangé, dit ; (6) en anglais Edit > Macros > Start recording.
#
#   tools/macros.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-macros.XXXXXX")"
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
json.dump({"format": "vsm-project", "version": 1, "title": "macros", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; $2 = le HOME (partagé quand la macro doit survivre au relancement) ; le reste : variables
    local nom="$1" maison="$2" n
    shift 2
    mkdir -p "$maison"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    local motif="VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
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

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

E="Édition"
M="$E > Macros"
TOUT="$E > Sélection > Tout sélectionner"
PLUS="$E > Hauteur > Transposer +1 demi-ton"
DROP="$E > Hauteur > Disposition des accords > Drop 2"
echo "=== D594 : les macros ==="
course main "$brouillon/h-main" VSM_DELAI=2300 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$TOUT;1200:menu:$PLUS;1500:menu:$DROP;1900:exporter-midi:$brouillon/main.mid"
course enr "$brouillon/h1" VSM_DELAI=2800 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Commencer l'enregistrement;1200:menu:$TOUT;1500:menu:$PLUS;1800:menu:$DROP;2100:menu:$M > Arrêter l'enregistrement (3 commande(s));2400:lister-menus"
course rejeu "$brouillon/h1" VSM_DELAI=2000 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Macro 1 (3 commande(s));1500:exporter-midi:$brouillon/rejeu.mid"
course annule1 "$brouillon/h1" VSM_DELAI=2400 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Macro 1 (3 commande(s));1400:touche:ctrl + Z;1900:exporter-midi:$brouillon/annule1.mid"
course annule2 "$brouillon/h1" VSM_DELAI=2800 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Macro 1 (3 commande(s));1400:touche:ctrl + Z;1800:touche:ctrl + Z;2300:exporter-midi:$brouillon/annule2.mid"
course grise-enr "$brouillon/h2" VSM_DELAI=2300 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$TOUT;1200:menu:$M > Commencer l'enregistrement;1500:menu:$DROP;1800:menu:$M > Arrêter l'enregistrement (1 commande(s))"
course grise "$brouillon/h2" VSM_DELAI=2000 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Macro 1 (1 commande(s));1400:exporter-midi:$brouillon/grise.mid"
course vide "$brouillon/h3" VSM_DELAI=2000 \
    VSM_GESTE_APRES="600:choisir:0;900:menu:$M > Commencer l'enregistrement;1200:menu:$M > Arrêter l'enregistrement (0 commande(s));1500:lister-menus"
course temoin "$brouillon/h4" VSM_DELAI=1800 VSM_GESTE_APRES="600:choisir:0;1200:exporter-midi:$brouillon/temoin.mid"
course anglais "$brouillon/h5" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="600:choisir:0;1000:lister-menus"

for c in main rejeu annule1 annule2 grise temoin; do printf '       %-8s [%s]\n' "$c" "$(hauteurs "$brouillon/$c.mid")"; done
for c in enr rejeu grise-enr grise vide; do grep -h "^VSM_MACRO" "$brouillon/$c.txt" | sed "s/^/       ($c) /"; done
rangee="$(grep -c "^VSM_MENU_LISTE : Édition > Macros > Macro 1 (3 commande(s))" "$brouillon/enr.txt")"
rien="$(grep -c "^VSM_MENU_LISTE : Édition > Macros > Macro 1" "$brouillon/vide.txt")"
en="$(grep -c "^VSM_MENU_LISTE : Edit > Macros > Start recording" "$brouillon/anglais.txt")"
echo "       au menu : Macro 1 (3) $rangee fois ; après l'arrêt à vide : $rien macro ; en anglais : $en"

ORIGINE="60 64 67 60 67 76 60 64 67 71 62 65 69 48 118 122 125 2 5 9"
ATTENDU="53 61 68 56 61 77 56 61 65 72 63 54 70 49 111 119 126 3 6 10"
PLUS1="61 65 68 61 68 77 61 65 68 72 63 66 70 49 119 123 126 3 6 10"
egal() { [ "$(hauteurs "$brouillon/$1.mid")" = "$2" ] && echo 1 || echo 0; }
verdict "(1) rangée : « Macro 1 », 3 commandes, au menu" \
    "$(grep -q "Macro 1 » rang.*3 commande" "$brouillon/enr.txt" && [ "$rangee" = 1 ] && echo 1 || echo 0)"
verdict "(2) les trois commandes à la main : l'attendu de D592 monté d'un demi-ton" "$(egal main "$ATTENDU")"
verdict "(2) la macro rejouée : le même export, note pour note" \
    "$(( $(egal rejeu "$ATTENDU") * $(grep -q "rejou.*3 commande" "$brouillon/rejeu.txt" && echo 1 || echo 0) ))"
# D589 : un retour au jeu d'origine ne prouve rien sans la preuve, dans LA MÊME course, que la macro a joué — le témoin
# (le binaire d'avant, sans macros) rendait « deux Ctrl+Z : le jeu d'origine » vert (25/10).
joue() { grep -q "rejou.*3 commande" "$brouillon/$1.txt" && echo 1 || echo 0; }
verdict "(3) un Ctrl+Z : la transposition seule" "$(( $(egal annule1 "$PLUS1") * $(joue annule1) ))"
verdict "(3) deux Ctrl+Z : le jeu d'origine" "$(( $(egal annule2 "$ORIGINE") * $(joue annule2) ))"
verdict "(4) un pas grisé : arrêtée au pas 1, l'export inchangé" \
    "$(( $(egal grise "$ORIGINE") * $(grep -q "arr.t.e au pas 1 sur 1" "$brouillon/grise.txt" && echo 1 || echo 0) ))"
verdict "(5) arrêtée à vide : rien de rangé, dit" \
    "$([ "$rien" = 0 ] && grep -q "rien de rang" "$brouillon/vide.txt" && echo 1 || echo 0)"
verdict "(6) en anglais : Edit > Macros > Start recording" "$([ "$en" = 1 ] && echo 1 || echo 0)"
verdict "le témoin sans geste : le jeu tel quel" "$(egal temoin "$ORIGINE")"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "MACROS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MACROS : une suite de commandes s'enregistre en la jouant, se rejoue d'un geste, et s'arrête en disant pourquoi"
