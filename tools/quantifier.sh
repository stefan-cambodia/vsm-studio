#!/usr/bin/env bash
# La garde de D354 : LA QUANTIFICATION DÉPLACE LES NOTES, ET CE QUI NE LA FAIT PAS LE DIT.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. huit notes posées HORS grille (décalées de -23 à +29 ticks) tombent
#      exactement sur la grille 1/16 après « Quantifier (100 %) » — vérifié sur
#      le **fichier MIDI exporté**, pas sur l'écran ;
#   2. la quantification DIT ce qu'elle a fait (« 8 note(s) quantifiées ») et,
#      quand elle ne fait rien, DIT POURQUOI — quatre sorties anticipées étaient
#      muettes, et c'est le logiciel qu'on soupçonnait ;
#   3. un bouton GRISÉ n'est pas « cliqué » : `cliquer:` le refuse et le dit
#      (`VSM_CLIC : … — GRISÉ`), au lieu d'écrire « cliqué » sur un geste qui
#      n'a pas eu lieu ;
#   4. un geste DIFFÉRÉ (`VSM_GESTE_APRES`) combiné à un verbe d'export
#      AVERTIT : l'export écrit au démarrage, donc avant le geste ;
#   5. une entrée de MENU grisée est dite « GRISÉE », et non « aucune entrée » —
#      la seconde envoie chercher une faute d'orthographe pour rien ;
#   6. `VSM_TOUCHE=pianoroll:…` atteint le clavier DU PIANO ROLL : sans cible,
#      un banc ne voyait que les commandes globales et lisait « touche inconnue »
#      sur des raccourcis qui existent.
#
# POURQUOI CES SIX-LÀ ENSEMBLE. Une seule mesure les a tous fait apparaître :
# « Quantifier » pressé au banc laissait le .mid inchangé, et il a fallu trois
# essais pour comprendre que la quantification était JUSTE — le premier clic
# arrivait avant la sélection (`VSM_GESTE_PISTE` passe avant
# `VSM_MENU_CONTEXTE`), le deuxième tapait sur un bouton grisé sans le dire, et
# le troisième arrivait après l'export. Trois pannes muettes pour un logiciel
# correct.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire ou mido manquent.
#
#   tools/quantifier.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="analyse/.venv/bin/python"
[ -x "$PY" ] || PY=python3
"$PY" -c "import mido" 2>/dev/null || { echo "REFUS : mido est requis pour relire le .mid"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-quantifier.XXXXXX")"
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
# HUIT NOTES HORS GRILLE, décalages choisis des deux côtés de la case : une
# quantification qui ne ferait « que descendre » passerait un test à sens unique.
decalages = [17, -23, 11, 29, -14, 7, 22, -9]
evs = [(0, b"\xff\x03\x04Lead"), (0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
t = 0
for i, dec in enumerate(decalages):
    cible = i * 240 + dec
    evs += [(max(0, cible - t), bytes([0x90, 60 + i, 100])), (100, bytes([0x80, 60 + i, 0]))]
    t = cible + 100
corps = b"".join(vlq(max(0, dt)) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "quant",
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

ticks() {   # les ticks des note_on d'un .mid
    "$PY" - "$1" <<'PY'
import sys
import mido
d = []
for tr in mido.MidiFile(sys.argv[1]).tracks:
    t = 0
    for e in tr:
        t += e.time
        if e.type == "note_on" and e.velocity > 0:
            d.append(t)
print(" ".join(str(x) for x in d))
PY
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D354 : la quantification, et ce qui ne la fait pas ==="

# (1) LE GESTE, PAR LE MENU DU CLIC DROIT — le seul chemin qui agit AVANT
# l'export du démarrage.
maison="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,pianoroll" \
    VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner;pianoroll:Quantifier (100 %)" \
    VSM_EXPORT_MIDI="$brouillon/apres.mid" VSM_CAPTURE="$brouillon/apres.png" \
    timeout 45 "$BIN" > "$brouillon/apres.txt" 2>&1
apres="$(ticks "$brouillon/apres.mid")"
echo "       ticks après quantification : $apres"
hors="$("$PY" -c "
import sys
print(sum(1 for x in '''$apres'''.split() if int(x) % 120 != 0))
")"
verdict "les huit notes tombent sur la grille 1/16 (hors grille : $hors)" \
        "$([ "$hors" = "0" ] && echo 1 || echo 0)"
verdict "la quantification DIT ce qu'elle a fait" \
        "$(grep -c 'VSM_QUANTIFIER : 8 note(s) quantifiées' "$brouillon/apres.txt")"

# (2) SANS SÉLECTION, ELLE DIT POURQUOI ELLE NE FAIT RIEN — par le RACCOURCI,
# seul chemin qui atteigne vraiment `quantizeSelection` sans sélection : l'entrée
# de menu, elle, est grisée et n'appelle rien (c'est le cas (2b)).
maison2="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison2" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,pianoroll" VSM_TOUCHE="pianoroll:ctrl + Q" \
    VSM_EXPORT_MIDI="$brouillon/sans.mid" VSM_CAPTURE="$brouillon/sans.png" \
    timeout 45 "$BIN" > "$brouillon/sans.txt" 2>&1
verdict "sans sélection, elle dit pourquoi elle ne fait rien" \
        "$(grep -c 'VSM_QUANTIFIER : rien fait' "$brouillon/sans.txt")"
verdict "la touche atteint le clavier DU PIANO ROLL (et le dit)" \
        "$(grep -c 'VSM_TOUCHE : « ctrl + Q » → pianoroll : prise' "$brouillon/sans.txt")"
sans="$(ticks "$brouillon/sans.mid")"
# LE TÉMOIN EST L'ORIGINAL, PAS L'AUTRE COURSE : « différent de la course
# quantifiée » serait vrai même si la touche avait tout déplacé ailleurs.
origine="$(ticks "$brouillon/projet/midi/arrangement.mid")"
verdict "et elle ne touche à rien (ticks identiques à l'original)" \
        "$([ "$sans" = "$origine" ] && echo 1 || echo 0)"

# (2b) UNE ENTRÉE DE MENU GRISÉE N'EST PAS UNE ENTRÉE ABSENTE. Sans sélection,
# « Quantifier (100 %) » est grisée : le journal disait « aucune entrée », ce qui
# envoie chercher une faute d'orthographe au lieu de la cause.
maison2b="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison2b" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,pianoroll" VSM_MENU_CONTEXTE="pianoroll:Quantifier (100 %)" \
    VSM_CAPTURE="$brouillon/grisee.png" \
    timeout 45 "$BIN" > "$brouillon/grisee.txt" 2>&1
verdict "une entrée de menu grisée est dite GRISÉE, pas « aucune entrée »" \
        "$(grep -c 'est GRISÉE — présente dans le menu' "$brouillon/grisee.txt")"

# (3) UN BOUTON GRISÉ N'EST PAS « CLIQUÉ », et (4) le geste différé avertit.
maison3="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison3" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=3000 \
    VSM_VUE="sans-rapport,pianoroll" VSM_GESTE_APRES="1500:cliquer:Quantifier" \
    VSM_EXPORT_MIDI="$brouillon/grise.mid" VSM_CAPTURE="$brouillon/grise.png" \
    timeout 45 "$BIN" > "$brouillon/grise.txt" 2>&1
verdict "un bouton grisé est REFUSÉ et dit, pas « cliqué »" \
        "$(grep -c 'VSM_CLIC : Quantifier — GRISÉ' "$brouillon/grise.txt")"
verdict "un geste différé avec un export AVERTIT (l'export écrit au démarrage)" \
        "$(grep -c 'VSM_GESTE_APRES : ATTENTION — VSM_EXPORT_MIDI écrit AU DÉMARRAGE' "$brouillon/grise.txt")"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
