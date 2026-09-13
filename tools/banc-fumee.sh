#!/usr/bin/env bash
# Le banc de fumée : les verbes de banc répondent-ils encore ?
#
# POURQUOI (13/09/2026, D229). La journée a ajouté six verbes (VSM_ABANDON,
# VSM_OPTIONS, VSM_ENREGISTRER, regle-pianoroll, les listings « ? », la liste de
# VSM_FICHIER) et fait passer dix-huit fenêtres modales par un chemin commun. Rien
# ne dit, demain, qu'un de ces chemins n'est pas mort en silence : les suites C++
# ne traversent pas l'interface, et un banc qui ne répond plus ressemble à un banc
# mal écrit. Ce script prend chaque verbe, cherche UNE ligne précise dans le
# journal, et rend un code non nul si elle manque.
#
# RÈGLES : chaque course tourne sous un HOME de brouillon (jamais celui de
# l'utilisateur, D77) et sur une COPIE du projet de banc quand elle écrit ; les
# courses sont courtes (aucun rendu audio) pour pouvoir tourner à côté d'une
# campagne.
#
#   tools/banc-fumee.sh            # tout
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
brouillon="${TMPDIR:-/tmp}/vsm-fumee-$$"
mkdir -p "$brouillon"
projet="$brouillon/projet"
cp -r reconstruction/travail/cdl "$projet"
rates=0

cas () {   # $1 nom  $2 motif attendu  $3... env=valeur
    local nom="$1" motif="$2"; shift 2
    local maison="$brouillon/h-$nom"; mkdir -p "$maison"
    local sortie
    sortie=$(timeout 90 env HOME="$maison" VSM_PROJET="$projet" \
                 VSM_CAPTURE="$brouillon/$nom.png" VSM_DELAI=900 "$@" "$BIN" 2>&1)
    if grep -qE "$motif" <<<"$sortie"; then
        printf '  OK   %-22s %s\n' "$nom" "$(grep -oE "$motif" <<<"$sortie" | head -1)"
    else
        printf '  RATÉ %-22s (attendu : %s)\n' "$nom" "$motif"
        rates=$((rates + 1))
    fi
}

echo "=== banc de fumée : les verbes de banc ==="
cas rapport      "VSM_RAPPORT : "                 VSM_RAPPORT=1 VSM_RAPPORT_LISTE=1
cas menu         "exécutée \(menu Fichier\)"      VSM_MENU="Nouveau projet"
cas abandon      "VSM_ABANDON : abandonner"       VSM_MENU="Ajouter une piste MIDI;Nouveau projet" VSM_ABANDON=abandonner
cas options      "VSM_OPTIONS : nom=Fumee"        VSM_MENU_CONTEXTE="regle:Poser un repère ici…" VSM_OPTIONS="nom=Fumee"
cas modale       "fenêtre modale ouverte"         VSM_MENU_CONTEXTE="regle:Poser un repère ici…"
cas listing      "regle-pianoroll = "             VSM_MENU_CONTEXTE="regle-pianoroll:?"
cas clip         "clip-midi = "                   VSM_MENU_CONTEXTE="clip-midi:?"
cas position     "VSM_POSITION : mesure 9 temps 1" VSM_POSITION=9
cas enregistrer  "VSM_ENREGISTRER : "             VSM_ENREGISTRER="$brouillon/ecrit"
cas selection    "VSM_SELECTION : "               VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner"
cas textes       "VSM_TEXTES : "                  VSM_TEXTES_LISTE=1

echo "=== $rates raté(s) ==="
rm -rf "$brouillon"
exit $((rates > 0))
