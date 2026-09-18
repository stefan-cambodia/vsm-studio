#!/usr/bin/env bash
# La garde de D362 : À L'OUVERTURE, L'ARRANGEMENT MONTRE LE MORCEAU.
#
# RÈGLE GARDÉE (19/09/2026) : ouvrir un projet cadre la vue d'arrangement sur
# l'étendue de ce projet — au moins **90 %** du morceau visible, et jamais moins.
#
# POURQUOI. L'arrangement gardait son zoom d'usine (`pixelsPerTick_ = 0.06`)
# quelle que soit la longueur du morceau : mesuré sur `children-c3-plafond`
# (227 mesures, 454 s), il en montrait **3,8 mesures, soit 1,7 %**. Il fallait
# presser Ctrl+0 à chaque ouverture pour voir ce qu'on venait d'ouvrir. Aucun des
# trois logiciels de référence ne demande ce geste.
#
# POURQUOI CADRER PLUTÔT QUE RESTAURER : `project.json` ne porte AUCUN état de
# vue (format, midi, title, tracks, transport, version), et les projets que la
# chaîne d'analyse produit n'en porteront jamais. Le jour où le format en
# portera un, c'est LUI qui devra primer — le cadrage est le repli.
#
# LA GARDE ENGENDRE SON PROJET plutôt que d'ouvrir un morceau du corpus : une
# garde qui dépendrait d'un dossier posé à côté d'elle se tairait le jour où il
# disparaît. Deux projets, l'un LONG (200 mesures) et l'autre COURT (4 mesures) :
# le second vérifie que le cadrage ne dépend pas de la longueur, et qu'un petit
# morceau ne se retrouve pas étalé sur une largeur absurde.
#
# Rend 0 si les deux tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/cadrage-ouverture.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-cadrage.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

engendrer() {   # $1 = dossier, $2 = nombre de mesures
    mkdir -p "$1/midi"
    python3 - "$1" "$2" <<'PY'
import json, struct, sys
d, mesures = sys.argv[1], int(sys.argv[2])
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
# Une note par mesure : le morceau fait donc exactement `mesures` mesures.
evs = [(0, b"\xff\x03\x04Long"), (0, b"\xff\x51\x03\x07\xa1\x20"),
       (0, b"\xff\x58\x04\x04\x02\x18\x08")]
for i in range(mesures):
    evs += [(0 if i == 0 else 1920 - 240, bytes([0x90, 60, 100])),
            (240, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(max(0, dt)) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "cadrage",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                        "instrument": {"preferredPlugin": "vsm.minimoog"},
                        "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0],
                                 "solo": False, "volume": 1.0},
                        "name": "Long"}]},
          open(d + "/project.json", "w"), indent=1)
PY
}

part_visible() {   # $1 = dossier du projet -> le pourcentage montré à l'ouverture
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_TAILLE="1280x742" VSM_PROJET="$1" VSM_DELAI=2500 \
        VSM_VUE="sans-rapport,arrangement" VSM_ARRANGEMENT=1 \
        VSM_CAPTURE="$brouillon/$(basename "$1").png" \
        timeout 60 "$BIN" > "$brouillon/$(basename "$1").txt" 2>&1
    grep "VSM_ARRANGEMENT : fen" "$brouillon/$(basename "$1").txt" | tail -1 \
        | sed 's/.*soit \([0-9.]*\) %.*/\1/'
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D362 : à l'ouverture, l'arrangement montre le morceau ==="

for cas in "long:200" "court:4"; do
    nom="${cas%%:*}"; mesures="${cas##*:}"
    engendrer "$brouillon/$nom" "$mesures"
    part="$(part_visible "$brouillon/$nom")"
    echo "       $nom ($mesures mesures) : $part % visible"
    # LA COMPARAISON SE FAIT EN ENTIERS : `[ ]` ne compare pas des décimales, et
    # un test qui échoue à comparer rendrait « faux » sans le dire.
    assez="$(python3 -c "print(1 if float('${part:-0}') >= 90.0 else 0)" 2>/dev/null || echo 0)"
    verdict "$nom : au moins 90 % du morceau visible à l'ouverture" "$assez"
done

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
