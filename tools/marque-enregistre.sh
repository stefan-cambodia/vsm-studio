#!/usr/bin/env bash
# LA GARDE DE D507 : LA MARQUE « NON ENREGISTRÉ » DIT VRAI APRÈS L'OUVERTURE D'UN PROJET.
# ET DE D508 : ET APRÈS DES NOTES DU PROJET TAPÉES, qui ne passent pas par l'historique.
# ET DE D517 : ET APRÈS UN BOUTON PHYSIQUE APPRIS, qui n'y passe pas non plus.
# ET DE D519 : ET APRÈS LA BOUCLE, LE CLIC OU LE PUNCH BASCULÉS, qui ne font pas de pas (D0, D503).
#
# RÈGLE GARDÉE (29/09/2026). La marque (l'astérisque du titre, et la question que
# pose la fermeture) se déduit de l'historique contre un repère posé à chaque
# enregistrement (D174). Avant D507, ouvrir un DOSSIER de projet vidait
# l'historique sans reposer le repère : le projet ouvert s'affichait « modifié »,
# puis, un geste plus tard, « enregistré » — et fermer ne demandait rien.
#
# COMMENT. Deux projets engendrés, A et B ; les préférences du HOME de banc
# portent B dans « Projets récents ». Une seule course : A ouvert, un geste
# (« Lente », la forme des fondus croisés, qui fait un pas depuis D506), A
# enregistré ; puis B ouvert PAR LE MENU « Projets récents » (le chemin de
# l'utilisateur, `loadProjectBundleFromFolder`) ; un geste sur B ; Ctrl+Z. Après
# chaque étape, `relever-titre` écrit ce que l'application croit (« non
# enregistré : oui/non »). Tout est joué en différé, dans l'ordre.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/marque-enregistre.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-marque.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

for nom in projet-a projet-b; do
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
json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY
done

h="$(mktemp -d "$brouillon/home.XXXX")"
mkdir -p "$h/VintageSynthMidiStudio"
python3 - "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" "$brouillon/projet-b" <<'PY'
import sys
from xml.sax.saxutils import quoteattr
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="projetsRecents" val='
    + quoteattr(sys.argv[2]) + '/>\n</PROPERTIES>\n')
PY

env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-a" VSM_VUE="sans-rapport" VSM_DELAI=3600 \
    VSM_GESTE_APRES="400:menu:Lente;700:enregistrer:$brouillon/projet-a2;1000:relever-titre;1300:menu:projet-b;1700:relever-titre;2000:menu:Rapide;2300:relever-titre;2600:touche:ctrl + Z;2900:relever-titre" \
    VSM_CAPTURE="$brouillon/marque.png" timeout 40 "$BIN" > "$brouillon/marque.txt" 2>&1
grep -E "VSM_(MENU|GESTE_APRES|TOUCHE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE)" "$brouillon/marque.txt" | sed 's/^/        journal : /' >&2
mapfile -t etats < <(grep -o "non enregistre : [a-z]*" "$brouillon/marque.txt" | cut -d' ' -f4)
mapfile -t titres < <(grep "VSM_TITRE_ETAT : " "$brouillon/marque.txt" | sed 's/VSM_TITRE_ETAT : //')

rates=0
etape() {   # $1 index ; $2 nom ; $3 attendu (oui|non)
    local e="${etats[$1]:-?}" t="${titres[$1]:-?}"
    if [ "$e" = "$3" ]; then printf '  OK   %-40s %s\n' "$2" "$t"
    else printf '  RATÉ %-40s %s (attendu : non enregistré %s)\n' "$2" "$t" "$3"; rates=$((rates + 1)); fi
}
echo "=== D507 : la marque « non enregistré » après l'ouverture d'un projet ==="
etape 0 "A : un geste, puis enregistré" non
etape 1 "B ouvert par « Projets récents »" non
etape 2 "B : un geste" oui
etape 3 "B : Ctrl+Z" non

# LA REPRISE APRÈS PANNE : le projet récupéré n'est PAS enregistré (il vient d'une
# copie de travail), et la marque doit le dire jusqu'au prochain enregistrement.
# La panne est un `kill -9` après une autosauvegarde forcée (la méthode de
# `autosauvegarde-vue.sh`, D368) ; on attend par PID, jamais par motif.
h2="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h2" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-b" VSM_VUE="sans-rapport" VSM_DELAI=2500 \
    VSM_MENU="Rapide" VSM_AUTOSAUVEGARDE=1 "$BIN" > "$brouillon/panne.txt" 2>&1 &
pid=$!
limite=$((SECONDS + 60))
while kill -0 "$pid" 2>/dev/null && [ "$SECONDS" -lt "$limite" ]; do
    grep -q "VSM_AUTOSAUVEGARDE :" "$brouillon/panne.txt" 2>/dev/null && break
    sleep 1
done
kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
env HOME="$h2" VSM_TAILLE="1280x800" VSM_VUE="sans-rapport" VSM_DELAI=4000 VSM_RECUPERER=1 \
    VSM_GESTE_APRES="2500:relever-titre" VSM_CAPTURE="$brouillon/repris.png" \
    timeout 60 "$BIN" > "$brouillon/repris.txt" 2>&1
e="$(grep -o "non enregistre : [a-z]*" "$brouillon/repris.txt" | tail -1 | cut -d' ' -f4)"
t="$(grep "VSM_TITRE_ETAT : " "$brouillon/repris.txt" | tail -1 | sed 's/VSM_TITRE_ETAT : //')"
if ! grep -q "VSM_RECUPERER : r" "$brouillon/repris.txt"; then
    printf '  RATÉ %-40s la boîte « Session interrompue » n'"'"'a pas répondu\n' "reprise après panne"
    rates=$((rates + 1))
elif [ "$e" = "oui" ]; then printf '  OK   %-40s %s\n' "reprise après panne" "$t"
else printf '  RATÉ %-40s %s (attendu : non enregistré oui)\n' "reprise après panne" "$t"; rates=$((rates + 1)); fi

# D508 : LES NOTES DU PROJET, écrites hors de l'historique (voulu : pas d'annulation
# pour des mots tapés dans une autre fenêtre) — mais la marque doit le dire.
h3="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h3" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-b" VSM_VUE="sans-rapport" VSM_DELAI=2600 \
    VSM_GESTE_APRES="400:relever-titre;700:notes:Refrain trop long, couper 4 mesures;1000:relever-titre;1300:touche:ctrl + Z;1600:relever-titre;1900:enregistrer:$brouillon/projet-b2;2200:relever-titre" \
    VSM_CAPTURE="$brouillon/notes.png" timeout 40 "$BIN" > "$brouillon/notes.txt" 2>&1
mapfile -t etats < <(grep -o "non enregistre : [a-z]*" "$brouillon/notes.txt" | cut -d' ' -f4)
mapfile -t titres < <(grep "VSM_TITRE_ETAT : " "$brouillon/notes.txt" | sed 's/VSM_TITRE_ETAT : //')
etape 0 "notes : projet ouvert" non
etape 1 "notes tapées" oui
etape 2 "notes tapées, puis Ctrl+Z" oui
etape 3 "notes enregistrées" non
if grep -q "Refrain trop long" "$brouillon/projet-b2/project.json" 2>/dev/null; then
    printf '  OK   %-40s les notes sont dans project.json\n' "notes : le fichier"
else
    printf '  RATÉ %-40s les notes ne sont pas dans project.json\n' "notes : le fichier"; rates=$((rates + 1))
fi
# D517 : LA COMMANDE MIDI APPRISE. Deux chemins (la frontière des threads) : le
# volume, le panoramique… sont DÉPOSÉS pour l'interface ; un PARAMÈTRE DE MACHINE est
# écrit par le thread MIDI lui-même — et l'enregistrement capture les machines
# VIVANTES, donc le bouton tourné change ce que Ctrl+S écrit. Les associations
# viennent des préférences du HOME, au format que l'application écrit ; le
# contrôleur entre par `cc-entrant:`, le point d'entrée d'un port MIDI.
# CC 20 → coupure du Minimoog (paramètre 9), CC 21 → volume de la piste, CC 22 libre.
h4="$(mktemp -d "$brouillon/home.XXXX")"
mkdir -p "$h4/VintageSynthMidiStudio"
python3 - "$h4/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" <<'PY2'
import json, sys
from xml.sax.saxutils import quoteattr
carte = {"format": "vsm.midilearn.v1", "mappings": [
    {"controller": 20, "kind": "instrumentParam", "track": 0, "param": 9, "min": 200.0, "max": 8000.0},
    {"controller": 21, "kind": "trackVolume", "track": 0, "min": 0.0, "max": 1.0}]}
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="midiLearnMappings" val='
    + quoteattr(json.dumps(carte)) + '/>\n</PROPERTIES>\n')
PY2
env HOME="$h4" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-b" VSM_VUE="sans-rapport" VSM_DELAI=3600 \
    VSM_GESTE_APRES="400:enregistrer:$brouillon/cc-0;700:relever-titre;1000:cc-entrant:22:100;1300:relever-titre;1600:cc-entrant:20:100;1900:relever-titre;2200:enregistrer:$brouillon/cc-1;2500:relever-titre;2800:cc-entrant:21:64;3100:relever-titre;3400:enregistrer:$brouillon/cc-2" \
    VSM_CAPTURE="$brouillon/cc.png" timeout 40 "$BIN" > "$brouillon/cc.txt" 2>&1
grep -E "VSM_(MENU|GESTE_APRES|TOUCHE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE)|Associations MIDI" "$brouillon/cc.txt" | sed 's/^/        journal : /' >&2
mapfile -t etats < <(grep -o "non enregistre : [a-z]*" "$brouillon/cc.txt" | cut -d' ' -f4)
mapfile -t titres < <(grep "VSM_TITRE_ETAT : " "$brouillon/cc.txt" | sed 's/VSM_TITRE_ETAT : //')
echo "=== D517 : la commande MIDI apprise ==="
[ "$(grep -c "VSM_CC_ENTRANT : " "$brouillon/cc.txt")" -eq 3 ] \
    || { printf '  RATÉ %-40s les trois contrôleurs ne sont pas tous entrés\n' "cc-entrant"; rates=$((rates + 1)); }
etape 0 "CC : projet enregistré une première fois" non
etape 1 "CC 22 (libre) — contrôle" non
etape 2 "CC 20 (coupure apprise)" oui
etape 3 "CC : enregistré" non
etape 4 "CC 21 (volume appris)" oui
# LE TÉMOIN QU'ELLE EN ÉTAIT PARTIE : la marque qui dit « oui » ne vaut que si le
# réglage a VRAIMENT bougé — lu dans le preset écrit avant et après le contrôleur.
change="$(python3 - "$brouillon/cc-0" "$brouillon/cc-1" <<'PY2'
import json, sys
from pathlib import Path
def lire(d):
    f = Path(d) / "instruments" / "track_00.synth.json"
    return json.loads(f.read_text())["parameters"] if f.is_file() else None
a, b = lire(sys.argv[1]), lire(sys.argv[2])
if a is None or b is None:
    print("ABSENT"); sys.exit()
print(" ; ".join(f"{k} {a[k]:.1f} -> {b.get(k, float('nan')):.1f}" for k in sorted(a) if a[k] != b.get(k)) or "RIEN")
PY2
)"
case "$change" in
    ABSENT) printf '  RATÉ %-40s preset introuvable dans cc-0 ou cc-1\n' "CC 20 : le preset écrit"; rates=$((rates + 1)) ;;
    RIEN)   printf '  RATÉ %-40s aucun réglage n'"'"'a changé entre les deux enregistrements\n' "CC 20 : le preset écrit"; rates=$((rates + 1)) ;;
    *)      printf '  OK   %-40s %s\n' "CC 20 : le preset écrit" "$change" ;;
esac
vol="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["tracks"][0]["mix"]["volume"])' "$brouillon/cc-2/project.json" 2>/dev/null)"
if [ -n "$vol" ] && [ "$vol" != "1.0" ]; then printf '  OK   %-40s volume 1.0 -> %s\n' "CC 21 : le projet écrit" "$vol"
else printf '  RATÉ %-40s volume « %s » (attendu : autre que 1.0)\n' "CC 21 : le projet écrit" "$vol"; rates=$((rates + 1)); fi
# D519 : LES BASCULES DU MORCEAU — boucle, clic, punch (« Active ») — et la boucle
# basculée par une commande apprise. Aucune ne fait de pas (D0, D503) ; toutes
# s'écrivent dans le bloc `transport`. Un projet « c » porte une région de punch
# éteinte (sans elle, « Active » est grisée). Chaque bascule suit un enregistrement :
# la marque repart de « non », et le fichier écrit APRÈS dit que la bascule a bien
# changé le morceau (le témoin qu'elle en était partie, D145).
mkdir -p "$brouillon/projet-c"
cp -r "$brouillon/projet-b/midi" "$brouillon/projet-c/"
python3 - "$brouillon/projet-b/project.json" "$brouillon/projet-c/project.json" <<'PY2'
import json, sys
p = json.load(open(sys.argv[1]))
p["title"] = "projet-c"
p["transport"]["punch"] = {"enabled": False, "startTick": 0, "endTick": 1920}
json.dump(p, open(sys.argv[2], "w"), indent=1)
PY2
h5="$(mktemp -d "$brouillon/home.XXXX")"
mkdir -p "$h5/VintageSynthMidiStudio"
python3 - "$h5/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" <<'PY2'
import json, sys
from xml.sax.saxutils import quoteattr
carte = {"format": "vsm.midilearn.v1", "mappings": [{"controller": 23, "kind": "transportLoop", "min": 0.0, "max": 1.0}]}
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="midiLearnMappings" val='
    + quoteattr(json.dumps(carte)) + '/>\n</PROPERTIES>\n')
PY2
env HOME="$h5" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-c" VSM_VUE="sans-rapport" VSM_DELAI=5200 \
    VSM_GESTE_APRES="400:relever-titre;700:menu:Boucle (marche / arrêt);1000:relever-titre;1300:enregistrer:$brouillon/bascule-1;1600:menu:Métronome (marche / arrêt);1900:relever-titre;2200:enregistrer:$brouillon/bascule-2;2500:menu:Enregistrement > Active;2800:relever-titre;3100:enregistrer:$brouillon/bascule-3;3400:cc-entrant:23:127;3700:relever-titre;4000:enregistrer:$brouillon/bascule-4;4300:menu:Tête : mesure suivante;4600:relever-titre" \
    VSM_CAPTURE="$brouillon/bascules.png" timeout 45 "$BIN" > "$brouillon/bascules.txt" 2>&1
grep -E "VSM_(MENU|GESTE_APRES|TOUCHE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE)|Associations MIDI" "$brouillon/bascules.txt" | sed 's/^/        journal : /' >&2
mapfile -t etats < <(grep -o "non enregistre : [a-z]*" "$brouillon/bascules.txt" | cut -d' ' -f4)
mapfile -t titres < <(grep "VSM_TITRE_ETAT : " "$brouillon/bascules.txt" | sed 's/VSM_TITRE_ETAT : //')
echo "=== D519 : les bascules du morceau ==="
etape 0 "bascules : projet ouvert" non
etape 1 "Boucle (marche / arrêt)" oui
etape 2 "Métronome (marche / arrêt)" oui
etape 3 "Enregistrement ▸ Active (punch)" oui
etape 4 "CC 23 appris sur la boucle" oui
etape 5 "contrôle : Tête : mesure suivante" non
lu="$(python3 - "$brouillon" <<'PY2'
import json, sys
from pathlib import Path
def t(n):
    f = Path(sys.argv[1]) / f"bascule-{n}" / "project.json"
    if not f.is_file():
        return None
    x = json.loads(f.read_text())["transport"]
    return (bool(x.get("loop", {}).get("enabled")), bool(x.get("metronome")), bool(x.get("punch", {}).get("enabled")))
e = [t(n) for n in (1, 2, 3, 4)]
if None in e:
    print("ABSENT"); sys.exit()
print(" ".join("/".join("oui" if v else "non" for v in x) for x in e))
PY2
)"
# boucle/clic/punch dans chaque fichier : chaque bascule change SA donnée, et elle seule.
if [ "$lu" = "oui/non/non oui/oui/non oui/oui/oui non/oui/oui" ]; then
    printf '  OK   %-40s boucle/clic/punch : %s\n' "bascules : les fichiers écrits" "$lu"
else
    printf '  RATÉ %-40s boucle/clic/punch : %s (attendu : oui/non/non oui/oui/non oui/oui/oui non/oui/oui)\n' \
        "bascules : les fichiers écrits" "$lu"; rates=$((rates + 1))
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
