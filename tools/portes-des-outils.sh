#!/usr/bin/env bash
# LA GARDE DE D497 : LES OUTILS ET LE ZOOM ± DU PIANO ROLL, AU MENU ÉDITION ET À LA
# TOUCHE.
#
# RÈGLE GARDÉE (29/09/2026) : chaque entrée « Édition > Outils > … », « Zoom avant »
# et « Zoom arrière » rend le MÊME état que la touche de la même commande dans le
# piano roll (`performShortcut`). Les deux appellent `setTool` / `zoomHorizontally`.
#
# COMMENT. Un projet de trois pistes est engendré ; chaque cas se joue sous un HOME
# neuf (D318) : par `VSM_MENU` (la porte) et par `VSM_TOUCHE=pianoroll:<touche>` (la
# touche, par `keyPressed` du piano roll). Le TÉMOIN est une course sans geste. L'état
# se relit sur `VSM_PIANOROLL_RANG` (`outil=`, `zoom=`), écrit après les gestes.
#
# CHAQUE CAS PORTE SON TÉMOIN (D145) : menu = touche ≠ témoin. « Sélection » est
# l'outil du démarrage : elle se juge depuis « Crayon », dont la course sert alors
# de témoin.
#
# ET LES DEUX LIBELLÉS QUI RESSEMBLENT À D'AUTRES : `VSM_MENU` prend le PREMIER
# libellé exact (le piège de « Automatique », 06/09). « Muet » doit atteindre
# l'outil (menu Édition), « Muet (piste choisie) » la piste (menu Piste).
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/portes-des-outils.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-portes-outils.XXXXXX")"
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
json.dump({"format": "vsm-project", "version": 1, "title": "outils",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("une"), t("deux"), t("trois")]},
          open(d + "/project.json", "w"), indent=1)
PY

# UN NOM DE FICHIER PAR COURSE (et non un compteur : `course` est appelée dans un
# `$(…)`, dont un compteur ne sortirait pas — D495).
course() {   # $1 = nom ; $2 = clé (outil|zoom) ; $3... = variables -> la valeur relue
    local nom="$1" cle="$2" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_DELAI=800 \
        VSM_VUE="sans-rapport" VSM_PIANOROLL_ZONES=1 VSM_CAPTURE="$brouillon/$nom.png" "${@:3}" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147).
    grep -E "VSM_(MENU|TOUCHE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)" "$brouillon/$nom.txt" | sed 's/^/        journal : /' >&2
    grep "VSM_PIANOROLL_RANG" "$brouillon/$nom.txt" | tail -1 | grep -o " $cle=[^ ]*" | cut -d= -f2
}

rates=0
cas() {   # $1 nom ; $2 clé ; $3 témoin ; $4 menu ; $5 touches ; $6 attendu (« ≠ » : différent du témoin)
    local nom="$1" cle="$2" temoin="$3" porte touche bon=0
    porte="$(course "$nom-menu" "$cle" VSM_MENU="$4")"
    touche="$(course "$nom-touche" "$cle" VSM_TOUCHE="$5")"
    if [ "$6" = "≠" ]; then
        [ -n "$porte" ] && [ "$porte" = "$touche" ] && [ "$porte" != "$temoin" ] && bon=1
    else
        [ "$porte" = "$6" ] && [ "$touche" = "$6" ] && [ "$temoin" != "$6" ] && bon=1
    fi
    if [ "$bon" -eq 1 ]; then
        printf '  OK   %-14s %s : témoin %s, menu %s, touche %s\n' "$nom" "$cle" "$temoin" "$porte" "$touche"
    else
        printf '  RATÉ %-14s %s : témoin %s, menu %s, touche %s (attendu %s)\n' "$nom" "$cle" "$temoin" "$porte" "$touche" "$6"
        rates=$((rates + 1))
    fi
}

echo "=== D497 : les outils et le zoom ± du piano roll, au menu Édition et à la touche ==="
outil0="$(course temoin outil)"
zoom0="$(course temoin-zoom zoom)"
cas crayon   outil "$outil0" "Crayon"   "pianoroll:2" crayon
crayon="$(course crayon-seul outil VSM_MENU="Crayon")"
cas selection outil "$crayon" "Crayon;Sélection" "pianoroll:2;pianoroll:1" selection
cas gomme    outil "$outil0" "Gomme"    "pianoroll:3" gomme
cas ciseaux  outil "$outil0" "Ciseaux"  "pianoroll:4" ciseaux
cas colle    outil "$outil0" "Colle"    "pianoroll:5" colle
cas muet     outil "$outil0" "Muet"     "pianoroll:6" muet
cas zoom-avant   zoom "$zoom0" "Zoom avant"   "pianoroll:=" "≠"
cas zoom-arriere zoom "$zoom0" "Zoom arrière" "pianoroll:-" "≠"
# ET LE ZOOM VA DANS LE BON SENS : « avant » agrandit, « arrière » réduit.
za="$(grep "VSM_PIANOROLL_RANG" "$brouillon/zoom-avant-menu.txt" | tail -1 | grep -o " zoom=[^ ]*" | cut -d= -f2)"
zr="$(grep "VSM_PIANOROLL_RANG" "$brouillon/zoom-arriere-menu.txt" | tail -1 | grep -o " zoom=[^ ]*" | cut -d= -f2)"
if python3 -c "import sys; a,t,r=map(float,sys.argv[1:]); sys.exit(0 if a>t>r else 1)" "${za:-0}" "${zoom0:-0}" "${zr:-0}"; then
    printf '  OK   %-14s zoom : arrière %s < témoin %s < avant %s\n' "sens" "$zr" "$zoom0" "$za"
else
    printf '  RATÉ %-14s zoom : arrière %s, témoin %s, avant %s (attendu arrière < témoin < avant)\n' "sens" "$zr" "$zoom0" "$za"
    rates=$((rates + 1))
fi
# LES DEUX « MUET » SE DÉPARTAGENT.
m1="$(course muet-outil outil VSM_MENU="Muet")"
grep -q "« Muet » exécutée (menu Édition)" "$brouillon/muet-outil.txt" && [ "$m1" = "muet" ] && ok=1 || ok=0
course muet-piste outil VSM_MENU="Muet (piste choisie)" > /dev/null
grep -q "« Muet (piste choisie) » exécutée (menu Piste)" "$brouillon/muet-piste.txt" && ok2=1 || ok2=0
if [ "$ok" -eq 1 ] && [ "$ok2" -eq 1 ]; then
    printf '  OK   %-14s « Muet » → l’outil (menu Édition) ; « Muet (piste choisie) » → la piste (menu Piste)\n' "deux-muet"
else
    printf '  RATÉ %-14s « Muet » → %s ; « Muet (piste choisie) » : %s\n' "deux-muet" \
           "$(grep -o "« Muet[^»]*» [a-zé]* (menu [^)]*)" "$brouillon/muet-outil.txt" | head -1)" \
           "$(grep -o "« Muet[^»]*» [a-zé]* (menu [^)]*)" "$brouillon/muet-piste.txt" | head -1)"
    rates=$((rates + 1))
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
