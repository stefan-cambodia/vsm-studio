#!/usr/bin/env bash
# D551.3 — ROGNER LES CLIPS AUX LOCATEURS (Édition ▸ Rogner les clips aux locateurs, le *Range ▸ Crop* de Cubase),
# MESURÉ PAR LE PROJET RELU ET PAR L'EXPORT.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM : une mesure = 1 920 ticks = 2 s). Projet Q : une piste, un
# clip A [0, 7680) — une note par mesure, chacune de trois temps —, les locateurs de 2 · 1 à 4 · 1 [1920, 5760).
#   (1) RIEN DE CHOISI → les clips qui touchent la plage sont rognés : A [1920, 5760), toujours choisi ;
#   (2) L'EXPORT : dans la plage (2,5 à 5,9 s), au bit près celui du TÉMOIN — le même projet, non rogné, SANS la note
#       d'avant la plage —, et avant elle (0 à 1,9 s) au bit près le sien. Pas celui du projet d'origine : la machine
#       qui a joué la note d'avant n'est plus dans le même état (le « caractère analogique » du Minimoog est aléatoire),
#       et l'écart à l'origine, 2,7·10⁻⁵ dans la plage, est publié à part. La première version du banc demandait le
#       bit près contre l'origine ; c'est ce témoin qui a départagé « la coupe est fausse » de « l'état diffère » ;
#   (3) un pas : Ctrl+Z → A [0, 7680) ;
#   (4) le VERROU : A verrouillé → rien de rogné, « Montage refusé : 1 clip verrouillé est resté en place » ;
#   (5) une sélection : deux pistes, A [0, 3840) et B [0, 3840), A seul choisi → A [1920, 3840), B intact ;
#   (6) des locateurs qui couvrent tout le clip ([0, 7680)) : « Rien à rogner » dit, aucun pas.
#
#   tools/rogner-aux-locateurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-rogner-locateurs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, notes, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90 | canal, 60, 100]); corps += vlq(1440) + bytes([0x80 | canal, 60, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
def entree(n, c, clips):
    return {"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
            "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}
clip = lambda n, d, l, **x: dict({"start": d, "length": l, "sourceStart": d, "sourceLength": l, "name": n}, **x)
def ecrire(nom, pistes_mid, pistes_json, boucle):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes_mid), 480) + b"".join(pistes_mid))
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": pistes_json}, open(f"{b}/{nom}/project.json", "w"), indent=1)
une = [piste("Partie", 0, [0, 1920, 3840, 5760], tempo=True)]
ecrire("Q", une, [entree("Partie", 0, [clip("A", 0, 7680)])], (1920, 5760))
ecrire("Q-verrou", une, [entree("Partie", 0, [clip("A", 0, 7680, locked=True)])], (1920, 5760))
ecrire("Q-tout", une, [entree("Partie", 0, [clip("A", 0, 7680)])], (0, 7680))
# LE TÉMOIN de (2) : le clip entier, les notes de la plage seules -- la machine y part du même état que dans le rogné.
ecrire("Q-temoin", [piste("Partie", 0, [1920, 3840], tempo=True)], [entree("Partie", 0, [clip("A", 0, 7680)])], (1920, 5760))
ecrire("R", [piste("Haut", 0, [0, 1920], tempo=True), piste("Bas", 1, [0, 1920])],
       [entree("Haut", 0, [clip("A", 0, 3840)]), entree("Bas", 1, [clip("B", 0, 3840)])], (1920, 5760))
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:longueur » des clips, triés par piste puis début, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{c.get('name', '?')}:{int(c.get('start', 0))}:{int(c.get('length', 0))}\" for t in p['tracks'] for c in sorted(t.get('clips', []), key=lambda c: c.get('start', 0))) or 'AUCUN')
" "$1"
}
releve() { local l; l="$(grep -h "^VSM_SELECTION_CLIPS : [0-9]* :" "$brouillon/$1.txt" | tail -1)"; [ -n "$l" ] && echo "${l#VSM_SELECTION_CLIPS : }" || echo "NON RELEVÉE"; }
compare() {   # « dedans:avant » — écart maximal b − a sur [2,5 ; 5,9] s, puis sur [0 ; 1,9] s
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(b) == 0 or float(np.max(np.abs(a))) == 0.0:
    print("ABSENT"); sys.exit(0)
i0, i1, j1 = int(2.5 * sr), int(5.9 * sr), int(1.9 * sr)
print(f"{float(np.max(np.abs(b[i0:i1] - a[i0:i1]))):.3g}:{float(np.max(np.abs(b[:j1] - a[:j1]))):.3g}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
ROGNER="menu:Rogner les clips aux locateurs"
REFUS="^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place"

echo "=== D551.3 : rogner les clips aux locateurs ==="
for x in Qr Qz; do cp -r "$brouillon/Q" "$brouillon/$x"; done
course rogner "$brouillon/Qr" VSM_DELAI=2800 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$ROGNER;1600:relever-selection;1900:enregistrer:$brouillon/Qr"
course annuler "$brouillon/Qz" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$ROGNER;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Qz"
course verrou "$brouillon/Q-verrou" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$ROGNER;1700:enregistrer:$brouillon/Q-verrou"
course choix "$brouillon/R" VSM_DELAI=2500 VSM_VUE="$VUE,premier-clip:0" VSM_GESTE_APRES="1200:$ROGNER;1700:enregistrer:$brouillon/R"
course tout "$brouillon/Q-tout" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$ROGNER;1500:relever-historique;1700:enregistrer:$brouillon/Q-tout"
course export-avant "$brouillon/Q" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course export-apres "$brouillon/Qr" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"
course export-temoin "$brouillon/Q-temoin" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"

r="$(clips "$brouillon/Qr/project.json")"; sel="$(releve rogner)"; z="$(clips "$brouillon/Qz/project.json")"
v="$(clips "$brouillon/Q-verrou/project.json")"; ch="$(clips "$brouillon/R/project.json")"
t="$(clips "$brouillon/Q-tout/project.json")"; hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/tout.txt" | tail -1)"
ex="$(compare "$brouillon/avant.wav" "$brouillon/apres.wav")"; et="$(compare "$brouillon/temoin.wav" "$brouillon/apres.wav")"
echo "       rogné [$r], choisi [$sel] ; Ctrl+Z [$z] ; verrouillé [$v] ; A seul choisi [$ch] ; tout dedans [$t]"
grep -h "^VSM_ROGNE_LOCATEURS" "$brouillon/rogner.txt" "$brouillon/verrou.txt" "$brouillon/choix.txt" "$brouillon/tout.txt" | sed 's/^/       /'
echo "       export, écart dans la plage : avant elle — contre le témoin $et ; contre l'origine $ex ; ${hist:-historique non relevé}"

verdict "(1) sans choix : le clip qui touche la plage est rogné aux deux locateurs, et reste choisi" \
    "$([ "$r" = "A:1920:3840" ] && [ "$sel" = "1 : Partie:1920" ] && echo 1 || echo 0)"
verdict "(2) l'export : au bit près celui du témoin sans la note d'avant, dans la plage et avant elle" \
    "$([ "$et" = "0:0" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le clip entier" "$([ "$z" = "A:0:7680" ] && echo 1 || echo 0)"
verdict "(4) le verrou : rien de rogné, le refus est dit" "$([ "$v" = "A:0:7680" ] && grep -q "$REFUS" "$brouillon/verrou.txt" && echo 1 || echo 0)"
verdict "(5) une sélection : seul le clip choisi est rogné" "$([ "$ch" = "A:1920:1920 B:0:3840" ] && echo 1 || echo 0)"
verdict "(6) tout dans la plage : « Rien à rogner » dit, aucun pas" \
    "$([ "$t" = "A:0:7680" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] && grep -q "^VSM_BOITE : Rien à rogner" "$brouillon/tout.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ROGNER-AUX-LOCATEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ROGNER-AUX-LOCATEURS : seule la plage reste, au bit près"
