#!/usr/bin/env bash
# D588.3 — LA PROFONDEUR DE L'HISTORIQUE, ET LE PAS OUBLIÉ DIT ; D588.4 — LA MARQUE « NON ENREGISTRÉ » LUE À L'IDENTITÉ
# DE L'ÉTAT, PAS À UNE PROFONDEUR. Mesurées par le nombre de pas, les lignes du journal, un relevé de la fenêtre et la
# marque du titre, le témoin (le binaire de D587, passé en argument) à côté.
#
# LES RÈGLES GARDÉES. Une piste MIDI ; chaque geste `volume:` ouvre un pas (D172 : trente gestes, trente pas), avec une
# valeur chaque fois différente — un geste qui ne change rien peut être retiré (D511). `relever-historique` dit le
# nombre de pas (`VSM_HISTORIQUE_PAS`), la profondeur, le compte des oubliés et l'en-tête que la fenêtre écrit
# (`VSM_HISTORIQUE_PROFONDEUR`) ; `relever-titre` la marque (`VSM_TITRE_ETAT`, D507) ; `VSM_HISTORIQUE_OUBLI` dit chaque
# sortie au moment où elle a lieu.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D588) :
#   (1) 130 gestes à la profondeur d'origine : 128 pas, 2 lignes « pas oublié », l'en-tête « 2 pas plus anciens
#       oubliés (profondeur 128) » ;
#   (2) la profondeur à 64, puis 70 gestes : 64 pas, 6 oubliés ;
#   (3) 70 gestes à 128, puis la profondeur à 64 : 64 pas, 6 oubliés d'un coup, dits en UNE ligne ;
#   (4) relancé dans le même HOME : 64 retenu ; en anglais, « Undo history depth » ;
#   (a) 130 gestes, enregistrer, un geste : « non enregistré : oui » ;
#   (b) deux gestes, enregistrer, Ctrl+Z (« oui »), un geste neuf : « oui » ;
#   (c) le témoin du retour vrai — enregistrer, un geste, Ctrl+Z : « non » (sans lui, une marque bloquée à « oui »
#       passerait (a) et (b)) ;
#   le TÉMOIN (le binaire de D587) : (1) 128 pas et AUCUNE ligne ; (a) et (b) « non ».
#
#   tools/historique-profondeur.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-historique.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + b"\x00\x90\x3c\x5a\x83\x60\x80\x3c\x00" + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "historique", "midi": {"file": "midi/arrangement.mid"},
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
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# n gestes `volume:` à partir de t0 ms, espacés de 25 ms, chacun d'une valeur différente ; écrit « t:volume:v;… ».
gestes() {
    python3 -c "import sys; n, t0, v0 = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]); print(';'.join(f'{t0 + 25 * k}:volume:{v0 + 0.002 * k:.3f}' for k in range(n)))" "$1" "$2" "$3"
}
pas() { grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_HISTORIQUE_PAS : \([0-9]*\) : .*/\1/p'; }
# Les champs par leur RANG, pas par leur nom : l'en-tête répète « oubliés » et « profondeur », et un `.*` gourmand
# s'arrêtait sur le sien.
champ() {
    local n; case "$2" in profondeur) n=1 ;; *) n=2 ;; esac
    grep -h "^VSM_HISTORIQUE_PROFONDEUR : " "$brouillon/$1.txt" | tail -1 \
        | sed -n "s/^VSM_HISTORIQUE_PROFONDEUR : profondeur \([0-9]*\) ; oubliés \([0-9]*\) ; .*/\\$n/p"
}
entete() { grep -h "^VSM_HISTORIQUE_PROFONDEUR : " "$brouillon/$1.txt" | tail -1 | sed -n 's/.*fenêtre « \(.*\) »$/\1/p'; }
oublis() { grep -c "^VSM_HISTORIQUE_OUBLI : " "$brouillon/$1.txt"; }
marques() { grep -h "^VSM_TITRE_ETAT : " "$brouillon/$1.txt" | sed -n 's/.*(non enregistre : \(oui\|non\))$/\1/p' | tr '\n' ' ' | sed 's/ $//'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
P64="menu:Édition > Profondeur de l'historique > 64 pas"

echo "=== D588.3 : la profondeur de l'historique, et le pas oublié dit ==="
course plein "" VSM_DELAI=4300 VSM_GESTE_APRES="$(gestes 130 400 0.300);3900:relever-historique"
course p64 "" VSM_DELAI=2900 VSM_GESTE_APRES="300:$P64;$(gestes 70 500 0.300);2500:relever-historique"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course apres64 "$maison" VSM_DELAI=2900 VSM_GESTE_APRES="$(gestes 70 300 0.300);2300:$P64;2500:relever-historique"
course retenu "$maison" VSM_DELAI=1400 VSM_GESTE_APRES="600:relever-historique;800:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1400 VSM_GESTE_APRES="800:lister-menus"

echo "=== D588.4 : la marque « non enregistré », lue à l'identité de l'état ==="
course plein-marque "" VSM_DELAI=4800 \
    VSM_GESTE_APRES="$(gestes 130 400 0.300);3800:enregistrer:$brouillon/plein-ecrit;4000:relever-titre;4200:volume:0.900;4400:relever-titre"
course annule-refait "" VSM_DELAI=2600 \
    VSM_GESTE_APRES="$(gestes 2 400 0.300);800:enregistrer:$brouillon/annule-ecrit;1000:relever-titre;1200:touche:ctrl + Z;1400:relever-titre;1600:volume:0.900;1800:relever-titre"
course retour "" VSM_DELAI=2200 \
    VSM_GESTE_APRES="400:enregistrer:$brouillon/retour-ecrit;600:relever-titre;800:volume:0.900;1000:relever-titre;1200:touche:ctrl + Z;1400:relever-titre"

for n in plein p64 apres64; do
    echo "       $n : $(pas $n) pas ; profondeur $(champ $n profondeur) ; $(champ $n oubliés) oublié(s) ; $(oublis $n) ligne(s) d'oubli ; en-tête « $(entete $n) »"
done
grep -h "^VSM_HISTORIQUE_OUBLI : " "$brouillon/plein.txt" "$brouillon/apres64.txt" | cut -c1-150 | sed 's/^/       /'
retenu_p="$(champ retenu profondeur)"
en="$(grep -c "^VSM_MENU_LISTE : Edit > Undo history depth > 128 steps (default) \[cochée\]" "$brouillon/anglais.txt")"
echo "       relancé : profondeur $retenu_p ; en anglais : $en entrée cochée"
for n in plein-marque annule-refait retour; do echo "       $n : marques $(marques $n)"; done

verdict "(1) 130 gestes : 128 pas, 2 lignes d'oubli, l'en-tête « 2 pas plus anciens oubliés (profondeur 128) »" \
    "$([ "$(pas plein)" = 128 ] && [ "$(oublis plein)" = 2 ] && entete plein | grep -q "^2 pas plus anciens oubliés (profondeur 128)" && echo 1 || echo 0)"
verdict "(2) la profondeur à 64, puis 70 gestes : 64 pas, 6 oubliés" \
    "$([ "$(pas p64)" = 64 ] && [ "$(champ p64 oubliés)" = 6 ] && [ "$(oublis p64)" = 6 ] && echo 1 || echo 0)"
verdict "(3) 70 gestes, puis la profondeur à 64 : 64 pas, 6 oubliés dits en une ligne" \
    "$([ "$(pas apres64)" = 64 ] && [ "$(champ apres64 oubliés)" = 6 ] && [ "$(oublis apres64)" = 1 ] \
        && grep -q "^VSM_HISTORIQUE_OUBLI : 6 pas oubliés d'un coup" "$brouillon/apres64.txt" && echo 1 || echo 0)"
verdict "(4) relancé : 64 retenu ; en anglais" "$([ "$retenu_p" = 64 ] && [ "$en" = 1 ] && echo 1 || echo 0)"
verdict "(a) historique plein, enregistrer, un geste : « non enregistré : oui »" \
    "$([ "$(marques plein-marque)" = "non oui" ] && echo 1 || echo 0)"
verdict "(b) enregistrer, Ctrl+Z, un geste neuf : « oui »" "$([ "$(marques annule-refait)" = "non oui oui" ] && echo 1 || echo 0)"
verdict "(c) le témoin du retour vrai — enregistrer, un geste, Ctrl+Z : « non »" \
    "$([ "$(marques retour)" = "non oui non" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "HISTORIQUE-PROFONDEUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "HISTORIQUE-PROFONDEUR : le pas qui sort de l'historique est dit, et la marque suit l'état, pas la profondeur"
