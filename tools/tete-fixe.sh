#!/usr/bin/env bash
# D569.2 — LA TÊTE FIXE (Affichage ▸ Tête fixe (la vue défile sous elle) ; le *Stationary Cursor* de Cubase), MESURÉE
# PAR LE DÉFILEMENT RELEVÉ EN LECTURE (`relever-defilement`), LE TÉMOIN À PAGES À CÔTÉ.
#
# LES RÈGLES GARDÉES. Trente-deux mesures d'une note ; l'arrangement zoomé trois crans (la fenêtre ne tient plus le
# morceau : 17 mesures sur 32) ; la tête posée à la mesure 12, passé le milieu ; la lecture lancée à 1 s ; quatre
# relevés à 2,0 / 2,5 / 3,0 / 3,5 s.
#   (1) tête fixe : le défilement AVANCE à chaque relevé, et la tête reste à la même distance du bord (± 120 ticks : un
#       tour de minuterie sépare le relevé de la tête de celui de la vue) ;
#   (2) le témoin, à pages (D11.3) : deux relevés consécutifs au moins gardent le MÊME défilement, la tête ayant avancé ;
#   (3) arrêtée, la tête fixe ne bouge rien : défilement 0 aux quatre relevés ;
#   (4) la case est RETENUE : cochée, le lancement suivant sous le même HOME la retrouve (« tête fixe » au relevé).
#
#   tools/tete-fixe.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-fixe.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + bytes([0x00, 0x90, 48, 80]) + vlq(32 * 1920) + bytes([0x80, 48, 0])
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.1}}]},
          open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" \
        VSM_TOUCHE="arrangement:=;arrangement:=;arrangement:=" VSM_ARRANGEMENT=1 VSM_POSITION=12 VSM_DELAI=4200 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|LECTURE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releves() {   # « défilement:tête » de chaque relevé
    grep -h "^VSM_DEFILEMENT : " "$brouillon/$1.txt" | sed 's/^VSM_DEFILEMENT : arrangement \([0-9-]*\), piano roll [0-9-]*, tete \([0-9-]*\).*/\1:\2/' | tr '\n' ' ' | sed 's/ $//'
}
FIXE="300:menu:Affichage > Tête fixe (la vue défile sous elle)"
RELEVES="2000:relever-defilement;2500:relever-defilement;3000:relever-defilement;3500:relever-defilement"

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D569.2 : la tête fixe ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course fixe "$maison" VSM_LECTURE=1000 VSM_GESTE_APRES="$FIXE;$RELEVES"
course pages "$(mktemp -d "$brouillon/home.XXXX")" VSM_LECTURE=1000 VSM_GESTE_APRES="$RELEVES"
course arretee "$(mktemp -d "$brouillon/home.XXXX")" VSM_GESTE_APRES="$FIXE;$RELEVES"
course retenue "$maison" VSM_DELAI=1500 VSM_TOUCHE="" VSM_GESTE_APRES="800:relever-arrangement"
rf="$(releves fixe)"; rp="$(releves pages)"; ra="$(releves arretee)"
retenue="$(grep -h "^VSM_ARRANGEMENT : aimant" "$brouillon/retenue.txt" | tail -1)"
echo "       tête fixe (défilement:tête) : $rf"
echo "       pages                       : $rp"
echo "       arrêtée                     : $ra"
echo "       relancé : $retenue"

verdict "(1) tête fixe : la vue avance à chaque relevé, la tête à distance constante du bord" \
    "$(python3 -c "
import sys
r = [tuple(map(int, x.split(':'))) for x in sys.argv[1].split()]
ok = len(r) == 4 and all(b[0] > a[0] for a, b in zip(r, r[1:])) and max(t - s for s, t in r) - min(t - s for s, t in r) <= 120
print(1 if ok else 0)" "$rf" 2>/dev/null || echo 0)"
verdict "(2) le témoin à pages : deux relevés consécutifs gardent le même défilement, la tête ayant avancé" \
    "$(python3 -c "
import sys
r = [tuple(map(int, x.split(':'))) for x in sys.argv[1].split()]
print(1 if len(r) == 4 and any(a[0] == b[0] and b[1] > a[1] for a, b in zip(r, r[1:])) else 0)" "$rp" 2>/dev/null || echo 0)"
verdict "(3) arrêtée : la tête fixe ne bouge rien" \
    "$(python3 -c "
import sys
r = [tuple(map(int, x.split(':'))) for x in sys.argv[1].split()]
print(1 if len(r) == 4 and all(s == 0 for s, t in r) else 0)" "$ra" 2>/dev/null || echo 0)"
verdict "(4) retenue : le lancement suivant la retrouve" "$(echo "$retenue" | grep -q ", tête fixe" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-FIXE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-FIXE : en lecture, la vue défile sous une tête qui reste au milieu ; arrêtée, rien ne bouge"
