#!/usr/bin/env bash
# D545.2 — LE TEMPO D'UNE PRISE, DÉTECTÉ ET ADOPTÉ PAR LE PROJET.
#
# LA RÈGLE GARDÉE. Une boucle de batterie de synthèse à 137,5 BPM (grosse caisse sur 1 et 3, caisse claire
# sur 2 et 4, charleston aux croches, quatre mesures), importée sur une piste audio d'un projet à 120 BPM.
#   (1) « Détecter le tempo de ce clip », adopté (VSM_CONFIRMER=oui) : le journal dit le tempo trouvé, et le
#       projet relu est à 137,5 (±0,1) ;
#   (2) refusé (VSM_CONFIRMER=non) : le projet relu reste à 120 — le témoin ;
#   (3) un pas : adopté puis Ctrl+Z → 120 ;
#   (4) la fenêtre photographiée en français et en anglais (la photo regardée à la main).
#
#   tools/tempo-detecte.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tempo.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr, bpm = 44100, 137.5
temps = 60.0 / bpm
x = [0.0] * (int(16 * temps * sr) + 1)
etat = 2463534242
def bruit():
    global etat
    etat ^= (etat << 13) & 0xFFFFFFFF; etat ^= etat >> 17; etat ^= (etat << 5) & 0xFFFFFFFF
    return etat / 4294967295.0 * 2.0 - 1.0
def poser(t, duree, onde):
    d = round(t * sr)
    for i in range(int(duree * sr)):
        if d + i < len(x): x[d + i] += onde(i / sr)
for k in range(16):
    t = k * temps
    if k % 2 == 0: poser(t, 0.15, lambda s: 0.8 * math.sin(2 * math.pi * 60 * s) * math.exp(-s / 0.05))
    else: poser(t, 0.10, lambda s: 0.5 * bruit() * math.exp(-s / 0.03))
    for j in range(2): poser(t + j * temps / 2, 0.03, lambda s: 0.25 * bruit() * math.exp(-s / 0.008))
crete = max(abs(v) for v in x) or 1.0
trames = b"".join(struct.pack("<hh", int(v / crete * 0.8 * 32767), int(v / crete * 0.8 * 32767)) for v in x)
with wave.open(f"{b}/boucle.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|CONFIRMER|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tempo() {   # le tempo au tick 0 d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    t = json.load(open(sys.argv[1]))['transport']['tempoChanges']
except Exception:
    print('ABSENT'); sys.exit(0)
print(f\"{sorted(t, key=lambda c: c.get('tick', 0))[0]['bpm']:.2f}\" if t else 'ABSENT')
" "$1"
}
pres() { python3 -c "
import sys
try:
    sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D545.2 : le tempo d'une prise ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/boucle.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in adopte refuse annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
MENU="clip-audio-tous:Détecter le tempo de ce clip"
course adopter "$brouillon/adopte" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/adopte"
course refuser "$brouillon/refuse" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_CONFIRMER=non \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/refuse"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"

grep -h "^VSM_TEMPO_DETECTE" "$brouillon/adopter.txt" | sed 's/^/       /'
ta="$(tempo "$brouillon/adopte/project.json")"; tr_="$(tempo "$brouillon/refuse/project.json")"; tz="$(tempo "$brouillon/annule/project.json")"
echo "       tempo du projet relu : adopté $ta ; refusé $tr_ ; adopté puis Ctrl+Z $tz"
verdict "(1) adopté : le journal dit le tempo trouvé, le projet relu à 137,5 (±0,1)" \
    "$(grep -q "^VSM_TEMPO_DETECTE : 137\.[45][0-9] BPM" "$brouillon/adopter.txt" && pres "$ta" 137.5 0.1 && echo 1 || echo 0)"
verdict "(2) refusé : le projet relu reste à 120 (le témoin)" "$(pres "$tr_" 120 0.001 && echo 1 || echo 0)"
verdict "(3) un pas : adopté puis Ctrl+Z → 120" "$(pres "$tz" 120 0.001 && echo 1 || echo 0)"

photos=0
for langue in fr en; do
    entree="Détecter le tempo de ce clip"; [ "$langue" = en ] && entree="Detect the tempo of this clip"
    titre="Tempo du clip"; [ "$langue" = en ] && titre="Clip tempo"
    for essai in 1 2 3; do   # D72 : une boîte absente d'une photo ne prouve rien
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
            VSM_DELAI=3500 VSM_MENU_CONTEXTE="clip-audio-tous:$entree" VSM_CAPTURE_PANNEAUX=1 \
            VSM_CAPTURE="$brouillon/fenetre-$langue.png" timeout 60 "$BIN" > "$brouillon/fenetre-$langue.txt" 2>&1
        grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt" && break
        sleep 20
    done
    if grep -q "^VSM_BOITE : $titre — fenêtre modale ouverte" "$brouillon/fenetre-$langue.txt" \
        && grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt"; then
        photos=$((photos + 1))
        sed -n 's/^VSM_CAPTURE_PANNEAUX : /       photo ('"$langue"') : /p' "$brouillon/fenetre-$langue.txt"
    fi
done
verdict "(4) la fenêtre ouverte et photographiée en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TEMPO-DETECTE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TEMPO-DETECTE : le tempo d'une prise se trouve au dixième, et s'adopte en un pas"
