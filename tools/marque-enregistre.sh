#!/usr/bin/env bash
# LA GARDE DE D507 : LA MARQUE « NON ENREGISTRÉ » DIT VRAI APRÈS L'OUVERTURE D'UN PROJET.
#
# RÈGLE GARDÉE (29/09/2026). La marque (l'astérisque du titre, et la question que
# pose la fermeture) se déduit de l'historique contre un repère posé à chaque
# enregistrement (D174). Avant D507, ouvrir un DOSSIER de projet vidait
# l'historique sans reposer le repère : le projet ouvert s'affichait « modifié »,
# puis, un geste plus tard, « enregistré » — et fermer ne demandait rien.
#
# COMMENT. Deux projets engendrés, A et B ; les préférences du HOME de banc
# portent B dans « Projets récents ». Une seule course : A ouvert, un geste
# (« Lente », la forme des fondus croisés, qui fait un pas depuis D506), A
# enregistré ; puis B ouvert PAR LE MENU « Projets récents » (le chemin de
# l'utilisateur, `loadProjectBundleFromFolder`) ; un geste sur B ; Ctrl+Z. Après
# chaque étape, `relever-titre` écrit ce que l'application croit (« non
# enregistré : oui/non »). Tout est joué en différé, dans l'ordre.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/marque-enregistre.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-marque.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

for nom in projet-a projet-b; do
    mkdir -p "$brouillon/$nom/midi"
    python3 - "$brouillon/$nom" "$nom" <<'PY'
import json, struct, sys
d, nom = sys.argv[1], sys.argv[2]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une")]
for i in range(4):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60 + i, 100])), (480, bytes([0x80, 60 + i, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY
done

h="$(mktemp -d "$brouillon/home.XXXX")"
mkdir -p "$h/VintageSynthMidiStudio"
python3 - "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" "$brouillon/projet-b" <<'PY'
import sys
from xml.sax.saxutils import quoteattr
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="projetsRecents" val='
    + quoteattr(sys.argv[2]) + '/>\n</PROPERTIES>\n')
PY

env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-a" VSM_VUE="sans-rapport" VSM_DELAI=3600 \
    VSM_GESTE_APRES="400:menu:Lente;700:enregistrer:$brouillon/projet-a2;1000:relever-titre;1300:menu:projet-b;1700:relever-titre;2000:menu:Rapide;2300:relever-titre;2600:touche:ctrl + Z;2900:relever-titre" \
    VSM_CAPTURE="$brouillon/marque.png" timeout 40 "$BIN" > "$brouillon/marque.txt" 2>&1
grep -E "VSM_(MENU|GESTE_APRES|TOUCHE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE)" "$brouillon/marque.txt" | sed 's/^/        journal : /' >&2
mapfile -t etats < <(grep -o "non enregistre : [a-z]*" "$brouillon/marque.txt" | cut -d' ' -f4)
mapfile -t titres < <(grep "VSM_TITRE_ETAT : " "$brouillon/marque.txt" | sed 's/VSM_TITRE_ETAT : //')

rates=0
etape() {   # $1 index ; $2 nom ; $3 attendu (oui|non)
    local e="${etats[$1]:-?}" t="${titres[$1]:-?}"
    if [ "$e" = "$3" ]; then printf '  OK   %-40s %s\n' "$2" "$t"
    else printf '  RATÉ %-40s %s (attendu : non enregistré %s)\n' "$2" "$t" "$3"; rates=$((rates + 1)); fi
}
echo "=== D507 : la marque « non enregistré » après l'ouverture d'un projet ==="
etape 0 "A : un geste, puis enregistré" non
etape 1 "B ouvert par « Projets récents »" non
etape 2 "B : un geste" oui
etape 3 "B : Ctrl+Z" non

# LA REPRISE APRÈS PANNE : le projet récupéré n'est PAS enregistré (il vient d'une
# copie de travail), et la marque doit le dire jusqu'au prochain enregistrement.
# La panne est un `kill -9` après une autosauvegarde forcée (la méthode de
# `autosauvegarde-vue.sh`, D368) ; on attend par PID, jamais par motif.
h2="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h2" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-b" VSM_VUE="sans-rapport" VSM_DELAI=2500 \
    VSM_MENU="Rapide" VSM_AUTOSAUVEGARDE=1 "$BIN" > "$brouillon/panne.txt" 2>&1 &
pid=$!
limite=$((SECONDS + 60))
while kill -0 "$pid" 2>/dev/null && [ "$SECONDS" -lt "$limite" ]; do
    grep -q "VSM_AUTOSAUVEGARDE :" "$brouillon/panne.txt" 2>/dev/null && break
    sleep 1
done
kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
env HOME="$h2" VSM_TAILLE="1280x800" VSM_VUE="sans-rapport" VSM_DELAI=4000 VSM_RECUPERER=1 \
    VSM_GESTE_APRES="2500:relever-titre" VSM_CAPTURE="$brouillon/repris.png" \
    timeout 60 "$BIN" > "$brouillon/repris.txt" 2>&1
e="$(grep -o "non enregistre : [a-z]*" "$brouillon/repris.txt" | tail -1 | cut -d' ' -f4)"
t="$(grep "VSM_TITRE_ETAT : " "$brouillon/repris.txt" | tail -1 | sed 's/VSM_TITRE_ETAT : //')"
if ! grep -q "VSM_RECUPERER : r" "$brouillon/repris.txt"; then
    printf '  RATÉ %-40s la boîte « Session interrompue » n'"'"'a pas répondu\n' "reprise après panne"
    rates=$((rates + 1))
elif [ "$e" = "oui" ]; then printf '  OK   %-40s %s\n' "reprise après panne" "$t"
else printf '  RATÉ %-40s %s (attendu : non enregistré oui)\n' "reprise après panne" "$t"; rates=$((rates + 1)); fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
