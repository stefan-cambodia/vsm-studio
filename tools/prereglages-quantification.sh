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
# D635 — LA LONGUEUR D'INSERTION DANS LE PRÉRÉGLAGE (« Insertion : 1/8 » choisie avant d'enregistrer « Triolets ») :
#   (5) le relevé d'enregistrement dit « insertion 1/8 » ; « Triolets » rappelé au second lancement (la longueur partie
#       de la grille) : le sous-menu coche « Insertion : 1/8 » ;
#   (6) un préréglage d'AVANT (enregistré par le binaire témoin, sans longueur), rappelé après « Insertion : 1/4 » : la
#       coche revient sur « Insertion : la grille » — le défaut, la règle de D575 ;
#   (7) le témoin (le binaire d'avant D635) rappelle « Triolets » : la coche reste sur « Insertion : la grille ».
#
#   tools/prereglages-quantification.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
TEMOIN="${2:-./build/temoin-D633/Vintage Synth MIDI Studio}"   # D635 : un binaire d'avant la longueur au préréglage

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-prereglages.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

course() {   # $1 = nom ; $2 = HOME ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    env HOME="$maison" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" VSM_DELAI=2500 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "${BINAIRE:-$BIN}" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|LISTE|VALEUR_POSEE)[A-Z_]* : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
grille() { grep -h "^VSM_PIANOROLL_GRILLE : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_PIANOROLL_GRILLE : //'; }
MENU="Édition > Préréglages de quantification"
INS="menu:Édition > Temps et durée > Longueur d'insertion > Insertion"   # D635 : le chemin de `pas-a-pas-longueur`
cochee() { grep -h "^VSM_MENU_LISTE : .*Insertion : .*\[cochée\]" "$brouillon/$1.txt" | sed -n 's/.*\(Insertion : [^[]*\) \[cochée\].*/\1/p' | tr '\n' ';'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D570.2 : les préréglages de quantification ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course enregistrer "$maison" VSM_OPTIONS="nom=Triolets" \
    VSM_GESTE_PISTE="liste:pianoroll.grille=1/8;liste:pianoroll.grille.modificateur=Triolet;valeur:pianoroll.swing=0.5" \
    VSM_GESTE_APRES="900:$INS : 1/8;1200:menu:$MENU > Enregistrer les réglages de quantification…"
course rappeler "$maison" VSM_GESTE_APRES="900:relever-grille;1200:menu:$MENU > Triolets;1500:relever-grille;1800:lister-menus"
# D635 (7) : le témoin rappelle le même préréglage, dans le même HOME, avant le retrait.
temoin_joue=0
if [ -x "$TEMOIN" ]; then
    BINAIRE="$TEMOIN" course t-rappeler "$maison" VSM_GESTE_APRES="1200:menu:$MENU > Triolets;1800:lister-menus"
    # D635 (6) : un préréglage d'AVANT, enregistré par le témoin dans un HOME à lui, rappelé par le binaire neuf.
    ancien="$(mktemp -d "$brouillon/home.XXXX")"
    BINAIRE="$TEMOIN" course t-ancien "$ancien" VSM_OPTIONS="nom=Ancien" \
        VSM_GESTE_APRES="900:$INS : 1/8;1200:menu:$MENU > Enregistrer les réglages de quantification…"
    course ancien "$ancien" VSM_GESTE_APRES="900:$INS : 1/4;1200:menu:$MENU > Ancien;1800:lister-menus"
    temoin_joue=1
fi
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
echo "       (5) coche après rappel : $(cochee rappeler)"
verdict "(5) l'enregistrement dit « insertion 1/8 »" "$(grep -h "^VSM_QUANTIFICATION : enregistré" "$brouillon/enregistrer.txt" | grep -q "insertion 1/8" && echo 1 || echo 0)"
verdict "(5) « Triolets » rappelé : « Insertion : 1/8 » cochée" "$([ "$(cochee rappeler)" = "Insertion : 1/8;" ] && echo 1 || echo 0)"
if [ "$temoin_joue" = 1 ]; then
    echo "       (6) préréglage d'avant : $(grep -h "^VSM_QUANTIFICATION : rappel" "$brouillon/ancien.txt" | head -1) ; coche $(cochee ancien)"
    echo "       (7) témoin : coche $(cochee t-rappeler)"
    verdict "(6) un préréglage d'avant, rappelé après « 1/4 » : « Insertion : la grille » cochée" "$([ "$(cochee ancien)" = "Insertion : la grille;" ] && echo 1 || echo 0)"
    verdict "(7) le témoin (avant D635) rappelle « Triolets » : « Insertion : la grille » — le défaut" "$([ "$(cochee t-rappeler)" = "Insertion : la grille;" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : (6) et (7) ne sont PAS joués"
fi

echo
if [ "$rates" -gt 0 ]; then echo "PREREGLAGES-QUANTIFICATION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PREREGLAGES-QUANTIFICATION : la grille, le modificateur, le swing et la longueur d'insertion se gardent sous un nom et se rappellent"
