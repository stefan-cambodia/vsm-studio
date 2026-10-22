#!/usr/bin/env bash
# D569.1 — EXTRAIRE L'AUTOMATION MIDI (Piste ▸ Extraire l'automation des CC 7 et 10 (N CC) ; l'*Extract MIDI
# Automation* de Cubase), MESURÉ PAR L'EXPORT — le même son avant et après — ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. « Partie », fader 0,8, panoramique −0,25, un do tenu huit secondes ; CC 7 : 100 à 0, 40 à 1 920,
# 127 à 3 840 ; CC 10 : 20 à 960, 110 à 2 880 ; un CC 74 à 0, qui va à la machine.
#   (1) le menu dit « (5 CC) » ; le projet relu : la courbe `mix.volume` en marches à 0, 1 920, 3 840 (0,8 × v/127), la
#       courbe `mix.pan` en marches à 0 (−0,25), 960, 2 880 (−0,25 + 2v/127 − 1, borné) ; plus aucun CC 7 ni 10 au .mid
#       exporté, le CC 74 resté ;
#   (2) L'ATTENDU ÉCRIT AVANT LA MESURE ÉTAIT « AU BIT PRÈS » — RÉFUTÉ le 22/10 (D569.1) : les deux exports diffèrent
#       sur un ou deux BLOCS de 128 échantillons autour de chaque marche (de 2,9 à 5,8 ms, crête −35,8 dBFS pour un
#       signal à −16), parce que le moteur applique un CC dès le DÉBUT du bloc où il tombe et la courbe en marches à
#       partir du bloc SUIVANT. Ce que la garde tient désormais : identiques AU BIT hors de ± 3 blocs (384 échantillons)
#       autour des quatre marches, et l'écart sous −30 dBFS dedans ;
#   (3) le témoin : sans extraction, le même projet rendu deux fois est identique (le rendu est déterministe) ;
#   (4) un pas : Ctrl+Z rend les CC et retire les courbes, après un geste qui a eu lieu.
#
#   tools/automation-extraite.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-extraite.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.wav "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, bytes([0xB0, 7, 100])), (0, bytes([0xB0, 74, 30])), (0, bytes([0x90, 60, 100])), (960, bytes([0xB0, 10, 20])),
       (1920, bytes([0xB0, 7, 40])), (2880, bytes([0xB0, 10, 110])), (3840, bytes([0xB0, 7, 127])), (7680, bytes([0x80, 60, 0]))]
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for at, o in evs:
    corps += vlq(at - t) + o; t = at
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": -0.25, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{d}/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
courbes() {   # « paramètre:tick=valeur[s]… » de chaque courbe, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
out = []
for c in d['tracks'][0].get('automation', []):
    out.append(c['parameter'] + ':' + ','.join(f\"{int(p['tick'])}={p['value']:.4f}{'s' if p.get('step') else ''}\" for p in c['points']))
print(' '.join(sorted(out)) or 'AUCUNE')" "$1"
}
ccs() {   # « contrôleur:tick:valeur » des CC du .mid, triés — ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import mido
try:
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
lus = []
for piste in m.tracks:
    t = 0
    for msg in piste:
        t += msg.time
        if msg.type == "control_change": lus.append((msg.control, t, msg.value))
print(" ".join(f"{c}:{t}:{v}" for c, t, v in sorted(lus)) or "AUCUN")
PY2
}
hors_marches() {   # « crête hors des fenêtres / crête dans les fenêtres » (dBFS) autour des instants donnés, ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) != len(b) or a.size == 0:
    print("ABSENT"); sys.exit(0)
d = np.abs(a - b).max(axis=1)
dedans = np.zeros(len(d), dtype=bool)
for t in (1.0, 2.0, 3.0, 4.0):   # les marches : CC 10 à 960 et 2 880, CC 7 à 1 920 et 3 840 (960 ticks = 1 s)
    c = int(round(t * sr)); dedans[max(0, c - 384):c + 384] = True
db = lambda x: "-inf" if x == 0.0 else f"{20 * np.log10(x):.1f}"
print(db(float(d[~dedans].max())) + "/" + db(float(d[dedans].max())))
PY2
}
ecart() {   # crête de la différence de deux exports en dBFS (« -inf » s'ils sont identiques), ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if a.size == 0 or b.size == 0 or float(np.max(np.abs(a))) == 0.0:
    print("ABSENT"); sys.exit(0)
n = min(len(a), len(b))
d = float(np.max(np.abs(a[:n] - b[:n])))
print("-inf" if d == 0.0 and len(a) == len(b) else f"{20 * np.log10(max(d, 1e-300)):.1f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Piste > Extraire l'automation des CC 7 et 10 (5 CC)"

echo "=== D569.1 : extraire l'automation MIDI ==="
for x in extrait annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course extraire "$brouillon/extrait" VSM_DELAI=2500 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:exporter-midi:$brouillon/extrait.mid;1900:enregistrer:$brouillon/extrait"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2000:exporter-midi:$brouillon/annule.mid;2300:enregistrer:$brouillon/annule"
course export-base "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base.wav"
course export-base2 "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base2.wav"
course export-extrait "$brouillon/extrait" VSM_DELAI=1500 VSM_EXPORT="$brouillon/extrait.wav"

ce="$(courbes "$brouillon/extrait")"; cz="$(courbes "$brouillon/annule")"
me="$(ccs "$brouillon/extrait.mid")"; mz="$(ccs "$brouillon/annule.mid")"
e="$(ecart "$brouillon/extrait.wav" "$brouillon/base.wav")"; t="$(ecart "$brouillon/base2.wav" "$brouillon/base.wav")"
hm="$(hors_marches "$brouillon/extrait.wav" "$brouillon/base.wav")"
grep -h "^VSM_AUTOMATION_EXTRAITE" "$brouillon/extraire.txt" | sed 's/^/       /'
echo "       courbes : [$ce]"
echo "       CC du .mid : extrait [$me] ; Ctrl+Z [$mz] ; courbes après Ctrl+Z [$cz]"
echo "       écart de l'export : extrait/origine $e dBFS (hors des marches / dedans : $hm) ; témoin origine/origine $t dBFS"

V='mix.pan:0=-0.2500s,960=-0.9350s,2880=0.4823s mix.volume:0=0.6299s,1920=0.2520s,3840=0.8000s'
verdict "(1) les courbes en marches, plus de CC 7 ni 10, le CC 74 resté" \
    "$([ "$ce" = "$V" ] && [ "$me" = "74:0:30" ] && echo 1 || echo 0)"
verdict "(2) l'export après extraction : identique au bit hors de ± 3 blocs des marches, sous −30 dBFS dedans" \
    "$(awk -v h="$hm" 'BEGIN { split(h, p, "/"); print (p[1] == "-inf" && p[2] != "" && (p[2] == "-inf" || p[2] + 0 < -30)) ? 1 : 0 }')"
verdict "(3) le témoin : le même projet rendu deux fois est identique" "$([ "$t" = "-inf" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les CC et retire les courbes, après un geste qui a eu lieu" \
    "$([ "$cz" = "AUCUNE" ] && [ "$mz" = "7:0:100 7:1920:40 7:3840:127 10:960:20 10:2880:110 74:0:30" ] \
       && grep -q "^VSM_AUTOMATION_EXTRAITE : 3 CC 7, 2 CC 10" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-EXTRAITE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-EXTRAITE : les CC 7 et 10 deviennent les courbes de la tranche, et le morceau sonne pareil"
