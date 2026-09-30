#!/usr/bin/env bash
# LA GARDE DE D520 : LES RÉGIONS DANS L'HISTORIQUE, LES BASCULES HORS DE LUI — PARTOUT.
#
# RÈGLE GARDÉE (29/09/2026). La région de boucle entrait dans l'historique par P, I
# et O (D29.1) mais pas par la règle du piano roll (Maj + glisser), ni par la bascule
# qui en pose une sur un projet qui n'en a pas ; la région de punch, par « La prendre
# sur la boucle » et « L'effacer » mais pas par Alt + glisser. L'historique restaurant
# des instantanés du projet entier, un changement sans pas DÉCALE le Ctrl+Z (D36.4) :
# un geste, une région tirée, Ctrl+Z — le geste d'avant était annulé aussi, deux en
# un. Et les BASCULES (boucle, punch) ne font pas de pas mais ne doivent pas être
# ramenées en arrière par le Ctrl+Z d'à côté (D518), ni échapper à la marque (D519).
# Et une boucle SANS RÉGION n'est pas active (D523) : le Maj+clic sans glissé.
#
# COMMENT. « Lente » (la forme des fondus croisés, un pas depuis D506) est le geste
# d'AVANT ; sa forme, lue dans le project.json enregistré après Ctrl+Z, dit s'il a
# été annulé. La règle est jouée par `glisser:pianoroll.regle:…` — le chemin de la
# souris, modificateur compris. Un HOME neuf par cas (D318).
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/regions-historique.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-regions.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# Cinq projets d'une piste de quatre notes (tout le morceau : 0 à 6 240 ticks).
#   vide      : ni boucle ni punch ;
#   boucle    : une région de boucle [0,1920], éteinte ;
#   boucle-on : la même, allumée ;
#   punch     : une région de punch [0,1920], éteinte ;
#   vide-on   : une boucle ALLUMÉE sur une région vide [2887,2887] — ce qu'écrivait
#               un Maj+clic avant D523.
for nom in vide boucle boucle-on punch vide-on; do
mkdir -p "$brouillon/$nom/midi"
python3 - "$brouillon/$nom" "$nom" <<'PY'
import json, struct, sys
d, nom = sys.argv[1], sys.argv[2]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une")]
for i in range(4):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60 + i, 100])), (480, bytes([0x80, 60 + i, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
transport = {"loop": {"enabled": nom in ("boucle-on", "vide-on"),
                      "endTick": 1920 if nom.startswith("boucle") else 2887 if nom == "vide-on" else 0,
                      "startTick": 2887 if nom == "vide-on" else 0},
             "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
if nom == "punch":
    transport["punch"] = {"enabled": False, "startTick": 0, "endTick": 1920}
json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
           "transport": transport,
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY
done

# SOUS UNE SESSION VERROUILLÉE, SEUL LE PREMIER LANCEMENT D'UNE RAFALE DESSINE SA
# FENÊTRE (mesuré le 29/09, D520 : 161 096 octets de photo pour le premier, 5 950 —
# une fenêtre vide — pour les suivants, quel que soit le projet ; espacés de 45 s,
# tous pleins). La règle n'a alors ni taille ni visibilité, et `glisser:` ne la
# trouve pas. La garde espace donc ses lancements quand la session est verrouillée,
# et le dit.
session="$(loginctl 2>/dev/null | awk -v u="$USER" '$3 == u && $4 ~ /seat/ {print $1; exit}')"   # la session à siège (voir verifier.sh)
verrouillee() { [ -n "$session" ] && loginctl show-session "$session" -p LockedHint 2>/dev/null | grep -q "=yes"; }
premier=1
lancer() {   # $1 nom ; $2 projet ; $3 gestes
    local h
    if verrouillee && [ "$premier" -eq 0 ]; then
        echo "        (session verrouillée : 45 s avant le lancement « $1 », sans quoi la fenêtre reste vide)"
        sleep 45
    fi
    premier=0
    local essai
    for essai in 1 2; do
        h="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/$2" VSM_VUE="sans-rapport" VSM_DELAI=3600 \
            VSM_GESTE_APRES="$3" VSM_CAPTURE="$brouillon/$1.png" timeout 45 "$BIN" > "$brouillon/$1.txt" 2>&1
        # UNE FENÊTRE VIDE N'EST PAS UNE MESURE : le geste n'a trouvé aucun composant.
        # Rejouée UNE fois, après une minute, et dit (comme l'audit, D510).
        grep -q "aucun composant visible de ce nom" "$brouillon/$1.txt" || break
        # D522 : la cause, telle que l'application l'a LUE (la fenêtre réduite par le
        # gestionnaire de fenêtres sous son plancher), et non supposée.
        grep "VSM_TAILLE : .*RÉDUITE" "$brouillon/$1.txt" | cut -c1-110 | sed 's/^/        /'
        [ "$essai" -eq 1 ] && { echo "        (« $1 » : fenêtre vide, geste non joué — rejoué après 60 s)"; sleep 60; }
    done
    # D147 : ce que l'application avertit se relaie AVANT de conclure.
    grep -E "VSM_(MENU|GESTE_APRES|TOUCHE|GESTE|GLISSE) : .*(aucune|aucun composant|refusé|JAMAIS|grisée|AUCUNE|inconnu)" \
        "$brouillon/$1.txt" | sed 's/^/        journal : /' >&2
}
lire() {   # $1 dossier enregistré → « fondus|boucle on|boucle début|boucle fin|punch on|punch début|punch fin »
    python3 - "$1/project.json" <<'PY'
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except OSError:
    print("ABSENT"); sys.exit()
t = p.get("transport", {})
b, u = t.get("loop", {}), t.get("punch", {})
print("|".join(str(x) for x in [p.get("crossfadeShape", "defaut"),
                                "oui" if b.get("enabled") else "non", b.get("startTick", 0), b.get("endTick", 0),
                                "oui" if u.get("enabled") else "non", u.get("startTick", 0), u.get("endTick", 0)]))
PY
}
pas_releves() { grep "VSM_HISTORIQUE_PAS" "$brouillon/$1.txt" | sed 's/^/        /'; }

rates=0
ok()   { printf '  OK   %-52s %s\n' "$1" "$2"; }
rate() { printf '  RATÉ %-52s %s\n' "$1" "$2"; rates=$((rates + 1)); }
juger() {   # $1 libellé ; $2 obtenu ; $3 attendu
    if [ "$2" = "$3" ]; then ok "$1" "$2"; else rate "$1" "« $2 » (attendu : « $3 »)"; fi
}

echo "=== D520 : les régions dans l'historique, les bascules hors de lui ==="
# A. LA RÉGION DE BOUCLE TIRÉE À LA RÈGLE.
lancer a boucle "400:menu:Lente;800:glisser:pianoroll.regle:0.45,0.5:0.75:maj;1100:relever-historique;1300:relever-boucle;1600:touche:ctrl + Z;1900:relever-historique;2200:enregistrer:$brouillon/a"
pas_releves a
grep "VSM_BOUCLE" "$brouillon/a.txt" | head -1 | sed 's/^/        après le glissé : /'
IFS='|' read -r f bo bd bf _ _ _ <<< "$(lire "$brouillon/a")"
l="$(grep "VSM_BOUCLE" "$brouillon/a.txt" | head -1)"
if [[ "$l" == *"projet oui ["* && "$l" != *"[0,1920]"* ]]; then
    ok "A. le glissé a posé une région" "(le témoin qu'elle en était partie)"
else
    rate "A. le glissé a posé une région" "« $l » : la région n'a pas bougé, rien ne se juge"
fi
juger "A. Ctrl+Z : « Lente » gardée (un pas, pas deux)" "$f" "slow"
juger "A. Ctrl+Z : la région de boucle d'avant" "$bd,$bf" "0,1920"

# B. LA RÉGION DE PUNCH TIRÉE À LA RÈGLE (Alt).
lancer b punch "400:menu:Lente;800:glisser:pianoroll.regle:0.45,0.5:0.75:alt;1100:relever-historique;1300:enregistrer:$brouillon/b0;1600:touche:ctrl + Z;1900:relever-historique;2200:enregistrer:$brouillon/b"
pas_releves b
IFS='|' read -r _ _ _ _ _ pd0 pf0 <<< "$(lire "$brouillon/b0")"
IFS='|' read -r f _ _ _ _ pd pf <<< "$(lire "$brouillon/b")"
if [ "$pd0,$pf0" != "0,1920" ] && [ -n "$pf0" ]; then ok "B. le glissé a posé une région de punch" "[$pd0,$pf0]"
else rate "B. le glissé a posé une région de punch" "[$pd0,$pf0] : rien ne se juge"; fi
juger "B. Ctrl+Z : « Lente » gardée (un pas, pas deux)" "$f" "slow"
juger "B. Ctrl+Z : la région de punch d'avant" "$pd,$pf" "0,1920"

# C. LA BASCULE QUI POSE UNE RÉGION (projet sans région).
lancer c vide "400:menu:Lente;800:menu:Boucle (marche / arrêt);1100:relever-historique;1400:touche:ctrl + Z;1700:relever-historique;2000:relever-boucle;2300:enregistrer:$brouillon/c"
pas_releves c
IFS='|' read -r f bo bd bf _ _ _ <<< "$(lire "$brouillon/c")"
juger "C. Ctrl+Z : « Lente » gardée (un pas, pas deux)" "$f" "slow"
juger "C. Ctrl+Z : plus de région, plus de boucle" "$bo $bd,$bf" "non 0,0"
case "$(grep "VSM_BOUCLE" "$brouillon/c.txt" | tail -1)" in
    *"moteur non"*) ok "C. … et le moteur ne boucle pas" "" ;;
    *) rate "C. … et le moteur ne boucle pas" "$(grep "VSM_BOUCLE" "$brouillon/c.txt" | tail -1)" ;;
esac

# D. LA BASCULE DU PUNCH, REPORTÉE COMME CELLE DE LA BOUCLE (D518).
lancer d punch "400:menu:Lente;800:menu:Enregistrement > Active;1100:enregistrer:$brouillon/d0;1400:touche:ctrl + Z;1700:enregistrer:$brouillon/d"
IFS='|' read -r _ _ _ _ po0 _ _ <<< "$(lire "$brouillon/d0")"
IFS='|' read -r f _ _ _ po _ _ <<< "$(lire "$brouillon/d")"
juger "D. avant Ctrl+Z : le punch allumé" "$po0" "oui"
juger "D. Ctrl+Z : « Lente » annulée" "$f" "defaut"
juger "D. Ctrl+Z : le punch reste allumé" "$po" "oui"

# E. LE DOUBLE-CLIC SUR LA RÈGLE : une bascule — pas de pas, mais la marque.
lancer e boucle-on "400:relever-titre;700:glisser:pianoroll.regle:0.5,0.5:0.5:double;1000:relever-titre;1300:relever-historique;1500:relever-boucle;1800:enregistrer:$brouillon/e"
# ÉTEINDRE N'EST PAS EFFACER : la règle renvoyait au projet sa propre copie de la
# région, que rien ne tenait à jour — [0,0] sur un projet qu'on vient d'ouvrir.
IFS='|' read -r _ bo bd bf _ _ _ <<< "$(lire "$brouillon/e")"
juger "E. double-clic : la région gardée, éteinte" "$bo $bd,$bf" "non 0,1920"
if grep -q "VSM_GLISSE : .*double — joué" "$brouillon/e.txt"; then ok "E. le double-clic a été joué" ""
else rate "E. le double-clic a été joué" "non (fenêtre vide deux fois ?) : rien de E ne se juge"; fi
mapfile -t marques < <(grep -o "non enregistre : [a-z]*" "$brouillon/e.txt" | cut -d' ' -f4)
juger "E. ouvert, puis double-clic : la marque" "${marques[0]:-?} ${marques[1]:-?}" "non oui"
juger "E. double-clic : aucun pas" "$(grep -o "VSM_HISTORIQUE_PAS : [0-9]*" "$brouillon/e.txt" | tail -1 | cut -d' ' -f3)" "0"
case "$(grep "VSM_BOUCLE" "$brouillon/e.txt" | tail -1)" in
    *"projet non"*"moteur non"*) ok "E. double-clic : la boucle éteinte" "" ;;
    *) rate "E. double-clic : la boucle éteinte" "$(grep "VSM_BOUCLE" "$brouillon/e.txt" | tail -1)" ;;
esac

# F. MAJ+CLIC SANS GLISSÉ (D523) : la règle pose une région VIDE [t,t] et la disait
# active — le moteur la bornait, le projet et le bouton la disaient allumée, et le
# fichier enregistrait une boucle allumée qui ne boucle rien. Une boucle sans région
# n'est pas active (l'invariant du punch). Le glissé va au point de départ : même
# région que l'appui seul.
lancer f boucle-on "400:glisser:pianoroll.regle:0.5,0.5:0.5:maj;700:relever-boucle;900:relever-historique;1200:enregistrer:$brouillon/f"
if grep -q "VSM_GLISSE : .*maj — joué" "$brouillon/f.txt"; then ok "F. le Maj+clic a été joué" ""
else rate "F. le Maj+clic a été joué" "non (fenêtre vide deux fois ?) : rien de F ne se juge"; fi
IFS='|' read -r _ bo bd bf _ _ _ <<< "$(lire "$brouillon/f")"
if [ "$bd" = "$bf" ] && [ "$bd" != "0" ]; then ok "F. la région effacée, ailleurs qu'en [0,1920]" "[$bd,$bf]"
else rate "F. la région effacée, ailleurs qu'en [0,1920]" "[$bd,$bf] : rien ne se juge"; fi
l="$(grep "VSM_BOUCLE" "$brouillon/f.txt" | tail -1)"
echo "        après le Maj+clic : $l"
juger "F. projet / moteur / bouton" \
      "$(sed -E 's/.*projet ([a-z]+) .*moteur ([a-z]+) .*bouton ([a-z]+).*/\1 \2 \3/' <<< "$l")" "non non non"
juger "F. le fichier enregistré : la boucle" "$bo" "non"
juger "F. les pas" "$(grep -o "VSM_HISTORIQUE_PAS : [0-9]*" "$brouillon/f.txt" | tail -1 | cut -d' ' -f3)" "1"

# G. LE MÊME ÉTAT PAR LA PORTE DU FICHIER (D523) : un projet écrit avant le correctif
# porte la boucle allumée sur [t,t]. Rouvert, l'interrupteur est éteint — la région
# gardée —, et le journal le dit.
lancer g vide-on "400:relever-boucle"
l="$(grep "VSM_BOUCLE" "$brouillon/g.txt" | tail -1)"
echo "        à l'ouverture : $l"
juger "G. rouvert : projet / moteur / bouton" \
      "$(sed -E 's/.*projet ([a-z]+) \[([0-9]+),([0-9]+)\].*moteur ([a-z]+) .*bouton ([a-z]+).*/\1 [\2,\3] \4 \5/' <<< "$l")" \
      "non [2887,2887] non non"
if grep -q "VSM_PROJET_CORRIGE : boucle allumée sur une région vide \[2887,2887\]" "$brouillon/g.txt"; then
    ok "G. … et le journal le dit" ""
else rate "G. … et le journal le dit" "aucune ligne VSM_PROJET_CORRIGE"; fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
