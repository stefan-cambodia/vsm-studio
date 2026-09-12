#!/bin/bash
# garder-batterie.sh — LA GARDE DE BATTERIE DU POSTE, écrite une fois pour
# toutes plutôt que réimprovisée à chaque campagne.
#
# LA RÈGLE QU'ELLE FAIT RESPECTER (consigne de l'utilisateur, 11 et 12/09/2026) :
#
#   1. Sous SEUIL % de batterie, le poste est MIS EN VEILLE — pas simplement
#      « autorisé » à s'endormir. Relâcher un inhibiteur rend la veille
#      possible ; si rien ne la déclenche, la batterie tombe à zéro et ce qui
#      tournait meurt. Le garde appelle donc `systemctl suspend` lui-même et
#      journalise SON CODE DE RETOUR : un refus de systemd doit se voir.
#   2. CE POSTE NE CHARGE QU'EN VEILLE (vérifié : 16 % avant une veille, 97 % au
#      réveil). Bloquer la veille n'est donc pas seulement consommer, c'est
#      EMPÊCHER LA RECHARGE. Le blocage ne se tient que pour protéger un travail
#      long qui mourrait d'une veille — jamais « par principe ».
#   3. D'où les deux modes : SANS `--pid`, le garde ne bloque RIEN et se contente
#      d'endormir au seuil (le cas d'une session de travail ordinaire) ; AVEC
#      `--pid`, il bloque la veille tant que ce processus vit, et l'endort quand
#      même au seuil.
#
# TROIS PIÈGES DÉJÀ PAYÉS, ET CE QUI LES ÉVITE ICI :
#
#   - `pkill -f garder-batterie` TUE LE SHELL qui porte le motif dans sa propre
#     ligne de commande (12/09). Le garde écrit donc son PID dans son journal et
#     s'arrête par `kill <PID>` ou par le fichier d'arrêt (`--arret`).
#   - MODIFIER LE SCRIPT PENDANT QU'IL TOURNE : bash relit le fichier par
#     décalage d'octets et exécute n'importe quoi. On écrit une nouvelle version,
#     on la lance, puis on arrête l'ancienne par son PID.
#   - LANCÉ DEPUIS LE SHELL D'UN OUTIL, il meurt avec la session, même sous
#     nohup. Le lancer par :
#         setsid nohup tools/garder-batterie.sh > /dev/null 2>&1 < /dev/null & disown
#     et vérifier par `pgrep -af garder-batterie` avant de croire le journal.
set -u

SEUIL=10
BAT_FORCEE=""
PID_SURVEILLE=""
HEURES=12
JOURNAL="$(cd "$(dirname "$0")/.." && pwd)/reconstruction/travail/garder-batterie.log"
ARRET=""

while [ $# -gt 0 ]; do
  case "$1" in
    --seuil) SEUIL="$2"; shift 2 ;;
    --pid) PID_SURVEILLE="$2"; shift 2 ;;
    --duree) HEURES="$2"; shift 2 ;;
    --journal) JOURNAL="$2"; shift 2 ;;
    --arret) ARRET="$2"; shift 2 ;;
    # POUR ÉPROUVER LA GARDE SANS VIDER LA BATTERIE : un dossier qui imite
    # /sys/class/power_supply/BAT0 (deux fichiers, « capacity » et « status »).
    # Une garde qu'on n'a jamais vue déclencher n'est pas une garde.
    --batterie) BAT_FORCEE="$2"; shift 2 ;;
    *) echo "option inconnue : $1" >&2; exit 2 ;;
  esac
done

BAT="${BAT_FORCEE:-$(ls -d /sys/class/power_supply/BAT* 2>/dev/null | head -1)}"
if [ -z "$BAT" ]; then
  echo "aucune batterie trouvée sous /sys/class/power_supply" >&2
  exit 3
fi
[ -n "$ARRET" ] || ARRET="${JOURNAL%.log}.arret"
DRAPEAU="${JOURNAL%.log}.inhibiteur"
mkdir -p "$(dirname "$JOURNAL")"

dire() { echo "$(date '+%d/%m %H:%M:%S') $*" >> "$JOURNAL"; }

# UN FICHIER D'ARRÊT RESTÉ LÀ NE DOIT PAS TUER L'INSTANCE SUIVANTE. Payé le
# 13/09 : poser l'arrêt pour relever une garde, puis lancer la neuve, et la
# neuve lit le même fichier — elle s'arrête dans la seconde, en le disant, mais
# le poste reste sans garde. Le drapeau est donc CONSOMMÉ au départ.
if [ -e "$ARRET" ]; then
  rm -f "$ARRET"
  dire "un fichier d'arrêt trainait ($ARRET) : consommé au départ, il ne vaut que pour l'instance qui le voit VIVRE"
fi

tenu=0
endormi=0
fin=$(( $(date +%s) + HEURES * 3600 ))
dire "départ : PID $$, seuil $SEUIL %, batterie $(cat "$BAT/capacity") % ($(cat "$BAT/status")), \
$( [ -n "$PID_SURVEILLE" ] && echo "veille BLOQUÉE tant que le PID $PID_SURVEILLE vit" || echo "aucun blocage de veille" ), \
arrêt par « kill $$ » ou « touch $ARRET », au plus tard dans $HEURES h"

while :; do
  [ -e "$ARRET" ] && { rm -f "$ARRET" "$DRAPEAU"; dire "arrêt demandé : veille rendue"; exit 0; }
  [ "$(date +%s)" -ge "$fin" ] && { rm -f "$DRAPEAU"; dire "durée écoulée ($HEURES h) : veille rendue"; exit 0; }
  if [ -n "$PID_SURVEILLE" ] && ! kill -0 "$PID_SURVEILLE" 2>/dev/null; then
    rm -f "$DRAPEAU"; dire "le PID $PID_SURVEILLE a fini : veille rendue"; exit 0
  fi

  cap=$(cat "$BAT/capacity" 2>/dev/null)
  etat=$(cat "$BAT/status" 2>/dev/null)

  if [ "$etat" = "Discharging" ] && [ -n "$cap" ] && [ "$cap" -le "$SEUIL" ]; then
    if [ $tenu = 1 ]; then
      rm -f "$DRAPEAU"; tenu=0
      dire "batterie $cap % : l'inhibiteur part, la veille redevient possible"
      sleep 20   # laisser l'inhibiteur expirer AVANT de demander la veille
    fi
    if [ $endormi = 0 ]; then
      dire "batterie $cap % : DEMANDE DE VEILLE (systemctl suspend)"
      systemctl suspend
      code=$?
      dire "systemctl suspend : code $code$( [ $code -ne 0 ] && echo '  ** REFUSÉ — la veille n a PAS eu lieu **' )"
      endormi=1
    fi
  else
    # Au-dessus du seuil (ou en charge) : on réarme, et l'on ne bloque QUE si
    # un travail long est explicitement surveillé.
    endormi=0
    if [ -n "$PID_SURVEILLE" ] && { [ $tenu = 0 ] || [ ! -e "$DRAPEAU" ]; }; then
      touch "$DRAPEAU"
      systemd-inhibit --what=sleep:idle --who="VSM Studio (PID $PID_SURVEILLE)" \
        --why="travail long en cours ; veille DÉCLENCHÉE sous $SEUIL % de batterie" --mode=block \
        bash -c "while [ -e '$DRAPEAU' ]; do sleep 15; done" &
      tenu=1
      dire "veille BLOQUÉE (batterie ${cap:-?} %, $etat, seuil $SEUIL %)"
    fi
  fi
  sleep 60
done
