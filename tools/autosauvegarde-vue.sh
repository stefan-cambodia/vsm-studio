#!/usr/bin/env bash
# La garde de D368 : UNE REPRISE APRÈS PANNE RETROUVE L'ÉCRAN, PAS SEULEMENT LE TRAVAIL.
#
# RÈGLE GARDÉE (19/09/2026) : la sauvegarde automatique écrit un `project.json`
# qui PORTE la vue d'arrangement (zoom, défilement), et le projet récupéré se
# rouvre à ce zoom-là — pas au cadrage automatique.
#
# POURQUOI. D363 a donné la vue à l'enregistrement MANUEL et a nommé ce qui
# restait : « la sauvegarde automatique n'emporte pas la vue — elle appelle
# `saveProjectBundle` sans ce paramètre […] C'est défendable et ce n'est pas
# mesuré. » Mesuré : le `project.json` de récupération n'avait AUCUN bloc `view`,
# et le projet repris s'ouvrait à 104,2 % (le cadrage) au lieu des 66,7 % où on
# l'avait laissé. Reprendre après une panne, c'est retrouver son écran.
#
# CE QUI A RENDU LA RÈGLE MESURABLE, et sans quoi elle ne l'était pas :
#   * `VSM_AUTOSAUVEGARDE=<fichier>` force une sauvegarde (elle part d'elle-même
#     toutes les trente secondes) et RECOPIE ce qu'elle a écrit — le dossier de
#     session porte un UUID tiré au lancement, et il est EFFACÉ à la fermeture
#     normale, si bien qu'un banc qui irait le lire ensuite ne trouve rien. Payé
#     une fois : le chemin imprimé pointait sur un fichier déjà disparu.
#
# LE CONTRÔLE EST DANS LA GARDE. « rouvert à 66,7 % » ne prouve rien tout seul :
# il faut que 66,7 % s'écarte du cadrage automatique, sinon les deux chiffres
# seraient égaux pour une raison qui n'a rien à voir. La garde mesure donc les
# deux, et exige qu'ils diffèrent.
#
# Rend 0 si les six contrôles tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/autosauvegarde-vue.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-autosave.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# LE PROJET EST ENGENDRÉ : une garde qui dépendrait d'un dossier posé à côté
# d'elle se tairait le jour où il disparaît.
mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" 40 <<'PY'
import json, struct, sys
d, mesures = sys.argv[1], int(sys.argv[2])
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x04Long"), (0, b"\xff\x51\x03\x07\xa1\x20"),
       (0, b"\xff\x58\x04\x04\x02\x18\x08")]
for i in range(mesures):
    evs += [(0 if i == 0 else 1920 - 240, bytes([0x90, 60, 100])),
            (240, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(max(0, dt)) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "autosauvegarde",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                        "instrument": {"preferredPlugin": "vsm.minimoog"},
                        "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0],
                                 "solo": False, "volume": 1.0},
                        "name": "Long"}]},
          open(d + "/project.json", "w"), indent=1)
PY

part_visible() {   # $1 = journal -> le pourcentage montré, vide si illisible
    # D368 : on ne lit QUE les relevés qui portent une part. Un relevé « MORCEAU
    # VIDE » n'en porte pas, et le `sed` d'avant rendait alors la ligne ENTIÈRE
    # au lieu d'un nombre — une comparaison de chaînes l'aurait pris pour une
    # valeur. Ce qui ne se lit pas rend vide, et le contrôle échoue franchement.
    grep "VSM_ARRANGEMENT : fen" "$1" | grep -o "soit [0-9.]* %" | tail -1 \
        | sed 's/soit \([0-9.]*\) %/\1/'
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D368 : la sauvegarde automatique emporte la vue ==="

# (1) zoomer, forcer une sauvegarde, récupérer le fichier écrit
maison="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement" VSM_TOUCHE="arrangement:=;arrangement:=" \
    VSM_ARRANGEMENT=1 VSM_AUTOSAUVEGARDE="$brouillon/recup.json" \
    VSM_CAPTURE="$brouillon/zoome.png" timeout 90 "$BIN" > "$brouillon/zoome.txt" 2>&1
zoome="$(part_visible "$brouillon/zoome.txt")"
echo "       zoomé à ${zoome:-?} %"
verdict "la sauvegarde automatique a écrit un fichier" \
        "$([ -s "$brouillon/recup.json" ] && echo 1 || echo 0)"
porte_la_vue="$(python3 - "$brouillon/recup.json" <<'PY'
import json, sys
try:
    v = json.load(open(sys.argv[1])).get("view")
except Exception:
    print(0); raise SystemExit
print(1 if isinstance(v, dict) and float(v.get("pixelsPerTick", 0)) > 0 else 0)
PY
)"
verdict "le project.json de récupération porte un bloc « view »" "$porte_la_vue"

# (2) le projet récupéré se rouvre AU MÊME endroit
mkdir -p "$brouillon/recupere/midi"
cp "$brouillon/recup.json" "$brouillon/recupere/project.json" 2>/dev/null || true
cp "$brouillon/projet/midi/arrangement.mid" "$brouillon/recupere/midi/" 2>/dev/null || true
maison2="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison2" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/recupere" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement" VSM_ARRANGEMENT=1 \
    VSM_CAPTURE="$brouillon/rouvert.png" timeout 90 "$BIN" > "$brouillon/rouvert.txt" 2>&1
rouvert="$(part_visible "$brouillon/rouvert.txt")"
echo "       récupéré, rouvert à ${rouvert:-?} %"

# LE CONTRÔLE : le zoom à la main doit s'écarter du cadrage automatique, sinon
# « rouvert = zoomé » serait vrai pour une raison qui n'a rien à voir.
maison3="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison3" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement" VSM_ARRANGEMENT=1 \
    VSM_CAPTURE="$brouillon/cadre.png" timeout 90 "$BIN" > "$brouillon/cadre.txt" 2>&1
cadre="$(part_visible "$brouillon/cadre.txt")"
echo "       le même projet SANS vue enregistrée s'ouvre à ${cadre:-?} % (le cadrage)"
verdict "le zoom à la main s'écarte du cadrage automatique" \
        "$(python3 -c "print(1 if abs(float('${zoome:-0}') - float('${cadre:-0}')) > 5 else 0)" 2>/dev/null || echo 0)"
verdict "le projet RÉCUPÉRÉ se rouvre au zoom où on l'a laissé" \
        "$([ -n "${zoome:-}" ] && [ "${zoome:-x}" = "${rouvert:-y}" ] && echo 1 || echo 0)"

# (3) LA VRAIE REPRISE APRÈS PANNE, et c'est ELLE que l'utilisateur fait.
#
# Les contrôles (1) et (2) mesurent le FORMAT : le fichier porte la vue, et un
# dossier qui la porte se rouvre là. Ils ne disent rien du GESTE — la boîte
# « Session interrompue » et son bouton « Récupérer », qui passent par
# `loadProjectBundleFromFolder(dossier, origine)` avec un SECOND argument que
# les contrôles précédents n'empruntent pas. Relire le code suffirait à s'en
# convaincre ; c'est exactement ce que ce dépôt ne fait pas.
#
# ON PROVOQUE DONC UNE VRAIE PANNE. Un lancement SANS `VSM_CAPTURE` ne quitte
# pas (leçon de D318) : ici c'est voulu, on le TUE, et la session reste dans son
# HOME faute d'avoir été fermée proprement — c'est son dossier survivant qui
# signale un plantage. Ne pas « réparer » ce lancement en lui ajoutant une
# capture : il n'y aurait plus de panne à récupérer.
# On attend par PID, jamais par motif (leçon du 12/09).
maison4="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$maison4" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement" VSM_TOUCHE="arrangement:=;arrangement:=" \
    VSM_ARRANGEMENT=1 VSM_AUTOSAUVEGARDE=1 "$BIN" > "$brouillon/panne.txt" 2>&1 &
pid=$!
limite=$((SECONDS + 90))
while kill -0 "$pid" 2>/dev/null && [ "$SECONDS" -lt "$limite" ]; do
    grep -q "VSM_AUTOSAUVEGARDE :" "$brouillon/panne.txt" 2>/dev/null && break
    sleep 1
done
kill -9 "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
avant_panne="$(part_visible "$brouillon/panne.txt")"
echo "       zoomé à ${avant_panne:-?} %, puis TUÉ sans fermeture propre"
verdict "la panne laisse une session à récupérer" \
        "$([ -n "$(ls -A "$maison4/VintageSynthMidiStudio/recuperation" 2>/dev/null)" ] && echo 1 || echo 0)"

# LE RELEVÉ PART EN DIFFÉRÉ, et c'est indispensable : la session récupérée est
# chargée par un rappel modal, APRÈS les relevés de démarrage. Mesuré avec le
# relevé de démarrage, la garde lisait « 3.8 mesure(s) sur 0.0 » — le projet
# vide d'AVANT la reprise — et en tirait un pourcentage.
env HOME="$maison4" VSM_TAILLE="1280x742" VSM_DELAI=5000 \
    VSM_VUE="sans-rapport,arrangement" VSM_RECUPERER=1 \
    VSM_GESTE_APRES="2500:relever-arrangement" \
    VSM_CAPTURE="$brouillon/repris.png" timeout 90 "$BIN" > "$brouillon/repris.txt" 2>&1
repris="$(part_visible "$brouillon/repris.txt")"
echo "       « Récupérer » rouvre à ${repris:-?} %"
verdict "la boîte « Session interrompue » a répondu" \
        "$(grep -q "VSM_RECUPERER : r" "$brouillon/repris.txt" && echo 1 || echo 0)"
verdict "la session RÉCUPÉRÉE retrouve l'écran de la panne" \
        "$([ -n "${avant_panne:-}" ] && [ "${avant_panne:-x}" = "${repris:-y}" ] && echo 1 || echo 0)"

echo
if [ "$rates" -ne 0 ]; then echo "D368 : $rates contrôle(s) raté(s)"; exit 1; fi
echo "D368 : la reprise après panne retrouve l'écran"
