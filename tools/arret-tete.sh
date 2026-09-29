#!/usr/bin/env bash
# LA GARDE DE D505 : STOP LAISSE LA TÊTE OÙ ELLE EST (sauf préférence, et sauf la
# fin du morceau).
#
# RÈGLE GARDÉE (29/09/2026). D14.5 : « Stop laisse la tête où elle est » ; la
# préférence « À l'arrêt, revenir au point de départ » la ramène au départ.
# Avant D505, `Transport::stop()` rembobinait à 0 à chaque geste d'arrêt : la
# préférence décochée donnait 0, et la fin d'un scrub aussi.
#
# COMMENT. Un projet de 32 mesures ; la lecture part de la mesure 3 (tick 3 840,
# `VSM_POSITION`), l'arrêt est joué en différé (`VSM_GESTE_APRES`) — la barre
# d'espace (`touche:spacebar`), puis le bouton Stop (`cliquer:Stop`) —, et la tête
# du transport relevée ensuite (`relever-tete`). Le scrub est joué par `VSM_VUE`
# (`scrub:5760:1` puis `scrub:5760:0`, le relâchement) et la tête relevée par
# `VSM_TETE`. Chaque course a son HOME neuf (D318) ; la préférence cochée est
# écrite dans le fichier de préférences de CE HOME.
#
# CONTRÔLES, qui ne doivent pas changer : préférence cochée → le départ (3 840) ;
# fin du morceau (un projet d'une mesure, joué jusqu'au bout) → 0.
# NON MESURÉE, ET DITE : la commande MIDI apprise — aucun verbe de banc n'en joue.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/arret-tete.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-arret-tete.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

projet() {   # $1 = dossier ; $2 = nombre de mesures
    mkdir -p "$1/midi"
    python3 - "$1" "$2" <<'PY'
import json, struct, sys
d, mesures = sys.argv[1], int(sys.argv[2])
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une")]
for i in range(mesures):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "arret", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY
}
projet "$brouillon/long" 32
projet "$brouillon/court" 1

course() {   # $1 nom ; $2 retour (0|1) ; $3... variables -> dernier tick relevé
    local nom="$1" h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    if [ "$2" = "1" ]; then
        mkdir -p "$h/VintageSynthMidiStudio"
        printf '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="retourAuDepartALArret" val="1"/>\n</PROPERTIES>\n' \
            > "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    fi
    env HOME="$h" VSM_TAILLE="1280x800" VSM_CAPTURE="$brouillon/$nom.png" "${@:3}" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(TOUCHE|GESTE_APRES|CLIC|VUE)[A-Z_]* : .*(AUCUNE|aucun|refusé|JAMAIS|GRISÉ|AMBIGU)" "$brouillon/$nom.txt" \
        | sed 's/^/        journal : /' >&2
    grep -o "VSM_TETE : tick -\?[0-9]*" "$brouillon/$nom.txt" | tail -1 | grep -o "\-\?[0-9]*$"
}

rates=0
juger() {   # $1 nom ; $2 tick ; $3 condition python sur t ; $4 attendu (texte)
    if [ -n "$2" ] && python3 -c "import sys; t=int(sys.argv[1]); sys.exit(0 if ($3) else 1)" "$2"; then
        printf '  OK   %-34s tête %s\n' "$1" "$2"
    else
        printf '  RATÉ %-34s tête %s (attendu : %s)\n' "$1" "${2:-?}" "$4"
        rates=$((rates + 1))
    fi
}

echo "=== D505 : Stop laisse la tête où elle est ==="
JOUER=(VSM_PROJET="$brouillon/long" VSM_VUE="sans-rapport" VSM_POSITION=3 VSM_LECTURE=1 VSM_DELAI=3000)
juger "Espace, préférence décochée" \
      "$(course espace 0 "${JOUER[@]}" VSM_GESTE_APRES="1500:touche:spacebar;2200:relever-tete")" \
      "t > 3840" "après le départ (3 840), là où elle s'est arrêtée"
juger "bouton Stop, préférence décochée" \
      "$(course bouton 0 "${JOUER[@]}" VSM_GESTE_APRES="1500:cliquer:Stop;2200:relever-tete")" \
      "t > 3840" "après le départ (3 840), là où elle s'est arrêtée"
juger "scrub relâché au tick 5 760" \
      "$(course scrub 0 VSM_PROJET="$brouillon/long" VSM_VUE="sans-rapport,scrub:5760:1,scrub:5760:0" VSM_TETE=1 VSM_DELAI=1500)" \
      "t == 5760" "5 760, là où on l'a relâché"
juger "Espace, préférence COCHÉE (contrôle)" \
      "$(course retour 1 "${JOUER[@]}" VSM_GESTE_APRES="1500:touche:spacebar;2200:relever-tete")" \
      "t == 3840" "3 840, le départ"
juger "scrub, préférence COCHÉE" \
      "$(course scrub-retour 1 VSM_PROJET="$brouillon/long" VSM_VUE="sans-rapport,scrub:5760:1,scrub:5760:0" VSM_TETE=1 VSM_DELAI=1500)" \
      "t == 5760" "5 760 : un scrub n'est pas une lecture, le retour au départ ne s'y applique pas"
juger "fin du morceau (contrôle)" \
      "$(course fin 0 VSM_PROJET="$brouillon/court" VSM_VUE="sans-rapport" VSM_LECTURE=1 VSM_DELAI=5000 VSM_GESTE_APRES="4500:relever-tete")" \
      "t == 0" "0 : la fin du morceau rembobine"
echo "    non mesurée (dite, pas comptée) : la commande MIDI apprise — aucun verbe de banc n'en joue."
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
