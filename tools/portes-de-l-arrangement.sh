#!/usr/bin/env bash
# LA GARDE DE D491 : LES QUATRE BASCULES DE L'ARRANGEMENT, AU MENU ET À LA TOUCHE.
#
# RÈGLE GARDÉE (29/09/2026) : l'aimantation (G), la grille à la mesure (M), le
# suivi de la tête (F) et les courbes d'automation (A) de l'arrangement ont une
# entrée au menu Affichage, cochée selon l'état, et cette entrée rend le MÊME état
# que la touche. Les deux passent par les fonctions `basculer…` de l'arrangement
# (D491) ; cette garde vérifie que c'est encore vrai à l'écran, pas seulement dans
# le code. Avant D491, F et A étaient écrites en dur hors de la table : aucun menu,
# aucune ligne dans la fenêtre des raccourcis, et `VSM_TOUCHE=arrangement:F`
# répondait « AUCUNE commande de ce clavier » (une touche fabriquée depuis sa
# description porte un caractère nul — le piège de D360).
#
# COMMENT. Un projet de trois pistes est engendré ; chaque cas se joue trois fois
# sous un HOME neuf (D318) : SANS geste (le témoin), par `VSM_MENU` (la porte), par
# `VSM_TOUCHE=arrangement:<touche>` (la touche, par `keyPressed` de l'arrangement,
# qui n'est appelé par le système que quand l'arrangement a le clavier). L'état se
# relit sur la DERNIÈRE ligne « VSM_ARRANGEMENT : aimant … » : le relevé
# `VSM_ARRANGEMENT=1` l'écrit APRÈS les gestes (D491). La coche du menu se relit
# par `VSM_MENU_LISTE`, qui s'exécute aussi après les gestes.
#
# CHAQUE CAS PORTE SON TÉMOIN (D145) : il n'est tenu que si menu = touche = attendu
# ET témoin ≠ attendu, et si la coche de l'entrée a suivi (cochée au témoin quand
# la bascule est active, décochée après le geste, ou l'inverse).
#
# Rend 0 si tous les cas tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/portes-de-l-arrangement.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-portes-arrangement.XXXXXX")"
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
json.dump({"format": "vsm-project", "version": 1, "title": "arrangement",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("une"), t("deux"), t("trois")]},
          open(d + "/project.json", "w"), indent=1)
PY

n=0
course() {   # $1 = nom ; $2 = libellé de l'entrée de menu à relire ; $3... = variables du geste
    n=$((n + 1))
    local maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x742" VSM_DELAI=800 \
        VSM_VUE="sans-rapport,arrangement" VSM_ARRANGEMENT=1 VSM_MENU_LISTE=1 \
        VSM_CAPTURE="$brouillon/$1-$n.png" "${@:3}" \
        timeout 40 "$BIN" > "$brouillon/$1-$n.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147) : un verbe mal adressé se
    # dit là, et c'est la seule trace qu'il laisse.
    grep -E "VSM_(MENU|TOUCHE|VUE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)" "$brouillon/$1-$n.txt" | sed 's/^/        journal : /' >&2
    python3 - "$brouillon/$1-$n.txt" "$2" <<'PY'
import re, sys
journal = open(sys.argv[1], encoding="utf-8", errors="replace").read()
libelle = sys.argv[2]
m = re.findall(r"VSM_ARRANGEMENT : aimant (\w+), (suit la tête|ne suit pas), automation (\w+)", journal)
if m:
    aimant, suit, courbes = m[-1]
    suit = "oui" if suit == "suit la tête" else "non"
else:
    aimant = suit = courbes = "?"
# LA COCHE DE L'ENTRÉE : « absente » si l'entrée n'existe pas (le témoin de D490).
coche = "absente"
for ligne in journal.splitlines():
    if ligne.startswith("VSM_MENU_LISTE : Affichage > " + libelle + " ") or ligne == "VSM_MENU_LISTE : Affichage > " + libelle:
        coche = "oui" if "[cochée]" in ligne else "non"
print(f"aimant={aimant} suit={suit} courbes={courbes} coche={coche}")
PY
}

rates=0
cas() {   # $1 nom ; $2 clé lue (aimant|suit|courbes) ; $3 attendu ; $4 libellé du menu ; $5 touche
    local nom="$1" cle="$2" attendu="$3" menu="$4" touche_="$5"
    local l_temoin l_porte l_touche temoin porte touche coche_t coche_p
    l_temoin="$(course "$nom-temoin" "$menu")"
    l_porte="$(course "$nom-menu" "$menu" VSM_MENU="$menu")"
    l_touche="$(course "$nom-touche" "$menu" VSM_TOUCHE="arrangement:$touche_")"
    temoin="$(echo "$l_temoin" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    porte="$(echo "$l_porte" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    touche="$(echo "$l_touche" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    coche_t="$(echo "$l_temoin" | grep -o "coche=[^ ]*" | cut -d= -f2)"
    coche_p="$(echo "$l_porte" | grep -o "coche=[^ ]*" | cut -d= -f2)"
    # LA COCHE SUIT L'ÉTAT : présente, et différente avant et après le geste.
    local coche_bonne=0
    [ "$coche_t" != "absente" ] && [ "$coche_p" != "absente" ] && [ "$coche_t" != "$coche_p" ] && coche_bonne=1
    if [ "$porte" = "$attendu" ] && [ "$touche" = "$attendu" ] && [ "$temoin" != "$attendu" ] && [ "$coche_bonne" -eq 1 ]; then
        printf '  OK   %-10s %s : témoin %s, menu %s, touche %s ; coche %s → %s\n' \
               "$nom" "$cle" "$temoin" "$porte" "$touche" "$coche_t" "$coche_p"
    else
        printf '  RATÉ %-10s %s : témoin %s, menu %s, touche %s ; coche %s → %s (attendu %s, témoin différent, coche qui suit)\n' \
               "$nom" "$cle" "$temoin" "$porte" "$touche" "$coche_t" "$coche_p" "$attendu"
        rates=$((rates + 1))
    fi
}

echo "=== D491 : les quatre bascules de l'arrangement, au menu Affichage et à la touche ==="
cas "aimant"  aimant  libre   "Aimantation dans l'arrangement"                "G"
cas "grille"  aimant  grille  "Grille à la mesure dans l'arrangement"         "M"
cas "suivi"   suit    non     "Suivre la tête de lecture dans l'arrangement"  "F"
cas "courbes" courbes visible "Courbes d'automation dans l'arrangement"       "A"
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
