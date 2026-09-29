#!/usr/bin/env bash
# LA GARDE DE D495 : LA GRILLE, LE SWING ET LA GAMME DU PIANO ROLL APPARTIENNENT AU
# MORCEAU.
#
# RÈGLE GARDÉE (29/09/2026). Ils vont dans le bloc `view` du projet (la règle de
# partage de D363 : ils dépendent du morceau). Donc : (1) un projet enregistré les
# porte, et rouvert il les retrouve — dans le piano roll ET dans sa barre ; (2) un
# AUTRE morceau ouvert ensuite n'en hérite pas ; (3) une valeur absurde est
# écartée, le défaut repris, et c'est DIT (`VSM_VUE_IGNOREE`).
#
# COMMENT. Un projet de trois pistes est engendré, et un double dont la vue porte
# des valeurs absurdes. Les gestes passent par la barre, comme la souris :
# `liste:<nom>=<entrée>` (D495) pour les listes, `valeur:` pour le swing, `cliquer:`
# pour « Gamme ». Le relevé `VSM_PIANOROLL_ZONES=1` écrit l'état du piano roll PUIS
# ce que la barre affiche (« … | barre : … ») ; chaque course a son HOME neuf (D318),
# si bien que rien ne passe par les préférences.
#
# CHAQUE CAS PORTE SON TÉMOIN (D145) : l'état des gestes diffère du défaut, sans
# quoi « retrouvé » et « jamais changé » rendraient la même ligne.
#
# « ouvrir-midi: » et « nouveau-projet » sont des GESTES (D495), joués dans l'ordre
# de `VSM_GESTE_PISTE`, donc APRÈS les réglages ; `VSM_MENU` et `VSM_VUE` agissent
# avant eux. NON MESURÉ, ET DIT : l'import d'un projet d'un autre DAW — il appelle
# la même fonction que les deux autres portes.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/grille-gamme-projet.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-grille-gamme.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/projet/midi" "$brouillon/absurde/midi" "$brouillon/type-inconnu/midi"
python3 - "$brouillon/projet" "$brouillon/absurde" "$brouillon/type-inconnu" <<'PY'
import json, struct, sys
d, absurde, type_inconnu = sys.argv[1], sys.argv[2], sys.argv[3]
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
for dossier in (d, absurde, type_inconnu):
    open(dossier + "/midi/arrangement.mid", "wb").write(data)
def t(nom):
    return {"channel": 0, "color": "#FF6B9BFF", "effects": [],
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
            "name": nom}
projet = {"format": "vsm-project", "version": 1, "title": "grille",
          "midi": {"file": "midi/arrangement.mid"},
          "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                        "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [t("une"), t("deux"), t("trois")]}
json.dump(projet, open(d + "/project.json", "w"), indent=1)
# QUATRE VALEURS QUI NE VEULENT RIEN DIRE — celles de l'attendu écrit : une grille
# qui n'existe pas, un modificateur inconnu, un swing hors de 0..1, une tonique hors
# de 0..11 (qui écarte la gamme ENTIÈRE : son type n'est alors pas lu).
projet["view"] = {"pianoRollGrid": "1/7", "pianoRollGridModifier": "wobble", "pianoRollSwing": 2,
                  "scale": {"root": 13, "type": "inconnu", "highlight": True}}
json.dump(projet, open(absurde + "/project.json", "w"), indent=1)
# ET UN TYPE SEUL INCONNU : la tonique valable est GARDÉE, le type revient à
# chromatique, le surlignage reste — c'est le type qui est faux, pas la gamme.
projet["view"] = {"scale": {"root": 5, "type": "inconnu", "highlight": True}}
json.dump(projet, open(type_inconnu + "/project.json", "w"), indent=1)
PY

DEFAUT="grille=1/16 modificateur=droit swing=0.00 tonique=0 mode=0 surlignage=non | barre : 1/16, Droit, 0 %, C, Chromatique, Gamme non"
GESTES="liste:pianoroll.grille=1/8;liste:pianoroll.grille.modificateur=Triolet;valeur:pianoroll.swing=0.5;liste:pianoroll.gamme.tonique=D;liste:pianoroll.gamme.mode=Dorien;cliquer:Gamme"
ETAT_DES_GESTES="grille=1/8 modificateur=triplet swing=0.50 tonique=2 mode=5 surlignage=oui | barre : 1/8, Triolet, 50 %, D, Dorien, Gamme oui"

# UN NOM DE FICHIER PAR CAS, pas un compteur : `course` s'appelle dans un `$(…)`,
# un sous-shell, et un compteur n'en sortirait pas (vu : « absurde-0.txt »).
course() {   # $1 = nom ; $2... = variables -> la dernière ligne VSM_PIANOROLL_GRILLE (sans son préfixe)
    local maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_TAILLE="1600x1000" VSM_DELAI=800 VSM_VUE="sans-rapport" \
        VSM_PIANOROLL_ZONES=1 VSM_CAPTURE="$brouillon/$1.png" "${@:2}" \
        timeout 40 "$BIN" > "$brouillon/$1.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147).
    grep -E "VSM_(MENU|TOUCHE|VUE|GESTE|CLIC|LISTE|VALEUR)[A-Z_]* : .*(aucune|AUCUNE|aucun |inconnu|illisible|grisée|GRISÉ|AMBIGU|refusé)" \
        "$brouillon/$1.txt" | sed 's/^/        journal : /' >&2
    grep "^VSM_PIANOROLL_GRILLE : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_PIANOROLL_GRILLE : //'
}

rates=0
juger() {   # $1 nom ; $2 obtenu ; $3 attendu ; $4 condition supplémentaire (0/1) ; $5 ce qu'elle dit
    if [ "$2" = "$3" ] && [ "$4" -eq 1 ]; then
        printf '  OK   %-10s %s%s\n' "$1" "$2" "${5:+ ; $5}"
    else
        printf '  RATÉ %-10s « %s » (attendu « %s »)%s\n' "$1" "$2" "$3" "${5:+ ; $5}"
        rates=$((rates + 1))
    fi
}

echo "=== D495 : la grille, le swing et la gamme du piano roll, portés par le projet ==="
apres="$(course ecrit VSM_PROJET="$brouillon/projet" VSM_GESTE_PISTE="$GESTES" VSM_ENREGISTRER="$brouillon/ecrit")"
champs="$(python3 - "$brouillon/ecrit/project.json" <<'PY'
import json, sys
try:
    vue = json.load(open(sys.argv[1], encoding="utf-8")).get("view", {})
except (OSError, ValueError):
    vue = {}
print(sum(1 for k in ("pianoRollGrid", "pianoRollGridModifier", "pianoRollSwing", "scale") if k in vue))
PY
)"
[ "$apres" != "$DEFAUT" ] && temoin_ok=1 || temoin_ok=0
juger "gestes" "$apres" "$ETAT_DES_GESTES" "$temoin_ok" "différent du défaut"
[ "$champs" = "4" ] && ok=1 || ok=0
juger "fichier" "$champs champ(s) sur 4 dans project.json" "4 champ(s) sur 4 dans project.json" "$ok"
rouvert="$(course rouvert VSM_PROJET="$brouillon/ecrit")"
juger "rouvert" "$rouvert" "$ETAT_DES_GESTES" 1
autre="$(course autre VSM_PROJET="$brouillon/projet" VSM_GESTE_PISTE="$GESTES;ouvrir-midi:$brouillon/projet/midi/arrangement.mid")"
juger "autre" "$autre" "$DEFAUT" 1 "un autre morceau n'hérite de rien"
nouveau="$(course nouveau VSM_PROJET="$brouillon/projet" VSM_GESTE_PISTE="$GESTES;nouveau-projet")"
juger "nouveau" "$nouveau" "$DEFAUT" 1 "un projet neuf n'hérite de rien"
absurde="$(course absurde VSM_PROJET="$brouillon/absurde")"
ecartes="$(grep -c "^VSM_VUE_IGNOREE : " "$brouillon/absurde.txt")"
[ "$ecartes" = "4" ] && ok=1 || ok=0
juger "absurde" "$absurde" "$DEFAUT" "$ok" "$ecartes valeur(s) écartée(s) et dite(s), attendu 4"
grep "^VSM_VUE_IGNOREE : " "$brouillon/absurde.txt" | sed 's/^/        /'
seul="$(course type-inconnu VSM_PROJET="$brouillon/type-inconnu")"
ecartes="$(grep -c "^VSM_VUE_IGNOREE : " "$brouillon/type-inconnu.txt")"
[ "$ecartes" = "1" ] && ok=1 || ok=0
juger "type seul" "$seul" \
      "grille=1/16 modificateur=droit swing=0.00 tonique=5 mode=0 surlignage=oui | barre : 1/16, Droit, 0 %, F, Chromatique, Gamme oui" \
      "$ok" "$ecartes valeur(s) écartée(s) et dite(s), attendu 1 — la tonique est gardée"
echo "    non mesuré (dit, pas compté) : l'import d'un projet d'un autre DAW — la même"
echo "    fonction qu'« ouvrir un MIDI » et que « Nouveau projet »."
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
