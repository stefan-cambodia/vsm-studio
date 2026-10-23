#!/usr/bin/env bash
# D583.2 — LA PLAGE DE RATTRAPAGE D'UNE PRISE MIDI (Enregistrement ▸ Rattrapage avant l'entrée ; le *MIDI Record Catch
# Range* de Cubase), MESURÉE PAR LE MIDI EXPORTÉ APRÈS UNE PRISE EN PUNCH, LE TÉMOIN SANS RATTRAPAGE À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Une piste MIDI vide armée par son bouton R (`armer:0`), la
# région de punch [2 s, 4 s) active, le décompte d'une mesure : F9 à 600 ms, le transport part de 0 et l'entrée tombe
# vers 2 600 ms de la course. Les notes entrent par `note-entrante:` (le point d'entrée du matériel), tenues 400 ms :
# frappées à 2 450 ms (150 ms avant l'entrée), à 2 200 ms (400 ms avant), et à 3 100 ms (dedans). La tête est relevée au
# moment de la première frappe (`relever-decompte`) : les marges de 100 ms et plus valent devant la gigue d'un geste
# différé, et le relevé dit si elles ont tenu.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D583) :
#   (1) à « Rattrapage : 250 ms » : 2 notes, la première au tick 1 920 (l'entrée, 2 s), l'autre dedans ;
#   (2) le TÉMOIN à « Rattrapage : aucun » : 1 note, celle de dedans ;
#   (3) relancé dans le même HOME : « Rattrapage : 250 ms » cochée ; en anglais, « Catch range: 250 ms ».
#
#   tools/rattrapage-prise.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-rattrapage.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\xff\x2f\x00"
os.makedirs(f"{b}/punch/midi", exist_ok=True)
open(f"{b}/punch/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(vide)) + vide)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
             "punch": {"enabled": True, "startTick": 1920, "endTick": 3840}}
json.dump({"format": "vsm-project", "version": 1, "title": "rattrapage", "midi": {"file": "midi/arrangement.mid"},
           "transport": transport,
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/punch/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/punch" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
debuts() {   # les débuts des notes d'un .mid, triés, ou « ABSENT » / « RIEN »
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
nombre() { case "$1" in ABSENT|RIEN) echo 0 ;; *) echo "$1" | wc -w ;; esac; }
premier() { case "$1" in ABSENT|RIEN) echo "$1" ;; *) echo "$1" | awk '{print $1}' ;; esac; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
R250="menu:Enregistrement > Rattrapage avant l'entrée > Rattrapage : 250 ms"
JOUER="2200:note-entrante:62:100:400;2450:relever-decompte;2450:note-entrante:60:100:400;3100:note-entrante:64:100:400"

echo "=== D583.2 : la plage de rattrapage d'une prise MIDI ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course rattrape "$maison" VSM_DELAI=5400 \
    VSM_GESTE_APRES="300:armer:0;400:$R250;600:touche:F9;$JOUER;4300:touche:F9;4800:exporter-midi:$brouillon/rattrape.mid"
course temoin "" VSM_DELAI=5400 \
    VSM_GESTE_APRES="300:armer:0;600:touche:F9;$JOUER;4300:touche:F9;4800:exporter-midi:$brouillon/temoin.mid"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

dr="$(debuts "$brouillon/rattrape.mid")"; dt="$(debuts "$brouillon/temoin.mid")"
grep -h "^VSM_REGLAGE_POSTE\|^VSM_DECOMPTE" "$brouillon"/rattrape.txt | sed 's/^/       rattrape : /'
grep -h "^VSM_DECOMPTE" "$brouillon"/temoin.txt | sed 's/^/       témoin : /'
echo "       débuts : à 250 ms [$dr] ; témoin [$dt]"
coche="$(grep -h "^VSM_MENU_LISTE : Enregistrement > Rattrapage avant l'entrée > Rattrapage : 250 ms" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Recording > Catch range before punch-in > Catch range: 250 ms" "$brouillon/anglais.txt")"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"

verdict "(1) à 250 ms : 2 notes, la première à 1 920" "$([ "$(nombre "$dr")" = 2 ] && [ "$(premier "$dr")" = 1920 ] && echo 1 || echo 0)"
verdict "(2) le témoin sans rattrapage : 1 note" "$([ "$(nombre "$dt")" = 1 ] && echo 1 || echo 0)"
verdict "(3) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "RATTRAPAGE-PRISE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RATTRAPAGE-PRISE : une note frappée juste avant l'entrée est gardée, posée à l'entrée ; plus tôt, elle reste écartée"
