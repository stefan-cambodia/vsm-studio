#!/usr/bin/env bash
# D585.1 — LES REMISES À ZÉRO APRÈS LA PRISE (Enregistrement ▸ Remettre les contrôleurs à zéro après la prise ; l'*Insert
# Reset Events after Record* de Cubase), MESURÉES PAR LE MIDI EXPORTÉ APRÈS UNE PRISE, LE TÉMOIN DÉCOCHÉ À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM). Une piste MIDI vide armée par son bouton R (`armer:0`), sans
# décompte. Les messages entrent au point d'entrée du matériel (`midi-entrant:`, `cc-entrant:`, `note-entrante:`), à
# 300 ms d'écart pendant la prise : un pitch bend à +4 096 (E0 00 60), la pédale (CC 64) à 127, le CC 1 à 100, une
# pression de canal à 64 (D0 40), une note ; l'arrêt, la pédale jamais relâchée.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D585) :
#   (1) cochée : le MIDI exporté porte 2 pitch bend (le second au centre), 2 CC 64 (le second à 0), 2 CC 1, 2 pressions,
#       1 note ; les quatre remises au MÊME tick, après le dernier événement joué ;
#   (2) le TÉMOIN décoché : 1 1 1 1 1 ;
#   (3) cochée, la prise ayant ramené elle-même la molette au centre et relâché la pédale : 2 pitch bend et 2 CC 64 —
#       rien de doublé —, 2 CC 1, 2 pressions ;
#   (4) Ctrl+Z après la prise cochée : 0 partout, en un pas ;
#   (5) relancé dans le même HOME : cochée ; en anglais, « Reset controllers after the take ».
#
#   tools/remises-apres-prise.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-remises.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(vide)) + vide)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "remises", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
compte() {   # « pb cc64 cc1 pression notes | remises » d'un .mid, ou « ABSENT »
    # « remises » : OK si les dernières valeurs des quatre genres sont au repos, au MÊME tick, et que ce tick n'est
    # précédé d'aucun autre événement plus tardif ; sinon ce qui manque.
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
ev = {"pb": [], "cc64": [], "cc1": [], "at": [], "notes": []}
autres = []
for tr in m.tracks:
    t = 0
    for x in tr:
        t += x.time
        if x.type == "pitchwheel": ev["pb"].append((t, x.pitch))
        elif x.type == "control_change" and x.control == 64: ev["cc64"].append((t, x.value))
        elif x.type == "control_change" and x.control == 1: ev["cc1"].append((t, x.value))
        elif x.type == "aftertouch": ev["at"].append((t, x.value))
        elif x.type == "note_on" and x.velocity > 0: ev["notes"].append((t, x.note)); autres.append(t)
        elif x.type in ("note_off", "note_on"): autres.append(t)
comptes = " ".join(str(len(ev[k])) for k in ("pb", "cc64", "cc1", "at", "notes"))
derniers = [ev[k][-1] for k in ("pb", "cc64", "cc1", "at") if ev[k]]
remises = "-"
if len(derniers) == 4:
    ticks = {d[0] for d in derniers}
    joues = [t for k in ("pb", "cc64", "cc1", "at") for t, _ in ev[k][:-1]] + autres
    if any(d[1] != 0 for d in derniers): remises = "pas-au-repos"
    elif len(ticks) != 1: remises = "ticks-differents"
    elif joues and max(joues) > next(iter(ticks)): remises = "avant-la-fin"
    else: remises = f"OK@{next(iter(ticks))}"
print(f"{comptes} {remises}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SANS="menu:Enregistrement > Sans décompte"
REMISE="menu:Enregistrement > Remettre les contrôleurs à zéro après la prise"
JOUER="1100:midi-entrant:E0:00:60;1400:cc-entrant:64:127;1700:cc-entrant:1:100;2000:midi-entrant:D0:40;2300:note-entrante:60:100:200"
RAMENER="2450:midi-entrant:E0:00:40;2600:cc-entrant:64:0"

echo "=== D585.1 : les remises à zéro après la prise ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course cochee "$maison" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$REMISE;800:touche:F9;$JOUER;2800:touche:F9;3200:exporter-midi:$brouillon/cochee.mid"
course temoin "" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:touche:F9;$JOUER;2800:touche:F9;3200:exporter-midi:$brouillon/temoin.mid"
course ramenee "" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$REMISE;800:touche:F9;$JOUER;$RAMENER;2800:touche:F9;3200:exporter-midi:$brouillon/ramenee.mid"
course defait "" VSM_DELAI=3800 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$REMISE;800:touche:F9;$JOUER;2800:touche:F9;3100:touche:ctrl + Z;3400:exporter-midi:$brouillon/defait.mid"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

cc="$(compte "$brouillon/cochee.mid")"; ct="$(compte "$brouillon/temoin.mid")"; cr="$(compte "$brouillon/ramenee.mid")"
cz="$(compte "$brouillon/defait.mid")"
grep -h "^VSM_REGLAGE_POSTE" "$brouillon"/cochee.txt | sed 's/^/       /'
echo "       (pitch bend, CC 64, CC 1, pression, notes, remises) : cochée [$cc] ; témoin [$ct] ; ramenée [$cr] ; Ctrl+Z [$cz]"
coche="$(grep -h "^VSM_MENU_LISTE : Enregistrement > Remettre les contrôleurs à zéro après la prise" "$brouillon/retenu.txt" | grep -c "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : Recording > Reset controllers after the take" "$brouillon/anglais.txt")"
echo "       relancé : $coche entrée cochée ; en anglais : $en entrée(s)"

champs() { echo "$1" | awk '{print $1, $2, $3, $4, $5}'; }
verdict "(1) cochée : 2 2 2 2 1, les quatre remises au même tick, après le dernier événement joué" \
    "$([ "$(champs "$cc")" = "2 2 2 2 1" ] && echo "$cc" | grep -q " OK@" && echo 1 || echo 0)"
verdict "(2) le témoin décoché : 1 1 1 1 1" "$([ "$(champs "$ct")" = "1 1 1 1 1" ] && echo 1 || echo 0)"
verdict "(3) ramenée par la prise : 2 2 2 2 1 — rien de doublé" "$([ "$(champs "$cr")" = "2 2 2 2 1" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z après la prise cochée : 0 partout, en un pas" "$([ "$(champs "$cz")" = "0 0 0 0 0" ] && echo 1 || echo 0)"
verdict "(5) retenu au poste ; en anglais" "$([ "$coche" = 1 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REMISES-APRES-PRISE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REMISES-APRES-PRISE : ce que la prise laisse hors du repos y revient à sa fin, et rien n'est doublé"
