#!/usr/bin/env bash
# LA GARDE DE D369 : UN PROJET SE ROUVRE SUR LA PISTE ET LE PIANO ROLL QU'ON A
# LAISSÉS, pas seulement sur le zoom d'arrangement.
#
# RÈGLE GARDÉE (20/09/2026) : le bloc `view` de `project.json` porte, en plus du
# zoom d'arrangement de D363, la PISTE CHOISIE et la vue du PIANO ROLL (zoom,
# défilement, note du haut, hauteur de rang) ; à l'ouverture c'est LUI qui prime,
# et les cadrages automatiques (D338 pour le piano roll, D362 pour l'arrangement)
# restent le repli.
#
# POURQUOI CES CHAMPS-LÀ ET PAS D'AUTRES. La règle de partage est celle de D363
# et elle tranche chaque champ : ce qui dépend du MORCEAU va dans le projet, ce
# qui n'en dépend pas reste dans les préférences. La piste choisie en dépend (la
# piste 3 d'un morceau n'est pas celle d'un autre), la fenêtre du piano roll
# aussi (elle cadre un matériau) ; l'onglet du dock et les fenêtres flottantes,
# non — ils restent où D363 les a laissés.
#
# CE QUI A RENDU LA RÈGLE MESURABLE, et sans quoi elle ne l'était pas :
#   * `VSM_PIANOROLL_ZONES=1` dit désormais la FENÊTRE DE HAUTEURS (`haut=`,
#     `bas=`, `lignes=`, `zoom=`, `defilement=`) et non plus le seul rang : le
#     reste vivait dans des champs privés que seule la peinture consommait.
#   * `VSM_PISTE_CHOISIE` est neuf. Aucun relevé ne disait quelle piste est
#     choisie : `VSM_GESTE : choisir` ne parle que du geste qu'on vient de
#     jouer, donc une piste choisie par le PROJET à l'ouverture — ce que cette
#     phase ajoute — ne laissait aucune trace.
#
# LE CONTRÔLE EST DANS LA GARDE. « rouvert sur la piste 2 » ne prouve rien tout
# seul : encore faut-il que 2 ne soit pas ce qu'on obtient sans rien enregistrer.
# La garde ouvre donc le MÊME projet sans bloc `view` et exige que les deux
# diffèrent — sur la piste comme sur la fenêtre de hauteurs.
#
# Rend 0 si les six contrôles tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/vue-du-morceau.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-vue.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# LE PROJET EST ENGENDRÉ, et ses TROIS pistes n'ont pas le même ambitus : c'est
# ce qui rend la fenêtre de hauteurs lisible. Une piste grave, une aiguë, une
# médium — reprendre la vue de l'une sur l'autre se verrait tout de suite.
mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))

def piste(nom, hauteur, mesures=24):
    evs = [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    for i in range(mesures):
        evs += [(0 if i == 0 else 1440, bytes([0x90, hauteur, 100])),
                (480, bytes([0x80, hauteur, 0]))]
    corps = b"".join(vlq(max(0, dt)) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps

entete = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
corps0 = b"".join(vlq(dt) + o for dt, o in entete) + b"\x00\xff\x2f\x00"
tete = b"MTrk" + struct.pack(">I", len(corps0)) + corps0
data = (b"MThd" + struct.pack(">IHHH", 6, 1, 4, 480) + tete
        + piste("grave", 33) + piste("medium", 60) + piste("aigue", 88))
open(d + "/midi/arrangement.mid", "wb").write(data)

def t(nom, couleur):
    return {"channel": 0, "color": couleur, "effects": [],
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False,
                     "volume": 1.0}, "name": nom}

json.dump({"format": "vsm-project", "version": 1, "title": "vue",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("grave", "#FF6B9BFF"), t("medium", "#FFD166FF"),
                       t("aigue", "#4CC9F0FF")]},
          open(d + "/project.json", "w"), indent=1)
PY

champ() {   # $1 = journal, $2 = clé (haut=, bas=, rang=) -> la valeur
    grep "VSM_PIANOROLL_RANG" "$1" | tail -1 | grep -o "$2[0-9-]*" | tail -1 | cut -d= -f2
}
piste_choisie() {   # $1 = journal -> l'index de la piste choisie
    grep "VSM_PISTE_CHOISIE :" "$1" | tail -1 | sed 's/.*CHOISIE : \([0-9]*\) sur.*/\1/'
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D369 : la piste choisie et le piano roll se rouvrent où on les a laissés ==="

# (1) choisir la piste AIGUË (2), cadrer son piano roll, enregistrer
maison="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=3000 \
    VSM_VUE="sans-rapport,pianoroll" VSM_GESTE_PISTE="choisir:2" \
    VSM_TOUCHE="pianoroll:ctrl + 0" VSM_PIANOROLL_ZONES=1 \
    VSM_ENREGISTRER="$brouillon/enregistre" \
    VSM_CAPTURE="$brouillon/avant.png" timeout 90 "$BIN" > "$brouillon/avant.txt" 2>&1
piste_avant="$(piste_choisie "$brouillon/avant.txt")"
haut_avant="$(champ "$brouillon/avant.txt" 'haut=')"
rang_avant="$(champ "$brouillon/avant.txt" 'rang=')"
echo "       piste ${piste_avant:-?}, fenêtre du piano roll haut=${haut_avant:-?} rang=${rang_avant:-?}"
verdict "le projet a été enregistré" \
        "$([ -s "$brouillon/enregistre/project.json" ] && echo 1 || echo 0)"
porte="$(python3 - "$brouillon/enregistre/project.json" <<'PY'
import json, sys
try:
    v = json.load(open(sys.argv[1])).get("view") or {}
except Exception:
    print(0); raise SystemExit
print(1 if v.get("selectedTrack") is not None and v.get("pianoRollTopNote") else 0)
PY
)"
verdict "le fichier porte « selectedTrack » et « pianoRollTopNote »" "$porte"

# (2) rouvrir : la piste et la fenêtre doivent être les mêmes
maison2="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison2" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/enregistre" VSM_DELAI=3000 \
    VSM_VUE="sans-rapport,pianoroll" VSM_PIANOROLL_ZONES=1 \
    VSM_CAPTURE="$brouillon/apres.png" timeout 90 "$BIN" > "$brouillon/apres.txt" 2>&1
piste_apres="$(piste_choisie "$brouillon/apres.txt")"
haut_apres="$(champ "$brouillon/apres.txt" 'haut=')"
rang_apres="$(champ "$brouillon/apres.txt" 'rang=')"
echo "       rouvert : piste ${piste_apres:-?}, haut=${haut_apres:-?} rang=${rang_apres:-?}"

# (3) LE TÉMOIN : le même projet SANS bloc « view ». Sans lui, « avant = après »
# serait vrai aussi le jour où les deux valent le défaut, et ne prouverait rien.
mkdir -p "$brouillon/sansvue/midi"
python3 - "$brouillon/enregistre/project.json" "$brouillon/sansvue/project.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d.pop("view", None)
json.dump(d, open(sys.argv[2], "w"), indent=1)
PY
cp "$brouillon/enregistre/midi/"*.mid "$brouillon/sansvue/midi/" 2>/dev/null || true
cp "$brouillon/enregistre/"*.synth.json "$brouillon/sansvue/" 2>/dev/null || true
maison3="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison3" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/sansvue" VSM_DELAI=3000 \
    VSM_VUE="sans-rapport,pianoroll" VSM_PIANOROLL_ZONES=1 \
    VSM_CAPTURE="$brouillon/temoin.png" timeout 90 "$BIN" > "$brouillon/temoin.txt" 2>&1
piste_temoin="$(piste_choisie "$brouillon/temoin.txt")"
haut_temoin="$(champ "$brouillon/temoin.txt" 'haut=')"
echo "       sans « view » : piste ${piste_temoin:-?}, haut=${haut_temoin:-?} (le défaut)"

verdict "la piste choisie s'écarte du défaut" \
        "$([ -n "${piste_avant:-}" ] && [ "${piste_avant:-x}" != "${piste_temoin:-x}" ] && echo 1 || echo 0)"
verdict "la fenêtre de hauteurs s'écarte du défaut" \
        "$([ -n "${haut_avant:-}" ] && [ "${haut_avant:-x}" != "${haut_temoin:-x}" ] && echo 1 || echo 0)"
verdict "le projet rouvert retrouve SA piste" \
        "$([ -n "${piste_avant:-}" ] && [ "${piste_avant:-x}" = "${piste_apres:-y}" ] && echo 1 || echo 0)"
verdict "le projet rouvert retrouve SA fenêtre de hauteurs" \
        "$([ -n "${haut_avant:-}" ] && [ "${haut_avant:-x}" = "${haut_apres:-y}" ] \
            && [ "${rang_avant:-x}" = "${rang_apres:-y}" ] && echo 1 || echo 0)"

echo
if [ "$rates" -ne 0 ]; then echo "D369 : $rates contrôle(s) raté(s)"; exit 1; fi
echo "D369 : un projet se rouvre sur la piste et le piano roll qu'on a laissés"
