#!/usr/bin/env bash
# LA GARDE DE D521 : UNE RAFALE DE COMMANDE MIDI APPRISE, UN PAS D'ANNULATION.
#
# RÈGLE GARDÉE (30/09/2026). Une commande apprise règle ce que la souris règle aussi
# — volume, panoramique, muet, solo, départ, paramètre de machine —, et la souris
# ouvre un pas (D154, D425). La commande apprise n'en ouvrait aucun (D508) : ces
# réglages étaient MIXTES, et l'historique restaurant des instantanés entiers, le
# Ctrl+Z d'à côté défaisait le bouton physique avec le geste qu'il annonçait. Une
# rafale — les messages d'une même cible sans silence de plus de 500 ms — fait UN
# pas, au libellé du même geste à la souris ; le paramètre de machine est photographié
# à sa valeur d'AVANT la rafale (le thread MIDI l'a déjà réglé quand l'interface voit
# la commande). Le transport et la boucle ne font pas de pas (D0, D519).
#
# COMMENT. Une piste Minimoog ; les associations viennent des préférences du HOME
# (CC 20 → coupure, CC 21 → volume, CC 23 → boucle) et les contrôleurs entrent par
# `cc-entrant:` (D517). « Lente » est le geste d'AVANT : sa forme de fondus, lue dans
# le fichier enregistré après Ctrl+Z, dit s'il a été annulé.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/commandes-apprises.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-commandes-apprises.XXXXXX")"
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
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60 + i, 100])), (480, bytes([0x80, 60 + i, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "apprises", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY

lancer() {   # $1 nom ; $2 gestes
    local h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    mkdir -p "$h/VintageSynthMidiStudio"
    python3 - "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" <<'PY'
import json, sys
from xml.sax.saxutils import quoteattr
carte = {"format": "vsm.midilearn.v1", "mappings": [
    {"controller": 20, "kind": "instrumentParam", "track": 0, "param": 9, "min": 200.0, "max": 8000.0},
    {"controller": 21, "kind": "trackVolume", "track": 0, "min": 0.0, "max": 1.0},
    {"controller": 23, "kind": "transportLoop", "min": 0.0, "max": 1.0}]}
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="midiLearnMappings" val='
    + quoteattr(json.dumps(carte)) + '/>\n</PROPERTIES>\n')
PY
    env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet" VSM_VUE="sans-rapport" VSM_DELAI=4200 \
        VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$1.png" timeout 45 "$BIN" > "$brouillon/$1.txt" 2>&1
    # D147 : ce que l'application avertit se relaie AVANT de conclure.
    grep -E "VSM_(MENU|GESTE_APRES|TOUCHE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE|inconnu)|Associations MIDI" \
        "$brouillon/$1.txt" | sed 's/^/        journal : /' >&2
}
rafale() {   # $1 cc ; $2 premier instant (ms) ; valeurs… — un message toutes les 100 ms
    local cc="$1" t="$2" v g=""
    shift 2
    for v in "$@"; do g="$g$t:cc-entrant:$cc:$v;"; t=$((t + 100)); done
    printf '%s' "$g"
}
lire() {   # $1 dossier → « fondus|volume|coupure »
    python3 - "$1" <<'PY'
import json, sys
from pathlib import Path
d = Path(sys.argv[1])
try:
    p = json.loads((d / "project.json").read_text())
    c = json.loads((d / "instruments" / "track_00.synth.json").read_text())["parameters"]["filter.1.cutoff"]
except (OSError, KeyError):
    print("ABSENT"); sys.exit()
print(f"{p.get('crossfadeShape', 'defaut')}|{p['tracks'][0]['mix']['volume']:.3f}|{c:.1f}")
PY
}
pas_de() { grep -o "VSM_HISTORIQUE_PAS : [0-9]*" "$brouillon/$1.txt" | sed -n "${2}p" | cut -d' ' -f3; }
libelles() { grep "VSM_HISTORIQUE_PAS : " "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/VSM_HISTORIQUE_PAS : [0-9]* : //'; }

rates=0
ok()   { printf '  OK   %-50s %s\n' "$1" "$2"; }
rate() { printf '  RATÉ %-50s %s\n' "$1" "$2"; rates=$((rates + 1)); }
juger() { if [ "$2" = "$3" ]; then ok "$1" "$2"; else rate "$1" "« $2 » (attendu : « $3 »)"; fi; }

echo "=== D521 : une rafale de commande apprise, un pas ==="
# A. LE VOLUME : Lente, une rafale de CC 21, Ctrl+Z.
lancer a "400:menu:Lente;$(rafale 21 700 40 50 60 70 80)1500:relever-historique;1700:enregistrer:$brouillon/a0;2000:touche:ctrl + Z;2300:enregistrer:$brouillon/a"
IFS='|' read -r f0 v0 _ <<< "$(lire "$brouillon/a0")"
IFS='|' read -r f v _ <<< "$(lire "$brouillon/a")"
juger "A. la rafale a réglé le volume (le témoin)" "$v0" "0.630"
juger "A. l'historique après la rafale" "$(pas_de a 1) pas : $(libelles a 1)" "2 pas : Forme des fondus croisés | Volume — une"
juger "A. Ctrl+Z : « Lente » gardée (un pas, pas deux)" "$f" "slow"
juger "A. Ctrl+Z : le volume d'avant la rafale" "$v" "1.000"

# B. LE PARAMÈTRE DE MACHINE : la coupure, réglée par le thread MIDI.
lancer b "400:menu:Lente;$(rafale 20 700 40 50 60 70 80)1500:relever-historique;1700:enregistrer:$brouillon/b0;2000:touche:ctrl + Z;2300:enregistrer:$brouillon/b"
IFS='|' read -r _ _ c0 <<< "$(lire "$brouillon/b0")"
IFS='|' read -r f _ c <<< "$(lire "$brouillon/b")"
juger "B. la rafale a réglé la coupure (le témoin)" "$c0" "5113.4"
juger "B. l'historique après la rafale" "$(pas_de b 1) pas : $(libelles b 1)" "2 pas : Forme des fondus croisés | Réglage de machine"
juger "B. Ctrl+Z : « Lente » gardée (un pas, pas deux)" "$f" "slow"
juger "B. Ctrl+Z : la coupure d'avant la rafale" "$c" "1200.0"

# B2. LA RAFALE SEULE : son Ctrl+Z doit rendre la coupure d'AVANT la rafale (1200), et
# non celle du premier message (2656,7 : 200 + 7800 × 40/127), que le thread MIDI a
# déjà réglée quand l'interface ouvre le pas. Sans geste d'avant, rien d'autre ne
# peut rendre 1200.
lancer b2 "$(rafale 20 400 40 50 60 70 80)1200:touche:ctrl + Z;1500:enregistrer:$brouillon/b2"
IFS='|' read -r _ _ c <<< "$(lire "$brouillon/b2")"
juger "B2. la rafale seule, puis Ctrl+Z : la coupure" "$c" "1200.0"

# C. DEUX RAFALES SÉPARÉES D'UN SILENCE : deux pas.
lancer c "$(rafale 21 400 40 50 60)$(rafale 21 1900 90 100)2600:relever-historique;2900:touche:ctrl + Z;3200:enregistrer:$brouillon/c"
IFS='|' read -r _ v _ <<< "$(lire "$brouillon/c")"
juger "C. deux rafales, 1,2 s de silence : les pas" "$(pas_de c 1)" "2"
juger "C. Ctrl+Z : le volume d'après la PREMIÈRE rafale" "$v" "0.472"

# D. CONTRÔLE : la boucle apprise est une bascule — pas de pas, la marque (D519).
lancer d "400:relever-titre;700:cc-entrant:23:127;1000:relever-historique;1300:relever-titre"
mapfile -t marques < <(grep -o "non enregistre : [a-z]*" "$brouillon/d.txt" | cut -d' ' -f4)
juger "D. CC 23 (boucle) : les pas" "$(pas_de d 1)" "0"
juger "D. CC 23 (boucle) : la marque" "${marques[0]:-?} ${marques[1]:-?}" "non oui"

# E. LA MARQUE SUIT L'HISTORIQUE : la rafale défaite, rien d'autre — « enregistré ».
lancer e "$(rafale 21 400 40 50 60 70 80)1200:relever-titre;1500:touche:ctrl + Z;1800:relever-titre"
mapfile -t marques < <(grep -o "non enregistre : [a-z]*" "$brouillon/e.txt" | cut -d' ' -f4)
juger "E. une rafale, puis Ctrl+Z : la marque" "${marques[0]:-?} ${marques[1]:-?}" "oui non"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
