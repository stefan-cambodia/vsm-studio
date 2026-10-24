#!/usr/bin/env bash
# D589.3 — L'ÉCOUTE ATTÉNUÉE (Mixage ▸ Écoute atténuée de 20 dB ; le *Dim* de la Control Room de Cubase), MESURÉE PAR LA
# CRÊTE DE CE QUI SORT CONTRE CELLE DU MASTER AU MÊME BLOC, PAR L'EXPORT, ET PAR LE TITRE DE LA TRANCHE MASTER.
#
# LES RÈGLES GARDÉES. Une piste MIDI, une machine, une note tenue quatre mesures ; la lecture par le bouton Play.
# `relever-ecoute` dit la crête du master et celle de ce qui SORT, d'un même bloc (un seul atomique), leur écart et le
# titre de la tranche master (`VSM_ECOUTE_RELEVE`) ; `exporter-audio:` exporte comme `VSM_EXPORT`, mais APRÈS les gestes.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D589) :
#   (1) cochée, en lecture : l'écart −20,0 dB (à 0,1 dB près) ; décochée : 0,0 dB — et le master sonne (au-dessus de
#       −60 dBFS) dans les deux courses, sans quoi deux silences donneraient un écart nul ;
#   (2) l'export, cochée, identique AU BIT près à l'export sans ;
#   (3) relancé dans le même HOME : décochée (un état de séance, non retenu) ;
#   (4) le titre « MASTER · DIM » cochée, « MASTER » décochée ; en anglais, « Dim the monitoring by 20 dB (never in an
#       export) » ;
#   (5) le TÉMOIN (le binaire de D588, passé en argument) : l'entrée absente, le verbe `relever-ecoute` inconnu.
#
#   tools/ecoute-attenuee.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ecoute-attenuee.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
# Une note tenue quatre mesures (7 680 ticks = 0x3C 0x00 en VLQ : 0xBC 0x00).
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + b"\x00\x90\x3c\x64\xbc\x00\x80\x3c\x00" + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "ecoute", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# Un champ du relevé : atténuée (oui/non), master, entendue, écart (en dB), titre, état — ou « ABSENT ».
champ() {
    python3 - "$brouillon/$1.txt" "$2" <<'PY2'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_ECOUTE_RELEVE : ")]
if not lignes:
    print("ABSENT"); sys.exit(0)
m = re.match(r"VSM_ECOUTE_RELEVE : atténuée (oui|non) ; crête du master (\S+) dBFS ; crête entendue (\S+) dBFS ; "
             r"écart (\S+) dB ; titre « (.*) » ; (.*)$", lignes[-1].rstrip("\n"))
if not m:
    print("ILLISIBLE"); sys.exit(0)
print(dict(zip(["attenuee", "master", "entendue", "ecart", "titre", "etat"], m.groups()))[sys.argv[2]])
PY2
}
pres() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
sonne() { python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) > -60 else 1)" "$1" 2>/dev/null; }
pareils() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2" && echo 1 || echo 0; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
DIM="menu:Mixage > Écoute atténuée de 20 dB (jamais dans un export)"

echo "=== D589.3 : l'écoute atténuée ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course attenue "$maison" VSM_DELAI=2000 VSM_GESTE_APRES="400:$DIM;700:cliquer:Play;1400:relever-ecoute;1500:cliquer:Stop"
course retenu "$maison" VSM_DELAI=1400 VSM_GESTE_APRES="800:lister-menus"
course pleine "" VSM_DELAI=2000 VSM_GESTE_APRES="700:cliquer:Play;1400:relever-ecoute;1500:cliquer:Stop"
course export-dim "" VSM_DELAI=2500 VSM_GESTE_APRES="400:$DIM;800:exporter-audio:$brouillon/dim.wav"
course export-plein "" VSM_DELAI=2500 VSM_GESTE_APRES="800:exporter-audio:$brouillon/plein.wav"
course anglais "" VSM_LANGUE=en VSM_DELAI=1400 VSM_GESTE_APRES="800:lister-menus"

for n in attenue pleine; do grep -h "^VSM_ECOUTE_RELEVE" "$brouillon/$n.txt" | tail -1 | sed "s/^/       $n : /"; done
grep -h "^VSM_ECOUTE :\|^VSM_EXPORT :" "$brouillon"/attenue.txt "$brouillon"/export-dim.txt "$brouillon"/export-plein.txt | cut -c1-160 | sed 's/^/       /'
retenu="$(grep -h "^VSM_MENU_LISTE : Mixage > Écoute atténuée" "$brouillon/retenu.txt")"
en="$(grep -c "^VSM_MENU_LISTE : Mixing > Dim the monitoring by 20 dB (never in an export)" "$brouillon/anglais.txt")"
echo "       exports (octets) : atténuée $(stat -c %s "$brouillon/dim.wav" 2>/dev/null || echo ABSENT), pleine $(stat -c %s "$brouillon/plein.wav" 2>/dev/null || echo ABSENT)"
echo "       relancé : « ${retenu:-ABSENTE} » ; en anglais : $en entrée"

verdict "(1) cochée : −20,0 dB ; décochée : 0,0 dB ; le master sonne dans les deux courses" \
    "$([ "$(champ attenue attenuee)" = oui ] && [ "$(champ attenue etat)" = "en lecture" ] \
        && sonne "$(champ attenue master)" && pres "$(champ attenue ecart)" -20.0 0.1 \
        && [ "$(champ pleine attenuee)" = non ] && sonne "$(champ pleine master)" && pres "$(champ pleine ecart)" 0.0 0.1 \
        && echo 1 || echo 0)"
verdict "(2) l'export, cochée, identique au bit près à l'export sans" \
    "$(pareils "$brouillon/dim.wav" "$brouillon/plein.wav")"
verdict "(3) relancé : décochée" \
    "$(echo "$retenu" | grep -q "^VSM_MENU_LISTE : Mixage > Écoute atténuée de 20 dB (jamais dans un export)$" && echo 1 || echo 0)"
verdict "(4) le titre « MASTER · DIM », puis « MASTER » ; en anglais" \
    "$([ "$(champ attenue titre)" = "MASTER · DIM" ] && [ "$(champ pleine titre)" = "MASTER" ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ECOUTE-ATTENUEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ECOUTE-ATTENUEE : ce qu'on entend baisse de 20 dB, ce que le master mesure et ce qu'on exporte ne bougent pas"
