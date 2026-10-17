#!/usr/bin/env bash
# D561.1 — CHOISIR LES CLIPS DES PISTES CHOISIES (Édition ▸ Sélectionner dans l'arrangement ▸ Les clips des pistes
# choisies (N) ; le *Select ▸ All on Selected Tracks* de Cubase), MESURÉ PAR LE RELEVÉ DE LA SÉLECTION ET PAR LE PROJET
# RELU APRÈS UN GESTE QUI AGIT SUR ELLE (Suppr).
#
# LES RÈGLES GARDÉES. Cinq pistes : « Dossier » (un dossier) qui contient « A » (a1, a2) ; « B » (b1, b2) ; « C » (c1) ;
# « Vide » (aucun clip).
#   (1) B choisie : le menu dit « (2) », la sélection en prend 2, Suppr ne retire que b1 et b2 ;
#   (2) B et C choisies (Maj+Alt+↓) : « (3) », Suppr retire b1, b2 et c1 ;
#   (3) le DOSSIER choisi apporte son contenu : « (2) », Suppr retire a1 et a2 ;
#   (4) « Vide » choisie : « (0) » GRISÉE ;
#   (5) la sélection est REMPLACÉE : « Tout sélectionner » d'abord, puis l'entrée avec B choisie — 2, et non 5.
#
#   tools/clips-des-pistes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-clips-pistes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, notes, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90 | canal, 60, 100]); corps += vlq(1440) + bytes([0x80 | canal, 60, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 5, 480) + piste("Dossier", 0, [], tempo=True) + piste("A", 1, [0, 3840])
    + piste("B", 2, [0, 3840]) + piste("C", 3, [1920]) + piste("Vide", 4, []))
clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
def entree(n, c, clips, **x):
    return dict({"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                 "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}, **x)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Dossier", 0, [], kind="folder", folderDepth=0),
                      entree("A", 1, [clip("a1", 0), clip("a2", 3840)], folderDepth=1),
                      entree("B", 2, [clip("b1", 0), clip("b2", 3840)]),
                      entree("C", 3, [clip("c1", 1920)]),
                      entree("Vide", 4, [])]},
          open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # les noms des clips qui restent, triés, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1] + '/project.json'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(sorted(x.get('name', '?') for t in p['tracks'] for x in t.get('clips', []))) or 'AUCUN')" "$1"
}
entree() {   # l'entrée telle que le menu Édition la liste
    grep -h "^VSM_MENU_LISTE : .*Les clips des pistes choisies" "$1" | tail -1 | sed 's/^VSM_MENU_LISTE : .*> //'
}
choisis() {   # le nombre de clips choisis que le geste a dit
    grep -h "^VSM_SELECTION_CLIPS : [0-9]* clip(s) choisi(s) (" "$1" | tail -1 | sed 's/^VSM_SELECTION_CLIPS : \([0-9]*\) .*/\1/'
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
ENTREE="menu:Édition > Sélectionner dans l'arrangement > Les clips des pistes choisies"

echo "=== D561.1 : choisir les clips des pistes choisies ==="
for x in b bc d; do cp -r "$brouillon/P" "$brouillon/$x"; done
course b "$brouillon/b" VSM_DELAI=2600 VSM_VUE="sans-rapport,arrangement,piste:2" \
    VSM_GESTE_APRES="800:lister-menus;1100:$ENTREE (2);1500:touche:arrangement:delete;2000:enregistrer:$brouillon/b"
course bc "$brouillon/bc" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement,piste:2" \
    VSM_GESTE_APRES="700:touche:shift + alt + cursor down;1000:lister-menus;1300:$ENTREE (3);1700:touche:arrangement:delete;2200:enregistrer:$brouillon/bc"
course dossier "$brouillon/d" VSM_DELAI=2600 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="800:lister-menus;1100:$ENTREE (2);1500:touche:arrangement:delete;2000:enregistrer:$brouillon/d"
course vide "$brouillon/P" VSM_DELAI=1800 VSM_VUE="sans-rapport,arrangement,piste:4" VSM_GESTE_APRES="800:lister-menus"
course remplace "$brouillon/P" VSM_DELAI=2200 VSM_VUE="sans-rapport,arrangement,piste:2,tout-choisir" \
    VSM_GESTE_APRES="1000:$ENTREE (2)"

cb="$(clips "$brouillon/b")"; cbc="$(clips "$brouillon/bc")"; cd_="$(clips "$brouillon/d")"
eb="$(entree "$brouillon/b.txt")"; ebc="$(entree "$brouillon/bc.txt")"; ed="$(entree "$brouillon/dossier.txt")"; ev="$(entree "$brouillon/vide.txt")"
nb="$(choisis "$brouillon/b.txt")"; nbc="$(choisis "$brouillon/bc.txt")"; nd="$(choisis "$brouillon/dossier.txt")"; nr="$(choisis "$brouillon/remplace.txt")"
echo "       B : [$eb] → $nb choisi(s), reste [$cb]"
echo "       B et C : [$ebc] → $nbc, reste [$cbc]"
echo "       Dossier : [$ed] → $nd, reste [$cd_]"
echo "       Vide : [$ev] ; après « tout choisir », B : $nr"

verdict "(1) B : « (2) », deux choisis, Suppr ne retire qu'eux" \
    "$([ "$eb" = "Les clips des pistes choisies (2)" ] && [ "$nb" = 2 ] && [ "$cb" = "a1 a2 c1" ] && echo 1 || echo 0)"
verdict "(2) B et C : « (3) », Suppr retire b1, b2, c1" \
    "$([ "$ebc" = "Les clips des pistes choisies (3)" ] && [ "$nbc" = 3 ] && [ "$cbc" = "a1 a2" ] && echo 1 || echo 0)"
verdict "(3) le dossier apporte son contenu : a1 et a2" \
    "$([ "$ed" = "Les clips des pistes choisies (2)" ] && [ "$nd" = 2 ] && [ "$cd_" = "b1 b2 c1" ] && echo 1 || echo 0)"
verdict "(4) une piste sans clip : « (0) » grisée" "$(echo "$ev" | grep -q "^Les clips des pistes choisies (0) \[grisée\]" && echo 1 || echo 0)"
verdict "(5) la sélection est remplacée : 2 après « tout choisir »" "$([ "$nr" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CLIPS-DES-PISTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CLIPS-DES-PISTES : la sélection prend les clips des pistes choisies, et ceux d'un dossier choisi"
