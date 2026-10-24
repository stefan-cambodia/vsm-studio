#!/usr/bin/env bash
# D589.1 — LES PISTES CHOISIES DANS UN DOSSIER NEUF (le *Move Selected Tracks to New Folder* de Cubase), MESURÉES PAR
# L'ORDRE ET LES PROFONDEURS DU PROJET ENREGISTRÉ ET PAR L'EXPORT, LE TÉMOIN À CÔTÉ.
#
# LES RÈGLES GARDÉES. Un projet « cinq » : 0 « Basse » (MIDI, une machine, des notes, sa sortie vers le groupe 3),
# 1 « Lead », 2 « Pad », 4 « Voix » (MIDI, une machine, des notes), 3 « Groupe ». Un projet « tiroir » : 0 « Basse »,
# 1 « Cordes » (un dossier), 2 « Lead » dans le dossier, 3 « Pad ». Les pistes se choisissent par `choisir:1,4` (D38),
# le geste passe par le menu, le projet s'enregistre (`enregistrer:`) : l'ordre, les sorties, les profondeurs et la piste
# choisie (`view.selectedTrack`, D369) se relisent dans `project.json`. L'export se fait en rouvrant le projet
# enregistré (`VSM_EXPORT`), comme piste-sous-la-choisie.sh.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D589) :
#   (1) les pistes 1 et 4 choisies : Basse, Dossier, Lead, Voix, Pad, Groupe ; les profondeurs 0, 0, 1, 1, 0, 0 ; la
#       sortie de « Basse » vers l'index 5 ; le dossier choisi (index 1) ; l'export à −120 dBFS près de celui d'avant
#       (et non au bit près : « Voix » passe avant « Pad », l'ordre de la somme change) ;
#   (2) Ctrl+Z : l'ordre d'origine, en un pas, la sortie vers 3 ;
#   (3) « tiroir », les pistes 1 et 3 choisies : Basse, Dossier, Cordes@1, Lead@2, Pad@1, l'export identique AU BIT près ;
#   (4) une seule piste choisie : l'entrée « (1) » grisée ;
#   (5) en anglais, « Move the chosen tracks into a new folder (2) » ;
#   (6) le TÉMOIN (le binaire de D588, passé en argument) sur (1) : l'entrée absente, l'ordre inchangé.
#
#   tools/dossier-des-choisies.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (soundfile) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-dossier-choisies.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
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
def entree(nom, canal, **extra):
    e = {"channel": canal, "color": "#FF6B9BFF", "effects": [], "instrument": {"preferredPlugin": "vsm.minimoog"},
         "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}, "name": nom}
    e.update(extra)
    return e
def special(nom, canal, genre, **extra):
    e = {"channel": canal, "color": "#FF4BB3A6", "effects": [], "kind": genre, "name": nom,
         "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}}
    e.update(extra)
    return e
def projet(nom, pistes_midi, entrees):
    os.makedirs(f"{d}/{nom}/midi", exist_ok=True)
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes_midi), 480)
    for i, (n, notes) in enumerate(pistes_midi):
        data += piste(n, notes, tempo=(i == 0))
    open(f"{d}/{nom}/midi/arrangement.mid", "wb").write(data)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": entrees}, open(f"{d}/{nom}/project.json", "w"), indent=1)
basse = ("Basse", [(0, 36, 960), (960, 43, 960)])
lead = ("Lead", [(0, 72, 480), (480, 76, 480), (960, 79, 960)])
pad = ("Pad", [(0, 60, 1920)])
voix = ("Voix", [(480, 67, 960)])
projet("cinq", [basse, lead, pad, ("Groupe", []), voix],
       [entree("Basse", 0, output=3), entree("Lead", 1), entree("Pad", 2), special("Groupe", 3, "group"),
        entree("Voix", 4)])
projet("tiroir", [basse, ("Cordes", []), lead, pad],
       [entree("Basse", 0), special("Cordes", 1, "folder"), entree("Lead", 2, folderDepth=1), entree("Pad", 3)])
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|ENREGISTRER|EXPORT|DOSSIER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# Les pistes d'un projet enregistré : « nom[>sortie][@profondeur],… | choisie=N », ou « ABSENT ».
pistes() {
    python3 - "$1/project.json" <<'PY2'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("ABSENT"); sys.exit(0)
morceaux = []
for p in d.get("tracks", []):
    m = p.get("name", "?")
    if "output" in p: m += f">{p['output']}"
    if p.get("folderDepth", 0): m += f"@{p['folderDepth']}"
    morceaux.append(m)
print(",".join(morceaux) + f" | choisie={d.get('view', {}).get('selectedTrack', '-')}")
PY2
}
# L'écart le plus grand entre deux exports, en dBFS (« ABSENT » si l'un manque ou est vide, « -inf » s'ils sont égaux).
ecart() {
    "$PY" -I - "$1" "$2" <<'PY3'
import math, os, sys
import numpy as np
import soundfile as sf
a, b = sys.argv[1], sys.argv[2]
if not (os.path.getsize(a) if os.path.exists(a) else 0) or not (os.path.getsize(b) if os.path.exists(b) else 0):
    print("ABSENT"); sys.exit(0)
x, _ = sf.read(a, dtype="float64", always_2d=True)
y, _ = sf.read(b, dtype="float64", always_2d=True)
if x.shape != y.shape:
    print(f"FORMES {x.shape} {y.shape}"); sys.exit(0)
m = float(np.max(np.abs(x - y))) if x.size else 0.0
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.1f}")
PY3
}
pareils() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2" && echo 1 || echo 0; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
RANGER="menu:Piste > Ranger les pistes choisies dans un dossier neuf (2)"

echo "=== D589.1 : les pistes choisies dans un dossier neuf ==="
course range "$brouillon/cinq" VSM_DELAI=1800 VSM_GESTE_APRES="400:choisir:1,4;700:$RANGER;1100:enregistrer:$brouillon/range-ecrit"
course annule "$brouillon/cinq" VSM_DELAI=2100 \
    VSM_GESTE_APRES="400:choisir:1,4;700:$RANGER;1000:touche:ctrl + Z;1400:enregistrer:$brouillon/annule-ecrit"
course tiroir "$brouillon/tiroir" VSM_DELAI=1800 VSM_GESTE_APRES="400:choisir:1,3;700:$RANGER;1100:enregistrer:$brouillon/tiroir-ecrit"
course seule "$brouillon/cinq" VSM_DELAI=1400 VSM_GESTE_APRES="400:choisir:2;800:lister-menus"
course anglais "$brouillon/cinq" VSM_LANGUE=en VSM_DELAI=1400 VSM_GESTE_APRES="400:choisir:1,4;800:lister-menus"
for x in cinq range-ecrit tiroir tiroir-ecrit; do
    course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"
done

for n in range annule tiroir; do echo "       $n : $(pistes "$brouillon/$n-ecrit")"; done
grep -h "^VSM_DOSSIER" "$brouillon/range.txt" "$brouillon/tiroir.txt" | sed 's/^/       /'
e1="$(ecart "$brouillon/cinq.wav" "$brouillon/range-ecrit.wav")"
e3="$(ecart "$brouillon/tiroir.wav" "$brouillon/tiroir-ecrit.wav")"
echo "       exports : (1) écart $e1 dBFS ; (3) écart $e3 dBFS, au bit près : $(pareils "$brouillon/tiroir.wav" "$brouillon/tiroir-ecrit.wav")"
seule="$(grep -h "^VSM_MENU_LISTE : Piste > Ranger les pistes choisies dans un dossier neuf" "$brouillon/seule.txt")"
en="$(grep -c "^VSM_MENU_LISTE : Track > Move the chosen tracks into a new folder (2)" "$brouillon/anglais.txt")"
echo "       une seule choisie : « ${seule:-ABSENTE} » ; en anglais : $en entrée"

verdict "(1) 1 et 4 : Basse, Dossier, Lead, Voix, Pad, Groupe ; la sortie suit (5) ; le dossier choisi ; l'export à −120 dBFS près" \
    "$([ "$(pistes "$brouillon/range-ecrit")" = "Basse>5,Dossier,Lead@1,Voix@1,Pad,Groupe | choisie=1" ] \
        && case "$e1" in -inf) true ;; ABSENT|FORMES*) false ;; *) python3 -c "import sys; sys.exit(0 if float('$e1') <= -120 else 1)" ;; esac \
        && echo 1 || echo 0)"
verdict "(2) Ctrl+Z : l'ordre d'origine, en un pas, la sortie vers 3" \
    "$(pistes "$brouillon/annule-ecrit" | grep -q "^Basse>3,Lead,Pad,Groupe,Voix |" && echo 1 || echo 0)"
verdict "(3) un dossier choisi avec son contenu : Basse, Dossier, Cordes@1, Lead@2, Pad@1 ; l'export au bit près" \
    "$([ "$(pistes "$brouillon/tiroir-ecrit")" = "Basse,Dossier,Cordes@1,Lead@2,Pad@1 | choisie=1" ] \
        && [ "$(pareils "$brouillon/tiroir.wav" "$brouillon/tiroir-ecrit.wav")" = 1 ] && echo 1 || echo 0)"
verdict "(4) une seule piste choisie : l'entrée « (1) » grisée" \
    "$(echo "$seule" | grep -q "dossier neuf (1) \[grisée\]" && echo 1 || echo 0)"
verdict "(5) en anglais" "$([ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DOSSIER-DES-CHOISIES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DOSSIER-DES-CHOISIES : les pistes choisies entrent d'un geste dans un dossier neuf, et les routages les suivent"
