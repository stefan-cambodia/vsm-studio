#!/usr/bin/env bash
# D565.3 — LES NOTES D'UNE PISTE (Piste ▸ Notes de la piste… ; les *Track Notes* de Cubase), MESURÉES PAR LE PROJET
# RELU ET PAR L'INFOBULLE DU NOM.
#
# LES RÈGLES GARDÉES. Deux pistes, « Basse » et « Nappe ».
#   (1) la fenêtre ouverte sur « Basse », un texte saisi (deux lignes, des accents), puis la piste suivante choisie — la
#       fenêtre la SUIT — et un autre texte : le projet relu porte les deux, chacune la sienne ;
#   (2) le projet est « modifié » par la frappe (le titre a son astérisque), sans pas d'annulation ;
#   (3) rouvert : l'infobulle du nom de « Basse » porte ses notes, et le menu dit le compte de caractères.
#
#   tools/notes-piste.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-notes-piste.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode() + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Basse", True) + piste("Nappe"))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}} for c, n in enumerate(("Basse", "Nappe"))]},
          open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|SAISIE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|aucun)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
memos() { python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + '=' + t.get('memo', '').replace(chr(10), '/') for t in d['tracks']))" "$1"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D565.3 : les notes d'une piste ==="
cp -r "$brouillon/P" "$brouillon/ecrit"
course ecrire "$brouillon/ecrit" VSM_DELAI=3200 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="800:menu:Piste > Notes de la piste... (vides);1200:saisir:piste.notes=Prise 3 : la bonne.\nRefaire l'attaque — « plus doux »;1500:relever-titre;1700:touche:alt + cursor down;2100:saisir:piste.notes=Nappe à doubler;2600:enregistrer:$brouillon/ecrit"
course relire "$brouillon/ecrit" VSM_DELAI=1800 VSM_VUE="sans-rapport,piste:0" VSM_TEXTES_LISTE=1 VSM_GESTE_APRES="900:lister-menus"

m="$(memos "$brouillon/ecrit")"
titre="$(grep -h "^VSM_TITRE_ETAT" "$brouillon/ecrire.txt" | head -1)"   # le journal de la course « ecrire »
bulle="$(grep -h "infobulle" "$brouillon/relire.txt" | grep -m1 "Prise 3")"
entree="$(grep -h "^VSM_MENU_LISTE : Piste > Notes de la piste" "$brouillon/relire.txt" | head -1 | sed 's/^VSM_MENU_LISTE : Piste > //')"
grep -h "^VSM_NOTES_PISTE\|^VSM_SAISIE" "$brouillon/ecrire.txt" | sed 's/^/       /' | cut -c1-160
echo "       projet relu : [$m]"
echo "       titre après la frappe : [$titre]"
echo "       infobulle : [${bulle:0:160}] ; menu : [$entree]"

verdict "(1) chacune ses notes, la fenêtre ayant suivi la piste choisie" \
    "$([ "$m" = "Basse=Prise 3 : la bonne./Refaire l'attaque — « plus doux » | Nappe=Nappe à doubler" ] && echo 1 || echo 0)"
verdict "(2) la frappe marque le projet modifié" "$(echo "$titre" | grep -q ' \*' && echo 1 || echo 0)"
verdict "(3) rouvert : l'infobulle du nom les porte, le menu dit les caractères" \
    "$([ -n "$bulle" ] && echo "$entree" | grep -q "^Notes de la piste... (5[0-9] caractères)" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NOTES-PISTE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NOTES-PISTE : chaque piste garde ses notes, au projet comme dans l'infobulle de son nom"
