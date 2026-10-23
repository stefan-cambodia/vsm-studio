#!/usr/bin/env bash
# D578.3 — LE MONITEUR MIDI (Affichage ▸ Moniteur MIDI ; le *MIDI Monitor* de Cubase), MESURÉ PAR LE RELEVÉ DE LA FENÊTRE
# APRÈS DES CONTRÔLEURS ENTRANTS, LE TÉMOIN FENÊTRE FERMÉE À CÔTÉ.
#
# LES RÈGLES GARDÉES. Les contrôleurs entrent par `cc-entrant:` — le point d'entrée du matériel
# (`AudioEngine::handleIncomingMidiMessage`, D517). L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D578) :
#   (1) la fenêtre ouverte, CC 7 = 100 puis CC 74 = 64 : 2 événements, dans cet ordre, canal 1, source « (interne) » ;
#       l'entrée du menu cochée, le titre « Moniteur MIDI (2) » ;
#   (2) « Effacer » : 0 ;
#   (3) le TÉMOIN : les deux mêmes contrôleurs fenêtre FERMÉE, puis ouverte : 0 — et le moteur les a reçus quand même
#       (`VSM_CC_ENTRANT` deux fois) ;
#   (4) en anglais : « MIDI Monitor », et la ligne dit « (internal) · channel 1 · CC 7 = 100 ».
#
#   tools/moniteur-midi.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-moniteur-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

course() {   # $1 = nom ; le reste : variables — le projet de démarrage, un HOME neuf
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|CLIC|CLIQUER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
compte() { grep -h "^VSM_MONITEUR_MIDI : " "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/^VSM_MONITEUR_MIDI : \([0-9]*\) .*/\1/'; }
lignes() { grep -h "^VSM_MONITEUR_MIDI_LIGNE : " "$brouillon/$1.txt" | sed 's/^VSM_MONITEUR_MIDI_LIGNE : [0-9:.]* · //'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D578.3 : le moniteur MIDI ==="
course ouvert VSM_DELAI=2600 \
    VSM_GESTE_APRES="900:menu:Moniteur MIDI;1100:cc-entrant:7:100;1200:cc-entrant:74:64;1500:relever-moniteur-midi;1600:lister-menus;1800:cliquer:moniteur.effacer;2000:relever-moniteur-midi"
course ferme VSM_DELAI=2200 \
    VSM_GESTE_APRES="900:cc-entrant:7:100;1000:cc-entrant:74:64;1300:menu:Moniteur MIDI;1600:relever-moniteur-midi"
course anglais VSM_LANGUE=en VSM_DELAI=2000 \
    VSM_GESTE_APRES="900:menu:MIDI Monitor;1100:cc-entrant:7:100;1400:relever-moniteur-midi"

n1="$(compte ouvert 1)"; n2="$(compte ouvert 2)"; nf="$(compte ferme 1)"
lo="$(lignes ouvert | tr '\n' ';')"; la="$(lignes anglais | tr '\n' ';')"
recus="$(grep -c "^VSM_CC_ENTRANT" "$brouillon/ferme.txt")"
coche="$(grep -h "^VSM_MENU_LISTE : Affichage > Moniteur MIDI" "$brouillon/ouvert.txt")"
titre="$(grep -h "^VSM_MONITEUR_MIDI : " "$brouillon/ouvert.txt" | head -1 | sed -n 's/.*fenêtre ouverte « \(.*\) ».*/\1/p')"
grep -h "^VSM_MONITEUR_MIDI : \|^VSM_CC_ENTRANT" "$brouillon"/ouvert.txt "$brouillon"/ferme.txt | sed 's/^/       /'
echo "       lignes ouvert [$lo] ; anglais [$la]"
echo "       menu [${coche#VSM_MENU_LISTE : }] ; titre [$titre] ; fermé : $recus contrôleur(s) reçu(s) par le moteur"

verdict "(1) fenêtre ouverte : 2 événements, CC 7 = 100 puis CC 74 = 64, canal 1, (interne) ; l'entrée cochée, le titre" \
    "$([ "$n1" = 2 ] && [ "$lo" = "(interne) · canal 1 · CC 7 = 100;(interne) · canal 1 · CC 74 = 64;" ] \
        && echo "$coche" | grep -q "\[cochée\]" && [ "$titre" = "Moniteur MIDI (2)" ] && echo 1 || echo 0)"
verdict "(2) Effacer : 0" "$([ "$n2" = 0 ] && echo 1 || echo 0)"
verdict "(3) le témoin fenêtre fermée : 0 recueilli, 2 reçus par le moteur" \
    "$([ "$nf" = 0 ] && [ "$recus" = 2 ] && echo 1 || echo 0)"
verdict "(4) en anglais : « MIDI Monitor », « (internal) · channel 1 · CC 7 = 100 »" \
    "$([ "$la" = "(internal) · channel 1 · CC 7 = 100;" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MONITEUR-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MONITEUR-MIDI : ce que le moteur reçoit se voit, et ne coûte rien fenêtre fermée"
