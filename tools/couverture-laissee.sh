#!/usr/bin/env bash
# D556 (H21) — UNE NOTE LAISSÉE HORS DES CLIPS N'EST PAS « ÉCRITE » : la couverture de D337 ne la rattrape pas au
# premier geste venu sur une AUTRE note. MESURÉ PAR LE .mid JOUÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4). Deux projets :
#   P : un clip A [0, 1 920) avec une note [1 200, 1 440) de vélocité 30 — la seule faible —, un clip B [3 840, 5 760),
#       et une note 64 [2 400, 2 880) ENTRE les deux, hors de tout clip : la lecture ne l'entend pas.
#   Q : un clip A [0, 7 680), une note par mesure (60, 62, 64, 65 ; celle de la mesure 2 de vélocité 30), puis
#       « Rogner les clips aux locateurs » sur [1 920, 5 760) : A ne fait plus entendre que les mesures 2 et 3.
#   (1) P, le TÉMOIN sans geste : le 64 de 2 400 n'est pas joué ;
#   (2) P, « Rendre muet » sur la note faible (une AUTRE note) : le 64 n'est toujours pas joué, et aucune ligne D337 ;
#   (3) Q, rogné puis « Rendre muet » sur la note faible : le rognage TIENT — 60 et 65 ne sont pas joués ;
#   (4) la couverture vit toujours pour ce qu'on ÉCRIT : une note écrite à 2 400 sur P y est jouée (D337).
#
#   tools/couverture-laissee.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-couverture.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def ecrire(nom, notes, clips, boucle=(0, 0)):
    ev = []
    for d, f, h, v in notes:
        ev.append((d, 1, bytes([0x90, h, v]))); ev.append((f, 0, bytes([0x80, h, 0])))
    ev.sort(key=lambda e: (e[0], e[1]))
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
    t = 0
    for tick, _, m in ev:
        corps += vlq(tick - t) + m; t = tick
    corps += b"\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "clips": [{"start": d, "length": l, "sourceStart": d, "sourceLength": l, "name": n} for n, d, l in clips]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
ecrire("P", [(480, 960, 60, 100), (1200, 1440, 62, 30), (2400, 2880, 64, 100), (3840, 4320, 65, 100)],
       [("A", 0, 1920), ("B", 3840, 1920)])
ecrire("Q", [(0, 480, 60, 100), (1920, 2400, 62, 30), (3840, 4320, 64, 100), (5760, 6240, 65, 100)],
       [("A", 0, 7680)], boucle=(1920, 5760))
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll,piste:0" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # les hauteurs jouées d'un .mid, dans l'ordre des départs, ou « ABSENT »
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
print(",".join(str(h) for _, h in sorted(out)) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MUET="500:menu:Édition > Sélection > Notes plus faibles que 64 (1);800:menu:Édition > Rendre muet / audible"

echo "=== D556 (H21) : une note laissée hors des clips n'est pas « écrite » ==="
course temoin "$brouillon/P" VSM_DELAI=2000 VSM_GESTE_APRES="1200:exporter-midi:$brouillon/temoin.mid"
course muet "$brouillon/P" VSM_DELAI=2200 VSM_GESTE_APRES="$MUET;1400:exporter-midi:$brouillon/muet.mid"
course rogne "$brouillon/Q" VSM_DELAI=2600 \
    VSM_GESTE_APRES="300:menu:Rogner les clips aux locateurs;$MUET;1800:exporter-midi:$brouillon/rogne.mid"
course ecrite "$brouillon/P" VSM_DELAI=2200 VSM_GESTE_APRES="800:ecrire-note:0:2400:480:67;1400:exporter-midi:$brouillon/ecrite.mid"

t="$(hauteurs "$brouillon/temoin.mid")"; m="$(hauteurs "$brouillon/muet.mid")"; r="$(hauteurs "$brouillon/rogne.mid")"
e="$(hauteurs "$brouillon/ecrite.mid")"
grep -h "^D337" "$brouillon/muet.txt" "$brouillon/rogne.txt" "$brouillon/ecrite.txt" | sed 's/^/       /'
echo "       P témoin [$t] ; P après « muet » [$m] ; Q rogné puis « muet » [$r] ; P avec une note écrite à 2 400 [$e]"

verdict "(1) le témoin : la note hors clip n'est pas jouée" "$([ "$t" = "60,62,65" ] && echo 1 || echo 0)"
verdict "(2) « muet » sur une autre note : la note laissée dehors le reste, sans ligne D337" \
    "$([ "$m" = "60,65" ] && ! grep -q "^D337" "$brouillon/muet.txt" && echo 1 || echo 0)"
verdict "(3) un clip rogné le reste après une édition" "$([ "$r" = "64" ] && echo 1 || echo 0)"
verdict "(4) une note ÉCRITE hors des clips est couverte et jouée (D337)" \
    "$(echo "$e" | grep -q "67" && grep -q "^D337" "$brouillon/ecrite.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COUVERTURE-LAISSEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COUVERTURE-LAISSEE : la couverture rattrape ce qu'on écrit, pas ce qu'on a laissé"
