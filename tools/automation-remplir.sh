#!/usr/bin/env bash
# D602 — LES PASSES QUI REMPLISSENT (les *Fill* du panneau d'automation de Cubase) ET L'ARRÊT PENDANT UNE PASSE TENUE,
# mesurés par la courbe enregistrée et la ligne d'état.
#
# LE JEU (celui de D599) : 480 ticks à la noire, 120 BPM, une note tenue au do 6 (1 046 Hz) du tick 0 au tick 7 680 sur le
# générateur de test ; volume statique 1,0 ; une courbe de volume plate à 0,25 ; W en Touch ; la boucle de 3 840 à 5 760
# (éteinte : *Loop* remplit la plage du cycle, qu'il joue ou non). LA PASSE, EN DEUX VALEURS : la main prise à 700 ms,
# posée à −6,0 dB (0,50), puis à 0 dB (1,0) à 1 100 ms, lâchée à 1 500 ms — pour que *Punch* se distingue du témoin.
# Le remplissage se règle dans le fichier de préférences du HOME de la course (jamais celui de l'utilisateur), et une
# course le règle par le MENU (« Remplir jusqu'à la fin du morceau ») pour mesurer le menu lui-même.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D602) :
#   (1) « aucun » (le témoin, du même code) : 0,5 puis 1,0 dans la passe, 0,25 hors d'elle ;
#   (2) Punch : 1,0 sur toute la passe (aucun point à 0,5), 0,25 hors d'elle ;
#   (3) To Start : 1,0 du tick 0 à la fin de la passe, 0,25 à la fin du morceau ;
#   (4) To End : 0,25 avant la passe, 0,5 dans la passe, 1,0 de sa fin jusqu'au tick 7 680 et au-delà ;
#   (5) Loop : la passe comme en (1), 1,0 de 3 840 à 5 760, 0,25 entre la passe et la boucle et après elle ;
#   (6) Trim + To End sur un palier (0,5 jusqu'à 1 440, puis 0,25), la main à +6,02 dB : 0,5 jusqu'à la fin ;
#   (7) l'arrêt pendant une passe tenue : 1,0 jusqu'au tick de l'arrêt, 0,25 dès le suivant (pas de rampe, le retour de
#       250 ms réglé) ; le lâcher après l'arrêt ne change plus la courbe ;
#   (8) la ligne d'état dit le remplissage ; en anglais, le sous-menu nommé.
#
#   tools/automation-remplir.sh [binaire de l'application]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-remplir.XXXXXX")"
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
for nom, mode, courbe in [
        ("plat", "touch", [{"tick": 0, "value": 0.25}, {"tick": 7680, "value": 0.25}]),
        ("palier", "trim", [{"tick": 0, "value": 0.5, "step": True}, {"tick": 1440, "value": 0.25},
                            {"tick": 7680, "value": 0.25}])]:
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "automationMode": mode,
             "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 1.0},
             "instrument": {"preferredPlugin": "vsm.testtone"},
             "automation": [{"parameter": "mix.volume", "points": courbe}]}
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": 3840, "endTick": 5760}},
               "tracks": [piste]}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; $2 = projet ; $3 = remplissage (mot des préférences, vide : aucun réglage posé) ; le reste : variables
    local nom="$1" projet="$2" remplir="$3" n maison
    shift 3
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    if [ -n "$remplir" ]; then
        mkdir -p "$maison/VintageSynthMidiStudio"
        printf '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="automationRemplir" val="%s"/>\n</PROPERTIES>\n' \
            "$remplir" > "$maison/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    fi
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|APPUI|RELACHE|VALEUR_POSEE|TOUCHE|MENU|BOITE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }
# La courbe enregistrée, et sa valeur en des ticks : « v(t1) v(t2) … » ; « fin » = le dernier point à 1,0 (ou à la
# valeur $3 pour Trim), « debut » = le premier point de la passe (le premier tick où la courbe n'est plus 0,25 / le
# palier).
lire() {   # $1 = dossier ; $2 = ticks séparés par des virgules (des nombres, ou « fin+N », « debut+N »)
    python3 - "$1" "$2" <<'PY'
import json, sys
try:
    p = json.load(open(sys.argv[1] + "/project.json", encoding="utf-8"))
    pts = next(c["points"] for c in p["tracks"][0]["automation"] if c["parameter"] == "mix.volume")
except Exception:
    print("ABSENT"); sys.exit(0)
def v(t):
    if t <= pts[0]["tick"]: return pts[0]["value"]
    if t >= pts[-1]["tick"]: return pts[-1]["value"]
    for a, b in zip(pts, pts[1:]):
        if a["tick"] <= t <= b["tick"]:
            if a.get("step") or b["tick"] == a["tick"]: return a["value"]
            return a["value"] + (t - a["tick"]) / (b["tick"] - a["tick"]) * (b["value"] - a["value"])
uns = [q["tick"] for q in pts if abs(q["value"] - 1.0) < 1e-3]
demis = [q["tick"] for q in pts if abs(q["value"] - 0.5) < 3e-3]
fin = max(uns) if uns else -1
debut = min(demis) if demis else (min(uns) if uns else -1)
sortie = []
for jeton in sys.argv[2].split(","):
    if jeton.startswith("fin"): t = fin + int(jeton[3:] or 0)
    elif jeton.startswith("debut"): t = debut + int(jeton[5:] or 0)
    else: t = int(jeton)
    sortie.append("%.4f" % v(t))
print(" ".join(sortie) + " | fin=%d debut=%d demis=%d" % (fin, debut, len(demis)))
PY
}
courbe() { python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
print(' '.join('%d:%.3f%s' % (q['tick'], q['value'], ':p' if q.get('step') else '')
               for c in p['tracks'][0]['automation'] if c['parameter'] == 'mix.volume' for q in c['points']))" "$1" 2>/dev/null || echo ABSENT; }
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D602 : les passes qui remplissent, et l'arrêt pendant une passe tenue ==="
PASSE="700:appuyer:mixeur.volume;800:valeur:mixeur.volume=-6;1100:valeur:mixeur.volume=0;1500:relacher:mixeur.volume;1600:relever-etat;1800:touche:spacebar"
# LES DOSSIERS PORTENT UN NUMÉRO, PAS LE MOT : « r-aucun » mettait « aucun » dans le chemin d'un `enregistrer:`, et le
# compte des avertissements l'a pris pour un échec au premier essai.
i=0
for m in aucun passe debut fin boucle; do
    i=$((i + 1)); cp_projet plat "r$i"
    course "r$i" "$brouillon/r$i" "$m" VSM_LECTURE=1 VSM_DELAI=2600 VSM_GESTE_APRES="$PASSE;2100:enregistrer:$brouillon/r$i"
done
# (6) Trim + To End, réglé par le MENU
cp_projet palier trim-fin
course trim-fin "$brouillon/trim-fin" "" VSM_LECTURE=1 VSM_DELAI=2600 \
    VSM_GESTE_APRES="300:menu:Remplir jusqu'à la fin du morceau;700:appuyer:mixeur.volume;800:valeur:mixeur.volume=0;1500:relacher:mixeur.volume;1600:relever-etat;1700:relever-automation;1800:touche:spacebar;2100:enregistrer:$brouillon/trim-fin"
# (7) l'arrêt pendant une passe tenue (« aucun », le retour de 250 ms d'origine) : la photo de la courbe à l'arrêt, puis
# après le lâcher
cp_projet plat arret-a; cp_projet plat arret-b
course arret "$brouillon/arret-a" "" VSM_LECTURE=1 VSM_DELAI=2600 \
    VSM_GESTE_APRES="700:appuyer:mixeur.volume;800:valeur:mixeur.volume=0;1400:touche:spacebar;1700:enregistrer:$brouillon/arret-a;1900:relacher:mixeur.volume;2200:enregistrer:$brouillon/arret-b"
# (8) en anglais
course en "$brouillon/plat" "" VSM_LANGUE=en VSM_DELAI=1200 VSM_GESTE_APRES="600:lister-menus"

i=0; for m in aucun passe debut fin boucle; do i=$((i + 1)); echo "       $m : [$(courbe "$brouillon/r$i")]"; done
L1="$(lire "$brouillon/r1" "debut+10,fin-10,fin+300,100,7000")"; echo "       (1) aucun : $L1"
set -- $L1
verdict "(1) aucun (le témoin) : 0,5 puis 1,0 dans la passe, 0,25 hors d'elle" \
    "$(proche "${1:-x}" 0.5 0.003 && proche "${2:-x}" 1.0 0.001 && proche "${3:-x}" 0.25 0.001 && proche "${4:-x}" 0.25 0.001 && proche "${5:-x}" 0.25 0.001 && echo 1 || echo 0)"
L2="$(lire "$brouillon/r2" "debut+10,fin-10,100,7000")"; echo "       (2) passe : $L2"
d2="$(echo "$L2" | sed -n 's/.*demis=\([0-9]*\).*/\1/p')"; set -- $L2
verdict "(2) Punch : 1,0 sur toute la passe (aucun point à 0,5), 0,25 hors d'elle" \
    "$([ "${d2:-1}" = 0 ] && proche "${1:-x}" 1.0 0.001 && proche "${2:-x}" 1.0 0.001 && proche "${3:-x}" 0.25 0.001 && proche "${4:-x}" 0.25 0.001 && echo 1 || echo 0)"
L3="$(lire "$brouillon/r3" "0,100,fin-10,7000")"; echo "       (3) début : $L3"
set -- $L3
verdict "(3) To Start : 1,0 du tick 0 à la fin de la passe, 0,25 à la fin du morceau" \
    "$(proche "${1:-x}" 1.0 0.001 && proche "${2:-x}" 1.0 0.001 && proche "${3:-x}" 1.0 0.001 && proche "${4:-x}" 0.25 0.001 && echo 1 || echo 0)"
L4="$(lire "$brouillon/r4" "100,debut+10,5000,7680,9000")"; echo "       (4) fin : $L4"
set -- $L4
verdict "(4) To End : 0,25 avant, 0,5 dans la passe, 1,0 jusqu'à la fin et au-delà" \
    "$(proche "${1:-x}" 0.25 0.001 && proche "${2:-x}" 0.5 0.003 && proche "${3:-x}" 1.0 0.001 && proche "${4:-x}" 1.0 0.001 && proche "${5:-x}" 1.0 0.001 && echo 1 || echo 0)"
L5="$(lire "$brouillon/r5" "debut+10,3000,3900,5700,6500")"; echo "       (5) boucle : $L5"
set -- $L5
verdict "(5) Loop : la passe gardée, 1,0 sur la boucle, 0,25 entre les deux et après" \
    "$(proche "${1:-x}" 0.5 0.003 && proche "${2:-x}" 0.25 0.001 && proche "${3:-x}" 1.0 0.001 && proche "${4:-x}" 1.0 0.001 && proche "${5:-x}" 0.25 0.001 && echo 1 || echo 0)"
L6="$(lire "$brouillon/trim-fin" "3000,5000,7680,9000")"; echo "       (6) Trim + To End : $L6 ; [$(courbe "$brouillon/trim-fin")]"
set -- $L6
verdict "(6) Trim + To End : 0,5 (le palier × 2) jusqu'à la fin et au-delà" \
    "$(proche "${1:-x}" 0.5 0.003 && proche "${2:-x}" 0.5 0.003 && proche "${3:-x}" 0.5 0.003 && proche "${4:-x}" 0.5 0.003 && echo 1 || echo 0)"
R6="$(grep -m1 "^VSM_AUTOMATION_REMPLIR" "$brouillon/trim-fin.txt")"
verdict "(6) le menu a réglé le remplissage (« fin » au relevé)" "$(echo "$R6" | grep -q ": fin$" && echo 1 || echo 0)"
Ca="$(courbe "$brouillon/arret-a")"; Cb="$(courbe "$brouillon/arret-b")"
L7="$(lire "$brouillon/arret-a" "fin,fin+1,fin+120")"; echo "       (7) arrêt : $L7 ; à l'arrêt [$Ca] ; après le lâcher [$Cb]"
set -- $L7
verdict "(7) l'arrêt pendant une passe tenue : 1,0 jusqu'à l'arrêt, 0,25 dès le tick suivant (pas de rampe)" \
    "$(proche "${1:-x}" 1.0 0.001 && proche "${2:-x}" 0.25 0.001 && proche "${3:-x}" 0.25 0.001 && echo 1 || echo 0)"
verdict "(7) le lâcher après l'arrêt ne change plus la courbe" "$([ "$Ca" != ABSENT ] && [ "$Ca" = "$Cb" ] && echo 1 || echo 0)"
e4="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/r4.txt")"; e6="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/trim-fin.txt")"
e1="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/r1.txt")"
echo "       (8) $e4 ; $e6 ; aucun : $e1"
verdict "(8) la ligne d'état dit le remplissage (et rien quand il n'y en a pas)" \
    "$(echo "$e4" | grep -q "Rempli jusqu'à la fin du morceau : 1.00" && echo "$e6" | grep -q "Rempli jusqu'à la fin du morceau" && ! echo "$e1" | grep -q "Rempli" && echo 1 || echo 0)"
en="$(grep -c "^VSM_MENU_LISTE : .*Fill at the end of a pass > Fill to the end of the song" "$brouillon/en.txt")"
verdict "(8) en anglais : « Fill at the end of a pass > Fill to the end of the song »" "$([ "$en" -ge 1 ] && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-REMPLIR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-REMPLIR : une passe remplit jusqu'où on le demande, et un arrêt pendant une passe tenue l'écrit sans rampe"
