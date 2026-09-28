#!/usr/bin/env bash
# LA GARDE DE D472 : LES SEPT COMMANDES « PISTE » FONT LA MÊME CHOSE AU MENU ET À LA TOUCHE.
#
# RÈGLE GARDÉE (28/09/2026) : les sept commandes de la famille « Piste » de la
# table (piste suivante / précédente, étendre le choix vers le bas / le haut,
# choisir toutes les pistes, muet et solo de la piste choisie) ont une entrée au
# menu Piste, et cette entrée REND LE MÊME ÉTAT que la touche. Avant D472, aucune
# n'avait d'entrée, et Ctrl+Maj+A n'avait aucune porte du tout (D370).
#
# COMMENT. Un projet de trois pistes est engendré ; chaque cas se joue trois fois,
# sous un HOME neuf (D318) : SANS geste (le témoin), par `VSM_MENU` (la porte), par
# `VSM_TOUCHE` (la touche, qui passe par `MainComponent::keyPressed` comme au
# clavier). L'état se relit sur `VSM_PISTE_CHOISIE` (piste active et choix
# multiple, D472) ou sur le `project.json` écrit par `VSM_ENREGISTRER` (muet, solo).
#
# CHAQUE CAS PORTE SON TÉMOIN (D145) : deux portes qui ne font rien sont d'accord,
# et cela ne prouve rien. Un cas n'est tenu que si porte = touche = attendu, ET
# témoin ≠ attendu.
#
# Rend 0 si les sept cas (et l'annulation du muet) tiennent, 1 sinon, 2 si le
# binaire manque.
#
#   tools/portes-des-pistes.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-portes-pistes.XXXXXX")"
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
    # LE TEMPO DANS LA PREMIÈRE PISTE, pas dans une piste à lui : l'import compte
    # une piste de tempo séparée comme une piste de plus (« le projet décrit 3
    # piste(s) mais le MIDI en contient 4 », vu à la première course), et tous
    # les attendus sur trois pistes tombaient à côté.
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
json.dump({"format": "vsm-project", "version": 1, "title": "portes",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [t("une"), t("deux"), t("trois")]},
          open(d + "/project.json", "w"), indent=1)
PY

n=0
course() {   # $1 = nom ; $2... = variables du geste -> écrit l'état relu sur la sortie
    n=$((n + 1))
    local maison sortie="$brouillon/ecrit-$n"
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_TAILLE="1280x742" VSM_DELAI=800 \
        VSM_PIANOROLL_ZONES=1 VSM_ENREGISTRER="$sortie" VSM_CAPTURE="$brouillon/$1-$n.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/$1-$n.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147) : un verbe mal adressé se
    # dit là, et c'est la seule trace qu'il laisse.
    grep -E "VSM_(MENU|TOUCHE) : .*(aucune|AUCUNE|inconnu|illisible|grisée)" "$brouillon/$1-$n.txt" | sed 's/^/        journal : /' >&2
    python3 - "$brouillon/$1-$n.txt" "$sortie/project.json" <<'PY'
import json, re, sys
journal = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.findall(r"VSM_PISTE_CHOISIE : (\d+) sur \d+, choix : \d+ \(([^)]*)\)", journal)
active, choix = (m[-1][0], m[-1][1].replace(" ", "")) if m else ("?", "?")
try:
    pistes = json.load(open(sys.argv[2], encoding="utf-8"))["tracks"]
    muet = "".join("M" if p["mix"].get("muted") else "-" for p in pistes)
    solo = "".join("S" if p["mix"].get("solo") else "-" for p in pistes)
except (OSError, KeyError, ValueError):
    muet = solo = "?"
print(f"active={active} choix={choix} muet={muet} solo={solo}")
PY
}

rates=0
cas() {   # $1 nom ; $2 clé lue (active|choix|muet|solo) ; $3 attendu ; $4 menu ; $5 touches
    local nom="$1" cle="$2" attendu="$3" menu="$4" touches="$5"
    local temoin porte touche
    temoin="$(course "$nom-temoin" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    porte="$(course "$nom-menu" VSM_MENU="$menu" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    touche="$(course "$nom-touche" VSM_TOUCHE="$touches" | grep -o "$cle=[^ ]*" | cut -d= -f2)"
    derniere_porte="$porte"
    if [ "$porte" = "$attendu" ] && [ "$touche" = "$attendu" ] && [ "$temoin" != "$attendu" ]; then
        printf '  OK   %-22s %s : témoin %s, menu %s, touche %s\n' "$nom" "$cle" "$temoin" "$porte" "$touche"
    else
        printf '  RATÉ %-22s %s : témoin %s, menu %s, touche %s (attendu %s, témoin différent)\n' \
               "$nom" "$cle" "$temoin" "$porte" "$touche" "$attendu"
        rates=$((rates + 1))
    fi
}

echo "=== D472 : les commandes « Piste », au menu et à la touche ==="
cas "piste-suivante"  active 1   "Piste suivante" "alt + cursor down"
cas "piste-precedente" active 1  "Piste suivante;Piste suivante;Piste précédente" \
                                 "alt + cursor down;alt + cursor down;alt + cursor up"
cas "etendre-bas"     choix 0,1,2 "Étendre le choix vers le bas;Étendre le choix vers le bas" \
                                  "shift + alt + cursor down;shift + alt + cursor down"
cas "etendre-haut"    choix 1,2  "Piste suivante;Piste suivante;Étendre le choix vers le haut" \
                                 "alt + cursor down;alt + cursor down;shift + alt + cursor up"
cas "toutes"          choix 0,1,2 "Choisir toutes les pistes" "ctrl + shift + A"
cas "muet"            muet M--   "Muet (piste choisie)" "shift + M"
muet_par_le_menu="$derniere_porte"
cas "solo"            solo S--   "Solo (piste choisie)" "shift + S"
# LE MUET POSÉ PAR LE MENU S'ANNULE (Ctrl+Z, par `keyPressed` comme au clavier) :
# `VSM_MENU` agit avant `VSM_TOUCHE`. SON TÉMOIN EST LE CAS « MUET » CI-DESSUS, et
# il est EXIGÉ : sur un binaire sans l'entrée de menu, rien n'est rendu muet, rien
# ne s'annule, et « --- » sortait vert (vu sur le binaire de D471) — un retour au
# point de départ ne prouve rien sans le témoin qui montre qu'on en était parti.
annule="$(course "muet-annule" VSM_MENU="Muet (piste choisie)" VSM_TOUCHE="ctrl + Z" | grep -o "muet=[^ ]*" | cut -d= -f2)"
if [ "$muet_par_le_menu" != "M--" ]; then
    printf '  RATÉ %-22s non jugé : le menu seul n'"'"'a pas rendu la piste muette (%s)\n' "muet-annule" "$muet_par_le_menu"
    rates=$((rates + 1))
elif [ "$annule" = "---" ]; then
    printf '  OK   %-22s muet : menu puis Ctrl+Z → %s\n' "muet-annule" "$annule"
else
    printf '  RATÉ %-22s muet : menu puis Ctrl+Z → %s (attendu ---)\n' "muet-annule" "$annule"
    rates=$((rates + 1))
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
