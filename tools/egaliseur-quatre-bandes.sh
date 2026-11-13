#!/usr/bin/env bash
# D642 — L'ÉGALISEUR DE TRANCHE À QUATRE BANDES (le *Hi-Mid* de l'égaliseur de Cubase) : l'insert « Equaliser » gagne une
# seconde cloche (*Mid 2 Freq*, *Mid 2 Gain*, *Mid 2 Q*), à la fin de sa liste, sautée à 0 dB.
#
# LES RÈGLES GARDÉES (une piste du générateur de test, `vsm.testtone`, qui joue le do 6 — la note 84, 1 046,5 Hz — huit
# secondes ; l'égaliseur posé par `VSM_MENU_CONTEXTE=ajout-effet:Equaliser` — ce qui le CHOISIT et montre ses boutons ;
# CORRIGÉ AU PREMIER ESSAI : un insert posé par le projet n'est pas choisi, et ses boutons ne sont pas à l'écran —
# *Mid 2 Freq* posé à 1 046,5 Hz par son bouton, dans les deux courses). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D642, (d) à (f)) :
#   (d) *Mid 2 Gain* à +6 dB par son bouton (`valeur:effet.Mid 2 Gain=6`) : la crête de l'export +6,0 dB (± 0,3) au-dessus de
#       l'export sans le geste ; le projet enregistré porte « effect.eq.mid2.gain » à 6 ;
#   (e) le témoin (le binaire d'avant D642) : le bouton inconnu, l'export inchangé ;
#   (f) la tranche montre dix boutons (la photo, regardée).
#
#   tools/egaliseur-quatre-bandes.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D641/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-egaliseur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + vlq(0) + bytes([0x90, 84, 100]) + vlq(7680) \
        + bytes([0x80, 84, 0]) + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/midi", exist_ok=True)
open(f"{b}/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "egaliseur", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "name": "Partie",
                       "effects": [],
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.5},
                       "instrument": {"preferredPlugin": "vsm.testtone", "preset": "instruments/track_00.synth.json"}}]},
          open(f"{b}/project.json", "w"), indent=1)
# CORRIGÉ AU PREMIER ESSAI DU BINAIRE NEUF : le générateur joue d'origine une DENT DE SCIE (`Waveform` = 1) — sa crête ne
# monte que de la part du spectre que la cloche relève (+5,10 dB mesurés) ; l'attendu parlait d'un SINUS, la machine en joue un.
os.makedirs(f"{b}/instruments", exist_ok=True)
json.dump({"format": "vsm-synth-preset", "version": 1, "name": "Sinus", "pluginId": "vsm.testtone", "machineName": "",
           "fidelity": "exact", "parameters": {"oscillator.1.waveform": 0}},
          open(f"{b}/instruments/track_00.synth.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie neuve du projet, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport,effets" VSM_MENU_CONTEXTE="ajout-effet:Equaliser" VSM_DELAI=7000 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|VALEUR|VALEUR_POSEE|VUE|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
crete() {
    python3 - "$1" <<'PY2'
import math, os, struct, sys
p = sys.argv[1]
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None; ext = b""
while i + 8 <= len(d):
    nom, t = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24]); ext = d[i + 8 + 24:i + 8 + 26]
    if nom == b"data": data = d[i + 8:i + 8 + t]
    i += 8 + t + (t & 1)
if fmt is None or data is None: print("ABSENT"); sys.exit(0)
tag, bits = fmt[0], fmt[5]
if tag == 0xFFFE: tag = int.from_bytes(ext, "little")
m = 0.0
if tag == 3:
    for (v,) in struct.iter_unpack("<f", data[: len(data) // 4 * 4]): m = max(m, abs(v))
else:
    n = bits // 8; plein = float(1 << (bits - 1))
    for k in range(0, len(data) - n + 1, n): m = max(m, abs(int.from_bytes(data[k:k + n], "little", signed=True)) / plein)
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.2f}")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
ecart() { python3 -c "import sys; a, b = sys.argv[1:3]; print('NA' if 'ABSENT' in (a, b) or '-inf' in (a, b) or '' in (a, b) else f'{float(a) - float(b):.2f}')" "$1" "$2"; }
pres() { python3 -c "import sys; v, c, t = sys.argv[1:4]; sys.exit(0 if v != 'NA' and abs(float(v) - float(c)) <= float(t) else 1)" "$@" 2>/dev/null && echo 1 || echo 0; }
gain_projet() { python3 -c "import json, sys; t = json.load(open(sys.argv[1] + '/project.json'))['tracks'][0]; print(t['effects'][0]['parameters'].get('effect.eq.mid2.gain', 'absent'))" "$1" 2>/dev/null || echo ABSENT; }

echo "=== D642 : l'égaliseur de tranche à quatre bandes ==="
FREQ="700:valeur:effet.Mid 2 Freq=1046.5"
course "$BIN" sans "$FREQ;1500:exporter-audio:$brouillon/sans.wav"
course "$BIN" avec "$FREQ;800:valeur:effet.Mid 2 Gain=6;1200:enregistrer:$brouillon/avec-projet;1500:exporter-audio:$brouillon/avec.wav"
CS="$(crete "$brouillon/sans.wav")"; CA="$(crete "$brouillon/avec.wav")"; E="$(ecart "$CA" "$CS")"; G="$(gain_projet "$brouillon/avec-projet")"
echo "       crêtes : sans le geste $CS dBFS ; Mid 2 à +6 dB $CA dBFS (écart $E dB) ; projet : effect.eq.mid2.gain = $G"
grep -h "^VSM_VALEUR" "$brouillon/avec.txt" | head -2 | sed 's/^/       /'
verdict "(d) Mid 2 à +6 dB : la crête +6,0 dB (± 0,3) au-dessus de l'export sans le geste" "$(pres "$E" 6.0 0.3)"
verdict "(d) le projet porte effect.eq.mid2.gain à 6" \
    "$(python3 -c "import sys; sys.exit(0 if sys.argv[1] not in ('absent', 'ABSENT') and abs(float(sys.argv[1]) - 6) < 1e-3 else 1)" "$G" 2>/dev/null && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-avec "$FREQ;800:valeur:effet.Mid 2 Gain=6;1500:exporter-audio:$brouillon/t-avec.wav"
    CT="$(crete "$brouillon/t-avec.wav")"; ET="$(ecart "$CT" "$CS")"
    echo "       témoin : crête $CT dBFS (écart $ET dB au neuf sans geste) ; $(grep -h "Mid 2" "$brouillon/t-avec.txt" | head -1 | cut -c1-140)"
    verdict "(e) témoin (avant D642) : le bouton inconnu, l'export inchangé" \
        "$( [ "$(pres "$ET" 0.0 0.05)" = 1 ] && grep -q "Mid 2 Gain" "$brouillon/t-avec.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "EGALISEUR-QUATRE-BANDES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "EGALISEUR-QUATRE-BANDES : la seconde cloche lève sa bande, et un projet d'avant sonne pareil"
