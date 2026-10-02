#!/usr/bin/env bash
# D532.2 bis — LES VERSIONS DE PISTE, PAR LE MENU : DUPLIQUER, ÉCRIRE, CHOISIR, ANNULER,
# RENOMMER, SUPPRIMER — et ce qui reste au fichier après chaque geste.
#
# LA RÈGLE GARDÉE. Une piste de deux notes (40, 43) ; une seule course enchaîne :
#   (1) « Dupliquer la version », puis une note de plus (47) : le fichier porte deux
#       versions, la seconde active ; l'arrangement {40, 43, 47}, versions.mid {40, 43} ;
#   (2) choisir « Version 1 » : l'arrangement {40, 43}, versions.mid {40, 43, 47} ;
#   (3) Ctrl+Z : de retour sur la seconde, l'arrangement {40, 43, 47} ;
#   (4) « Renommer la version… » répondu « Refrain » : le nom est au fichier ;
#   (5) « Supprimer la version » (l'active) : la voisine sort — une version, l'arrangement
#       {40, 43} ;
#   (6) le projet de (4), rouvert : l'infobulle du nom dit « Version « Refrain » — 2 sur 2 ».
# Les notes se comparent en MULTIENSEMBLES (CLAUDE.md, 13/09), et chaque fichier se lit
# avant de juger : une comparaison dont un côté manque n'est pas un verdict.
#
#   tools/versions-piste.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
PY=analyse/.venv/bin/python
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
"$PY" -c "import mido" 2>/dev/null || { echo "REFUS : mido est requis pour relire les .mid"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-versions-piste.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs, t = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Basse")], 0
for debut, h in ((0, 40), (480, 43)):
    evs += [(debut - t, bytes([0x90, h, 100])), (240, bytes([0x80, h, 0]))]
    t = debut + 240
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Basse"}]}, open(d + "/project.json", "w"), indent=1)
PY

b="$brouillon"
maison="$(mktemp -d "$b/home.XXXX")"   # D318 : un HOME NEUF par course
env HOME="$maison" VSM_PROJET="$b/projet" VSM_TAILLE="1280x800" VSM_DELAI=7000 VSM_VUE="sans-rapport" \
    VSM_OPTIONS="nom=Refrain" \
    VSM_GESTE_APRES="1000:choisir:0;1400:menu:Dupliquer la version;1800:ecrire-note:0:960:240:47;2200:enregistrer:$b/e1;2600:menu:Version « Version 1 »;3000:enregistrer:$b/e2;3400:touche:ctrl + Z;3800:enregistrer:$b/e3;4200:menu:Renommer la version…;4800:enregistrer:$b/e4;5200:menu:Supprimer la version;5600:enregistrer:$b/e5" \
    VSM_CAPTURE="$b/versions.png" timeout 60 "$BIN" > "$b/journal.txt" 2>&1
grep -E "VSM_(GESTE_APRES|GESTE|MENU|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$b/journal.txt" \
    | sed "s/^/        journal : /" >&2
grep "VSM_VERSION" "$b/journal.txt" | sed 's/^/        /'

# « versions | active | noms | arrangement | versions.mid », ou « ABSENT ».
etat() {
    "$PY" - "$1" <<'PY'
import glob, json, os, sys
import mido
d = sys.argv[1]
try:
    projet = json.load(open(d + "/project.json"))
except Exception:
    print("ABSENT"); sys.exit(0)
piste = projet["tracks"][0]
versions = piste.get("versions", [])
def hauteurs(chemin):
    if not os.path.exists(chemin): return "-"
    h = sorted(m.note for tr in mido.MidiFile(chemin).tracks for m in tr if m.type == "note_on" and m.velocity > 0)
    return ",".join(map(str, h)) or "vide"
arr = glob.glob(d + "/midi/arrangement*.mid") or glob.glob(d + "/*.mid")
print(len(versions), piste.get("activeVersion", "-"), "/".join(v["name"] for v in versions) or "-",
      hauteurs(arr[0]) if arr else "-", hauteurs(d + "/midi/versions.mid"), sep=" | ")
PY
}
rates=0
juger() {   # $1 = libellé ; $2 = état ; $3 = attendu
    if [ "$2" = "$3" ]; then printf '  OK   %s\n         %s\n' "$1" "$2"
    else printf '  RATÉ %s\n         obtenu  : %s\n         attendu : %s\n' "$1" "$2" "$3"; rates=$((rates + 1)); fi
}
echo "=== D532.2 bis : les versions de piste par le menu ==="
echo "       (versions | active | noms | arrangement | versions.mid)"
juger "(1) dupliquer puis écrire : deux versions, la seconde active" "$(etat "$b/e1")" "2 | 1 | Version 1/Version 2 | 40,43,47 | 40,43"
juger "(2) choisir la première : sa matière sort, l'autre est rangée" "$(etat "$b/e2")" "2 | 0 | Version 1/Version 2 | 40,43 | 40,43,47"
juger "(3) Ctrl+Z : de retour sur la seconde" "$(etat "$b/e3")" "2 | 1 | Version 1/Version 2 | 40,43,47 | 40,43"
juger "(4) renommée « Refrain »" "$(etat "$b/e4")" "2 | 1 | Version 1/Refrain | 40,43,47 | 40,43"
# Après suppression, le versions.mid de e5 est celui que l'enregistrement ÉCRIT : une seule
# version, active, donc aucune piste rangée — le fichier n'est pas écrit (« - »).
juger "(5) supprimer l'active : la voisine sort, une seule reste" "$(etat "$b/e5")" "1 | 0 | Version 1 | 40,43 | -"

# (6) CE QUE LA LIGNE MONTRE : le projet enregistré après le renommage, rouvert, dit sa
# version dans l'infobulle du nom (relevé du démarrage : il n'y a ici aucun geste).
if [ -s "$b/e4/project.json" ]; then
    maison="$(mktemp -d "$b/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$b/e4" VSM_TAILLE="1280x800" VSM_DELAI=2500 VSM_VUE="sans-rapport" \
        VSM_TEXTES_LISTE=1 VSM_CAPTURE="$b/rouvert.png" timeout 60 "$BIN" > "$b/rouvert.txt" 2>&1
    bulle="$(grep -c '^VSM_TEXTE : infobulle : Version « Refrain » — 2 sur 2$' "$b/rouvert.txt")"
    echo "       rouvert : « Version « Refrain » — 2 sur 2 » ×$bulle"
    if [ "$bulle" -ge 1 ]; then printf '  OK   %s\n' "(6) la ligne dit la version qu'on entend"
    else printf '  RATÉ %s\n' "(6) la ligne dit la version qu'on entend"; rates=$((rates + 1)); fi
else
    printf '  RATÉ %s\n' "(6) le projet de (4) manque : rien à rouvrir"; rates=$((rates + 1))
fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
