#!/usr/bin/env bash
# D588.1 — LA PISTE NEUVE SOUS LA PISTE CHOISIE (l'*Add Track* de Cubase, de Live, de Reaper), MESURÉE PAR L'ORDRE DES
# PISTES DU PROJET ENREGISTRÉ ET PAR UN EXPORT IDENTIQUE AU BIT PRÈS AVANT ET APRÈS L'AJOUT, LE TÉMOIN À CÔTÉ.
#
# LES RÈGLES GARDÉES. Un projet de trois pistes : 0 « Basse » (MIDI, une machine, des notes, sa sortie vers le groupe 2),
# 1 « Lead » (MIDI, une machine, des notes), 2 « Groupe ». Un second : 0 « Basse », 1 « Dossier », 2 « Lead » dans le
# dossier, 3 « Fin » hors du dossier — sans elle, le contenu du dossier finit la liste, « après le dossier » et « au
# bout » tombent au même index, et le témoin passait le contrôle (4) (vu le 24/10 à 22 h 37). La piste se choisit par `choisir:` (D38), s'ajoute par le menu, et le projet s'enregistre (`enregistrer:`) :
# l'ordre, les sorties, les profondeurs et la piste choisie (`view.selectedTrack`, D369) se relisent dans
# `project.json`. L'export se fait en rouvrant le projet enregistré (`VSM_EXPORT`), comme transposition-globale.sh.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D588) :
#   (1) la piste 0 choisie, *Ajouter une piste MIDI* : l'ordre Basse, Piste 4, Lead, Groupe ; la sortie de « Basse »
#       vers l'index 3 ; la piste neuve choisie (index 1) ; l'export identique au bit près à celui d'avant ;
#   (2) Ctrl+Z : 3 pistes dans l'ordre d'origine, l'export identique ;
#   (3) la dernière piste choisie : la piste neuve à l'index 3 ;
#   (4) le dossier choisi, qui contient « Lead » : la piste neuve après « Lead », à la profondeur du dossier, AVANT
#       « Fin » ;
#   (5) le TÉMOIN (le binaire de D587, passé en argument) sur (1) : la piste neuve à l'index 3.
#
#   tools/piste-sous-la-choisie.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-piste-sous.XXXXXX")"
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
projet("trois", [basse, lead, ("Groupe", [])],
       [entree("Basse", 0, output=2), entree("Lead", 1), special("Groupe", 2, "group")])
projet("dossier", [basse, ("Dossier", []), lead, ("Fin", [])],
       [entree("Basse", 0), special("Dossier", 1, "folder"), entree("Lead", 2, folderDepth=1), entree("Fin", 3)])
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|ENREGISTRER|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
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
pareils() {   # deux exports présents, non vides, identiques au bit près
    [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2" && echo 1 || echo 0
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
AJOUT="menu:Piste > Ajouter une piste MIDI"

echo "=== D588.1 : la piste neuve sous la piste choisie ==="
course ajoute "$brouillon/trois" VSM_DELAI=1800 VSM_GESTE_APRES="400:choisir:0;700:$AJOUT;1100:enregistrer:$brouillon/ajoute-ecrit"
course annule "$brouillon/trois" VSM_DELAI=2100 \
    VSM_GESTE_APRES="400:choisir:0;700:$AJOUT;1000:touche:ctrl + Z;1400:enregistrer:$brouillon/annule-ecrit"
course derniere "$brouillon/trois" VSM_DELAI=1800 VSM_GESTE_APRES="400:choisir:2;700:$AJOUT;1100:enregistrer:$brouillon/derniere-ecrit"
course dossier "$brouillon/dossier" VSM_DELAI=1800 VSM_GESTE_APRES="400:choisir:1;700:$AJOUT;1100:enregistrer:$brouillon/dossier-ecrit"
for x in trois ajoute-ecrit annule-ecrit; do
    course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"
done

for n in ajoute annule derniere dossier; do echo "       $n : $(pistes "$brouillon/$n-ecrit")"; done
grep -h "^VSM_PISTE_AJOUTEE\|^VSM_MACHINES" "$brouillon/ajoute.txt" | tail -2 | sed 's/^/       /'
echo "       exports (octets) : avant $(stat -c %s "$brouillon/trois.wav" 2>/dev/null || echo ABSENT), après l'ajout $(stat -c %s "$brouillon/ajoute-ecrit.wav" 2>/dev/null || echo ABSENT), après Ctrl+Z $(stat -c %s "$brouillon/annule-ecrit.wav" 2>/dev/null || echo ABSENT)"

verdict "(1) sous la piste 0 : Basse, Piste 4, Lead, Groupe ; la sortie suit (3) ; la piste neuve choisie ; l'export identique" \
    "$([ "$(pistes "$brouillon/ajoute-ecrit")" = "Basse>3,Piste 4,Lead,Groupe | choisie=1" ] \
        && [ "$(pareils "$brouillon/trois.wav" "$brouillon/ajoute-ecrit.wav")" = 1 ] && echo 1 || echo 0)"
verdict "(2) Ctrl+Z : l'ordre d'origine, en un pas, l'export identique" \
    "$(pistes "$brouillon/annule-ecrit" | grep -q "^Basse>2,Lead,Groupe |" \
        && [ "$(pareils "$brouillon/trois.wav" "$brouillon/annule-ecrit.wav")" = 1 ] && echo 1 || echo 0)"
verdict "(3) la dernière piste choisie : la piste neuve au bout (index 3)" \
    "$([ "$(pistes "$brouillon/derniere-ecrit")" = "Basse>2,Lead,Groupe,Piste 4 | choisie=3" ] && echo 1 || echo 0)"
verdict "(4) le dossier choisi : la piste neuve après son contenu, à sa profondeur, avant « Fin »" \
    "$([ "$(pistes "$brouillon/dossier-ecrit")" = "Basse,Dossier,Lead@1,Piste 5,Fin | choisie=3" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PISTE-SOUS-LA-CHOISIE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PISTE-SOUS-LA-CHOISIE : la piste neuve naît sous la piste choisie, et les routages la suivent"
