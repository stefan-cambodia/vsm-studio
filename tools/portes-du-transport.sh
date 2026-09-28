#!/usr/bin/env bash
# LA GARDE DE D483 : LE MENU TRANSPORT FAIT CE QUE FONT LES TOUCHES.
#
# RÈGLE GARDÉE (29/09/2026) : chaque entrée du menu Transport rend le MÊME état
# que la touche de la même commande. Les deux passent par `executerCommande`
# (D483) ; cette garde vérifie que c'est encore vrai à l'écran, pas seulement
# dans le code.
#
# COMMENT. Un projet de trois pistes (huit mesures de notes) est engendré ; chaque
# cas se joue trois fois sous un HOME neuf (D318) : SANS geste (le témoin), par
# `VSM_MENU` (la porte), par `VSM_TOUCHE` (la touche, par `keyPressed`). La tête se
# relit sur le transport (`VSM_TETE`, D483 — le libellé de position n'est réécrit
# que par la minuterie et restait à « mes. 1 · 1 ») ; la boucle et ses locateurs
# dans le `project.json` écrit par le geste différé `enregistrer:` (D483).
#
# CHAQUE CAS PORTE SON TÉMOIN (D145) : il n'est tenu que si menu = touche =
# attendu ET témoin ≠ attendu. « Retour au début » part donc de la fin : depuis
# le début, il ne prouverait rien.
#
# CE QUI N'EST PAS MESURÉ, ET POURQUOI : lecture/arrêt, enregistrer et métronome
# (au banc, aucune carte son n'est ouverte : le transport n'avance pas et Rec est
# grisé) ; marqueurs suivant/précédent (le projet n'en a pas) ; tête au début de la
# sélection (aucune sélection au démarrage). Dit ici, jamais compté comme tenu.
#
# Rend 0 si tous les cas tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/portes-du-transport.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-portes-transport.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, hauteur, tempo=b""):
    evs = ([(0, tempo)] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    for i in range(8):
        evs += [(0 if i == 0 else 1440, bytes([0x90, hauteur, 100])), (480, bytes([0x80, hauteur, 0]))]
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
data = (b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480)
        + piste("une", 40, tempo=b"\xff\x51\x03\x07\xa1\x20") + piste("deux", 60) + piste("trois", 80))
open(d + "/midi/arrangement.mid", "wb").write(data)
def t(nom):
    return {"channel": 0, "color": "#FF6B9BFF", "effects": [],
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
            "name": nom}
json.dump({"format": "vsm-project", "version": 1, "title": "transport",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("une"), t("deux"), t("trois")]},
          open(d + "/project.json", "w"), indent=1)
PY

n=0
course() {   # $1 = nom ; $2... = variables du geste -> « tete=… boucle=… debut=… fin=… »
    n=$((n + 1))
    local maison sortie="$brouillon/ecrit-$n"
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    # L'ENREGISTREMENT EST UN GESTE DIFFÉRÉ (D483) : la boucle et le métronome
    # basculent par `triggerClick()`, qui POSTE un message ; `VSM_ENREGISTRER`,
    # qui agit au démarrage, écrivait l'état d'avant (vu : « boucle false » par le
    # menu ET par la touche, alors que les deux la basculaient).
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x742" VSM_DELAI=1200 \
        VSM_TETE=1 VSM_GESTE_APRES="500:enregistrer:$sortie" VSM_CAPTURE="$brouillon/$1-$n.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/$1-$n.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147).
    grep -E "VSM_(MENU|TOUCHE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)" "$brouillon/$1-$n.txt" | sed 's/^/        journal : /' >&2
    python3 - "$brouillon/$1-$n.txt" "$sortie/project.json" <<'PY'
import json, re, sys
journal = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.findall(r"VSM_TETE : tick (-?\d+)", journal)
tete = m[-1] if m else "?"
try:
    boucle = json.load(open(sys.argv[2], encoding="utf-8"))["transport"]["loop"]
    active, debut, fin = str(boucle.get("enabled")).lower(), boucle.get("startTick"), boucle.get("endTick")
except (OSError, KeyError, ValueError):
    active = debut = fin = "?"
print(f"tete={tete} boucle={active} debut={debut} fin={fin}")
PY
}

rates=0
derniere_porte=""
cas() {   # $1 nom ; $2 clé ; $3 attendu (« ≠0 » : non nul et identique aux deux portes) ; $4 menu ; $5 touches
    local nom="$1" cle="$2" attendu="$3" menu="$4" touches="$5"
    local temoin porte touche
    temoin="$(course "$nom-temoin" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    porte="$(course "$nom-menu" VSM_MENU="$menu" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    touche="$(course "$nom-touche" VSM_TOUCHE="$touches" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    derniere_porte="$porte"
    local bon=0
    if [ "$attendu" = "≠0" ]; then
        [ "$porte" = "$touche" ] && [ -n "$porte" ] && [ "$porte" != "0" ] && [ "$porte" != "?" ] && [ "$temoin" != "$porte" ] && bon=1
    else
        [ "$porte" = "$attendu" ] && [ "$touche" = "$attendu" ] && [ "$temoin" != "$attendu" ] && bon=1
    fi
    if [ "$bon" -eq 1 ]; then
        printf '  OK   %-20s %s : témoin %s, menu %s, touche %s\n' "$nom" "$cle" "$temoin" "$porte" "$touche"
    else
        printf '  RATÉ %-20s %s : témoin %s, menu %s, touche %s (attendu %s, témoin différent)\n' \
               "$nom" "$cle" "$temoin" "$porte" "$touche" "$attendu"
        rates=$((rates + 1))
    fi
}

echo "=== D483 : le menu Transport fait ce que font les touches ==="
cas "fin-du-morceau"   tete ≠0    "Aller à la fin du morceau" "end"
fin_du_morceau="$derniere_porte"
cas "mesure-suivante"  tete 3840  "Tête : mesure suivante;Tête : mesure suivante" \
                                  "shift + alt + cursor right;shift + alt + cursor right"
cas "mesure-precedente" tete 1920 "Tête : mesure suivante;Tête : mesure suivante;Tête : mesure précédente" \
                                  "shift + alt + cursor right;shift + alt + cursor right;shift + alt + cursor left"
cas "temps-suivant"    tete 480   "Tête : temps suivant" "alt + cursor right"
cas "temps-precedent"  tete 480   "Tête : temps suivant;Tête : temps suivant;Tête : temps précédent" \
                                  "alt + cursor right;alt + cursor right;alt + cursor left"
cas "debut-de-boucle"  debut 1920 "Tête : mesure suivante;Début de boucle à la tête" \
                                  "shift + alt + cursor right;I"
cas "fin-de-boucle"    fin 3840   "Tête : mesure suivante;Tête : mesure suivante;Fin de boucle à la tête" \
                                  "shift + alt + cursor right;shift + alt + cursor right;O"
cas "boucle"           boucle true "Boucle (marche / arrêt)" "/"
# « RETOUR AU DÉBUT » PART DE LA FIN : depuis le début il rendrait 0 sans rien
# prouver. Son témoin est le cas « fin du morceau », qui doit avoir quitté 0.
retour_menu="$(course "retour-menu" VSM_MENU="Aller à la fin du morceau;Retour au début" | grep -o "tete=[^ ]*" | cut -d= -f2)"
retour_touche="$(course "retour-touche" VSM_TOUCHE="end;home" | grep -o "tete=[^ ]*" | cut -d= -f2)"
if [ -z "$fin_du_morceau" ] || [ "$fin_du_morceau" = "0" ] || [ "$fin_du_morceau" = "?" ]; then
    printf '  RATÉ %-20s non jugé : « fin du morceau » n'"'"'a pas quitté 0 (%s)\n' "retour-au-debut" "$fin_du_morceau"
    rates=$((rates + 1))
elif [ "$retour_menu" = "0" ] && [ "$retour_touche" = "0" ]; then
    printf '  OK   %-20s tete : depuis %s, menu %s, touche %s\n' "retour-au-debut" "$fin_du_morceau" "$retour_menu" "$retour_touche"
else
    printf '  RATÉ %-20s tete : depuis %s, menu %s, touche %s (attendu 0)\n' "retour-au-debut" "$fin_du_morceau" "$retour_menu" "$retour_touche"
    rates=$((rates + 1))
fi
echo "    non mesurés (dit, pas compté) : lecture/arrêt, enregistrer, métronome — aucune carte son au banc ;"
echo "    marqueurs — le projet n'en a pas ; tête au début de la sélection — aucune sélection."
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
