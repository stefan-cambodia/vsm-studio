#!/usr/bin/env bash
# La garde de D348 : ON MODIFIE UNE VALEUR DEPUIS LA LISTE D'ÉVÉNEMENTS.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. la saisie ÉCRIT DANS LE PROJET — vérifié non pas sur la case affichée mais
#      sur le **fichier MIDI exporté** : la liste pourrait montrer autre chose que
#      ce que le modèle porte, et c'est justement ce qu'une garde doit exclure ;
#   2. une valeur hors bornes est **REFUSÉE**, jamais bornée en silence : celui
#      qui tape 300 ne doit pas obtenir 127 sans le savoir ;
#   3. une colonne qui n'a pas de sens pour la famille est refusée ;
#   4. la modification est ANNULABLE — un pas « Modifier un événement » entre
#      dans l'historique ;
#   5. l'éditeur en place se VOIT (forme `ligne:colonne:?`, qui l'ouvre et le
#      laisse ouvert) : il a d'abord été peint SOUS la table, et le journal
#      disait « saisie ouverte » pendant que la photo ne montrait rien.
#
# Colonnes : 1 position, 4 numéro, 5 valeur, 6 durée.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire ou mido manquent.
#
#   tools/liste-editer.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="analyse/.venv/bin/python"
[ -x "$PY" ] || PY=python3
"$PY" -c "import mido" 2>/dev/null || { echo "REFUS : mido est requis pour relire le .mid"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-liste-editer.XXXXXX")"
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
evs = [(0, b"\xff\x03\x04Lead"), (0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
for i in range(8):                      # huit noires à 100 de vélocité, 480 ticks
    h = 48 + i
    evs += [(0, bytes([0x90, h, 100])), (480, bytes([0x80, h, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "liste",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                        "instrument": {"preferredPlugin": "vsm.minimoog"},
                        "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                        "name": "Lead"}]},
          open(d + "/project.json", "w"), indent=1)
PY

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D348 : modifier une valeur depuis la liste d'événements ==="

maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement,liste" VSM_TEXTES_LISTE=1 \
    VSM_LISTE_EDITER="0:5:42;0:4:72;0:6:240;0:5:300;0:2:3" \
    VSM_EXPORT_MIDI="$brouillon/apres.mid" \
    VSM_CAPTURE="$brouillon/liste.png" \
    timeout 45 "$BIN" > "$brouillon/journal.txt" 2>&1
rc=$?
j="$(cat "$brouillon/journal.txt")"

lu="$("$PY" - "$brouillon/apres.mid" <<'PY'
import sys
import mido
m = mido.MidiFile(sys.argv[1])
for tr in m.tracks:
    t = 0
    ouvertes = {}
    for e in tr:
        t += e.time
        if e.type == "note_on" and e.velocity > 0:
            ouvertes[e.note] = (t, e.velocity)
        elif e.type in ("note_off",) or (e.type == "note_on" and e.velocity == 0):
            if e.note in ouvertes:
                deb, vel = ouvertes.pop(e.note)
                print(f"{e.note} {deb} {vel} {t - deb}")
                raise SystemExit(0)
print("aucune note")
PY
)"
echo "       première note du .mid exporté : hauteur/tick/vélocité/durée = $lu"
verdict "les trois saisies sont ÉCRITES et relues dans le .mid (attendu : 72 0 42 240)" \
        "$([ "$lu" = "72 0 42 240" ] && echo 1 || echo 0)"
verdict "une vélocité à 300 est REFUSÉE, jamais bornée" \
        "$(grep -c 'VSM_LISTE : 300 — refusé' <<<"$j")"
verdict "la colonne « Nature » n'est pas modifiable" \
        "$(grep -c 'VSM_LISTE : colonne 2 non modifiable' <<<"$j")"
verdict "la modification entre dans l'historique (annulable)" \
        "$(grep -c 'VSM_HISTORIQUE : Modifier un événement' <<<"$j")"
verdict "code de sortie 0 (relevé : $rc)" "$([ "$rc" -eq 0 ] && echo 1 || echo 0)"

# (5) L'ÉDITEUR EN PLACE SE VOIT, ET LA MESURE PORTE SON TÉMOIN.
#
# Compter les pixels ambre d'une photo NE PROUVE RIEN tout seul : la colonne
# « Nature » écrit « Note » en ambre sur chaque ligne, et le compte passait le
# seuil SANS éditeur (361 px contre 577 avec, mesuré). Deux courses, une seule
# variable — la forme « ? » —, et c'est leur DIFFÉRENCE qui décide.
compter_ambre() {
    "$PY" - "$1" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print(0); raise SystemExit(0)
im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
W, H = im.size
n = 0
for y in range(H // 2, H):
    for x in range(W):
        r, g, b = px[x, y]
        if r > 190 and 130 < g < 190 and b < 110:
            n += 1
print(n)
PY
}
for cas in "sans:" "avec:2:5:?"; do
    nom="${cas%%:*}"
    consigne="${cas#*:}"
    maison2="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison2" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
        VSM_VUE="sans-rapport,arrangement,liste" VSM_LISTE_EDITER="$consigne" \
        VSM_CAPTURE="$brouillon/$nom.png" \
        timeout 45 "$BIN" > "$brouillon/$nom.txt" 2>&1
done
verdict "la forme « ? » ouvre la saisie et le dit" \
        "$(grep -c 'VSM_LISTE : saisie ouverte sur la ligne 2, colonne 5' "$brouillon/avec.txt")"
sans="$(compter_ambre "$brouillon/sans.png")"
avec="$(compter_ambre "$brouillon/avec.png")"
verdict "le contour ambre de l'éditeur est SUR LA PHOTO : $sans px sans, $avec px avec (règle : +150)" \
        "$([ $((${avec:-0} - ${sans:-0})) -ge 150 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
