#!/usr/bin/env bash
# D577.2 — MASQUER LES PISTES SANS DONNÉES (Piste ▸ Masquer les pistes sans données (N), le *Hide Tracks without Data*
# de Cubase), MESURÉ PAR LE PROJET RELU, LE TÉMOIN « Retirer les pistes vides » À CÔTÉ.
#
# LES RÈGLES GARDÉES. Projet de quatre pistes : « Basse » (des notes), « Vide A » (rien), « Lead » (des notes), « Vide B »
# (rien). L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D577) :
#   (1) le menu dit « (2) » avant le geste ; après, « (0) » et l'entrée grisée ;
#   (2) masquer : les quatre pistes toujours là, « Vide A » et « Vide B » masquées — 2 masquées ;
#   (3) puis *Afficher toutes les pistes* : 0 masquée ;
#   (4) un pas : Ctrl+Z rend 0 masquée ;
#   (5) le TÉMOIN, *Retirer les pistes vides* sur le même projet : 2 pistes restent — la même règle, l'autre geste.
#
#   tools/masquer-pistes-sans-donnees.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-masquer-vides.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, notes=(), tempo=False, canal=0):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90 | canal, 60, 100]); corps += vlq(480) + bytes([0x80 | canal, 60, 0])
        t = debut + 480
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
def entree(nom, canal):
    return {"channel": canal, "color": "#FF6B9BFF", "effects": [], "name": nom, "mix": dict(mix),
            "instrument": {"preferredPlugin": "vsm.minimoog"}}
os.makedirs(f"{b}/P/midi", exist_ok=True)
pistes = [piste("Basse", [0, 960], tempo=True), piste("Vide A"), piste("Lead", [480], canal=2), piste("Vide B")]
open(f"{b}/P/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + b"".join(pistes))
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Basse", 0), entree("Vide A", 1), entree("Lead", 2), entree("Vide B", 3)]},
          open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
pistes() {   # « nom[*] » des pistes, dans l'ordre (* = masquée), ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + ('*' if t.get('hidden') else '') for t in p['tracks']) or 'AUCUNE')
" "$1"
}
entree_menu() {   # $1 = journal ; la DERNIÈRE ligne du menu Piste qui porte l'entrée, puis la première
    grep -h "^VSM_MENU_LISTE : Piste > Masquer les pistes sans données" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
MASQUER="menu:Masquer les pistes sans données"

echo "=== D577.2 : masquer les pistes sans données ==="
for x in Pm Pa Pz Pr; do cp -r "$brouillon/P" "$brouillon/$x"; done
course masquer "$brouillon/Pm" VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="900:lister-menus;1200:$MASQUER;1500:lister-menus;1800:enregistrer:$brouillon/Pm"
course afficher "$brouillon/Pa" VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1000:$MASQUER;1400:menu:Afficher toutes les pistes;1800:enregistrer:$brouillon/Pa"
course annuler "$brouillon/Pz" VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1000:$MASQUER;1400:touche:ctrl + Z;1800:enregistrer:$brouillon/Pz"
course retirer "$brouillon/Pr" VSM_DELAI=2400 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1000:menu:Retirer les pistes vides;1600:enregistrer:$brouillon/Pr"

m="$(pistes "$brouillon/Pm/project.json")"; a="$(pistes "$brouillon/Pa/project.json")"
z="$(pistes "$brouillon/Pz/project.json")"; r="$(pistes "$brouillon/Pr/project.json")"
avant="$(entree_menu "$brouillon/masquer.txt" | head -1)"; apres="$(entree_menu "$brouillon/masquer.txt" | tail -1)"
n_avant="$(entree_menu "$brouillon/masquer.txt" | wc -l)"
# Le nombre d'astérisques, pas une fin de ligne : « Vide A* » est suivi d'une espace après le découpage (la première
# version ancrait `\*$` et comptait 1 sur un projet qui en avait 2).
compte() { [ "$1" = ABSENT ] && echo "-" || echo "$1" | grep -o '\*' | wc -l; }
# (3) et (4) ne prouvent rien sans le masquage DE LA MÊME COURSE : « 0 masquée » est aussi l'état de départ.
masque_ici() { grep -q "^VSM_PISTES_SANS_DONNEES : 2 masquée(s)" "$1" && echo 1 || echo 0; }
grep -h "^VSM_PISTES_SANS_DONNEES\|^VSM_PISTES_VIDES" "$brouillon/masquer.txt" "$brouillon/retirer.txt" | sed 's/^/       /'
echo "       menu avant : [${avant#VSM_MENU_LISTE : }] ; après : [${apres#VSM_MENU_LISTE : }] ($n_avant relevé(s))"
echo "       masquées : [$m] ; puis tout afficher [$a] ; puis Ctrl+Z [$z] ; témoin retirer [$r]"

verdict "(1) le menu dit (2) avant ; (0), grisée, après" \
    "$([ "$n_avant" = 2 ] && [ "${avant#VSM_MENU_LISTE : }" = "Piste > Masquer les pistes sans données (2)" ] \
        && [ "${apres#VSM_MENU_LISTE : }" = "Piste > Masquer les pistes sans données (0) [grisée]" ] && echo 1 || echo 0)"
verdict "(2) les quatre pistes restent, 2 masquées : Vide A et Vide B" \
    "$([ "$m" = "Basse | Vide A* | Lead | Vide B*" ] && [ "$(compte "$m")" = 2 ] && echo 1 || echo 0)"
verdict "(3) masquées puis Afficher toutes les pistes : 0 masquée" \
    "$([ "$(masque_ici "$brouillon/afficher.txt")" = 1 ] && [ "$a" = "Basse | Vide A | Lead | Vide B" ] && echo 1 || echo 0)"
verdict "(4) un pas : masquées puis Ctrl+Z, 0 masquée" \
    "$([ "$(masque_ici "$brouillon/annuler.txt")" = 1 ] && [ "$z" = "Basse | Vide A | Lead | Vide B" ] && echo 1 || echo 0)"
verdict "(5) le témoin Retirer les pistes vides : 2 pistes restent" "$([ "$r" = "Basse | Lead" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MASQUER-PISTES-SANS-DONNEES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MASQUER-PISTES-SANS-DONNEES : les pistes sans données se masquent, rien n'est retiré"
