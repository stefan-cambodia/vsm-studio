#!/usr/bin/env bash
# D550.2 — COUPER LES CLIPS AUX LOCATEURS (Édition ▸ Couper les clips aux locateurs, le *Split Loop* de Cubase), MESURÉ
# PAR LE PROJET RELU ET PAR L'EXPORT.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4). Projet Q : une piste, un clip A [0, 7680) — quatre mesures, une note
# par mesure qui ne passe aucune barre —, les locateurs de 2 · 1 à 4 · 1 [1920, 5760).
#   (1) RIEN DE CHOISI → tout clip qu'un locateur traverse est coupé : trois clips relus, [0, 1920) [1920, 5760)
#       [5760, 7680), et le morceau d'ENTRE les locateurs reste choisi (« 1 : Partie:1920 ») ;
#   (2) le geste suivant porte sur lui : Suppr → il reste [0, 1920) et [5760, 7680) ;
#   (3) L'EXPORT NE BOUGE PAS d'un échantillon : couper n'est pas changer le son (le projet coupé contre le projet
#       d'origine, écart maximal 0) ;
#   (4) un pas : la coupe puis Ctrl+Z → un seul clip ;
#   (5) le VERROU : A verrouillé → rien n'est coupé, « Montage refusé : 1 clip verrouillé est resté en place » ;
#   (6) des locateurs sur les BORDS des clips (Q-bords : A [0, 1920), B [1920, 3840), locateurs [1920, 3840)) :
#       « Rien à couper » dit, aucun pas.
#
#   tools/couper-aux-locateurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-couper-locateurs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, notes):
    corps = b"\x00\xff\x51\x03\x07\xa1\x20" + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(1440) + bytes([0x80, 60, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def ecrire(nom, clips, boucle):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + piste("Partie", [0, 1920, 3840, 5760]))
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
clip = lambda n, d, l, **x: dict({"start": d, "length": l, "sourceStart": d, "sourceLength": l, "name": n}, **x)
ecrire("Q", [clip("A", 0, 7680)], (1920, 5760))
ecrire("Q-verrou", [clip("A", 0, 7680, locked=True)], (1920, 5760))
ecrire("Q-bords", [clip("A", 0, 1920), clip("B", 1920, 1920)], (1920, 3840))
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « début:longueur » des clips, triés, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = sorted((int(x.get('start', 0)), int(x.get('length', 0))) for t in p['tracks'] for x in t.get('clips', []))
print(' '.join(f'{d}:{l}' for d, l in c) or 'AUCUN')
" "$1"
}
releve() { local l; l="$(grep -h "^VSM_SELECTION_CLIPS : [0-9]* :" "$brouillon/$1.txt" | tail -1)"; [ -n "$l" ] && echo "${l#VSM_SELECTION_CLIPS : }" || echo "NON RELEVÉE"; }
ecart_max() {   # écart maximal entre deux .wav, « ABSENT » si l'un manque ou est vide
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(b) == 0 or float(np.max(np.abs(a))) == 0.0:
    print("ABSENT"); sys.exit(0)
n = min(len(a), len(b))
print(f"{float(np.max(np.abs(a[:n] - b[:n]))):.3g} (crête {float(np.max(np.abs(a))):.3f}, {len(a)} et {len(b)} trames)")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
COUPER="menu:Couper les clips aux locateurs"
REFUS="^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place"

echo "=== D550.2 : couper les clips aux locateurs ==="
for x in Qc Qs Qz; do cp -r "$brouillon/Q" "$brouillon/$x"; done
course couper "$brouillon/Qc" VSM_DELAI=2800 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$COUPER;1600:relever-selection;1900:enregistrer:$brouillon/Qc"
course supprimer "$brouillon/Qs" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$COUPER;1600:touche:arrangement:delete;2100:enregistrer:$brouillon/Qs"
course annuler "$brouillon/Qz" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$COUPER;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Qz"
course verrou "$brouillon/Q-verrou" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$COUPER;1700:enregistrer:$brouillon/Q-verrou"
course bords "$brouillon/Q-bords" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$COUPER;1500:relever-historique;1700:enregistrer:$brouillon/Q-bords"
course export-avant "$brouillon/Q" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course export-apres "$brouillon/Qc" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"

c="$(clips "$brouillon/Qc/project.json")"; sel="$(releve couper)"; s="$(clips "$brouillon/Qs/project.json")"
z="$(clips "$brouillon/Qz/project.json")"; v="$(clips "$brouillon/Q-verrou/project.json")"
bo="$(clips "$brouillon/Q-bords/project.json")"; hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/bords.txt" | tail -1)"
ex="$(ecart_max "$brouillon/avant.wav" "$brouillon/apres.wav")"
echo "       coupé [$c], choisis [$sel] ; puis Suppr [$s] ; puis Ctrl+Z [$z] ; verrouillé [$v] ; bords [$bo]"
grep -h "^VSM_COUPE_LOCATEURS" "$brouillon/couper.txt" "$brouillon/verrou.txt" "$brouillon/bords.txt" | sed 's/^/       /'
echo "       export, écart maximal coupé − d'origine : $ex ; ${hist:-historique non relevé}"

verdict "(1) sans choix : le clip traversé est coupé deux fois, le morceau d'entre les locateurs est choisi" \
    "$([ "$c" = "0:1920 1920:3840 5760:1920" ] && [ "$sel" = "1 : Partie:1920" ] && echo 1 || echo 0)"
verdict "(2) le geste suivant porte sur lui : Suppr laisse les deux bords" "$([ "$s" = "0:1920 5760:1920" ] && echo 1 || echo 0)"
verdict "(3) l'export ne bouge pas d'un échantillon" "$([ "${ex%% *}" = 0 ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend le clip entier" "$([ "$z" = "0:7680" ] && echo 1 || echo 0)"
verdict "(5) le verrou : rien de coupé, le refus est dit" \
    "$([ "$v" = "0:7680" ] && grep -q "$REFUS" "$brouillon/verrou.txt" && echo 1 || echo 0)"
verdict "(6) des locateurs sur les bords : « Rien à couper » dit, aucun pas" \
    "$([ "$bo" = "0:1920 1920:1920" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Rien à couper" "$brouillon/bords.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COUPER-AUX-LOCATEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COUPER-AUX-LOCATEURS : les clips se coupent aux deux locateurs, sans changer le son"
