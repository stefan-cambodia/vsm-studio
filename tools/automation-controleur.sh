#!/usr/bin/env bash
# D603 — LA MAIN D'UN CONTRÔLEUR APPRIS (une rafale de D521 pose la main, 500 ms de silence la lèvent) ET LA SUSPENSION DE
# L'ÉCRITURE (le *Suspend Write* de Cubase), mesurées par ce que le moteur joue, le relevé et la courbe enregistrée.
#
# LE JEU (celui de D599) : 480 ticks à la noire, 120 BPM, une note tenue au do 6 (1 046 Hz) sur le générateur de test ;
# volume statique 1,0. Les associations dans les préférences du HOME de chaque course — CC 21 → volume de la piste,
# CC 22 → son premier départ —, et les contrôleurs entrent par `cc-entrant:` (D517). LA RAFALE : trois messages DISTINCTS
# (125, 126, 127) à 700, 900 et 1 100 ms — distincts pour qu'aucun filtrage des répétitions ne la raccourcisse.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D603) :
#   (1) W éteint, volume plat à 0,25 : joué 1,000 et la main « volume » à 1 200 ms ; joué 0,250, la main « - », le fader à
#       −12,0 dB à 2 000 ms ; le témoin : joué 0,250 pendant la rafale ;
#   (2) W en Touch : la courbe enregistrée vaut 1,0 au tick du relevé de 1 200 ms, 0,25 à celui de 2 600 ms ; le témoin :
#       1,0 aux deux — CORRIGÉ APRÈS LA MESURE : le témoin n'écrit rien (voir le contrôle) ;
#   (3) un départ appris, statique 0 sous une courbe plate à 0,5, W éteint : entendu 1,00 pendant, 0,50 après ; le
#       témoin : 0,50 pendant ;
#   (4) W en Touch, « Suspendre l'écriture du volume » au menu, la passe de D599 à la souris : joué 1,000 pendant la prise,
#       la courbe enregistrée identique (deux points à 0,25), « écriture suspendue » dit ; le témoin : la courbe réécrite ;
#   (5) en anglais, le sous-menu nommé.
#
#   tools/automation-controleur.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D602/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-controleur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + vlq(0) + bytes([0x90, 84, 100]) + vlq(7680) \
        + bytes([0x80, 84, 0]) + b"\x00\xff\x2f\x00"
def plat(parametre, valeur):
    return {"parameter": parametre, "points": [{"tick": 0, "value": valeur}, {"tick": 7680, "value": valeur}]}
ECHO = [{"name": "Écho", "effect": "eq", "returnGain": 1.0, "preFader": True, "parameters": {}}]
for nom, mode, bus, departs, courbes in [
        ("volume", "", [], [], [plat("mix.volume", 0.25)]),
        ("touch", "touch", [], [], [plat("mix.volume", 0.25)]),
        ("depart", "", ECHO, [0.0], [plat("mix.send.1", 0.5)])]:
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "automation": courbes,
             "mix": {"muted": False, "pan": 0.0, "sends": departs, "solo": False, "volume": 1.0},
             "instrument": {"preferredPlugin": "vsm.testtone"}}
    if mode: piste["automationMode"] = mode
    projet = {"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
              "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                            "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}, "tracks": [piste]}
    if bus: projet["sends"] = bus
    json.dump(projet, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = dossier du projet ; le reste : variables
    local bin="$1" nom="$2" projet="$3" n maison
    shift 3
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    mkdir -p "$maison/VintageSynthMidiStudio"
    python3 - "$maison/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" <<'PY'
import json, sys
from xml.sax.saxutils import quoteattr
carte = {"format": "vsm.midilearn.v1", "mappings": [
    {"controller": 21, "kind": "trackVolume", "track": 0, "min": 0.0, "max": 1.0},
    {"controller": 22, "kind": "trackSend", "track": 0, "slot": 0, "min": 0.0, "max": 1.0}]}
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="midiLearnMappings" val='
    + quoteattr(json.dumps(carte)) + '/>\n</PROPERTIES>\n')
PY
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|APPUI|RELACHE|VALEUR_POSEE|TOUCHE|MENU|BOITE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }
champ() {   # $1 fichier ; $2 rang ; $3 motif
    python3 - "$1" "$2" "$3" <<'PY'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_AUTOMATION_TRANCHE : piste 0 ")]
k = int(sys.argv[2])
if len(lignes) < k: print("ABSENT"); sys.exit(0)
m = re.search(sys.argv[3], lignes[k - 1])
print(m.group(1) if m else "?")
PY
}
valeur_a() {   # la valeur de la courbe de volume enregistrée au tick $2
    python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
t = int(sys.argv[2])
pts = next(c['points'] for c in p['tracks'][0]['automation'] if c['parameter'] == 'mix.volume')
if t <= pts[0]['tick']: print('%.4f' % pts[0]['value']); sys.exit(0)
if t >= pts[-1]['tick']: print('%.4f' % pts[-1]['value']); sys.exit(0)
for a, b in zip(pts, pts[1:]):
    if a['tick'] <= t <= b['tick']:
        if a.get('step') or b['tick'] == a['tick']: print('%.4f' % a['value'])
        else: print('%.4f' % (a['value'] + (t - a['tick']) / (b['tick'] - a['tick']) * (b['value'] - a['value'])))
        break" "$1" "$2" 2>/dev/null || echo "?"
}
courbe() { python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
print(' '.join('%d:%.4f' % (q['tick'], q['value']) for c in p['tracks'][0]['automation'] if c['parameter'] == 'mix.volume'
               for q in c['points']))" "$1" 2>/dev/null || echo ABSENT; }
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

RAFALE_V="700:cc-entrant:21:125;900:cc-entrant:21:126;1100:cc-entrant:21:127"
RAFALE_D="700:cc-entrant:22:125;900:cc-entrant:22:126;1100:cc-entrant:22:127"
jouer() {   # $1 = binaire ; $2 = préfixe des noms (« » ou « t- »)
    local bin="$1" p="$2"
    cp_projet touch "${p}touch-e"; cp_projet touch "${p}ecr-e"
    course "$bin" "${p}volume" "$brouillon/volume" VSM_LECTURE=1 VSM_DELAI=2400 \
        VSM_GESTE_APRES="$RAFALE_V;1200:relever-automation;2000:relever-automation"
    course "$bin" "${p}touch" "$brouillon/${p}touch-e" VSM_LECTURE=1 VSM_DELAI=3600 \
        VSM_GESTE_APRES="$RAFALE_V;1200:relever-automation;2600:relever-automation;3000:touche:spacebar;3300:enregistrer:$brouillon/${p}touch-e"
    course "$bin" "${p}depart" "$brouillon/depart" VSM_LECTURE=1 VSM_DELAI=2400 \
        VSM_GESTE_APRES="$RAFALE_D;1200:relever-automation;2000:relever-automation"
    course "$bin" "${p}ecriture" "$brouillon/${p}ecr-e" VSM_LECTURE=1 VSM_DELAI=2600 \
        VSM_GESTE_APRES="300:menu:Suspendre l'écriture du volume;700:appuyer:mixeur.volume;800:valeur:mixeur.volume=0;1300:relever-automation;1400:relever-etat;1600:relacher:mixeur.volume;1900:touche:spacebar;2200:enregistrer:$brouillon/${p}ecr-e"
}

echo "=== D603 : la main d'un contrôleur appris, la suspension de l'écriture ==="
jouer "$BIN" ""
course "$BIN" en "$brouillon/volume" VSM_LANGUE=en VSM_DELAI=1200 VSM_GESTE_APRES="600:lister-menus"

J1a="$(champ "$brouillon/volume.txt" 1 'jou\S+ (-?[\d.]+) /')"; M1a="$(champ "$brouillon/volume.txt" 1 'main (\S+)')"
J1b="$(champ "$brouillon/volume.txt" 2 'jou\S+ (-?[\d.]+) /')"; M1b="$(champ "$brouillon/volume.txt" 2 'main (\S+)')"
F1b="$(champ "$brouillon/volume.txt" 2 'fader (-?[\d.]+) dB')"
echo "       (1) W éteint : pendant joué $J1a, main $M1a ; après joué $J1b, main $M1b, fader $F1b dB"
verdict "(1) W éteint : joué 1,000 et la main « volume » pendant la rafale ; 0,250, « - », −12,0 dB après" \
    "$(proche "$J1a" 1.0 0.001 && [ "$M1a" = volume ] && proche "$J1b" 0.25 0.001 && [ "$M1b" = "-" ] && proche "$F1b" -12.04 0.1 && echo 1 || echo 0)"
T2a="$(champ "$brouillon/touch.txt" 1 't\S+te (\d+)')"; T2b="$(champ "$brouillon/touch.txt" 2 't\S+te (\d+)')"
V2a="$(valeur_a "$brouillon/touch-e" "$T2a")"; V2b="$(valeur_a "$brouillon/touch-e" "$T2b")"
echo "       (2) Touch : la courbe [$(courbe "$brouillon/touch-e")] ; au tick $T2a : $V2a ; au tick $T2b : $V2b"
verdict "(2) Touch : 1,0 au relevé pendant la rafale, 0,25 au relevé d'après (la passe fermée à la fin de la rafale)" \
    "$(proche "$V2a" 1.0 0.001 && proche "$V2b" 0.25 0.001 && echo 1 || echo 0)"
D3a="$(champ "$brouillon/depart.txt" 1 'd\S+part 1 [\d.-]+ \(courbe [\d.-]+, entendu (-?[\d.]+)\)')"
D3b="$(champ "$brouillon/depart.txt" 2 'd\S+part 1 [\d.-]+ \(courbe [\d.-]+, entendu (-?[\d.]+)\)')"
M3a="$(champ "$brouillon/depart.txt" 1 'main (\S+)')"
echo "       (3) départ appris : entendu $D3a pendant (main $M3a), $D3b après"
verdict "(3) un départ appris : entendu 1,00 pendant la rafale, 0,50 après" \
    "$(proche "$D3a" 1.0 0.005 && proche "$D3b" 0.5 0.005 && echo 1 || echo 0)"
J4="$(champ "$brouillon/ecriture.txt" 1 'jou\S+ (-?[\d.]+) /')"; C4="$(courbe "$brouillon/ecr-e")"
E4="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/ecriture.txt")"
echo "       (4) écriture suspendue : joué $J4 pendant ; courbe [$C4] ; $E4"
verdict "(4) l'écriture du volume suspendue : joué 1,000, la courbe identique, et dit" \
    "$(proche "$J4" 1.0 0.001 && [ "$C4" = "0:0.2500 7680:0.2500" ] && echo "$E4" | grep -q "criture suspendue" && echo 1 || echo 0)"
en="$(grep -c "^VSM_MENU_LISTE : .*Suspend automation write > Suspend volume write" "$brouillon/en.txt")"
verdict "(5) en anglais : « Suspend automation write > Suspend volume write »" "$([ "$en" -ge 1 ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    jouer "$TEMOIN" "t-"
    tJ1="$(champ "$brouillon/t-volume.txt" 1 'jou\S+ (-?[\d.]+) /')"
    tT2a="$(champ "$brouillon/t-touch.txt" 1 't\S+te (\d+)')"; tT2b="$(champ "$brouillon/t-touch.txt" 2 't\S+te (\d+)')"
    tV2a="$(valeur_a "$brouillon/t-touch-e" "$tT2a")"; tV2b="$(valeur_a "$brouillon/t-touch-e" "$tT2b")"
    tD3="$(champ "$brouillon/t-depart.txt" 1 'd\S+part 1 [\d.-]+ \(courbe [\d.-]+, entendu (-?[\d.]+)\)')"
    tC4="$(courbe "$brouillon/t-ecr-e")"; tab="$(grep -c "VSM_MENU : .*aucune entr" "$brouillon/t-ecriture.txt")"
    echo "       témoin : (1) joué $tJ1 ; (2) $tV2a / $tV2b [$(courbe "$brouillon/t-touch-e")] ; (3) entendu $tD3 ; (4) « aucune entrée » $tab fois, courbe [$tC4]"
    verdict "(1) témoin : joué 0,250 pendant la rafale" "$(proche "$tJ1" 0.25 0.001 && echo 1 || echo 0)"
    # CORRIGÉ APRÈS LA MESURE, ET DIT (ROADMAP-daw, D603 mesurée) : l'attendu écrit avant la mesure prêtait au témoin ce
    # que D29.3 annonçait — sa passe courant jusqu'à l'arrêt, 1,0 aux deux relevés. Mesuré, il n'écrit RIEN : la console
    # était reconstruite à chaque message appris (`mixer_.setProject`, D10.2), et la passe détruite avec sa tranche. Le
    # témoin garde donc sa courbe d'origine — c'est le défaut que D603 corrige, et ce contrôle le garde visible.
    verdict "(2) témoin : la courbe intacte (deux points à 0,25) — sa passe détruite à chaque message" \
        "$([ "$(courbe "$brouillon/t-touch-e")" = "0:0.2500 7680:0.2500" ] && proche "$tV2a" 0.25 0.001 && echo 1 || echo 0)"
    verdict "(3) témoin : entendu 0,50 pendant la rafale" "$(proche "$tD3" 0.5 0.005 && echo 1 || echo 0)"
    verdict "(4) témoin : l'entrée absente, la courbe réécrite" "$([ "$tab" -ge 1 ] && [ "$tC4" != "0:0.2500 7680:0.2500" ] && [ "$tC4" != ABSENT ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : les témoins ne sont PAS joués"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-CONTROLEUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-CONTROLEUR : un contrôleur appris pose la main et la lève, et l'écriture se suspend par famille"
