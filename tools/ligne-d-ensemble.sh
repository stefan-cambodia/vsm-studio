#!/usr/bin/env bash
# D591.1 — LA LIGNE D'ENSEMBLE DU MORCEAU (Affichage ▸ Ligne d'ensemble du morceau ; l'*Overview Line* de Cubase),
# MESURÉE PAR SON RELEVÉ, PAR LE DÉFILEMENT RELU APRÈS LE GESTE ET PAR LA PHOTO, LE TÉMOIN À CÔTÉ.
#
# LES RÈGLES GARDÉES. Un projet de 64 mesures en 4/4 (480 ticks à la noire) : « Longue », une note toutes les quatre
# mesures de la 1 à la fin de la 64 (un clip de 0 à 122 880) ; « Milieu », de la 33 à la fin de la 48 (61 440 à 92 160).
# Une première course relève la largeur L de la bande ; le projet « huit » porte alors une vue (`view.pixelsPerTick`,
# D363) de L / 15 360 pixels par tick, soit HUIT mesures, défilement 0. `relever-ensemble` dit ce que la peinture lit
# (`VSM_ENSEMBLE`) ; la bande se vise par son nom (`clic:` et `glisser:` en fractions de SA largeur, qui est celle de
# l'arrangement : 150 px d'en-têtes, puis L).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D591) :
#   (1) éteinte d'origine : « montrée non » ; (1b) avec un témoin passé en second argument, la zone de l'arrangement
#       photographiée identique au pixel près à celle du témoin ;
#   (2) allumée : « morceau 0..122880 ticks ; vue 0..15360 ticks », le rectangle de 0 à L/8 (à 1 px près), 2 traits ;
#   (3) un clic aux trois quarts de la bande : le défilement à 84 480 (à 122 880 / L ticks près), la vue toujours de
#       15 360 ticks ;
#   (4) le rectangle tiré de L/8 vers la droite : le défilement à 15 360 (à 122 880 / L près), la vue de 15 360 ;
#   (5) relancé dans le même HOME : montrée, l'entrée cochée ; en anglais, « View > Song overview line » ;
#   (6) le TÉMOIN (le binaire de D590, passé en PREMIER argument) : l'entrée et le verbe absents.
#
#   tools/ligne-d-ensemble.sh [binaire] [témoin pour (1b)]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (PIL) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ensemble.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

# Le projet ; un second argument pose la vue (pixels par tick, défilement 0).
projet() {
    python3 - "$@" <<'PY'
import json, os, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, notes, tempo=False):
    evs = ([(0, b"\xff\x51\x03\x07\xa1\x20")] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for debut, hauteur, duree in notes:
        evs += [(debut - t, bytes([0x90, hauteur, 100])), (duree, bytes([0x80, hauteur, 0]))]
        t = debut + duree
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
M = 1920
longue = [(k * 4 * M, 48, 4 * M) for k in range(16)]
milieu = [(32 * M + k * 4 * M, 60, 4 * M) for k in range(4)]
os.makedirs(f"{d}/midi", exist_ok=True)
open(f"{d}/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Longue", longue, tempo=True) + piste("Milieu", milieu))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
doc = {"format": "vsm-project", "version": 1, "title": "ensemble", "midi": {"file": "midi/arrangement.mid"},
       "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                     "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
       "tracks": [{"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                   "instrument": {"preferredPlugin": "vsm.minimoog"}} for c, n in ((0, "Longue"), (1, "Milieu"))]}
if len(sys.argv) > 2:
    doc["view"] = {"pixelsPerTick": float(sys.argv[2]), "scrollTick": 0}
json.dump(doc, open(f"{d}/project.json", "w"), indent=1)
PY
}

course() {   # $1 = nom ; $2 = projet ; $3 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" dossier="$2" maison="$3"
    shift 3
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$dossier" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "${BINAIRE:-$BIN}" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|CLIC|GLISSER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releve() { grep -h "^VSM_ENSEMBLE : montrée" "$brouillon/$1.txt" | tail -1; }
# Le défilement et la longueur de la vue, lus dans le relevé : « A L » (ou « - - »).
vue() { releve "$1" | sed -n 's/.* ; vue \([0-9]*\)\.\.\([0-9]*\) ticks.*/\1 \2/p' | awk '{print $1, $2 - $1}'; }
pres() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MONTRER="menu:Affichage > Ligne d'ensemble du morceau"

echo "=== D591.1 : la ligne d'ensemble du morceau ==="
projet "$brouillon/base"
course largeur "$brouillon/base" "" VSM_DELAI=1500 VSM_GESTE_APRES="300:$MONTRER;700:relever-ensemble"
L="$(releve largeur | sed -n 's/.* sur \([0-9]*\) px.*/\1/p')"
[ -n "$L" ] && [ "$L" -gt 0 ] 2>/dev/null || L=0
echo "       largeur de la bande : ${L} px ($(releve largeur | cut -c1-140))"
if [ "$L" -gt 0 ]; then
    projet "$brouillon/huit" "$(python3 -c "print($L / 15360)")"
else
    projet "$brouillon/huit"
fi
# Les positions, en fractions de la largeur de la bande (150 px d'en-têtes, puis L).
fr() { python3 -c "import sys; L = float(sys.argv[1]); print(f'{(150 + float(sys.argv[2]) * L) / (150 + L):.6f}' if L > 0 else '0.5')" "$L" "$1"; }
maison="$(mktemp -d "$brouillon/home.XXXX")"
course eteinte "$brouillon/huit" "" VSM_DELAI=1200 VSM_GESTE_APRES="500:relever-ensemble"
course allumee "$brouillon/huit" "$maison" VSM_DELAI=1500 VSM_GESTE_APRES="300:$MONTRER;800:relever-ensemble"
course clic "$brouillon/huit" "" VSM_DELAI=1800 \
    VSM_GESTE_APRES="300:$MONTRER;700:clic:Ligne d'ensemble:$(fr 0.75),0.5;1100:relever-ensemble"
course tirer "$brouillon/huit" "" VSM_DELAI=1800 \
    VSM_GESTE_APRES="300:$MONTRER;700:glisser:Ligne d'ensemble:$(fr 0.0625),0.5:$(fr 0.1875),0.5;1100:relever-ensemble"
course relance "$brouillon/huit" "$maison" VSM_DELAI=1500 VSM_GESTE_APRES="500:relever-ensemble;800:lister-menus"
course anglais "$brouillon/huit" "" VSM_LANGUE=en VSM_DELAI=1200 VSM_GESTE_APRES="500:lister-menus"

for n in eteinte allumee clic tirer relance; do echo "       $n : $(releve "$n" | cut -c16-200)"; done
coche="$(grep -h "^VSM_MENU_LISTE : Affichage > Ligne d'ensemble du morceau" "$brouillon/relance.txt")"
en="$(grep -c "^VSM_MENU_LISTE : View > Song overview line" "$brouillon/anglais.txt")"
echo "       relancé : « ${coche:-ABSENTE} » ; en anglais : $en entrée"
tpp="$(python3 -c "import sys; L = float(sys.argv[1]); print(122880 / L if L > 0 else 0)" "$L")"
huitieme="$(python3 -c "import sys; print(round(float(sys.argv[1]) / 8))" "$L")"

verdict "(1) éteinte d'origine : « montrée non »" \
    "$([ "$(releve eteinte)" = "VSM_ENSEMBLE : montrée non" ] && echo 1 || echo 0)"
verdict "(2) allumée : morceau 0..122880, vue 0..15360, le rectangle de 0 à L/8 ($huitieme px) à 1 px près, 2 traits" \
    "$(releve allumee | grep -q "^VSM_ENSEMBLE : montrée oui ; morceau 0\.\.122880 ticks ; vue 0\.\.15360 ticks ; rectangle 0\.\." \
        && pres "$(releve allumee | sed -n 's/.*rectangle 0\.\.\([0-9]*\) sur.*/\1/p')" "$huitieme" 1 \
        && releve allumee | grep -q " ; 2 trait(s)$" && echo 1 || echo 0)"
verdict "(3) un clic aux trois quarts : le défilement à 84 480 (à $tpp ticks près), la vue de 15 360" \
    "$(set -- $(vue clic); [ $# = 2 ] && pres "$1" 84480 "$tpp" && [ "$2" = 15360 ] && echo 1 || echo 0)"
verdict "(4) le rectangle tiré de L/8 : le défilement à 15 360 (à $tpp ticks près), la vue de 15 360" \
    "$(set -- $(vue tirer); [ $# = 2 ] && pres "$1" 15360 "$tpp" && [ "$2" = 15360 ] && echo 1 || echo 0)"
verdict "(5) relancé : montrée, l'entrée cochée ; en anglais" \
    "$(releve relance | grep -q "^VSM_ENSEMBLE : montrée oui" && echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

# (1b) LA ZONE DE L'ARRANGEMENT, ÉTEINTE, CONTRE LE TÉMOIN — au pixel près ; jouée seulement avec un témoin passé, et
# DITE quand elle ne l'est pas (jamais comptée verte sans avoir été jouée). Quarante-cinq secondes avant : sous une
# session verrouillée, seul le premier lancement d'une rafale dessine sa fenêtre (D520).
if [ -n "$TEMOIN" ] && [ -x "$TEMOIN" ]; then
    sleep 45
    BINAIRE="$TEMOIN" course photo-temoin "$brouillon/huit" "" VSM_DELAI=1200
    sleep 45
    course photo-neuf "$brouillon/huit" "" VSM_DELAI=1200
    egal="$("$PY" -I - "$brouillon/photo-neuf.png" "$brouillon/photo-temoin.png" <<'PY3'
import os, sys
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
if not all(os.path.exists(f) and os.path.getsize(f) for f in (a, b)):
    print("ABSENT"); sys.exit(0)
zone = (305, 125, 1195, 700)   # l'arrangement à 1600 × 1000, sous la barre de transport (le CPU y change)
x, y = Image.open(a).convert("RGB").crop(zone), Image.open(b).convert("RGB").crop(zone)
# Deux photos VIDES sont identiques aussi (D77) : chacune doit montrer quelque chose.
for nom, im in (("neuf", x), ("témoin", y)):
    couleurs = im.getcolors(maxcolors=1 << 20)
    if couleurs is None or len(couleurs) < 50:
        print(f"VIDE ({nom} : {0 if couleurs is None else len(couleurs)} couleurs)"); sys.exit(0)
boite = ImageChops.difference(x, y).getbbox()
print("1" if boite is None else f"0 {boite}")
PY3
)"
    echo "       (1b) zone de l'arrangement, neuf contre témoin : $egal"
    verdict "(1b) éteinte : l'arrangement identique au témoin, au pixel près" "$([ "$egal" = 1 ] && echo 1 || echo 0)"
else
    echo "  ---- (1b) non jouée : aucun témoin passé en second argument"
fi

echo
if [ "$rates" -gt 0 ]; then echo "LIGNE-D-ENSEMBLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LIGNE-D-ENSEMBLE : la bande montre le morceau entier et la vue, un clic l'y centre, le rectangle la fait défiler"
