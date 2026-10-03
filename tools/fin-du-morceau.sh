#!/usr/bin/env bash
# D540 — LA FIN D'UN PROJET EST CE QU'IL FAIT ENTENDRE, MESURÉE PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Le moteur planifie, le transport s'arrête et l'export se termine à la fin de ce
# que le projet fait ENTENDRE — pas à la fin de son matériau. À 120 BPM, une mesure = 2 s.
#   (1) une COPIE LIÉE (D34.2) de la dernière mesure, posée au-delà de la fin du matériau, SONNE :
#       la crête de la mesure 2 de l'export vaut à 6 dB près celle de la mesure 1 (même motif) ;
#   (2) « Insérer du silence » d'une mesure au début d'un morceau à clips : la dernière mesure,
#       poussée de 2 s, SONNE encore (crête de [4 s, 6 s) à 6 dB près de celle de la mesure jouée
#       avant l'insertion) ;
#   (3) « Supprimer le temps » de la dernière mesure d'un morceau à clips : l'export RACCOURCIT
#       (de 2 s à 0,1 s près) ;
#   (4) le témoin de (1) : la même copie, le matériau prolongé d'une note qu'aucune fenêtre ne lit.
# Chaque .wav se lit AVANT de juger (présent, non vide), et le journal relaie les avertissements.
#
#   tools/fin-du-morceau.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
PY="$racine/analyse/.venv/bin/python"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$PY" ] || { echo "REFUS : $PY absent — l'environnement d'analyse lit les .wav"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fin.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n:
        o.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(o))
def projet(nom, notes, clips, boucle=(0, 0)):
    """notes : (début, hauteur, durée) dans le matériau ; clips : (fenêtre, début, longueur)."""
    d = f"{b}/{nom}"; os.makedirs(f"{d}/midi", exist_ok=True)
    evs = []
    for debut, h, duree in notes:
        evs += [(debut, 1, bytes([0x90, h, 100])), (debut + duree, 0, bytes([0x80, h, 0]))]
    evs.sort(key=lambda e: (e[0], e[1]))
    corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano", 0
    for tick, _, o in evs:
        corps += vlq(tick - t) + o; t = tick
    corps += b"\x00\xff\x2f\x00"
    open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                  + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                           "clips": [{"sourceStart": s, "sourceLength": l, "start": p, "length": l, "color": "#FF6B9BFF"}
                                     for s, p, l in clips],
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                           "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
motif = [(0, 48, 240), (960, 55, 240)]
projet("copie", motif, [(0, 0, 1920), (0, 1920, 1920)])
projet("copie-temoin", motif + [(5000, 60, 240)], [(0, 0, 1920), (0, 1920, 1920)])
deux = [(0, 48, 480), (1920, 55, 480)]
projet("insertion", deux, [(0, 0, 1920), (1920, 1920, 1920)], boucle=(0, 1920))
projet("suppression", deux, [(0, 0, 1920), (1920, 1920, 1920)], boucle=(1920, 3840))
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_TAILLE="1280x800" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# « durée_s crête_dB_par_fenêtre_de_2_s… » d'un .wav, ou « ABSENT »
mesurer() {
    "$PY" - "$1" <<'PY'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(x) == 0:
    print("ABSENT"); sys.exit(0)
m = np.abs(x).max(axis=1)
fen = 2 * sr
cretes = [20 * np.log10(max(m[i:i + fen].max(), 1e-9)) for i in range(0, len(m), fen)]
print(f"{len(m) / sr:.3f} " + " ".join(f"{c:.1f}" for c in cretes))
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
champ() { echo "$1" | awk -v k="$2" '{print $k}'; }   # $2 = 1 pour la durée, 2… pour les fenêtres
proche() { "$PY" -c "import sys; a, b, t = map(float, sys.argv[1:]); sys.exit(0 if abs(a - b) <= t else 1)" "$1" "$2" "$3"; }

echo "=== D540 : la fin d'un projet est ce qu'il fait entendre ==="
course copie VSM_PROJET="$brouillon/copie" VSM_DELAI=1500 VSM_EXPORT="$brouillon/copie.wav"
course copie-temoin VSM_PROJET="$brouillon/copie-temoin" VSM_DELAI=1500 VSM_EXPORT="$brouillon/copie-temoin.wav"
course inserer VSM_PROJET="$brouillon/insertion" VSM_DELAI=3000 \
    VSM_GESTE_APRES="1200:menu:Insérer du silence entre les locateurs;1800:enregistrer:$brouillon/insere"
course supprimer VSM_PROJET="$brouillon/suppression" VSM_DELAI=3000 \
    VSM_GESTE_APRES="1200:menu:Supprimer le temps entre les locateurs;1800:enregistrer:$brouillon/supprime"
course avant VSM_PROJET="$brouillon/insertion" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course insere VSM_PROJET="$brouillon/insere" VSM_DELAI=1500 VSM_EXPORT="$brouillon/insere.wav"
course supprime VSM_PROJET="$brouillon/supprime" VSM_DELAI=1500 VSM_EXPORT="$brouillon/supprime.wav"

c="$(mesurer "$brouillon/copie.wav")"; ct="$(mesurer "$brouillon/copie-temoin.wav")"
av="$(mesurer "$brouillon/avant.wav")"; in="$(mesurer "$brouillon/insere.wav")"; su="$(mesurer "$brouillon/supprime.wav")"
echo "       copie          : $c"
echo "       copie (témoin) : $ct"
echo "       avant          : $av"
echo "       inséré         : $in"
echo "       supprimé       : $su"
juge() { [ "$1" != "ABSENT" ] && [ -n "$(champ "$1" "$2")" ]; }
verdict "(4) le témoin : la copie sonne quand le matériau va au-delà" \
    "$(juge "$ct" 3 && proche "$(champ "$ct" 3)" "$(champ "$ct" 2)" 6 && echo 1 || echo 0)"
verdict "(1) la copie liée au-delà du matériau sonne" \
    "$(juge "$c" 3 && proche "$(champ "$c" 3)" "$(champ "$c" 2)" 6 && echo 1 || echo 0)"
verdict "(2) après « Insérer du silence », la dernière mesure sonne encore" \
    "$(juge "$in" 4 && juge "$av" 3 && proche "$(champ "$in" 4)" "$(champ "$av" 3)" 6 && echo 1 || echo 0)"
verdict "(3) après « Supprimer le temps » de la dernière mesure, l'export raccourcit de 2 s" \
    "$(juge "$su" 1 && juge "$av" 1 && proche "$(champ "$av" 1)" "$("$PY" -c "print($(champ "$su" 1) + 2)")" 0.1 && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FIN DU MORCEAU : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FIN DU MORCEAU : le moteur, le transport et l'export finissent où finit ce qu'on entend"
