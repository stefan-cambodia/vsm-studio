#!/usr/bin/env bash
# D604 — LES REPÈRES À ÉTENDUE (les *Cycle Markers* de Cubase) : poser un repère de cycle sur les locateurs, les
# locateurs et le zoom sur un cycle, la fin dans la fenêtre des repères, la relecture à l'octet d'un projet d'avant.
#
# LE JEU : 480 ticks à la noire, 4/4, 120 BPM, une piste Minimoog, une note par mesure sur huit mesures ; un repère de
# position « Couplet » au tick 0. Un menu contextuel de règle par course (`VSM_MENU_CONTEXTE`, joué au démarrage, à la
# tête posée par `VSM_POSITION`, en mesures).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D604) :
#   (1) les locateurs 1 920–5 760 : « Poser un repère de cycle sur les locateurs » → « Couplet » à 0 et « Cycle 1 » de
#       1 920 à 5 760 ; le fichier porte `"endTick": 5760` sur le second, rien sur le premier ; Ctrl+Z → le seul
#       « Couplet » ; le témoin : l'entrée absente ;
#   (2) les locateurs à 0–480 et la tête dans le cycle : « Poser les locateurs sur ce cycle » → 1 920–5 760, depuis les
#       deux règles ; « Zoomer sur ce cycle » → la fenêtre de l'arrangement couvre 1 920–5 760 à 5 % près — CORRIGÉ
#       après la mesure : la règle de D565.2, 5 % devant et 6,1 % derrière (voir le contrôle) ;
#   (3) un projet de D603 (un repère de position seul) se relit et se réécrit à l'octet — le même fichier que celui que
#       le binaire de D603 écrit ;
#   (4) la fenêtre des repères lit la fin du cycle ; la règle photographiée ; en anglais, les entrées nommées.
#
#   tools/reperes-cycle.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D603/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-reperes-cycle.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano"
t = 0
for i in range(8):
    evs += vlq(i * 1920 - t) + bytes([0x90, 60, 100]) + vlq(480) + bytes([0x80, 60, 0]); t = i * 1920 + 480
evs += b"\x00\xff\x2f\x00"
def projet(nom, reperes, boucle):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(evs)) + evs)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "markers": reperes,
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Piano",
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
projet("base", [{"tick": 0, "name": "Couplet"}], (1920, 5760))
projet("cycle", [{"tick": 0, "name": "Couplet"}, {"tick": 1920, "name": "Cycle 1", "endTick": 5760}], (0, 480))
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = projet ; $4 = position (mesure) ; $5 = menu contextuel ; $6 = gestes ; le reste : variables
    local bin="$1" nom="$2" projet="$3" pos="$4" menu="$5" gestes="$6" n
    shift 6
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_POSITION="$pos" \
        VSM_MENU_CONTEXTE="$menu" VSM_DELAI=2600 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|MENU_CONTEXTE|POSITION|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE|grisée)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }
reperes() {   # les repères d'un project.json : « tick:nom[:fin] » séparés par « | »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print('|'.join('%d:%s%s' % (m['tick'], m['name'], (':%d' % m['endTick']) if 'endTick' in m else '') for m in p.get('markers', [])))" "$1"
}
boucle() { python3 -c "
import json, sys
try:
    l = json.load(open(sys.argv[1] + '/project.json'))['transport']['loop']
except Exception:
    print('ABSENT'); sys.exit(0)
print('%d:%d' % (l.get('startTick', 0), l.get('endTick', 0)))" "$1"; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

POSER="regle:Poser un repère de cycle sur les locateurs"
jouer() {   # $1 = binaire ; $2 = préfixe
    local bin="$1" p="$2"
    cp_projet base "${p}pose"
    course "$bin" "${p}pose" "$brouillon/${p}pose" 1 "$POSER" \
        "1000:enregistrer:$brouillon/${p}pose-a;1300:touche:ctrl + Z;1600:enregistrer:$brouillon/${p}pose-b" VSM_VUE="sans-rapport,reperes"
    cp_projet cycle "${p}loc"; cp_projet cycle "${p}locpr"
    course "$bin" "${p}loc" "$brouillon/${p}loc" 3 "regle:Poser les locateurs sur ce cycle" "1000:enregistrer:$brouillon/${p}loc" \
        VSM_VUE="sans-rapport,arrangement"
    course "$bin" "${p}locpr" "$brouillon/${p}locpr" 3 "regle-pianoroll:Poser les locateurs sur ce cycle" \
        "1000:enregistrer:$brouillon/${p}locpr" VSM_VUE="sans-rapport"
}

echo "=== D604 : les repères à étendue ==="
jouer "$BIN" ""
cp_projet cycle zoom; cp_projet cycle fen
course "$BIN" zoom "$brouillon/zoom" 3 "regle:Zoomer sur ce cycle" "" VSM_VUE="sans-rapport,arrangement"
course "$BIN" fen "$brouillon/fen" 1 "" "1000:relever-reperes" VSM_VUE="sans-rapport,reperes"
course "$BIN" en "$brouillon/cycle" 3 "regle:?" "" VSM_LANGUE=en VSM_VUE="sans-rapport"
# (3) À L'OCTET : le projet d'avant enregistré par le binaire neuf, et par le témoin
cp_projet base octet-neuf; cp_projet base octet-temoin
course "$BIN" octet "$brouillon/octet-neuf" 1 "" "800:enregistrer:$brouillon/octet-neuf"

R1a="$(reperes "$brouillon/pose-a")"; R1b="$(reperes "$brouillon/pose-b")"
echo "       (1) posé : [$R1a] ; Ctrl+Z : [$R1b]"
verdict "(1) « Cycle 1 » de 1 920 à 5 760 à côté de « Couplet » (sans fin) ; Ctrl+Z le retire" \
    "$([ "$R1a" = "0:Couplet|1920:Cycle 1:5760" ] && [ "$R1b" = "0:Couplet" ] && echo 1 || echo 0)"
B2="$(boucle "$brouillon/loc")"; B2p="$(boucle "$brouillon/locpr")"
echo "       (2) locateurs : arrangement [$B2], piano roll [$B2p]"
verdict "(2) les locateurs sur le cycle, depuis les deux règles : 1 920–5 760" \
    "$([ "$B2" = "1920:5760" ] && [ "$B2p" = "1920:5760" ] && echo 1 || echo 0)"
Z="$(grep -m1 "^VSM_ZOOM_CYCLE" "$brouillon/zoom.txt")"; echo "       (2) $Z"
z1="$(echo "$Z" | sed -n 's/.*de \([0-9]*\) à \([0-9]*\) ticks.*/\1/p')"; z2="$(echo "$Z" | sed -n 's/.*de \([0-9]*\) à \([0-9]*\) ticks.*/\2/p')"
# CORRIGÉ APRÈS LA MESURE, ET DIT (ROADMAP-daw, D604 mesurée) : l'attendu écrit avant la mesure voulait « 5 % près de part
# et d'autre », sans relire la règle de cadrage que le zoom partage avec la sélection et les locateurs (D565.2) : la plage
# sur 90 % de la largeur, UN VINGTIÈME de marge avant — donc 5 % devant et 6,1 % derrière (1/0,9 − 1 − 0,05). Mesuré :
# 1 728 à 5 994, exactement cette règle. Le contrôle garde la règle, à 1 % de l'étendue près.
verdict "(2) « Zoomer sur ce cycle » : la fenêtre de D565.2 — 1 728 (5 % devant) à 5 995 (6,1 % derrière), à 1 % près" \
    "$([ -n "$z1" ] && [ -n "$z2" ] && python3 -c "import sys; a,b=int(sys.argv[1]),int(sys.argv[2]); e=3840; g=1920-e/20; d=g+e/0.9; sys.exit(0 if abs(a-g)<=0.01*e and abs(b-d)<=0.01*e else 1)" "$z1" "$z2" && echo 1 || echo 0)"
F4="$(grep -m1 "^VSM_REPERES" "$brouillon/fen.txt")"; echo "       (4) $F4"
verdict "(4) la fenêtre des repères lit la fin du cycle" "$(echo "$F4" | grep -q "Cycle 1 → mes. 4 · 1" && echo 1 || echo 0)"
EN="$(grep -m1 "^VSM_MENU_CONTEXTE : regle = " "$brouillon/en.txt")"
verdict "(4) en anglais : « Add a cycle marker on the locators », « Set the locators to this cycle », « Zoom to this cycle »" \
    "$(echo "$EN" | grep -q "Add a cycle marker on the locators" && echo "$EN" | grep -q "Set the locators to this cycle" && echo "$EN" | grep -q "Zoom to this cycle" && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    jouer "$TEMOIN" "t-"
    course "$TEMOIN" t-octet "$brouillon/octet-temoin" 1 "" "800:enregistrer:$brouillon/octet-temoin"
    tR1="$(reperes "$brouillon/t-pose-a")"; tB2="$(boucle "$brouillon/t-loc")"
    tab="$(grep -c "VSM_MENU_CONTEXTE : .*\(introuvable\|aucun\)" "$brouillon/t-pose.txt")"
    echo "       témoin : (1) [$tR1], « introuvable » $tab fois ; (2) locateurs [$tB2]"
    verdict "(1) témoin : l'entrée absente, aucun cycle posé" "$([ "$tab" -ge 1 ] && [ "$tR1" = "0:Couplet" ] && echo 1 || echo 0)"
    verdict "(2) témoin : les locateurs inchangés (0–480)" "$([ "$tB2" = "0:480" ] && echo 1 || echo 0)"
    if [ -s "$brouillon/octet-neuf/project.json" ] && [ -s "$brouillon/octet-temoin/project.json" ]; then
        verdict "(3) un projet de D603 réécrit à l'octet (le même fichier que le témoin écrit)" \
            "$(cmp -s "$brouillon/octet-neuf/project.json" "$brouillon/octet-temoin/project.json" && echo 1 || echo 0)"
    else
        verdict "(3) un projet de D603 réécrit à l'octet : un des deux fichiers MANQUE" 0
    fi
else
    echo "       témoin absent ($TEMOIN) : les témoins ne sont PAS joués"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "REPERES-CYCLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REPERES-CYCLE : un repère s'étend sur une plage, les locateurs et le zoom s'y posent, et un projet d'avant se relit à l'octet"
