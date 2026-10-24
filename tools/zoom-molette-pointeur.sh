#!/usr/bin/env bash
# D587.2 — LE ZOOM À LA MOLETTE AUTOUR DU POINTEUR (Ctrl+molette, dans l'arrangement et dans le piano roll), MESURÉ PAR
# L'ABSCISSE DU TICK VISÉ AVANT ET APRÈS UN CRAN, LE TÉMOIN (le binaire de D586, passé en argument) À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4). Une piste, un clip de 32 mesures [0, 61 440) ; la vue enregistrée au
# projet (D363, D369) : 0,1 px/tick, défilée à la mesure 5 (tick 7 680), dans les deux vues. Le cran passe par
# `molette:<vue>:0.3,0.5:±1:ctrl` — le pointeur à 0,3 de la largeur, loin du bord gauche et du centre, pour que le
# témoin ne tombe pas juste par hasard. L'abscisse d'un tick se lit sur des relevés qui existaient avant D587 :
# l'arrangement par le clip à l'écran (`relever-clips`, x du début et de la fin), le piano roll par son zoom, son
# défilement et son clavier (`relever-pianoroll` : `zoom=`, `defilement=`, `clavier=`, `taille=`).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D587) :
#   (1) l'arrangement, un cran vers le haut : le zoom ×1,15 (à 1 % près), le tick visé à la même abscisse (à 1 px près) ;
#   (2) un cran vers le bas : ÷1,15, de même ;
#   (3) *Annuler le zoom* : la vue d'avant, en un pas ;
#   (4) le piano roll, un cran vers le haut : ×1,15, le tick visé à la même abscisse ;
#   (5) le piano roll, Ctrl+Maj+molette : le zoom horizontal inchangé ;
#   (6) le TÉMOIN : le tick visé DÉPLACÉ de plus de 10 px dans les deux vues (relancer avec le binaire de D586).
#
#   tools/zoom-molette-pointeur.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zoom-molette.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for i in range(32):   # une noire au début de chaque mesure
    corps += vlq(0 if i == 0 else 1440) + b"\x90\x3c\x5a" + vlq(480) + b"\x80\x3c\x00"
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 61440, "start": 0, "length": 61440, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "zoom-molette", "midi": {"file": "midi/arrangement.mid"},
           "view": {"pixelsPerTick": 0.1, "scrollTick": 7680, "pianoRollPixelsPerTick": 0.1, "pianoRollScrollTick": 7680,
                    # la vue du piano roll n'est reprise qu'ENTIÈRE (`reprendreLaVue`, D369) : sans note du haut ni
                    # rang, il retombait sur son cadrage (0,080 px/tick, défilé à 0) — vu au premier passage.
                    "pianoRollTopNote": 72, "pianoRollNoteHeight": 16},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "clips": [clip], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = la vue ; le reste : variables
    local nom="$1" vue="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport,$vue" "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|MOLETTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
ARR="molette:arrangement:0.3,0.5"
PR="molette:pianoroll:0.3,0.5"

echo "=== D587.2 : le zoom à la molette autour du pointeur ==="
course haut arrangement VSM_DELAI=1600 VSM_GESTE_APRES="900:relever-clips;1000:$ARR:1:ctrl;1200:relever-clips"
course bas arrangement VSM_DELAI=1600 VSM_GESTE_APRES="900:relever-clips;1000:$ARR:-1:ctrl;1200:relever-clips"
course annule arrangement VSM_DELAI=1800 \
    VSM_GESTE_APRES="900:relever-clips;1000:$ARR:1:ctrl;1200:menu:Affichage > Annuler le zoom;1400:relever-clips"
course pr-haut pianoroll VSM_DELAI=1600 VSM_GESTE_APRES="900:relever-pianoroll;1000:$PR:1:ctrl;1200:relever-pianoroll"
course pr-vertical pianoroll VSM_DELAI=1600 VSM_GESTE_APRES="900:relever-pianoroll;1000:$PR:1:ctrl+maj;1200:relever-pianoroll"

# Une ligne par course : le facteur, et l'écart (px) entre l'abscisse du tick visé après le cran et le pointeur.
eval "$(python3 - "$brouillon" <<'PY'
import re, sys
b = sys.argv[1]
def lignes(nom, motif):
    return [l for l in open(f"{b}/{nom}.txt", encoding="utf-8", errors="replace") if l.startswith(motif)]
def arrangement(nom):
    ls = [l for l in lignes(nom, "VSM_CLIP_ECRAN : piste 0 ")]
    if len(ls) < 2: return None
    vues = []
    for l in (ls[0], ls[-1]):
        m = re.search(r" x ([0-9.-]+)\.\.([0-9.-]+) y .* sur ([0-9]+)x", l)
        if not m: return None
        x1, x2, w = float(m.group(1)), float(m.group(2)), float(m.group(3))
        vues.append(((x2 - x1) / 61440.0, x1, w))
    (p0, o0, w), (p1, o1, _) = vues
    xp = 0.3 * w
    vise = (xp - o0) / p0
    return p1 / p0, (o1 + vise * p1) - xp
def pianoroll(nom):
    ls = lignes(nom, "VSM_PIANOROLL_RANG : ")
    if len(ls) < 2: return None
    vues = []
    for l in (ls[0], ls[-1]):
        z = re.search(r" zoom=([0-9.]+)", l); d = re.search(r" defilement=([0-9-]+)", l)
        c = re.findall(r" clavier=([0-9]+)", l); t = re.search(r" taille=([0-9]+)x", l)
        if not (z and d and c and t): return None
        vues.append((float(z.group(1)), float(d.group(1)), float(c[-1]), float(t.group(1))))
    (z0, d0, c0, w), (z1, d1, c1, _) = vues
    xp = 0.3 * w
    vise = d0 + (xp - c0) / z0
    return z1 / z0, (c1 + (vise - d1) * z1) - xp
res = {"HAUT": arrangement("haut"), "BAS": arrangement("bas"), "ANNULE": arrangement("annule"),
       "PRHAUT": pianoroll("pr-haut"), "PRVERT": pianoroll("pr-vertical")}
for k, v in res.items():
    print(f"F_{k}={v[0]:.4f} E_{k}={v[1]:.1f}" if v else f"F_{k}= E_{k}=")
# (5) : le zoom horizontal inchangé ne prouve rien si la molette n'a rien fait — le RANG doit avoir changé.
rangs = [re.search(r" rang=([0-9]+)", l) for l in lignes("pr-vertical", "VSM_PIANOROLL_RANG : ")]
rangs = [int(m.group(1)) for m in rangs if m]
print(f"RANGS_PRVERT='{rangs[0]}->{rangs[-1]}'" if len(rangs) >= 2 else "RANGS_PRVERT=")
PY
)"
for k in HAUT BAS ANNULE PRHAUT PRVERT; do f="F_$k"; e="E_$k"; echo "       $k : facteur ${!f:-?}, le tick visé à ${!e:-?} px du pointeur"; done
proche() { python3 -c "import sys; a, b, t = sys.argv[1:4]; sys.exit(0 if a and abs(float(a) - float(b)) <= float(t) else 1)" "$1" "$2" "$3"; }

verdict "(1) l'arrangement, un cran vers le haut : ×1,15, le tick visé sous le pointeur" \
    "$(proche "$F_HAUT" 1.15 0.0115 && proche "$E_HAUT" 0 1 && echo 1 || echo 0)"
verdict "(2) un cran vers le bas : ÷1,15, le tick visé sous le pointeur" \
    "$(proche "$F_BAS" 0.8696 0.0087 && proche "$E_BAS" 0 1 && echo 1 || echo 0)"
verdict "(3) Annuler le zoom : la vue d'avant, en un pas" \
    "$(proche "$F_ANNULE" 1 0.0001 && proche "$E_ANNULE" 0 0.5 && echo 1 || echo 0)"
verdict "(4) le piano roll, un cran vers le haut : ×1,15, le tick visé sous le pointeur" \
    "$(proche "$F_PRHAUT" 1.15 0.0115 && proche "$E_PRHAUT" 0 1 && echo 1 || echo 0)"
echo "       PRVERT : le rang ${RANGS_PRVERT:-?} px"
verdict "(5) le piano roll, Ctrl+Maj+molette : le zoom horizontal inchangé, le rang changé" \
    "$(proche "$F_PRVERT" 1 0.0001 && [ -n "$RANGS_PRVERT" ] && [ "${RANGS_PRVERT%->*}" != "${RANGS_PRVERT#*->}" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZOOM-MOLETTE-POINTEUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZOOM-MOLETTE-POINTEUR : Ctrl+molette zoome autour du point visé, dans l'arrangement et le piano roll"
