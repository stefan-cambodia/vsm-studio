#!/usr/bin/env bash
# D535.2 — LES INSTANTANÉS DE LA CONSOLE : PRENDRE, RAPPELER, ANNULER — MESURÉS PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Deux pistes ; une course qui :
#   prend « Instantané 1 », enregistre (PA) ; met la piste 1 à 0,5 et la piste 2 en muet ; prend
#   « Instantané 2 » ; rappelle « Instantané 2 » (rien à changer : aucun pas, et c'est dit) ;
#   enregistre (PB) ; rappelle « Instantané 1 », enregistre (PR) ; Ctrl+Z, enregistre (PU).
# Puis quatre EXPORTS AUDIO (la leçon de D332 : une valeur au modèle n'est pas un son) :
#   (1) PR = PA au bit près ;  (2) PU = PB au bit près ;  (3) le témoin : PB ≠ PA ;
#   (4) project.json de PR : les deux noms, la piste 1 à son volume d'avant, la 2 audible ; celui
#       de PU : le mixage B ET les deux noms (Ctrl+Z n'a défait que le rappel) ;
#   (5) le journal : « 2 piste(s) rappelée(s), 2 changée(s), 0 sans état » ; « rien à changer » ;
#   (6) « Prendre » sous un nom pris (« Instantané 1 ») → la fenêtre « Remplacer l'instantané ? »
#       s'ouvre, sans réponse rien n'est remplacé.
# Chaque fichier se lit AVANT de juger (deux fichiers présents et non vides avant un cmp), et les
# avertissements du journal sont relayés.
#
#   tools/instantanes-console.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-instantanes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/deux/midi"
python3 - "$brouillon/deux" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, canal, notes, tempo=False):
    evs = ([(0, b"\xff\x51\x03\x07\xa1\x20")] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for debut, hauteur, duree in notes:
        evs += [(debut - t, bytes([0x90 | canal, hauteur, 100])), (duree, bytes([0x80 | canal, hauteur, 0]))]
        t = debut + duree
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def entree(nom, canal):
    return {"channel": canal, "color": "#FF6B9BFF", "effects": [], "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}, "name": nom}
data = b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480)
data += piste("Basse", 0, [(0, 45, 960), (960, 48, 960)], tempo=True)
data += piste("Nappe", 1, [(0, 64, 1920)])
open(f"{d}/midi/arrangement.mid", "wb").write(data)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Basse", 0), entree("Nappe", 1)]}, open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables en plus
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU|OPTIONS|CONFIRMER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|grisée)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# La console du fichier, lue par Python : « noms | volumes | muets », ou « ABSENT ».
lire() {
    python3 - "$1/project.json" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("ABSENT"); sys.exit(0)
pistes = d.get("tracks", [])
print(",".join(d.get("mixSnapshots", [])) or "-", "|",
      ",".join(f"{p['mix']['volume']:.3f}" for p in pistes), "|",
      ",".join("muet" if p["mix"]["muted"] else "audible" for p in pistes))
PY
}
taille() { stat -c %s "$1" 2>/dev/null || echo absent; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
# Deux exports se comparent s'ils existent TOUS DEUX et ne sont pas vides (ordre de marche, 13/09).
pareils() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2"; }
differents() { [ -s "$1" ] && [ -s "$2" ] && ! cmp -s "$1" "$2"; }

echo "=== D535.2 : les instantanés de la console ==="
P="Mixage > Instantanés de la console"
course suite "$brouillon/deux" VSM_DELAI=6500 VSM_CONFIRMER=oui VSM_GESTE_APRES="\
1200:menu:$P > Prendre un instantané…;1600:enregistrer:$brouillon/PA;\
1900:choisir:0;2100:volume:0.5;2300:choisir:1;2500:muet;\
2800:menu:$P > Prendre un instantané…;3100:menu:$P > Rappeler « Instantané 2 »;3400:enregistrer:$brouillon/PB;\
3700:menu:$P > Rappeler « Instantané 1 »;4000:enregistrer:$brouillon/PR;\
4400:touche:ctrl + Z;4800:enregistrer:$brouillon/PU;5200:relever-historique"
grep "^VSM_INSTANTANE" "$brouillon/suite.txt" | sed 's/^/       /'
for x in PA PB PR PU; do echo "       $x : $(lire "$brouillon/$x")"; done

for x in PA PB PR PU; do
    course "export-$x" "$brouillon/$x" VSM_DELAI=2500 VSM_EXPORT="$brouillon/$x.wav"
done
echo "       exports (octets) : PA $(taille "$brouillon/PA.wav"), PB $(taille "$brouillon/PB.wav"), PR $(taille "$brouillon/PR.wav"), PU $(taille "$brouillon/PU.wav")"
verdict "(1) rappeler « Instantané 1 » : l'export de PR = celui de PA, au bit près" \
    "$(pareils "$brouillon/PR.wav" "$brouillon/PA.wav" && echo 1 || echo 0)"
verdict "(2) Ctrl+Z après le rappel : l'export de PU = celui de PB, au bit près" \
    "$(pareils "$brouillon/PU.wav" "$brouillon/PB.wav" && echo 1 || echo 0)"
verdict "(3) le témoin : PB ≠ PA (le mixage changé s'entend)" \
    "$(differents "$brouillon/PB.wav" "$brouillon/PA.wav" && echo 1 || echo 0)"
verdict "(4) project.json de PR : deux noms, la basse à 0,800, la nappe audible" \
    "$([ "$(lire "$brouillon/PR")" = "Instantané 1,Instantané 2 | 0.800,0.800 | audible,audible" ] && echo 1 || echo 0)"
verdict "(4) project.json de PB : la basse à 0,500, la nappe muette" \
    "$([ "$(lire "$brouillon/PB")" = "Instantané 1,Instantané 2 | 0.500,0.800 | audible,muet" ] && echo 1 || echo 0)"
# PU porte les DEUX noms : Ctrl+Z a défait le RAPPEL et lui seul. Un rappel fait sans son pas
# laisserait Ctrl+Z défaire la prise d'« Instantané 2 » — même mixage, mais un nom de moins.
verdict "(4) project.json de PU : le mixage B et les deux noms (Ctrl+Z n'a défait que le rappel)" \
    "$([ "$(lire "$brouillon/PU")" = "Instantané 1,Instantané 2 | 0.500,0.800 | audible,muet" ] && echo 1 || echo 0)"
verdict "(5) le journal : 2 pistes rappelées, 2 changées, 0 sans état" \
    "$(grep -q "^VSM_INSTANTANE : rappelé « Instantané 1 » — 2 piste(s) rappelée(s), 2 changée(s), 0 sans état, 0 insert(s) laissé(s)" "$brouillon/suite.txt" && echo 1 || echo 0)"
verdict "(5) rappeler l'instantané qu'on vient de prendre : rien à changer, aucun pas" \
    "$(grep -q "^VSM_INSTANTANE : rappel de « Instantané 2 » — rien à changer, aucun pas" "$brouillon/suite.txt" && echo 1 || echo 0)"

course remplacer "$brouillon/PB" VSM_DELAI=3000 VSM_OPTIONS="nom=Instantané 1" \
    VSM_GESTE_APRES="1200:menu:$P > Prendre un instantané…"
boite="$(grep "^VSM_BOITE : Remplacer l'instantané" "$brouillon/remplacer.txt" | head -1)"
echo "       ${boite:-aucune boîte « Remplacer »}"
verdict "(6) un nom pris : la question est posée, et rien n'est remplacé sans réponse" \
    "$([ -n "$boite" ] && ! grep -q "^VSM_INSTANTANE : remplacé" "$brouillon/remplacer.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "INSTANTANÉS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "INSTANTANÉS : la console se garde, se rappelle au bit près, et s'annule"
