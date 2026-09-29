#!/usr/bin/env bash
# LA GARDE DE D503 : LE MÉTRONOME EST UNE DONNÉE DU MORCEAU, ET L'EXPORT NE CLIQUE PAS.
#
# RÈGLE GARDÉE (29/09/2026). L'interrupteur Clic vit dans le projet, à côté de la
# boucle (`"metronome": true` dans le bloc transport, écrit seulement quand il est
# allumé). Le rendu hors ligne ne le lit pas : un export avec et sans le champ
# rend le MÊME fichier.
#
# COMMENT. Un projet d'une piste. (1) Clic allumé par le menu Transport, projet
# enregistré par un geste DIFFÉRÉ (le bouton bascule par `triggerClick`, qui
# POSTE : D483) ; le fichier est lu. (2) Le dossier écrit est rouvert (HOME neuf) et
# le menu relevé : l'entrée « Métronome (marche / arrêt) » doit être [cochée].
# (3) Basculé deux fois, le champ disparaît. (4) Deux exports, avec et sans le
# champ, comparés par `cmp` — après avoir vérifié qu'ils existent et ne sont pas
# vides (une comparaison dont un côté manque rend « différent », pas « raté »).
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/metronome-projet.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-metronome.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une")]
for i in range(4):
    evs += [(0 if i == 0 else 480, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "clic", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY

lancer() {   # $1 nom ; $2... variables
    local h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$h" VSM_TAILLE="1280x800" VSM_DELAI=1500 VSM_VUE="sans-rapport" \
        VSM_CAPTURE="$brouillon/$1.png" "${@:2}" timeout 60 "$BIN" > "$brouillon/$1.txt" 2>&1
    grep -E "VSM_(MENU|GESTE_APRES) : .*(aucune|grisée|refusé|JAMAIS)" "$brouillon/$1.txt" | sed 's/^/        journal : /' >&2
}
champ() {   # $1 = project.json -> « true », « absent » ou « illisible »
    python3 - "$1" <<'PY'
import json, sys
try:
    t = json.load(open(sys.argv[1], encoding="utf-8"))["transport"]
    print("true" if t.get("metronome") is True else ("absent" if "metronome" not in t else repr(t["metronome"])))
except (OSError, KeyError, ValueError):
    print("illisible")
PY
}
coche() {   # $1 = journal -> « oui » / « non » / « absente »
    local l
    l="$(grep "VSM_MENU_LISTE : Transport > Métronome" "$1" | head -1)"
    [ -z "$l" ] && { echo absente; return; }
    case "$l" in *"[cochée]"*) echo oui ;; *) echo non ;; esac
}

rates=0
dire() {   # $1 nom ; $2 obtenu ; $3 attendu
    if [ "$2" = "$3" ]; then printf '  OK   %-30s %s\n' "$1" "$2"
    else printf '  RATÉ %-30s %s (attendu %s)\n' "$1" "$2" "$3"; rates=$((rates + 1)); fi
}

echo "=== D503 : le clic appartient au morceau, et l'export ne clique pas ==="
lancer allume VSM_PROJET="$brouillon/projet" VSM_MENU="Métronome (marche / arrêt)" \
       VSM_GESTE_APRES="700:enregistrer:$brouillon/allume"
premier="$(champ "$brouillon/allume/project.json")"
dire "enregistré allumé : champ" "$premier" true
lancer rouvert VSM_PROJET="$brouillon/allume" VSM_MENU_LISTE=1
dire "rouvert : entrée cochée" "$(coche "$brouillon/rouvert.txt")" oui
lancer sans VSM_PROJET="$brouillon/projet" VSM_MENU_LISTE=1
dire "projet sans le champ : cochée" "$(coche "$brouillon/sans.txt")" non
lancer deux-fois VSM_PROJET="$brouillon/projet" VSM_MENU="Métronome (marche / arrêt);Métronome (marche / arrêt)" \
       VSM_GESTE_APRES="700:enregistrer:$brouillon/deux-fois"
# SON TÉMOIN EST LE PREMIER CAS (D145) : sur un binaire qui n'écrit jamais le champ,
# « absent » sortirait vert sans rien prouver.
if [ "$premier" = "true" ]; then
    dire "basculé deux fois : champ" "$(champ "$brouillon/deux-fois/project.json")" absent
else
    printf '  RATÉ %-30s non jugé : le premier cas n'"'"'a pas écrit le champ\n' "basculé deux fois"
    rates=$((rates + 1))
fi

# L'EXPORT NE CLIQUE PAS : le même morceau, avec et sans le champ.
lancer export-sans VSM_PROJET="$brouillon/projet" VSM_EXPORT="$brouillon/sans.wav"
lancer export-avec VSM_PROJET="$brouillon/allume" VSM_EXPORT="$brouillon/avec.wav"
if [ -s "$brouillon/sans.wav" ] && [ -s "$brouillon/avec.wav" ]; then
    if cmp -s "$brouillon/sans.wav" "$brouillon/avec.wav"; then
        printf '  OK   %-30s identiques (%s octets)\n' "export avec / sans clic" "$(stat -c %s "$brouillon/avec.wav")"
    else
        printf '  RATÉ %-30s les deux exports diffèrent\n' "export avec / sans clic"; rates=$((rates + 1))
    fi
else
    printf '  RATÉ %-30s un export manque ou est vide (sans : %s, avec : %s)\n' "export avec / sans clic" \
           "$(stat -c %s "$brouillon/sans.wav" 2>/dev/null || echo absent)" "$(stat -c %s "$brouillon/avec.wav" 2>/dev/null || echo absent)"
    rates=$((rates + 1))
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
