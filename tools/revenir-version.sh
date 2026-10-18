#!/usr/bin/env bash
# D563.2 — REVENIR À LA VERSION ENREGISTRÉE (Fichier ▸ Revenir à la version enregistrée… ; le *Revert* de Cubase),
# MESURÉ PAR L'ÉTAT RECHARGÉ.
#
# LES RÈGLES GARDÉES. Un projet enregistré avec UN repère ; on en pose un second (le projet est modifié).
#   (1) confirmé (VSM_CONFIRMER=oui) : le projet rechargé n'a plus qu'un repère, et le titre n'a plus d'astérisque ;
#       la boîte disait que le geste ne s'annule pas ;
#   (2) refusé (VSM_CONFIRMER=non) : les deux repères restent ;
#   (3) rien de modifié : l'entrée est GRISÉE.
#
#   tools/revenir-version.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-revenir.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 0, "name": "Début"}],
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
reperes() { python3 -c "
import json, sys
try:
    print(len(json.load(open(sys.argv[1] + '/project.json', encoding='utf-8')).get('markers', [])))
except Exception:
    print('ABSENT')" "$1"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REVENIR="menu:Fichier > Revenir à la version enregistrée..."
VUE="sans-rapport,tete-temps:4"

echo "=== D563.2 : revenir à la version enregistrée ==="
course oui "$brouillon/P" VSM_DELAI=2600 VSM_VUE="$VUE" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="900:touche:insert;1200:lister-menus;1500:$REVENIR;2000:enregistrer:$brouillon/apres-oui"
course oui-titre "$brouillon/P" VSM_DELAI=2400 VSM_VUE="$VUE" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="900:touche:insert;1500:$REVENIR"
course non "$brouillon/P" VSM_DELAI=2600 VSM_VUE="$VUE" VSM_CONFIRMER=non \
    VSM_GESTE_APRES="900:touche:insert;1500:$REVENIR;2000:enregistrer:$brouillon/apres-non"
course intact "$brouillon/P" VSM_DELAI=1800 VSM_VUE="sans-rapport" VSM_GESTE_APRES="900:lister-menus"

ro="$(reperes "$brouillon/apres-oui")"; rn="$(reperes "$brouillon/apres-non")"; rp="$(reperes "$brouillon/P")"
modifie="$(grep -h "^VSM_MENU_LISTE : Fichier > Revenir" "$brouillon/oui.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Fichier > //')"
intact="$(grep -h "^VSM_MENU_LISTE : Fichier > Revenir" "$brouillon/intact.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Fichier > //')"
titre="$(grep -h "^VSM_TITRE" "$brouillon/oui-titre.txt" | head -1)"
grep -h "^VSM_REVENIR\|^VSM_BOITE : Revenir" "$brouillon/oui.txt" "$brouillon/non.txt" | sed 's/^/       /' | cut -c1-200
echo "       repères — revenu : [$ro] ; refusé : [$rn] ; sur le disque : [$rp] ; menu modifié [$modifie] ; intact [$intact]"
echo "       $titre"

verdict "(1) confirmé : un repère, plus d'astérisque, et la boîte disait que cela ne s'annule pas" \
    "$([ "$ro" = 1 ] && grep -q "^VSM_REVENIR : recharg" "$brouillon/oui.txt" && [ -n "$titre" ] && ! echo "$titre" | grep -q ' \*$' \
        && grep -q "^VSM_BOITE : Revenir à la version enregistrée : .*ne s'annule pas" "$brouillon/oui.txt" && echo 1 || echo 0)"
verdict "(2) refusé : les deux repères restent" \
    "$([ "$rn" = 2 ] && grep -q "^VSM_REVENIR : refus" "$brouillon/non.txt" && echo 1 || echo 0)"
verdict "(3) modifié : active ; rien de modifié : grisée" \
    "$([ "$modifie" = "Revenir à la version enregistrée..." ] && [ "$intact" = "Revenir à la version enregistrée... [grisée]" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REVENIR-VERSION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REVENIR-VERSION : le projet revient à son dernier enregistrement, après confirmation, et seulement s'il a changé"
