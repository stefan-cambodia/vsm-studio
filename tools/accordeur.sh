#!/usr/bin/env bash
# D605 — L'ACCORDEUR (le *Tuner* de Cubase, en insert), mesuré dans l'application : ce que sa case montre et ce que son
# relevé dit, le transport en lecture puis arrêté ; le diapason de l'insert ; l'anglais ; le témoin.
#
# LE JEU : 480 ticks à la noire, 120 BPM, une piste au générateur de test jouant UNE note tenue, le do 6 (la note 84,
# 1 046,5 Hz au diapason 440), du tick 0 au tick 7 680 ; un insert « tuner » (son diapason 440, ou 442).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D605) :
#   (3) en lecture, la case lit le nom de `nomDeNote(84)` — « C6 » au style d'origine de D586.3 — et l'écart à 0 c (± 2) ;
#       le diapason à 442 Hz → −7,9 c (± 2) ; le transport arrêté → « — » ; le témoin (D604) : l'effet inconnu ;
#   (4) en anglais, « Tuner » et le nom de la note en lettres ; la case photographiée.
#
#   tools/accordeur.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D604/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-accordeur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + vlq(0) + bytes([0x90, 84, 100]) + vlq(7680) \
        + bytes([0x80, 84, 0]) + b"\x00\xff\x2f\x00"
for nom, ref in (("a440", 440.0), ("a442", 442.0)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "name": "Partie",
                           "effects": [{"type": "tuner", "parameters": {"effect.tuner.reference": ref}}],
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                           "instrument": {"preferredPlugin": "vsm.testtone"}}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = projet ; le reste : variables
    local bin="$1" nom="$2" projet="$3" n
    shift 3
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
releve() { grep "^VSM_ACCORDEUR : " "$brouillon/$1.txt" | sed -n "${2}p"; }
cents_de() { echo "$1" | sed -n 's/.* mesure(s), \(-\?[0-9.]*\) c, clart.*/\1/p'; }
case_de() { echo "$1" | sed -n 's/.* ; case « \(.*\) »$/\1/p'; }
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

# L'ARRÊT PAR LE BOUTON. CORRIGÉ APRÈS LE PREMIER ESSAI : la barre d'espace, la vue des effets ouverte, a été prise par
# la fenêtre (`VSM_TOUCHE : « spacebar » → fenêtre : prise`) et le transport ne s'est pas arrêté — l'accordeur mesurait
# encore, à juste titre, une note qui jouait toujours.
GESTES="1500:relever-inserts;1700:cliquer:Stop;2600:relever-inserts"
echo "=== D605 : l'accordeur ==="
course "$BIN" a440 "$brouillon/a440" VSM_LECTURE=1 VSM_VUE="sans-rapport,effets" VSM_DELAI=3000 VSM_GESTE_APRES="$GESTES"
course "$BIN" a442 "$brouillon/a442" VSM_LECTURE=1 VSM_VUE="sans-rapport,effets" VSM_DELAI=2000 VSM_GESTE_APRES="1500:relever-inserts"
course "$BIN" en "$brouillon/a440" VSM_LANGUE=en VSM_LECTURE=1 VSM_VUE="sans-rapport,effets" VSM_DELAI=2000 VSM_GESTE_APRES="1500:relever-inserts"

L1="$(releve a440 1)"; L2="$(releve a440 2)"; L3="$(releve a442 1)"; L4="$(releve en 1)"
echo "       en lecture : $L1"
echo "       arrêté     : $L2"
echo "       à 442 Hz   : $L3"
echo "       en anglais : $L4"
c1="$(cents_de "$L1")"; k1="$(case_de "$L1")"; k2="$(case_de "$L2")"; c3="$(cents_de "$L3")"; k4="$(case_de "$L4")"
verdict "(3) en lecture : la case lit « Tuner · C6 · … · 1046.5 Hz », l'écart à 0 c (± 2)" \
    "$(echo "$k1" | grep -q "^Tuner · C6 · .* · 104[0-9]\.[0-9] Hz$" && proche "${c1:-x}" 0 2 && echo 1 || echo 0)"
verdict "(3) le diapason à 442 Hz : l'écart à −7,9 c (± 2)" "$(proche "${c3:-x}" -7.9 2 && echo 1 || echo 0)"
verdict "(3) le transport arrêté : la case lit « — »" "$([ "$k2" = "Tuner · —" ] && echo 1 || echo 0)"
verdict "(4) en anglais : « Tuner · C6 · … »" "$(echo "$k4" | grep -q "^Tuner · C6 · " && echo 1 || echo 0)"

TEMOIN_OK=0
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a440 "$brouillon/a440" VSM_LECTURE=1 VSM_VUE="sans-rapport,effets" VSM_DELAI=2000 VSM_GESTE_APRES="1500:relever-inserts"
    tl="$(grep -c "^VSM_ACCORDEUR" "$brouillon/t-a440.txt")"; ti="$(grep -ci "tuner" "$brouillon/t-a440.txt")"
    echo "       témoin : $tl relevé(s) d'accordeur ; « tuner » nommé $ti fois au journal"
    verdict "(3) témoin : aucun accordeur (l'effet inconnu du binaire de D604)" "$([ "$tl" = 0 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ACCORDEUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ACCORDEUR : l'insert lit la note qui le traverse, à son diapason, et dit quand il n'entend rien"
