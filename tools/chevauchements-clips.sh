#!/usr/bin/env bash
# D562.2 — RETIRER LES CHEVAUCHEMENTS DES CLIPS (Édition ▸ Retirer les chevauchements des clips (N) ; le *Delete Overlaps*
# de Cubase), MESURÉ PAR LE PROJET RELU ET PAR L'EXPORT MIDI.
#
# LES RÈGLES GARDÉES. Une piste MIDI, six noires (0, 480 … 2 400), deux clips qui lisent le même matériau : A [0, 1 920)
# et B [960, 2 880) — les noires de 960 et 1 440 sont jouées DEUX fois.
#   (1) le menu dit « (1) » ;
#   (2) retirés : A finit à 960 (longueur 960), B garde son début ; l'export MIDI passe de 8 attaques à 6 ;
#   (3) un pas : Ctrl+Z rend A à 1 920, après un geste qui a eu lieu ;
#   (4) A verrouillée : rien ne change, et le journal le compte (« 1 verrouillé(s) ») ;
#   (5) après : « (0) » GRISÉE.
#
#   tools/chevauchements-clips.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-chevauchements.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Basse"
t = 0
for debut in range(0, 2880, 480):
    corps += vlq(debut - t) + bytes([0x90, 48, 100]) + vlq(240) + bytes([0x80, 48, 0])
    t = debut + 240
corps += b"\x00\xff\x2f\x00"
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
for nom, verrou in (("base", False), ("verrou", True)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                       + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    a = {"start": 0, "length": 1920, "sourceStart": 0, "sourceLength": 1920, "name": "A"}
    if verrou: a["locked"] = True
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Basse", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "clips": [a, {"start": 960, "length": 1920, "sourceStart": 960, "sourceLength": 1920, "name": "B"}]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:longueur » des clips, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{c.get('name')}:{c['start']}:{c['length']}\" for t in d['tracks'] for c in t.get('clips', [])))" "$1"
}
attaques() {   # le nombre de note-on d'un .mid exporté, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
print(sum(1 for t in m.tracks for e in t if e.type == "note_on" and e.velocity > 0))
PY2
}
entree() { grep -h "^VSM_MENU_LISTE : .*Retirer les chevauchements des clips" "$1" | tail -1 | sed 's/^VSM_MENU_LISTE : .*> //'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="menu:Édition > Retirer les chevauchements des clips (1)"

echo "=== D562.2 : retirer les chevauchements des clips ==="
for x in r z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course retirer "$brouillon/r" VSM_DELAI=2600 VSM_GESTE_APRES="800:lister-menus;1100:$MENU;1400:lister-menus;1800:enregistrer:$brouillon/r"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_GESTE_APRES="1000:$MENU;1400:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
course verrou "$brouillon/verrou" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$MENU;1500:enregistrer:$brouillon/verrou"
course export-temoin "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT_MIDI="$brouillon/temoin.mid"
course export-r "$brouillon/r" VSM_DELAI=1500 VSM_EXPORT_MIDI="$brouillon/r.mid"

cr="$(clips "$brouillon/r")"; cz="$(clips "$brouillon/z")"; cv="$(clips "$brouillon/verrou")"
at="$(attaques "$brouillon/temoin.mid")"; ar="$(attaques "$brouillon/r.mid")"
# « Édition > Retirer… » et non « .*Retirer… » : l'entrée « Annuler : Retirer les chevauchements des clips » la contient
# aussi (la première écriture de ce banc la prenait pour l'entrée).
mapfile -t menus < <(grep -h "^VSM_MENU_LISTE : Édition > Retirer les chevauchements des clips" "$brouillon/retirer.txt" | sed 's/^VSM_MENU_LISTE : Édition > //')
grep -h "^VSM_CHEVAUCHEMENTS_CLIPS" "$brouillon/retirer.txt" "$brouillon/verrou.txt" | sed 's/^/       /'
echo "       menu avant [${menus[0]:-}] après [${menus[1]:-}] ; retirés [$cr] ; Ctrl+Z [$cz] ; verrouillé [$cv]"
echo "       attaques exportées : témoin $at, après $ar"

verdict "(1) le menu dit « (1) »" "$([ "${menus[0]:-}" = "Retirer les chevauchements des clips (1)" ] && echo 1 || echo 0)"
verdict "(2) A finit où B commence ; 8 attaques exportées deviennent 6" \
    "$([ "$cr" = "A:0:960 B:960:1920" ] && [ "$at" = 8 ] && [ "$ar" = 6 ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend A entière, après un geste qui a eu lieu" \
    "$([ "$cz" = "A:0:1920 B:960:1920" ] && grep -q "^VSM_CHEVAUCHEMENTS_CLIPS : 1 recouvrement(s), 1 rogn" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) A verrouillée : rien ne change, et c'est compté" \
    "$([ "$cv" = "A:0:1920 B:960:1920" ] && grep -q "^VSM_CHEVAUCHEMENTS_CLIPS : 1 recouvrement(s), 0 rogn.*1 verrouill" "$brouillon/verrou.txt" && echo 1 || echo 0)"
verdict "(5) après : « (0) » grisée" "$(echo "${menus[1]:-}" | grep -q "^Retirer les chevauchements des clips (0) \[grisée\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CHEVAUCHEMENTS-CLIPS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CHEVAUCHEMENTS-CLIPS : deux clips ne lisent plus deux fois le même passage, et une note ne sonne plus en double"
