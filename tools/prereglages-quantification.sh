#!/usr/bin/env bash
# D570.2 — LES PRÉRÉGLAGES DE QUANTIFICATION (Édition ▸ Préréglages de quantification ; les *Quantize Presets* de
# Cubase), MESURÉS PAR LA BARRE RELUE (`relever-grille`, le relevé de D495).
#
# LES RÈGLES GARDÉES. Premier lancement : la barre posée à 1/8, triolet, swing 50 % (les listes de la barre, le chemin
# de D495), puis « Enregistrer les réglages de quantification… » sous le nom « Triolets ». Second lancement, même HOME :
#   (1) le témoin : la barre à son défaut, 1/16 droit, swing 0 ;
#   (2) « Triolets » rappelé : 1/8, triolet, swing 0,50 — le piano roll ET la barre ; la gamme non touchée ;
#   (3) « Retirer un préréglage ▸ Triolets » : le menu ne le propose plus (« (aucun préréglage) ») ;
#   (4) un HOME neuf ne le connaît pas : les préréglages sont au poste, pas au morceau.
#
#   tools/prereglages-quantification.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-prereglages.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

course() {   # $1 = nom ; $2 = HOME ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    env HOME="$maison" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" VSM_DELAI=2500 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|LISTE|VALEUR_POSEE)[A-Z_]* : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
grille() { grep -h "^VSM_PIANOROLL_GRILLE : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_PIANOROLL_GRILLE : //'; }
MENU="Édition > Préréglages de quantification"

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D570.2 : les préréglages de quantification ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course enregistrer "$maison" VSM_OPTIONS="nom=Triolets" \
    VSM_GESTE_PISTE="liste:pianoroll.grille=1/8;liste:pianoroll.grille.modificateur=Triolet;valeur:pianoroll.swing=0.5" \
    VSM_GESTE_APRES="1200:menu:$MENU > Enregistrer les réglages de quantification…"
course rappeler "$maison" VSM_GESTE_APRES="900:relever-grille;1200:menu:$MENU > Triolets;1500:relever-grille"
course retirer "$maison" VSM_GESTE_APRES="1200:menu:$MENU > Retirer un préréglage > Triolets;1500:lister-menus"
course neuf "$(mktemp -d "$brouillon/home.XXXX")" VSM_GESTE_APRES="1000:lister-menus"
avant="$(grep -h "^VSM_PIANOROLL_GRILLE : " "$brouillon/rappeler.txt" | head -1 | sed 's/^VSM_PIANOROLL_GRILLE : //')"
apres="$(grille rappeler)"
grep -h "^VSM_QUANTIFICATION" "$brouillon/enregistrer.txt" "$brouillon/rappeler.txt" "$brouillon/retirer.txt" | sed 's/^/       /'
echo "       avant le rappel : $avant"
echo "       après           : $apres"
aucun() { grep -h "^VSM_MENU_LISTE : $MENU > " "$brouillon/$1.txt" | grep -v "Enregistrer les réglages" | sed "s/^VSM_MENU_LISTE : $MENU > //" | tr '\n' ';'; }
echo "       menu après retrait : $(aucun retirer) ; HOME neuf : $(aucun neuf)"

verdict "(1) le témoin : 1/16 droit, swing 0" "$(echo "$avant" | grep -q "^grille=1/16 modificateur=droit swing=0.00" && echo 1 || echo 0)"
verdict "(2) « Triolets » rappelé : 1/8 triolet swing 0,50, au piano roll et à la barre ; la gamme intacte" \
    "$(echo "$apres" | grep -q "^grille=1/8 modificateur=triplet swing=0.50 tonique=0 mode=0 surlignage=non | barre : 1/8, Triolet, 50 %" && echo 1 || echo 0)"
verdict "(3) retiré : le menu ne le propose plus" "$([ "$(aucun retirer)" = "(aucun préréglage) [grisée];" ] && echo 1 || echo 0)"
verdict "(4) un HOME neuf ne le connaît pas" "$([ "$(aucun neuf)" = "(aucun préréglage) [grisée];" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PREREGLAGES-QUANTIFICATION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PREREGLAGES-QUANTIFICATION : la grille, le modificateur et le swing se gardent sous un nom et se rappellent"
