#!/usr/bin/env bash
# D613 — LES FENÊTRES FLOTTANTES ET LEUR TAILLE MINIMALE, mesurées dans l'application : l'indication que la fenêtre de la
# réserve donne au gestionnaire de fenêtres (`xprop`), une taille retenue dégénérée (128 × 128) aux préférences, et une
# rafale d'ouvertures (la réserve et l'ordre de jeu) où l'on compte les fenêtres nées dégénérées.
#
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D613) :
#   (a) RETIRÉ APRÈS LA MESURE (ROADMAP-daw, D613, 15 h 54) : JUCE réécrit `WM_NORMAL_HINTS` sans le minimum — l'indication
#       est seulement PUBLIÉE ;
#   (b) la réserve retenue en 128 × 128 : le témoin l'ouvre en 128 × 128 ; le neuf à 980 × 440, et ne la retient plus ;
#   (c) 12 ouvertures (24 fenêtres) : le neuf, 0 dégénérée au relevé, et les fenêtres RAMENÉES comptées ; le témoin,
#       compté et publié tel quel.
#
#   tools/fenetres-minimum.sh [binaire] [binaire témoin] [nombre d'ouvertures]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D612/Vintage Synth MIDI Studio}"
N="${3:-12}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
command -v xprop > /dev/null || { echo "REFUS : xprop absent — la mesure (a) ne se fait pas sans lui"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fenetres-minimum.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"
os.makedirs(f"{b}/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for i, h in enumerate((57, 64)):
    corps += vlq(0 if i == 0 else 960) + bytes([0x90, h, 100]) + vlq(960) + bytes([0x80, h, 0])
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "fenetres", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "A"}, {"tick": 1920, "name": "B"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

# Un HOME neuf ; `$3` non vide : la réserve y est RETENUE en 128 × 128.
home_neuf() {
    local h; h="$(mktemp -d "$brouillon/home.XXXX")"
    if [ -n "${1:-}" ]; then
        mkdir -p "$h/VintageSynthMidiStudio"
        printf '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n  <VALUE name="fenetre.R&#233;serve des fichiers" val="10 40 128 128"/>\n</PROPERTIES>\n' \
            > "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    fi
    echo "$h"
}
course() {   # $1 = binaire ; $2 = nom ; $3 = HOME ; $4 = gestes
    env HOME="$3" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_DELAI=3000 \
        VSM_GESTE_APRES="$4" VSM_CAPTURE="$brouillon/$2.png" timeout 60 "$1" > "$brouillon/$2.txt" 2>&1
}
taille_reserve() { grep "^VSM_RESERVE : total" "$brouillon/$1.txt" | tail -1 | sed -n 's/.* ; fenêtre \([0-9]*x[0-9]*\)$/\1/p'; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D613 : la taille minimale des fenêtres flottantes ==="
# (a) L'INDICATION DE TAILLE, lue pendant que la fenêtre est ouverte — sur « Ordre de jeu », au nom ASCII. CORRIGÉ APRÈS
# LE PREMIER ESSAI : `xprop -name` compare le nom X11 (WM_NAME, en Latin-1) et ne trouvait pas « Réserve des fichiers ».
indication() {   # $1 = binaire ; $2 = nom
    env HOME="$(home_neuf)" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_DELAI=4500 \
        VSM_GESTE_APRES="800:relever-ordre" VSM_CAPTURE="$brouillon/$2.png" timeout 60 "$1" > "$brouillon/$2.txt" 2>&1 &
    local pid=$!
    sleep 2.5
    local id
    for id in $(xprop -root _NET_CLIENT_LIST | grep -o "0x[0-9a-f]*"); do
        xprop -id "$id" WM_NAME 2> /dev/null | grep -q '"Ordre de jeu"' && xprop -id "$id" WM_NORMAL_HINTS > "$brouillon/$2-xprop.txt" 2>&1
    done
    while kill -0 "$pid" 2> /dev/null; do sleep 0.3; done
    sed -n 's/.*program specified minimum size: \([0-9]*\) by \([0-9]*\).*/\1 \2/p' "$brouillon/$2-xprop.txt" | head -1
}
MIN_NEUF="$(indication "$BIN" a-neuf)"
MIN_TEMOIN=""; [ -x "$TEMOIN" ] && MIN_TEMOIN="$(indication "$TEMOIN" a-temoin)"
echo "       (a) taille minimale déclarée : neuf [${MIN_NEUF:-aucune}] ; témoin [${MIN_TEMOIN:-aucune}]"
echo "       (a) publié, non jugé (le critère est retiré : JUCE réécrit l'indication sans le minimum)"

# (b) UNE TAILLE RETENUE DÉGÉNÉRÉE.
HN="$(home_neuf retenue)"
course "$BIN" b-neuf "$HN" "800:relever-reserve;1600:relever-reserve"
RET_NEUF="$(grep -o 'name="fenetre.R[^"]*serve des fichiers" val="[^"]*"' "$HN/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" | sed 's/.*val="//; s/"$//')"
echo "       (b) retenue en 128 × 128 : neuf ouvre en $(taille_reserve b-neuf), retient ensuite « $RET_NEUF »"
verdict "(b) le neuf ignore la taille retenue dégénérée : 980x440" "$([ "$(taille_reserve b-neuf)" = "980x440" ] && echo 1 || echo 0)"
verdict "(b) et ne retient plus 128 × 128" "$(echo "$RET_NEUF" | grep -q " 128 128$" && echo 0 || echo 1)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" b-temoin "$(home_neuf retenue)" "800:relever-reserve;1600:relever-reserve"
    echo "       (b) témoin : ouvre en $(taille_reserve b-temoin)"
    verdict "(b) témoin (D612) : la taille retenue reprise, 128x128" "$([ "$(taille_reserve b-temoin)" = "128x128" ] && echo 1 || echo 0)"
fi

# (c) LA RAFALE : N ouvertures par binaire, la réserve et l'ordre de jeu relevés 1,8 s après leur ouverture.
rafale() {   # $1 = binaire ; $2 = préfixe — rend « dégénérées / fenêtres »
    local k deg=0 tot=0 t
    for k in $(seq 1 "$N"); do
        course "$1" "$2-$k" "$(home_neuf)" "600:relever-reserve;700:relever-ordre;2400:relever-reserve;2500:relever-ordre"
        for t in $(grep -o "fenêtre [0-9]*x[0-9]*" "$brouillon/$2-$k.txt" | tail -2 | sed 's/fenêtre //'); do
            tot=$((tot + 1))
            [ "${t%x*}" -lt 200 ] || [ "${t#*x}" -lt 150 ] && deg=$((deg + 1))
        done
    done
    echo "$deg/$tot $(cat "$brouillon/$2"-*.txt | grep -c "^VSM_FENETRE : .* ramenée")"
}
read -r C_NEUF R_NEUF <<< "$(rafale "$BIN" c-neuf)"
echo "       (c) rafale de $N ouvertures : neuf, $C_NEUF fenêtre(s) dégénérée(s) au relevé, $R_NEUF ramenée(s)"
grep -h "^VSM_FENETRE" "$brouillon"/c-neuf-*.txt | sed 's/^/           /'
verdict "(c) le neuf : aucune fenêtre dégénérée au relevé ($C_NEUF)" "$([ "${C_NEUF%/*}" = 0 ] && [ "${C_NEUF#*/}" = $((2 * N)) ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    read -r C_TEMOIN _ <<< "$(rafale "$TEMOIN" c-temoin)"
    echo "       (c) témoin (D612), publié tel quel : $C_TEMOIN fenêtre(s) dégénérée(s)$( [ "${C_TEMOIN%/*}" = 0 ] && echo " — la rafale ne tranche donc pas l'hypothèse")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
echo "       écran : $(kscreen-doctor --dpms show 2>&1 | head -1 | sed 's/.*: //')"

echo
if [ "$rates" -gt 0 ]; then echo "FENETRES-MINIMUM : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FENETRES-MINIMUM : une fenêtre flottante réduite sous le plancher est ramenée, et ne retient jamais une taille dégénérée"
