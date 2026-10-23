#!/usr/bin/env bash
# D581.1 — LA VOIX DU DESSUS ET LA VOIX DU DESSOUS (Édition ▸ Sélection ▸ La voix du dessus (N) / La voix du dessous (N) ;
# le *Select Highest / Lowest Notes* des éditeurs), MESURÉES PAR LE MIDI EXPORTÉ APRÈS LA SUPPRESSION DE LA SÉLECTION,
# LE TÉMOIN SANS GESTE À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire ; la fenêtre d'un accord, une quadruple-croche : 30 ticks). Le jeu de l'attendu :
# do mi sol à 0 ; ré fa la à 480, 486, 492 (un accord joué) ; si grave seul à 960 ; do et son octave à 1440 ; deux notes
# à 1920 et 1960 (40 ticks : deux accords d'une note).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D581) :
#   (1) « La voix du dessus (6) » puis Supprimer : il reste 5 notes, do mi ré fa do (60 64 62 65 60) ;
#   (2) « La voix du dessous (6) » puis Supprimer : il reste 5 notes, mi sol fa la et l'octave (64 67 65 69 72) ;
#   (3) le TÉMOIN sans geste : 11 notes ;
#   (4) en anglais : « Top voice (6) », « Bottom voice (6) ».
#
#   tools/voix-dessus-dessous.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-voix.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 60), (0, 64), (0, 67), (480, 62), (486, 65), (492, 69), (960, 59), (1440, 60), (1440, 72), (1920, 48), (1960, 55)]
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
json.dump({"format": "vsm-project", "version": 1, "title": "voix", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
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
nombre() { case "$1" in ABSENT|RIEN) echo 0 ;; *) echo "$1" | wc -w ;; esac; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D581.1 : la voix du dessus et la voix du dessous ==="
course dessus VSM_DELAI=2200 VSM_GESTE_APRES="600:choisir:0;900:menu:Édition > Sélection > La voix du dessus (6);1200:menu:Édition > Supprimer;1600:exporter-midi:$brouillon/dessus.mid"
course dessous VSM_DELAI=2200 VSM_GESTE_APRES="600:choisir:0;900:menu:Édition > Sélection > La voix du dessous (6);1200:menu:Édition > Supprimer;1600:exporter-midi:$brouillon/dessous.mid"
course temoin VSM_DELAI=2000 VSM_GESTE_APRES="600:choisir:0;1400:exporter-midi:$brouillon/temoin.mid"
course anglais VSM_LANGUE=en VSM_DELAI=1800 VSM_GESTE_APRES="600:choisir:0;1000:lister-menus"

hd="$(hauteurs "$brouillon/dessus.mid")"; hs="$(hauteurs "$brouillon/dessous.mid")"; ht="$(hauteurs "$brouillon/temoin.mid")"
grep -h "VSM_STATUT\|voix du dess" "$brouillon"/dessus.txt "$brouillon"/dessous.txt | grep -v "^VSM_GESTE_APRES" | sed 's/^/       /' | head -6
echo "       restent : sans le dessus [$hd] ; sans le dessous [$hs] ; témoin [$ht]"
en="$(grep -ch "^VSM_MENU_LISTE : Edit > .*\(Top voice (6)\|Bottom voice (6)\)" "$brouillon/anglais.txt")"
echo "       en anglais : $en entrée(s)"

verdict "(1) sans la voix du dessus : 60 64 62 65 60" "$([ "$hd" = "60 64 62 65 60" ] && echo 1 || echo 0)"
verdict "(2) sans la voix du dessous : 64 67 65 69 72" "$([ "$hs" = "64 67 65 69 72" ] && echo 1 || echo 0)"
verdict "(3) le témoin : 11 notes" "$([ "$(nombre "$ht")" = 11 ] && echo 1 || echo 0)"
verdict "(4) en anglais (2 entrées)" "$([ "$en" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "VOIX-DESSUS-DESSOUS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "VOIX-DESSUS-DESSOUS : de chaque accord, la note du dessus ou du dessous, et une note seule est sa propre voix"
