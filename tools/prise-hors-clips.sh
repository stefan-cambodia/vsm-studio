#!/usr/bin/env bash
# D587.1 — LA PRISE MIDI COUVERTE COMME UNE NOTE ÉCRITE (le *Snap MIDI Parts to Bars* de Cubase : une prise fait sa
# partie), MESURÉE PAR LE MIDI EXPORTÉ ET PAR LES CLIPS DU PROJET ENREGISTRÉ APRÈS UNE PRISE, LE TÉMOIN À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM, 4/4 : 1 920 ticks la mesure). Une piste MIDI armée par son bouton R
# (`armer:0`), sans décompte, l'enregistrement lancé à la tête 0 (F9) ; deux notes entrent au point d'entrée du matériel
# (`note-entrante:`, 60 puis 62) pendant la mesure 1 ; F9 arrête ; le MIDI s'exporte (`exporter-midi:`), le projet
# s'enregistre (`enregistrer:`) et ses clips se relisent dans `project.json`.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D587) :
#   (1) le seul clip de la piste à la mesure 20 (une note dedans) : 3 notes exportées, et un clip [0, 1 920) né ;
#   (2) le seul clip à la mesure 3 : 3 notes exportées, et ce clip étendu vers la gauche jusqu'au tick 0 ;
#   (3) Ctrl+Z après (1) : 1 note, 1 clip, en un pas ;
#   (4) une piste sans clip : 2 notes exportées, aucun clip ajouté ;
#   (5) le TÉMOIN (le binaire de D586, passé en argument) sur (1) : 1 note exportée, aucun clip né.
#
#   tools/prise-hors-clips.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-prise-clips.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

# Trois projets : « loin » (le clip à la mesure 20), « pres » (à la mesure 3), « sans » (ni clip ni note).
python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
for nom, debut in (("loin", 36480), ("pres", 3840), ("sans", None)):
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
    if debut is not None:
        corps += vlq(debut) + b"\x90\x48\x5a" + vlq(480) + b"\x80\x48\x00"
    corps += b"\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
             "instrument": {"preferredPlugin": "vsm.minimoog"}}
    if debut is not None:
        piste["clips"] = [{"sourceStart": debut, "sourceLength": 1920, "start": debut, "length": 1920,
                           "color": "#FF6B9BFF"}]
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [piste]}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/$projet" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI|ENREGISTRER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # le nombre de notes d'un .mid exporté, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
print(sum(1 for tr in m.tracks for x in tr if x.type == "note_on" and x.velocity > 0))
PY2
}
clips() {   # les clips de la piste 0 du projet enregistré, « [début,fin) … », ou « ABSENT »
    python3 - "$1/project.json" <<'PY3'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("ABSENT"); sys.exit(0)
cs = sorted((c.get("start", 0), c.get("start", 0) + c.get("length", 0)) for c in d["tracks"][0].get("clips", []))
print(" ".join(f"[{a},{b})" for a, b in cs) if cs else "aucun")
PY3
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SANS="menu:Enregistrement > Sans décompte"
PRISE="300:armer:0;400:$SANS;800:touche:F9;1100:note-entrante:60:100:150;1400:note-entrante:62:100:150;2000:touche:F9"

echo "=== D587.1 : la prise MIDI hors des clips ==="
course loin loin VSM_DELAI=3200 VSM_GESTE_APRES="$PRISE;2400:exporter-midi:$brouillon/loin.mid;2600:enregistrer:$brouillon/loin-ecrit"
course pres pres VSM_DELAI=3200 VSM_GESTE_APRES="$PRISE;2400:exporter-midi:$brouillon/pres.mid;2600:enregistrer:$brouillon/pres-ecrit"
course defait loin VSM_DELAI=3400 \
    VSM_GESTE_APRES="$PRISE;2300:touche:ctrl + Z;2600:exporter-midi:$brouillon/defait.mid;2800:enregistrer:$brouillon/defait-ecrit"
course sans sans VSM_DELAI=3200 VSM_GESTE_APRES="$PRISE;2400:exporter-midi:$brouillon/sans.mid;2600:enregistrer:$brouillon/sans-ecrit"

for n in loin pres defait sans; do
    echo "       $n : $(notes "$brouillon/$n.mid") note(s) exportée(s) ; clips $(clips "$brouillon/$n-ecrit")"
done
grep -h "^D337 : " "$brouillon"/loin.txt "$brouillon"/pres.txt "$brouillon"/sans.txt | sed 's/^/       /'
d337_sans="$(grep -c "^D337 : " "$brouillon/sans.txt")"
# (3) ne prouve rien si la prise n'a pas eu lieu : « défait » et « jamais fait » exportent la même note (CLAUDE.md, le
# témoin d'une annulation). La course dit donc d'abord que la prise a couvert ses notes, AVANT le Ctrl+Z.
d337_defait="$(awk '/^D337 : /{d=NR} /VSM_TOUCHE : .*ctrl \+ Z/{z=NR} END{print (d && z && d < z) ? 1 : 0}' "$brouillon/defait.txt")"

verdict "(1) le clip à la mesure 20 : 3 notes, un clip [0,1920) né" \
    "$([ "$(notes "$brouillon/loin.mid")" = 3 ] && [ "$(clips "$brouillon/loin-ecrit")" = "[0,1920) [36480,38400)" ] && echo 1 || echo 0)"
verdict "(2) le clip à la mesure 3 : 3 notes, le clip étendu jusqu'au tick 0" \
    "$([ "$(notes "$brouillon/pres.mid")" = 3 ] && [ "$(clips "$brouillon/pres-ecrit")" = "[0,5760)" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : 1 note, 1 clip, en un pas — la prise couverte AVANT (D337 au journal)" \
    "$([ "$d337_defait" = 1 ] && [ "$(notes "$brouillon/defait.mid")" = 1 ] && [ "$(clips "$brouillon/defait-ecrit")" = "[36480,38400)" ] && echo 1 || echo 0)"
verdict "(4) une piste sans clip : 2 notes, la règle ne la touche pas" \
    "$([ "$(notes "$brouillon/sans.mid")" = 2 ] && [ "$d337_sans" = 0 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PRISE-HORS-CLIPS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PRISE-HORS-CLIPS : une prise jouée hors des clips se couvre comme une note écrite, et s'entend"
