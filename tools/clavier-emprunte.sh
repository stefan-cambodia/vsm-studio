#!/usr/bin/env bash
# LA GARDE DE D492 : ACTIF, LE CLAVIER D'ORDINATEUR PREND SES LETTRES, QUELLE QUE
# SOIT LA VUE QUI A LE CLAVIER.
#
# RÈGLE GARDÉE (29/09/2026). D11.7 promet que le clavier d'ordinateur, actif,
# « emprunte les lettres aux raccourcis ». Mais JUCE distribue une touche d'abord à
# la vue qui a le clavier (`juce_ComponentPeer.cpp:200-220`), et le piano roll
# (G, D) et l'arrangement (G, F, A) ont des commandes sur ces lettres : clavier
# actif et piano roll cliqué, G basculait l'aimantation au lieu de jouer un sol.
#
# COMMENT. Un projet de trois pistes est engendré. Chaque course joue les dix-neuf
# touches du clavier (a s d f g h j k l ; — w e t y u o p — z x ; le « ; » s'écrit
# « #3b », `VSM_TOUCHE` séparant ses touches par « ; ») par `VSM_TOUCHE=focus:<vue>:…`,
# qui rejoue la chaîne de `ComponentPeer::handleKeyPress` depuis la vue et dit QUI
# a pris la touche ; le clavier dit ce qu'il joue (`VSM_CLAVIER : note N`, `octave N`).
# Quatre courses : clavier actif / inactif × piano roll / arrangement, chacune sous
# un HOME neuf (D318).
#
# LE CONTRÔLE (clavier INACTIF) PORTE LE TÉMOIN DE LA RÈGLE : les lettres y restent
# des commandes — G et D prises par le piano roll, G, F et A par l'arrangement,
# aucune note. Sans lui, « 0 touche prise par une vue » ne distinguerait pas « le
# clavier les prend » de « les vues ne répondent plus à rien ».
#
# Rend 0 si les quatre courses tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/clavier-emprunte.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-clavier-emprunte.XXXXXX")"
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
def piste(nom, hauteur, tempo=b""):
    evs = ([(0, tempo)] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    for i in range(8):
        evs += [(0 if i == 0 else 1440, bytes([0x90, hauteur, 100])), (480, bytes([0x80, hauteur, 0]))]
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
data = (b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480)
        + piste("une", 40, tempo=b"\xff\x51\x03\x07\xa1\x20") + piste("deux", 60) + piste("trois", 80))
open(d + "/midi/arrangement.mid", "wb").write(data)
def t(nom):
    return {"channel": 0, "color": "#FF6B9BFF", "effects": [],
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
            "name": nom}
json.dump({"format": "vsm-project", "version": 1, "title": "clavier",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("une"), t("deux"), t("trois")]},
          open(d + "/project.json", "w"), indent=1)
PY

TOUCHES="A S D F G H J K L #3b W E T Y U O P Z X"

course() {   # $1 = vue (pianoroll|arrangement) ; $2 = actif|inactif -> une ligne de verdict
    local vue="$1" etat="$2" maison suite="" t
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    for t in $TOUCHES; do suite="$suite;focus:$vue:$t"; done
    local menu=()
    [ "$etat" = "actif" ] && menu=(VSM_MENU="Clavier d'ordinateur")
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x742" VSM_DELAI=800 \
        VSM_VUE="sans-rapport,arrangement" "${menu[@]}" VSM_TOUCHE="${suite#;}" \
        VSM_CAPTURE="$brouillon/$vue-$etat.png" \
        timeout 60 "$BIN" > "$brouillon/$vue-$etat.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147). « touche inconnue ou sans
    # commande » est ATTENDU pour une lettre que personne ne prend (clavier inactif,
    # lettre sans commande) : il est compté, pas listé — la ligne de la chaîne dit
    # déjà « personne ne l'a prise », et c'est elle que le verdict lit.
    grep -E "VSM_(MENU|TOUCHE|VUE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)" "$brouillon/$vue-$etat.txt" \
        | grep -v "touche inconnue ou sans commande" | sed 's/^/        journal : /' >&2
    local sans
    sans="$(grep -c "touche inconnue ou sans commande" "$brouillon/$vue-$etat.txt")"
    [ "$sans" -gt 0 ] && echo "        journal : $sans touche(s) que personne n'a prise ($vue, clavier $etat)" >&2
    python3 - "$brouillon/$vue-$etat.txt" "$vue" <<'PY'
import re, sys
journal, vue = sys.argv[1], sys.argv[2]
nom_vue = {"pianoroll": "piano roll", "arrangement": "arrangement"}[vue]
prises, notes, octaves, lues = [], 0, 0, 0
attente = []   # ce que le clavier a dit AVANT la ligne de la touche qui l'a déclenché
for ligne in open(journal, encoding="utf-8", errors="replace"):
    if ligne.startswith("VSM_CLAVIER : "):
        attente.append(ligne.split(" : ", 1)[1].split()[0])
        continue
    m = re.match(r"VSM_TOUCHE : « (.+?) » → focus (.+?) : (.*)$", ligne.rstrip("\n"))
    if not m:
        continue
    lues += 1
    touche, _, verdict = m.groups()
    if verdict == "prise par " + nom_vue:
        prises.append(touche.replace("#3b", ";"))
    notes += attente.count("note")
    octaves += attente.count("octave")
    attente = []
print(f"lues={lues} prises_par_la_vue={','.join(prises) or '-'} notes={notes} octaves={octaves}")
PY
}

rates=0
juger() {   # $1 vue ; $2 état ; $3 prises attendues ; $4 notes attendues ; $5 octaves attendus
    local ligne lues prises notes octaves
    ligne="$(course "$1" "$2")"
    lues="$(echo "$ligne" | grep -o 'lues=[^ ]*' | cut -d= -f2)"
    prises="$(echo "$ligne" | grep -o 'prises_par_la_vue=[^ ]*' | cut -d= -f2)"
    notes="$(echo "$ligne" | grep -o 'notes=[^ ]*' | cut -d= -f2)"
    octaves="$(echo "$ligne" | grep -o 'octaves=[^ ]*' | cut -d= -f2)"
    if [ "$lues" = "19" ] && [ "$prises" = "$3" ] && [ "$notes" = "$4" ] && [ "$octaves" = "$5" ]; then
        printf '  OK   %-12s clavier %-8s 19 touches : prises par la vue %s, %s notes, %s octaves\n' "$1" "$2" "$prises" "$notes" "$octaves"
    else
        printf '  RATÉ %-12s clavier %-8s %s touches lues : prises par la vue %s, %s notes, %s octaves (attendu 19, %s, %s, %s)\n' \
               "$1" "$2" "$lues" "$prises" "$notes" "$octaves" "$3" "$4" "$5"
        rates=$((rates + 1))
    fi
}

echo "=== D492 : actif, le clavier d'ordinateur prend ses dix-neuf touches ; inactif, elles restent des commandes ==="
juger pianoroll   actif   -     17 2
juger arrangement actif   -     17 2
juger pianoroll   inactif D,G   0  0
juger arrangement inactif A,F,G 0  0
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
