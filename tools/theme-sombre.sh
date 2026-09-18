#!/usr/bin/env bash
# La garde de D347 : AUCUNE SURFACE CLAIRE DANS UNE APPLICATION SOMBRE.
#
# RÈGLE GARDÉE (18/09/2026). Un composant JUCE qu'on oublie de colorer garde le
# gris clair de `LookAndFeel_V4`. Cela ne casse rien, ne fait échouer aucun test,
# et ne se voit que sur une photo : l'en-tête du tableau d'événements est resté
# ainsi pendant toute la vie du logiciel — **2 095 × 22 px à 164 de luminance**,
# la seule surface claire de l'application.
#
# La garde photographie les six onglets du dock du bas sous les deux vues du
# centre, et passe chaque image à `tools/surfaces-claires.py`, qui cherche une
# SUITE CONTIGUË de pixels clairs et peu saturés (le texte clair, lui, ne donne
# que des suites de quelques pixels ; les clips et les accents sont saturés).
#
# Rend 0 si aucune surface claire, 1 sinon, 2 si le binaire manque.
#
#   tools/theme-sombre.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
python3 -c "import numpy, PIL" 2>/dev/null || { echo "REFUS : numpy et Pillow sont requis"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-theme-sombre.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# UN PROJET ENGENDRÉ : la garde ne dépend d'aucune donnée du poste, et les
# quelques notes suffisent à remplir la liste d'événements et les lanes.
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
for i in range(24):
    h = 48 + (i % 12)
    evs += [(0, bytes([0x90, h, 64 + i])), (240, bytes([0x80, h, 0]))]
    evs += [(0, bytes([0xb0, 74, 20 + i * 3])), (0, b"")] if False else []
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "theme",
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
echo "=== D347 : aucune surface claire dans une application sombre ==="
for vue in "arrangement,mixer" "arrangement,liste" "arrangement,automation" \
           "arrangement,midi-cc" "arrangement,tempo" "arrangement,effets" \
           "pianoroll,liste" "pianoroll,mixer"; do
    nom="$(tr -c 'a-z0-9' '-' <<<"$vue")"
    # UNE PHOTO QUI NE VIENT PAS N'EST PAS UN DÉFAUT DE THÈME : elle se
    # relance. Enchaînée derrière d'autres gardes qui lancent l'application,
    # celle-ci a raté une capture sur dix courses — et la garde a alors accusé
    # le thème. C'est la leçon de D72 et D91 (une absence sur une photo ne
    # prouve rien) appliquée à une garde plutôt qu'à une boîte.
    for essai in 1 2; do
        maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
        env VSM_TAILLE="1280x742" HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
            VSM_VUE="sans-rapport,$vue" VSM_CAPTURE="$brouillon/$nom.png" \
            timeout 45 "$BIN" > "$brouillon/$nom.txt" 2>&1
        [ -f "$brouillon/$nom.png" ] && break
    done
    if [ ! -f "$brouillon/$nom.png" ]; then
        verdict "$vue : photo prise (deux essais)" 0
        continue
    fi
    sortie="$(python3 tools/surfaces-claires.py "$brouillon/$nom.png")"
    compte="$(sed -n 's/^SURFACES_CLAIRES \([0-9]*\)$/\1/p' <<<"$sortie")"
    verdict "$vue : $compte surface(s) claire(s)" "$([ "${compte:-1}" = "0" ] && echo 1 || echo 0)"
    [ "${compte:-1}" = "0" ] || grep '^SURFACE CLAIRE' <<<"$sortie" | sed 's/^/       /'
done

# D348 : ET LES FENÊTRES QUI NE SONT PAS LA FENÊTRE SOCLE. `VSM_CAPTURE` ne
# photographie que celle-ci ; les panneaux flottants et les boîtes de dialogue
# sont des fenêtres à part, que `VSM_CAPTURE_PANNEAUX` écrit une par une. Sans
# cette course, le balayage ne couvrait que le dock — c'est-à-dire l'endroit où
# le défaut avait été trouvé, et nulle part ailleurs.
# LES BOÎTES MODALES N'EN SONT PAS, ET C'EST MESURÉ DEPUIS D95 : « une boîte
# modale demandée au démarrage d'un banc n'est plus là au moment de la photo »
# — une photo sur sept en D72, AUCUNE sous un écran verrouillé en D95. Mesuré de
# nouveau ici : 0 fenêtre photographiée sur 5 courses avec `VSM_BOITE_ESSAI`.
# C'est pour cela que `VSM_BOITE` existe. Le fond d'une boîte reste donc hors de
# ce balayage, et le dire vaut mieux que de garder un verdict qui ne peut pas
# être vert.
#
# ET LA PREMIÈRE VERSION DE CETTE GARDE PASSAIT À VIDE : `wc -l <<<"$images"`
# rend **1** sur une chaîne vide, si bien que « 1 fenêtre(s), 0 claire(s) »
# s'affichait alors qu'aucune image n'avait été écrite. Un compte se vérifie sur
# le cas vide avant de servir de verdict.
for cas in "historique,spectre,ordre,prises|" "navigateur|"; do
    vues="${cas%%|*}"
    boite="${cas##*|}"
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    nom="pan-$(tr -c 'a-z0-9' '-' <<<"$vues$boite")"
    # TROIS ESSAIS ICI, ET NON DEUX : une boîte modale demandée par
    # `VSM_BOITE_ESSAI` ne s'ouvre pas à tous les coups (D72, D91 — « une boîte
    # absente d'une photo ne prouve rien, relancer avant de conclure »), et son
    # absence n'est pas un défaut de thème.
    for essai in 1 2 3; do
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env VSM_TAILLE="1280x742" HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_DELAI=3000 \
            VSM_VUE="sans-rapport,arrangement,$vues" VSM_BOITE_ESSAI="$boite" \
            VSM_CAPTURE_PANNEAUX=1 VSM_CAPTURE="$brouillon/$nom.png" \
            timeout 45 "$BIN" > "$brouillon/$nom.txt" 2>&1
        grep -q '^VSM_CAPTURE_PANNEAUX : ' "$brouillon/$nom.txt" && break
    done
    images="$(sed -n 's/^VSM_CAPTURE_PANNEAUX : //p' "$brouillon/$nom.txt")"
    if [ -z "$images" ]; then
        verdict "$vues${boite:+ + boîte « $boite »} : au moins une fenêtre photographiée" 0
        continue
    fi
    mauvaises=0
    while IFS= read -r img; do
        [ -f "$img" ] || continue
        c="$(python3 tools/surfaces-claires.py "$img" | sed -n 's/^SURFACES_CLAIRES \([0-9]*\)$/\1/p')"
        [ "${c:-1}" = "0" ] || { mauvaises=$((mauvaises + 1)); echo "       $(basename "$img") : $c surface(s)"; }
    done <<<"$images"
    verdict "$vues${boite:+ + boîte « $boite »} : $(grep -c . <<<"$images") fenêtre(s), $mauvaises claire(s)" \
            "$([ "$mauvaises" -eq 0 ] && echo 1 || echo 0)"
done

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
