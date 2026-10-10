#!/usr/bin/env bash
# D549.3 — LES DISPOSITIONS NOMMÉES, MESURÉES PAR LE RELEVÉ DE LA FENÊTRE ET LES PRÉFÉRENCES RELUES.
#
# LA RÈGLE GARDÉE. Le projet de démonstration ; un HOME de banc gardé d'un lancement à l'autre — c'est tout
# l'objet : une disposition se retrouve au lancement suivant.
#   (1) lancement 1 : le rack masqué (`sans-rack`), le volet du bas à 420 px (`VSM_DOCK_BAS`), « Enregistrer la
#       disposition… » sous le nom « Mixage » → le journal dit « enregistrée « Mixage » : … rack non … bas oui,
#       volets …/…/420 » ; le piano roll au centre (`pianoroll`), enregistrée sous « Édition » ;
#   (2) lancement 2, même HOME, la disposition par défaut (rack montré) : le relevé dit « rack oui » — le
#       TÉMOIN —, puis « Affichage > Dispositions > Mixage » → « rack non, bas 420 », centre arrangement ;
#   (3) lancement 3 : « Édition » rappelée → centre pianoroll ;
#   (4) « Mixage » enregistrée une seconde fois : remplacée, et la boîte le dit ; « Retirer une disposition >
#       Édition » → elle n'est plus au menu (relevé des menus), « Mixage » y reste ;
#   (5) le menu photographié en français et en anglais (regardé à la main) ;
#   (6) les préférences de l'UTILISATEUR intactes (le HOME est celui du banc).
#
#   tools/dispositions.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
DEMO="$racine/docs/examples/demo-project"
[ -d "$DEMO" ] || { echo "REFUS : $DEMO absent"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-dispositions.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT
cp -r "$DEMO" "$brouillon/projet"
maison="$(mktemp -d "$brouillon/home.XXXX")"   # UN HOME pour toute la série : c'est ce qu'on mesure

course() {   # $1 = nom ; le reste : variables
    local nom="$1"
    shift
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
    sleep 2
}
ligne() { grep -h "^VSM_DISPOSITION : $2" "$brouillon/$1.txt" | tail -1; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SAUVER="menu:Affichage > Dispositions > Enregistrer la disposition…"

echo "=== D549.3 : les dispositions nommées ==="
course sauver-mixage VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,sans-rack" VSM_DOCK_BAS=420 VSM_OPTIONS="nom=Mixage" \
    VSM_GESTE_APRES="1200:$SAUVER"
course sauver-edition VSM_DELAI=2500 VSM_VUE="sans-rapport,pianoroll" VSM_OPTIONS="nom=Édition" \
    VSM_GESTE_APRES="1200:$SAUVER"
course rappeler-mixage VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:relever-disposition;1500:menu:Affichage > Dispositions > Mixage;2000:relever-disposition"
course rappeler-edition VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:menu:Affichage > Dispositions > Édition;1700:relever-disposition"
course remplacer VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,sans-rack" VSM_OPTIONS="nom=Mixage" \
    VSM_GESTE_APRES="1200:$SAUVER"
course retirer VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:menu:Affichage > Dispositions > Retirer une disposition > Édition;1800:lister-menus"

lm="$(ligne sauver-mixage enregistrée)"; le="$(ligne sauver-edition enregistrée)"
avant="$(grep -h "^VSM_DISPOSITION : courante" "$brouillon/rappeler-mixage.txt" | head -1)"
apres="$(grep -h "^VSM_DISPOSITION : courante" "$brouillon/rappeler-mixage.txt" | tail -1)"
rm_="$(ligne rappeler-mixage rappelée)"; re_="$(grep -h "^VSM_DISPOSITION : courante" "$brouillon/rappeler-edition.txt" | tail -1)"
echo "       ${lm:-mixage non enregistrée}"
echo "       ${le:-édition non enregistrée}"
echo "       avant le rappel : ${avant:-aucun relevé}"
echo "       après « Mixage » : ${apres:-aucun relevé}"
echo "       après « Édition » : ${re_:-aucun relevé}"
grep -h "^VSM_DISPOSITION : remplacée\|^VSM_BOITE : Enregistrer la disposition" "$brouillon/remplacer.txt" | sed 's/^/       /' | cut -c1-140
grep -h "^VSM_DISPOSITION : retirée" "$brouillon/retirer.txt" | sed 's/^/       /'
menus="$(grep -h "Dispositions" "$brouillon/retirer.txt" | grep -v "^VSM_DISPOSITION" | tr '\n' ' ')"

verdict "(1) enregistrées : « Mixage » (rack non, bas 420), « Édition » (centre pianoroll)" \
    "$(echo "$lm" | grep -q "« Mixage » : fenêtre unique, centre arrangement, pistes oui, rack non, bas oui, volets [0-9]*/[0-9]*/420$" \
        && echo "$le" | grep -q "« Édition » : fenêtre unique, centre pianoroll" && echo 1 || echo 0)"
verdict "(2) rappelée au lancement suivant : le témoin montre le rack, « Mixage » le masque, bas à 420" \
    "$(echo "$avant" | grep -q "rack oui" && echo "$apres" | grep -q "centre arrangement, pistes oui, rack non, bas oui, volets [0-9]*/[0-9]*/420$" \
        && [ -n "$rm_" ] && echo 1 || echo 0)"
verdict "(3) « Édition » rappelée : le piano roll au centre" "$(echo "$re_" | grep -q "centre pianoroll" && echo 1 || echo 0)"
verdict "(4) remplacée et dite ; « Édition » retirée, « Mixage » reste au menu" \
    "$(grep -q "^VSM_DISPOSITION : remplacée « Mixage »" "$brouillon/remplacer.txt" \
        && grep -q "^VSM_BOITE : Enregistrer la disposition : La disposition « Mixage » existait" "$brouillon/remplacer.txt" \
        && grep -q "^VSM_DISPOSITION : retirée « Édition »" "$brouillon/retirer.txt" \
        && echo "$menus" | grep -q "Dispositions > Mixage" && ! echo "$menus" | grep -q "Dispositions > Édition" && echo 1 || echo 0)"

photos=0
for langue in fr en; do
    entree="Affichage > Dispositions"; [ "$langue" = en ] && entree="View > Layouts"
    env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" \
        VSM_DELAI=2500 VSM_MENU_PHOTO="$entree:$brouillon/menu-$langue.png" VSM_CAPTURE="$brouillon/fenetre-$langue.png" timeout 60 "$BIN" \
        > "$brouillon/menu-$langue.txt" 2>&1
    grep -E "VSM_MENU_PHOTO : .*(aucun|AUCUN|introuvable)" "$brouillon/menu-$langue.txt" | sed "s/^/        journal (menu-$langue) : /" >&2
    [ -s "$brouillon/menu-$langue.png" ] && ! grep -qE "VSM_MENU_PHOTO : .*(aucun|AUCUN|introuvable)" "$brouillon/menu-$langue.txt" \
        && photos=$((photos + 1))
    sleep 3
done
echo "       photos du menu : $brouillon/menu-fr.png, menu-en.png (VSM_GARDER pour les garder)"
verdict "(5) le menu photographié en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DISPOSITIONS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DISPOSITIONS : une disposition nommée se garde, se rappelle au lancement suivant, se remplace et se retire"
