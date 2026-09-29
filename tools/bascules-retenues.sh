#!/usr/bin/env bash
# LA GARDE DE D493 : LES QUATRE BASCULES DE L'ARRANGEMENT SURVIVENT AU RELANCEMENT.
# ET DE D494 : L'AIMANT, LE SUIVI ET LES FANTÔMES DU PIANO ROLL AUSSI (paires C, D).
# ET DE D496 : LA VÉLOCITÉ DES NOTES DESSINÉES (paire E).
#
# RÈGLE GARDÉE (29/09/2026) : l'aimantation, la grille à la mesure, le suivi de la
# tête et les courbes d'automation de l'arrangement sont des PRÉFÉRENCES (la règle
# de partage de D363 : elles disent comment on travaille, pas ce qu'est le
# morceau). Ce qu'on a basculé — au menu ou à la touche — se retrouve au lancement
# suivant.
#
# COMMENT. Chaque paire joue deux lancements sous le MÊME HOME neuf : le premier
# fait les gestes et relève l'état qu'ils ont produit, le second ne fait RIEN et
# relève l'état retrouvé (`VSM_ARRANGEMENT=1` l'écrit après les gestes, D491). Un
# HOME réutilisé rouvre ses autosauvegardes (D318) : aucun projet n'est ouvert, et
# chaque lancement se termine par sa photo, sans session interrompue.
#
# DEUX PAIRES, PARCE QUE LE RELEVÉ ÉCRIT « aimant libre » quel que soit l'état de la
# grille : la paire A bascule aimantation, suivi et courbes (par le menu), la paire
# B la seule grille (par la touche, `arrangement:M`).
#
# CHAQUE PAIRE PORTE SON TÉMOIN (D145) : l'état après les gestes doit DIFFÉRER du
# défaut — sinon « retrouvé » et « jamais changé » rendraient le même relevé. Et un
# HOME neuf sans gestes doit rendre le défaut : retenir ne change pas le départ.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/bascules-retenues.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-bascules-retenues.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

DEFAUT="aimant=mesure suit=oui courbes=cachée"
n=0
lancement() {   # $1 = HOME ; $2... = variables du geste -> « aimant=… suit=… courbes=… »
    n=$((n + 1))
    env HOME="$1" VSM_TAILLE="1280x742" VSM_DELAI=800 VSM_VUE="sans-rapport,arrangement" \
        VSM_ARRANGEMENT=1 VSM_CAPTURE="$brouillon/photo-$n.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/journal-$n.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147).
    grep -E "VSM_(MENU|TOUCHE|VUE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)|Session interrompue" \
        "$brouillon/journal-$n.txt" | sed 's/^/        journal : /' >&2
    python3 - "$brouillon/journal-$n.txt" <<'PY'
import re, sys
m = re.findall(r"VSM_ARRANGEMENT : aimant (\w+), (suit la tête|ne suit pas), automation (\w+)",
               open(sys.argv[1], encoding="utf-8", errors="replace").read())
if m:
    aimant, suit, courbes = m[-1]
    print(f"aimant={aimant} suit={'oui' if suit == 'suit la tête' else 'non'} courbes={courbes}")
else:
    print("aimant=? suit=? courbes=?")
PY
}

rates=0
paire() {   # $1 nom ; $2 état attendu après les gestes ; $3... gestes
    local nom="$1" attendu="$2" maison apres relance cles
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    apres="$(lancement "$maison" "${@:3}")"
    relance="$(lancement "$maison")"
    cles="$(grep -oh 'name="arrangement[A-Za-z]*"' "$maison"/VintageSynthMidiStudio/*.settings 2>/dev/null | wc -l)"
    if [ "$apres" = "$attendu" ] && [ "$attendu" != "$DEFAUT" ] && [ "$relance" = "$apres" ]; then
        printf '  OK   paire %s : après les gestes « %s », au relancement « %s » ; %s clé(s) écrite(s)\n' "$nom" "$apres" "$relance" "$cles"
    else
        printf '  RATÉ paire %s : après les gestes « %s », au relancement « %s » (attendu « %s » deux fois, différent du défaut) ; %s clé(s) écrite(s)\n' \
               "$nom" "$apres" "$relance" "$attendu" "$cles"
        rates=$((rates + 1))
    fi
}

# D494 : LE PIANO ROLL. Son relevé (`VSM_PIANOROLL_ZONES=1`) dit l'état ET ce que
# le bouton montre (« aimant=non/non » : piano roll / bouton) — un état relu que la
# barre ne montrerait pas serait retenu à moitié.
DEFAUT_PR="aimant=oui/oui suit=oui/oui fantomes=oui/oui velocite=100/100"
lancement_pr() {   # $1 = HOME ; $2... = variables du geste
    n=$((n + 1))
    env HOME="$1" VSM_TAILLE="1600x1000" VSM_DELAI=800 VSM_VUE="sans-rapport" \
        VSM_PIANOROLL_ZONES=1 VSM_CAPTURE="$brouillon/photo-$n.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/journal-$n.txt" 2>&1
    grep -E "VSM_(MENU|TOUCHE|VUE|GESTE|CLIC) : .*(aucune|AUCUNE|aucun |inconnu|illisible|grisée|GRISÉ|AMBIGU)|Session interrompue" \
        "$brouillon/journal-$n.txt" | sed 's/^/        journal : /' >&2
    python3 - "$brouillon/journal-$n.txt" <<'PY2'
import re, sys
m = re.findall(r"VSM_PIANOROLL : aimant (\w+) \(bouton (\w+)\), suit (\w+) \(bouton (\w+)\), fantômes (\w+) \(bouton (\w+)\), vélocité (\d+) \(curseur (\d+)\)",
               open(sys.argv[1], encoding="utf-8", errors="replace").read())
if m:
    a, ab, s, sb, f, fb, v, vc = m[-1]
    print(f"aimant={a}/{ab} suit={s}/{sb} fantomes={f}/{fb} velocite={v}/{vc}")
else:
    print("aimant=? suit=? fantomes=? velocite=?")
PY2
}
paire_pr() {   # $1 nom ; $2 état attendu après les gestes ; $3... gestes
    local nom="$1" attendu="$2" maison apres relance cles
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    apres="$(lancement_pr "$maison" "${@:3}")"
    relance="$(lancement_pr "$maison")"
    cles="$(grep -oh 'name="pianoRoll[A-Za-z]*"' "$maison"/VintageSynthMidiStudio/*.settings 2>/dev/null | wc -l)"
    if [ "$apres" = "$attendu" ] && [ "$attendu" != "$DEFAUT_PR" ] && [ "$relance" = "$apres" ]; then
        printf '  OK   paire %s : après les gestes « %s », au relancement « %s » ; %s clé(s) écrite(s)\n' "$nom" "$apres" "$relance" "$cles"
    else
        printf '  RATÉ paire %s : après les gestes « %s », au relancement « %s » (attendu « %s » deux fois, différent du défaut) ; %s clé(s) écrite(s)\n' \
               "$nom" "$apres" "$relance" "$attendu" "$cles"
        rates=$((rates + 1))
    fi
}

echo "=== D493 : les bascules de l'arrangement retrouvées au lancement suivant ==="
paire A "aimant=libre suit=non courbes=visible" \
      VSM_MENU="Aimantation dans l'arrangement;Suivre la tête de lecture dans l'arrangement;Courbes d'automation dans l'arrangement"
paire B "aimant=grille suit=oui courbes=cachée" VSM_TOUCHE="arrangement:M"
# LE DÉFAUT NE CHANGE PAS : un HOME neuf, sans geste.
neuf="$(lancement "$(mktemp -d "$brouillon/home.XXXX")")"
if [ "$neuf" = "$DEFAUT" ]; then
    printf '  OK   HOME neuf, sans geste : « %s »\n' "$neuf"
else
    printf '  RATÉ HOME neuf, sans geste : « %s » (attendu « %s »)\n' "$neuf" "$DEFAUT"
    rates=$((rates + 1))
fi
echo "=== D494 : l'aimant, le suivi et les fantômes du piano roll retrouvés, et montrés par leur bouton ==="
paire_pr C "aimant=non/non suit=non/non fantomes=non/non velocite=100/100" VSM_GESTE_PISTE="cliquer:Aimant;cliquer:Suivre;cliquer:Fantômes"
paire_pr D "aimant=non/non suit=oui/oui fantomes=oui/oui velocite=100/100" VSM_TOUCHE="pianoroll:G"
# D496 : LA VÉLOCITÉ DES NOTES DESSINÉES, par la saisie du curseur (le chemin de `valeur:`).
paire_pr E "aimant=oui/oui suit=oui/oui fantomes=oui/oui velocite=64/64" VSM_GESTE_PISTE="valeur:pianoroll.velocite=64"
neuf="$(lancement_pr "$(mktemp -d "$brouillon/home.XXXX")")"
if [ "$neuf" = "$DEFAUT_PR" ]; then
    printf '  OK   HOME neuf, sans geste : « %s »\n' "$neuf"
else
    printf '  RATÉ HOME neuf, sans geste : « %s » (attendu « %s »)\n' "$neuf" "$DEFAUT_PR"
    rates=$((rates + 1))
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
