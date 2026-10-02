#!/usr/bin/env bash
# D532.3 bis — LA LIGNE D'ACCORDS DANS L'APPLICATION : LES BANDES, LES GESTES DES DEUX RÈGLES,
# ET « CALER SUR LES ACCORDS » LÀ OÙ LA NOTE SONNE.
#
# LA RÈGLE GARDÉE.
#   (2) sans accord, aucune bande — la règle du piano roll garde 22 px ; avec trois accords
#       (C à 960, Am7/G à 1920, F à 3840), les deux bandes listent C, Am7/G et F dans
#       l'ordre, la règle du piano roll prend 20 px et la grille des notes les cède, la zone
#       des pistes de l'arrangement aussi ;
#   (3) par le menu de la règle, à la TÊTE DE LECTURE : « Poser un accord ici… » avec
#       « Am7/G » à la mesure 3 → project.json porte (3840, Am7/G), et Ctrl+Z l'ôte ; la
#       règle du PIANO ROLL pose « Bb7 » à la mesure 2 → (1920, A#7) ; « Cmaj13 » est refusé
#       par une boîte « Accord illisible », rien d'écrit ; à la mesure 2 temps 3,
#       « Modifier cet accord… » réécrit l'accord EN VIGUEUR (Am7/G à 1920 → Dm à 1920) et
#       « Retirer cet accord » l'ôte ; les deux sont grisées avant le premier accord ;
#   (4) « Caler sur les accords » (« Tout sélectionner » d'abord) : le .mid exporté, relu en
#       MULTIENSEMBLE de hauteurs, passe de {62,62,64,71,64,67} à {62,60,64,72,65,65} — la note
#       d'avant le premier accord reste — avec ses cinq comptes au journal (4 · 1 · 1 · 0 · 0),
#       et Ctrl+Z rend le premier ; grisée sans accord.
#   Les multiensembles attendus sont ceux de la règle de `snapNotesToChords` (core, tenue par
#   `core/tests/test_chord_track.cpp`) appliquée à la main : ré sous C → do (égalité vers le
#   grave), si sous Am7/G → do, mi et sol sous F → fa.
# Chaque fichier se lit AVANT de juger (une comparaison dont un côté manque n'est pas un
# verdict), et les avertissements du journal sont relayés.
#
#   tools/accords.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-accords.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/nu/midi" "$brouillon/accorde/midi"
python3 - "$brouillon" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, notes):
    evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for debut, hauteur, duree in notes:
        evs += [(debut - t, bytes([0x90, hauteur, 100])), (duree, bytes([0x80, hauteur, 0]))]
        t = debut + duree
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
# Ré avant le premier accord ; ré et mi sous C ; si sous Am7/G ; mi et sol sous F.
NOTES = [(0, 62, 240), (960, 62, 240), (1200, 64, 240), (1920, 71, 480), (3840, 64, 480), (4320, 67, 480)]
def projet(dossier, accords):
    open(f"{dossier}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + piste("Piano", NOTES))
    doc = {"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}
    if accords:
        doc["chords"] = [{"tick": t, "chord": c} for t, c in accords]
    json.dump(doc, open(f"{dossier}/project.json", "w"), indent=1)
projet(d + "/nu", [])
projet(d + "/accorde", [(960, "C"), (1920, "Am7/G"), (3840, "F")])
PY

course() {   # $1 = nom ; $2 = projet ; $3 = VSM_GESTE_APRES ; le reste : variables en plus
    local nom="$1" projet="$2" gestes="$3" maison
    shift 3
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/$projet" VSM_TAILLE="1600x1000" VSM_DELAI=4500 \
        VSM_VUE="sans-rapport" VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU|MENU_CONTEXTE|VUE|POSITION|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# La ligne d'accords d'un project.json : « tick:symbole … », « sans-cle », ou « ABSENT ».
accords() {
    python3 - "$1/project.json" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("ABSENT"); sys.exit(0)
print(" ".join(f"{a['tick']}:{a['chord']}" for a in d["chords"]) if "chords" in d else "sans-cle")
PY
}
# Le multiensemble des hauteurs d'un .mid (notes jouées, toutes pistes), trié — ou « ABSENT ».
hauteurs() {
    python3 - "$1" <<'PY'
import sys
from collections import Counter
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
if len(data) < 14:
    print("ABSENT"); sys.exit(0)
i, compte = 14, Counter()
while i + 8 <= len(data):
    longueur = int.from_bytes(data[i + 4:i + 8], "big")
    corps, i = data[i + 8:i + 8 + longueur], i + 8 + longueur
    j, statut = 0, 0
    while j < len(corps):
        while corps[j] & 0x80: j += 1          # delta (VLQ)
        j += 1
        if corps[j] == 0xFF:
            j += 2
            n = 0
            while corps[j] & 0x80: n = (n << 7) | (corps[j] & 0x7F); j += 1
            n = (n << 7) | corps[j]; j += 1 + n
            continue
        if corps[j] in (0xF0, 0xF7):
            j += 1
            n = 0
            while corps[j] & 0x80: n = (n << 7) | (corps[j] & 0x7F); j += 1
            n = (n << 7) | corps[j]; j += 1 + n
            continue
        if corps[j] & 0x80: statut = corps[j]; j += 1
        taille = 1 if (statut & 0xF0) in (0xC0, 0xD0) else 2
        if (statut & 0xF0) == 0x90 and corps[j + 1] > 0: compte[corps[j]] += 1
        j += taille
print(",".join(str(h) for h in sorted(compte.elements())))
PY
}
derniere() { grep "$2" "$brouillon/$1.txt" | tail -1; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D532.3 bis : la ligne d'accords dans l'application ==="

# --- (2) les bandes
course bandes-nu nu "1500:relever-accords;1600:relever-arrangement" VSM_PIANOROLL_ZONES=1
course bandes-accorde accorde "1500:relever-accords;1600:relever-arrangement" VSM_PIANOROLL_ZONES=1
for n in bandes-nu bandes-accorde; do
    derniere $n "^VSM_ACCORDS : arrangement" | sed "s/^/       $n : /"
    derniere $n "^VSM_ACCORDS : pianoroll" | sed "s/^/       $n : /"
done
regle() { derniere "$1" "^VSM_PIANOROLL_ZONES" | grep -o 'regle=[0-9]*' | cut -d= -f2; }
notes() { derniere "$1" "^VSM_PIANOROLL_ZONES" | grep -o 'notes=[0-9]*' | cut -d= -f2; }
zone() { derniere "$1" "^VSM_ARRANGEMENT : pistes" | grep -o 'zone [0-9]*' | cut -d' ' -f2; }
r0="$(regle bandes-nu)"; r1="$(regle bandes-accorde)"; g0="$(notes bandes-nu)"; g1="$(notes bandes-accorde)"
z0="$(zone bandes-nu)"; z1="$(zone bandes-accorde)"
echo "       piano roll : règle ${r0:-?} → ${r1:-?} px, grille ${g0:-?} → ${g1:-?} px ; arrangement : zone des pistes ${z0:-?} → ${z1:-?} px"
sans_bande() { derniere bandes-nu "^VSM_ACCORDS : $1" | grep -q "bande 0 px, 0 accord(s) au projet, 0 dans la vue" && echo 1 || echo 0; }
trois() { derniere bandes-accorde "^VSM_ACCORDS : $1" | grep -qE "bande 20 px, 3 accord\(s\) au projet, 3 dans la vue — C@[0-9-]+, Am7/G@[0-9-]+, F@[0-9-]+$" && echo 1 || echo 0; }
verdict "(2) sans accord : aucune bande, ni dans l'arrangement ni au piano roll" "$([ "$(sans_bande arrangement)$(sans_bande pianoroll)" = 11 ] && echo 1 || echo 0)"
verdict "(2) trois accords : les deux bandes écrivent C, Am7/G, F, dans l'ordre" "$([ "$(trois arrangement)$(trois pianoroll)" = 11 ] && echo 1 || echo 0)"
verdict "(2) la règle du piano roll : 22 px sans accord, 42 avec, et la grille cède les 20" \
    "$([ "${r0:-}" = 22 ] && [ "${r1:-}" = 42 ] && [ -n "${g0:-}" ] && [ -n "${g1:-}" ] && [ $((g0 - g1)) = 20 ] && echo 1 || echo 0)"
verdict "(2) la zone des pistes de l'arrangement cède les 20 px" \
    "$([ -n "${z0:-}" ] && [ -n "${z1:-}" ] && [ $((z0 - z1)) = 20 ] && echo 1 || echo 0)"

# --- (3) les gestes des deux règles, à la tête de lecture
course poser nu "1500:enregistrer:$brouillon/poser-ecrit;2000:touche:ctrl + Z;2500:enregistrer:$brouillon/poser-annule" \
    VSM_POSITION=3 VSM_MENU_CONTEXTE="regle:Poser un accord ici…" VSM_OPTIONS="accord=Am7/G"
p1="$(accords "$brouillon/poser-ecrit")"; p2="$(accords "$brouillon/poser-annule")"
grep "^VSM_ACCORD :" "$brouillon/poser.txt" | sed 's/^/        /'
echo "       posé : $p1 ; après Ctrl+Z : $p2"
verdict "(3) « Poser un accord ici… » à la mesure 3 : (3840, Am7/G)" "$([ "$p1" = "3840:Am7/G" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : la clé « chords » disparaît" "$([ "$p2" = "sans-cle" ] && echo 1 || echo 0)"

course poser-pr nu "1500:enregistrer:$brouillon/poser-pr-ecrit" \
    VSM_POSITION=2 VSM_MENU_CONTEXTE="regle-pianoroll:Poser un accord ici…" VSM_OPTIONS="accord=Bb7"
p3="$(accords "$brouillon/poser-pr-ecrit")"
echo "       par la règle du piano roll : $p3"
verdict "(3) la règle du piano roll pose « Bb7 » à la mesure 2 : (1920, A#7)" "$([ "$p3" = "1920:A#7" ] && echo 1 || echo 0)"

course illisible nu "1500:enregistrer:$brouillon/illisible-ecrit" \
    VSM_MENU_CONTEXTE="regle:Poser un accord ici…" VSM_OPTIONS="accord=Cmaj13"
p4="$(accords "$brouillon/illisible-ecrit")"
boite="$(grep -c "VSM_BOITE.*Accord illisible" "$brouillon/illisible.txt")"
echo "       « Cmaj13 » : $p4 ; boîtes « Accord illisible » au journal : $boite"
verdict "(3) « Cmaj13 » refusé et DIT, rien d'écrit" "$([ "$p4" = "sans-cle" ] && [ "$boite" -ge 1 ] && echo 1 || echo 0)"

course modifier accorde "1500:enregistrer:$brouillon/modifier-ecrit" \
    VSM_POSITION=2.3 VSM_MENU_CONTEXTE="regle:Modifier cet accord…" VSM_OPTIONS="accord=Dm"
p5="$(accords "$brouillon/modifier-ecrit")"
course retirer accorde "1500:enregistrer:$brouillon/retirer-ecrit" \
    VSM_POSITION=2.3 VSM_MENU_CONTEXTE="regle:Retirer cet accord"
p6="$(accords "$brouillon/retirer-ecrit")"
echo "       à la mesure 2 temps 3 — modifié : $p5 ; retiré : $p6"
verdict "(3) « Modifier cet accord… » réécrit l'accord EN VIGUEUR à son tick (1920 : Am7/G → Dm)" \
    "$([ "$p5" = "960:C 1920:Dm 3840:F" ] && echo 1 || echo 0)"
verdict "(3) « Retirer cet accord » ôte l'accord en vigueur" "$([ "$p6" = "960:C 3840:F" ] && echo 1 || echo 0)"

course grisees accorde "" VSM_MENU_CONTEXTE="regle:?;regle-pianoroll:?"
course actives accorde "" VSM_POSITION=2.3 VSM_MENU_CONTEXTE="regle:?;regle-pianoroll:?"
g_av="$(grep -E "^VSM_MENU_CONTEXTE : regle(-pianoroll)? = " "$brouillon/grisees.txt" | grep -o "Modifier cet accord… \[grisee\]\|Retirer cet accord \[grisee\]" | wc -l)"
a_ap="$(grep -E "^VSM_MENU_CONTEXTE : regle(-pianoroll)? = " "$brouillon/actives.txt" | grep -oE "Modifier cet accord…( \[grisee\])?|Retirer cet accord( \[grisee\])?" | grep -vc grisee)"
echo "       avant le premier accord : $g_av entrée(s) grisée(s) sur 4 ; à la mesure 2 temps 3 : $a_ap active(s) sur 4"
verdict "(3) modifier et retirer grisées avant le premier accord, actives sous un accord, dans les deux règles" \
    "$([ "$g_av" = 4 ] && [ "$a_ap" = 4 ] && echo 1 || echo 0)"

# --- (4) « Caler sur les accords »
course caler accorde "1500:exporter-midi:$brouillon/cale.mid;2000:touche:ctrl + Z;2500:exporter-midi:$brouillon/annule.mid" \
    VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner;pianoroll:Caler sur les accords"
h1="$(hauteurs "$brouillon/cale.mid")"; h2="$(hauteurs "$brouillon/annule.mid")"
bilan="$(derniere caler "^VSM_ACCORDS_CALAGE")"
echo "       $bilan"
echo "       calé : {$h1} ; après Ctrl+Z : {$h2}"
verdict "(4) le .mid calé : {60,62,64,65,65,72}" "$([ "$h1" = "60,62,64,65,65,72" ] && echo 1 || echo 0)"
verdict "(4) les cinq comptes : 4 déplacées, 1 déjà juste, 1 avant le premier accord, 0, 0" \
    "$(echo "$bilan" | grep -q "4 déplacée(s), 1 déjà dans l'accord, 1 avant le premier accord, 0 sous deux harmonies, 0 entendue(s) nulle part" && echo 1 || echo 0)"
verdict "(4) Ctrl+Z rend {62,62,64,64,67,71}" "$([ "$h2" = "62,62,64,64,67,71" ] && echo 1 || echo 0)"
course caler-nu nu "" VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner;pianoroll:?"
gr="$(grep "^VSM_MENU_CONTEXTE : pianoroll = " "$brouillon/caler-nu.txt" | grep -c "Caler sur les accords \[grisee\]")"
verdict "(4) « Caler sur les accords » grisée sans accord" "$([ "$gr" -ge 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ACCORDS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ACCORDS : la ligne se voit, se pose, se corrige et se suit"
