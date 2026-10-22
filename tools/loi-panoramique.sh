#!/usr/bin/env bash
# D571.2 — LA LOI DE PANORAMIQUE DU PROJET (Mixage ▸ Loi de panoramique ; le *Stereo Pan Law* de Cubase), MESURÉE PAR
# L'EXPORT (D332 : une règle du son se pose dans `interchange`, et c'est l'export qui le prouve) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. « Voix » joue un do de deux secondes au CENTRE, fader à 0,5.
#   (1) le défaut : le projet réenregistré sans y toucher n'écrit pas `panLaw` ;
#   (2) chaque entrée du menu écrit sa loi au projet (`0dB`, `-4.5dB`, `-6dB`) ; −3 dB n'en écrit aucune ;
#   (3) L'ATTENDU ÉCRIT AVANT LA MESURE (D571) : au centre, la crête de l'export tombe de 3,01, 4,52 et 6,02 dB sous
#       celle de 0 dB, à 0,05 dB près, les deux canaux à égalité ;
#   (4) le témoin : tournée à fond à gauche, l'export sous 0 dB et sous −6 dB est identique à l'échantillon près ;
#   (5) un pas : Ctrl+Z rend le projet sans loi écrite, après un geste qui a eu lieu.
#
#   tools/loi-panoramique.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-loi-pan.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x04Voix"
corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x8f, 0x00]) + bytes([0x80, 60, 0])   # do4 de 0 à 1 920 ticks (2 s)
corps += b"\x00\xff\x2f\x00"
for nom, pan in (("base", 0.0), ("gauche", -1.0)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voix",
                           "mix": {"muted": False, "pan": pan, "solo": False, "volume": 0.5},
                           "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
loi() {   # la loi écrite au projet, « (aucune) », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(d.get('panLaw', '(aucune)'))" "$1"
}
cretes() {   # « crête gauche, crête droite » de l'export en dBFS, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if x.size == 0 or x.shape[1] < 2 or np.max(np.abs(x)) <= 1e-9:
    print("ABSENT"); sys.exit(0)
print(" ".join(f"{20 * np.log10(np.max(np.abs(x[:, k]))):.4f}" for k in (0, 1)))
PY2
}
memes() {   # 1 si les deux exports sont identiques à l'échantillon près, 0 sinon (ou si l'un manque)
    "$PY" - "$1" "$2" <<'PY3'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float32", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float32", always_2d=True)
except Exception:
    print(0); sys.exit(0)
print(1 if a.size > 0 and a.shape == b.shape and np.array_equal(a, b) and np.max(np.abs(a)) > 1e-6 else 0)
PY3
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="menu:Mixage > Loi de panoramique"

echo "=== D571.2 : la loi de panoramique du projet ==="
declare -A LIBELLE=([z]="0 dB au centre (balance)" [m3]="−3 dB au centre (puissance constante)"
                    [m45]="−4,5 dB au centre" [m6]="−6 dB au centre (linéaire)")
for k in z m3 m45 m6 annule defaut; do cp -r "$brouillon/base" "$brouillon/$k"; done
cp -r "$brouillon/gauche" "$brouillon/gauche-z"; cp -r "$brouillon/gauche" "$brouillon/gauche-m6"
for k in z m3 m45 m6; do
    course "menu-$k" "$brouillon/$k" VSM_DELAI=2500 VSM_VUE="sans-rapport" \
        VSM_GESTE_APRES="1200:$MENU > ${LIBELLE[$k]};1700:enregistrer:$brouillon/$k"
done
course menu-gauche-z "$brouillon/gauche-z" VSM_DELAI=2500 VSM_VUE="sans-rapport" \
    VSM_GESTE_APRES="1200:$MENU > ${LIBELLE[z]};1700:enregistrer:$brouillon/gauche-z"
course menu-gauche-m6 "$brouillon/gauche-m6" VSM_DELAI=2500 VSM_VUE="sans-rapport" \
    VSM_GESTE_APRES="1200:$MENU > ${LIBELLE[m6]};1700:enregistrer:$brouillon/gauche-m6"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport" \
    VSM_GESTE_APRES="1200:$MENU > ${LIBELLE[m6]};1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"
course defaut "$brouillon/defaut" VSM_DELAI=2000 VSM_VUE="sans-rapport" VSM_GESTE_APRES="1200:enregistrer:$brouillon/defaut"
for k in z m3 m45 m6 gauche-z gauche-m6; do
    course "export-$k" "$brouillon/$k" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$k.wav"
done

lz="$(loi "$brouillon/z")"; l3="$(loi "$brouillon/m3")"; l45="$(loi "$brouillon/m45")"; l6="$(loi "$brouillon/m6")"
la="$(loi "$brouillon/annule")"; ld="$(loi "$brouillon/defaut")"
echo "       lois écrites : 0 dB [$lz] ; −3 dB [$l3] ; −4,5 dB [$l45] ; −6 dB [$l6] ; Ctrl+Z [$la] ; sans geste [$ld]"
grep -h "^VSM_CONSOLE : loi de panoramique" "$brouillon"/menu-*.txt "$brouillon/annuler.txt" | sort | uniq -c | sed 's/^/       /'
cz="$(cretes "$brouillon/z.wav")"; c3="$(cretes "$brouillon/m3.wav")"; c45="$(cretes "$brouillon/m45.wav")"; c6="$(cretes "$brouillon/m6.wav")"
echo "       crêtes (g d, dBFS) : 0 dB [$cz] ; −3 dB [$c3] ; −4,5 dB [$c45] ; −6 dB [$c6]"
ecarts="$(awk -v z="$cz" -v a="$c3" -v b="$c45" -v c="$c6" 'BEGIN {
    if (z == "ABSENT" || a == "ABSENT" || b == "ABSENT" || c == "ABSENT") { print "ABSENT"; exit }
    split(z, Z, " "); split(a, A, " "); split(b, B, " "); split(c, C, " ")
    printf "%.3f %.3f %.3f %.4f %.4f %.4f", A[1] - Z[1], B[1] - Z[1], C[1] - Z[1], A[1] - A[2], B[1] - B[2], C[1] - C[2] }')"
echo "       écarts à 0 dB (gauche) et gauche − droite : [$ecarts]"
gm="$(memes "$brouillon/gauche-z.wav" "$brouillon/gauche-m6.wav")"

verdict "(1) le défaut : réenregistré sans geste, le projet n'écrit pas de loi" "$([ "$ld" = "(aucune)" ] && echo 1 || echo 0)"
verdict "(2) chaque entrée écrit sa loi ; −3 dB n'en écrit aucune" \
    "$([ "$lz" = "0dB" ] && [ "$l45" = "-4.5dB" ] && [ "$l6" = "-6dB" ] && [ "$l3" = "(aucune)" ] && echo 1 || echo 0)"
verdict "(3) au centre : −3,01, −4,52 et −6,02 dB sous 0 dB, à 0,05 dB près, les canaux à égalité" \
    "$(awk -v e="$ecarts" 'BEGIN { if (e == "ABSENT") { print 0; exit } split(e, E, " ")
        ok = (E[1] + 3.0103 < 0.05 && E[1] + 3.0103 > -0.05) && (E[2] + 4.5154 < 0.05 && E[2] + 4.5154 > -0.05) \
          && (E[3] + 6.0206 < 0.05 && E[3] + 6.0206 > -0.05)
        for (k = 4; k <= 6; k++) ok = ok && E[k] < 0.001 && E[k] > -0.001
        print ok ? 1 : 0 }')"
verdict "(4) le témoin : tournée à fond, 0 dB et −6 dB rendent le même export, à l'échantillon près" "$gm"
verdict "(5) un pas : Ctrl+Z rend le projet sans loi, après un geste qui a eu lieu" \
    "$([ "$la" = "(aucune)" ] && grep -q "^VSM_CONSOLE : loi de panoramique -6 dB" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LOI-PANORAMIQUE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LOI-PANORAMIQUE : la loi choisie règle le centre, les extrémités ne bougent pas, et l'export le dit"
