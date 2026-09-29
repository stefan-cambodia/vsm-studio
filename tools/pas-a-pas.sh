#!/usr/bin/env bash
# LA GARDE DE D500 : LA SAISIE PAS À PAS AVANCE, AU RYTHME D'UN MUSICIEN.
#
# RÈGLE GARDÉE (29/09/2026). D13.5 : « chaque note reçue s'écrit à la position, de
# la longueur de la grille, puis la tête avance d'un pas ; Entrée avance sans
# note, Retour arrière recule ; la tête de lecture EST la position d'insertion ».
# Avant D500, la saisie n'avançait que la tête DESSINÉE du piano roll, que la
# minuterie remettait à celle du transport entre deux notes : tapées à 500 ms
# d'intervalle, les notes tombaient toutes au tick 0.
#
# COMMENT. « Clavier d'ordinateur » et « Pas à pas » armés, les touches sont jouées
# par le geste `touche:` (D500) en DIFFÉRÉ, à 500 ms d'intervalle
# (`VSM_GESTE_APRES`) ; le projet est enregistré ensuite (geste `enregistrer:`) et
# ses notes relues ; la tête du transport par `relever-tete`.
#
# PAS DE COURSE « D'UN BLOC » (VSM_TOUCHE au démarrage), et c'est une leçon du
# banc, pas du logiciel : les notes du clavier sont POSTÉES au fil d'interface et
# écrites après le bloc, alors qu'Entrée et Retour arrière agissent tout de suite
# — la course mesurait l'ordre du banc (0, 120, 240, 360 avant comme après D500),
# pas ce qu'un musicien obtient.
#
# Grille 1/16 = 120 ticks. Suite : A (do) S (ré) Entrée D (mi) Retour-arrière F (fa).
# Attendu : do 0, ré 120, mi 360 (Entrée laisse un silence à 240), fa 360
# (Retour arrière revient d'un pas) ; tête finale 480.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/pas-a-pas.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || PY=python3

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pas-a-pas.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

ATTENDU="0:60 120:62 360:64 360:65"

lire() {   # $1 = dossier écrit -> « tick:note … » triés
    "$PY" - "$1/midi/arrangement.mid" <<'PY'
import sys
try:
    import mido
except ImportError:
    print("REFUS-mido"); sys.exit(0)
try:
    m = mido.MidiFile(sys.argv[1])
except OSError:
    print("aucun-fichier"); sys.exit(0)
ons = []
for t in m.tracks:
    tick = 0
    for msg in t:
        tick += msg.time
        if msg.type == "note_on" and msg.velocity > 0:
            ons.append((tick, msg.note))
print(" ".join(f"{t}:{n}" for t, n in sorted(ons)))
PY
}

rates=0
juger() {   # $1 nom ; $2 notes ; $3 tête (ou « - »)
    local bon=0
    [ "$2" = "$ATTENDU" ] && { [ "$3" = "-" ] || [ "$3" = "480" ]; } && bon=1
    if [ "$bon" -eq 1 ]; then
        printf '  OK   %-10s notes %s ; tête %s\n' "$1" "$2" "$3"
    else
        printf '  RATÉ %-10s notes %s ; tête %s (attendu %s ; tête 480)\n' "$1" "$2" "$3" "$ATTENDU"
        rates=$((rates + 1))
    fi
}

echo "=== D500 : la saisie pas à pas avance, au rythme d'un musicien ==="
h="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_MENU="Clavier d'ordinateur" \
    VSM_GESTE_PISTE="cliquer:Pas à pas" VSM_DELAI=4200 \
    VSM_GESTE_APRES="400:touche:x11:A;900:touche:x11:S;1400:touche:return;1900:touche:x11:D;2400:touche:backspace;2900:touche:x11:F;3300:relever-tete;3500:enregistrer:$brouillon/rythme" \
    VSM_CAPTURE="$brouillon/rythme.png" timeout 40 "$BIN" > "$brouillon/rythme.txt" 2>&1
grep -E "VSM_(TOUCHE|GESTE_APRES|CLIC|MENU) : .*(AUCUNE|aucune|refusé|JAMAIS|GRISÉ|illisible)" "$brouillon/rythme.txt" | sed 's/^/        journal : /' >&2
tete="$(grep -o "VSM_TETE : tick -\?[0-9]*" "$brouillon/rythme.txt" | tail -1 | grep -o "\-\?[0-9]*$")"
juger "rythme" "$(lire "$brouillon/rythme")" "${tete:-?}"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
