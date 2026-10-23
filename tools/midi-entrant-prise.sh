#!/usr/bin/env bash
# D584.1, D584.2 — LES CHANGEMENTS DE PROGRAMME JOUÉS AU CLAVIER, ET CE QUE LA PRISE GARDE (Enregistrement ▸ Ce que la
# prise garde ; le *MIDI Filter ▸ Record* de Cubase), MESURÉS PAR LE MIDI EXPORTÉ APRÈS UNE PRISE, LES TÉMOINS À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM). Une piste MIDI vide armée par son bouton R (`armer:0`), sans
# décompte. Les messages entrent au point d'entrée du matériel : `midi-entrant:` (D584) pour le programme 5 (C0 05), le
# pitch bend (E0 00 50) et la pression de canal (D0 40), `cc-entrant:` pour le CC 1 à 100, `note-entrante:` pour la note
# — à 300 ms d'écart, pendant la prise.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D584) :
#   (1) tout coché : le MIDI exporté porte 1 programme 5, 1 pitch bend, 1 CC 1 à 100, 1 pression, 1 note ;
#   (2) le TÉMOIN hors prise — les mêmes messages, transport à l'arrêt, sans enregistrer : 0 programme 5 ;
#   (3) le pitch bend et les programmes décochés : 0 programme 5, 0 pitch bend ; le CC, la pression et la note, 1 chacun ;
#   (4) Ctrl+Z après la prise tout cochée : 0 programme 5 et 0 note, en un pas ;
#   (5) relancé dans le même HOME : les deux cases décochées ; en anglais, « The take keeps program changes ».
#
#   tools/midi-entrant-prise.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-midi-entrant.XXXXXX")"
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
json.dump({"format": "vsm-project", "version": 1, "title": "midi-entrant", "midi": {"file": "midi/arrangement.mid"},
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
compte() {   # « programme5 pli cc1 pression notes » d'un .mid, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
n = dict(pc=0, pb=0, cc=0, at=0, notes=0)
for tr in m.tracks:
    for x in tr:
        if x.type == "program_change" and x.program == 5: n["pc"] += 1
        elif x.type == "pitchwheel": n["pb"] += 1
        elif x.type == "control_change" and x.control == 1 and x.value == 100: n["cc"] += 1
        elif x.type == "aftertouch": n["at"] += 1
        elif x.type == "note_on" and x.velocity > 0: n["notes"] += 1
print(f"{n['pc']} {n['pb']} {n['cc']} {n['at']} {n['notes']}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SANS="menu:Enregistrement > Sans décompte"
SANS_PLI="menu:Enregistrement > Ce que la prise garde > La prise garde le pitch bend"
SANS_PC="menu:Enregistrement > Ce que la prise garde > La prise garde les changements de programme"
JOUER="1100:midi-entrant:C0:05;1400:midi-entrant:E0:00:50;1700:cc-entrant:1:100;2000:midi-entrant:D0:40;2300:note-entrante:60:100:200"

echo "=== D584 : les changements de programme joués au clavier, et ce que la prise garde ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course tout "" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:touche:F9;$JOUER;2800:touche:F9;3200:exporter-midi:$brouillon/tout.mid"
course hors "" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;$JOUER;3200:exporter-midi:$brouillon/hors.mid"
course filtre "$maison" VSM_DELAI=3600 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$SANS_PLI;600:$SANS_PC;800:touche:F9;$JOUER;2800:touche:F9;3200:exporter-midi:$brouillon/filtre.mid"
course defait "" VSM_DELAI=3800 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:touche:F9;$JOUER;2800:touche:F9;3100:touche:ctrl + Z;3400:exporter-midi:$brouillon/defait.mid"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

ct="$(compte "$brouillon/tout.mid")"; ch="$(compte "$brouillon/hors.mid")"; cf="$(compte "$brouillon/filtre.mid")"
cz="$(compte "$brouillon/defait.mid")"
grep -h "^VSM_MIDI_ENTRANT\|^VSM_REGLAGE_POSTE" "$brouillon"/tout.txt "$brouillon"/filtre.txt | sed 's/^/       /'
echo "       (programme 5, pitch bend, CC 1, pression, notes) : tout coché [$ct] ; hors prise [$ch] ; filtrés [$cf] ; Ctrl+Z [$cz]"
decoches="$(grep -h "^VSM_MENU_LISTE : Enregistrement > Ce que la prise garde > La prise garde \(le pitch bend\|les changements de programme\)" "$brouillon/retenu.txt" | grep -vc "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : Recording > What the take keeps > The take keeps program changes" "$brouillon/anglais.txt")"
echo "       relancé : $decoches case(s) décochée(s) sur 2 ; en anglais : $en entrée(s)"

champ() { echo "$1" | awk -v i="$2" '{print $i}'; }
verdict "(1) tout coché : 1 programme 5, 1 pitch bend, 1 CC 1, 1 pression, 1 note" "$([ "$ct" = "1 1 1 1 1" ] && echo 1 || echo 0)"
verdict "(2) le témoin hors prise : 0 programme 5" "$([ "$(champ "$ch" 1)" = 0 ] && echo 1 || echo 0)"
verdict "(3) pitch bend et programmes décochés : 0 0, et 1 1 1" "$([ "$cf" = "0 0 1 1 1" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z après la prise : 0 programme, 0 note, en un pas" "$([ "$(champ "$cz" 1)" = 0 ] && [ "$(champ "$cz" 5)" = 0 ] && echo 1 || echo 0)"
verdict "(5) retenu au poste ; en anglais" "$([ "$decoches" = 2 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MIDI-ENTRANT-PRISE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MIDI-ENTRANT-PRISE : un programme joué s'enregistre ; ce que la prise ne garde pas, elle ne l'écrit pas"
