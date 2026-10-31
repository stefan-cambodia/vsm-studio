#!/usr/bin/env bash
# D620 — UN CLIP AUDIO OUVERT COMPTE JUSQU'AU BOUT DE SON FICHIER, mesuré dans l'application et par `vsm-render` : un projet
# d'une piste audio SANS clip (l'ouverture lui pose un clip de longueur nulle, `materializeImplicitClips`) et d'un MIDI
# vide ; Play pressé (`cliquer:Play`) et le transport relevé (`relever-ordre`), l'export de l'application
# (`exporter-audio:`, D589.3) et le rendu de `vsm-render`, leurs durées lues dans l'en-tête des WAV.
#
# LE JEU : un fichier de 3 s, mono, 48 kHz ; 120 BPM.
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D620) :
#   Play → en lecture, la tête entre 0,8 et 1,6 s au relevé d'1,2 s (le témoin : arrêté, 0,000) ; l'export de
#   l'application 5,0 s (le témoin : 2,0 s, l'hypothèse tranchée à 01 h 44) ; la durée égale à celle de `vsm-render`.
#
#   tools/clip-audio-ouvert.sh [binaire] [binaire témoin] — `vsm-render` est pris À CÔTÉ de chaque binaire s'il y est
#   (les témoins `build/temoin-D*/`), sinon dans `build/tools/`.
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D619/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
rendu_de() { local d; d="$(dirname "$1")"; if [ -x "$d/vsm-render" ]; then echo "$d/vsm-render"; else echo "./build/tools/vsm-render"; fi; }
RENDU="$(rendu_de "$BIN")"
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-clip-ouvert.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1] + "/projet"; sr = 48000
os.makedirs(f"{b}/audio", exist_ok=True); os.makedirs(f"{b}/midi", exist_ok=True)
with wave.open(f"{b}/audio/a.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<h", int(3000 * math.sin(2 * math.pi * 220 * n / sr))) for n in range(3 * sr)))
t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(t)) + t)
json.dump({"format": "vsm-project", "version": 1, "title": "ouvert", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "A",
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "audio": {"file": "audio/a.wav", "sampleRate": sr, "frames": 3 * sr, "channels": 1}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_CLIPS=1 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC)|VSM_EXPORT : ÉCHEC"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# La durée d'un WAV, en secondes au millième ; « ABSENT » s'il manque.
duree() {
    python3 - "$1" <<'PY2'
import os, struct, sys
p = sys.argv[1]
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None
while i + 8 <= len(d):
    nom, t = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24])
    if nom == b"data": data = t
    i += 8 + t + (t & 1)
print("ABSENT" if fmt is None or data is None else f"{data / (fmt[1] * fmt[5] // 8) / fmt[2]:.3f}")
PY2
}
transport() { grep "^VSM_ORDRE : relevé" "$brouillon/$1.txt" | tail -1 | sed -n 's/.* ; tête \([0-9.]*\) s ; \([a-zé ]*\) ; case.*/\1 \2/p'; }
dans() { python3 -c "import sys; sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

LIRE="1000:cliquer:Play;2200:relever-ordre"
echo "=== D620 : un clip audio ouvert compte jusqu'au bout de son fichier ==="
course "$BIN" lecture "$LIRE"
course "$BIN" export "800:exporter-audio:$brouillon/export.wav"
"$RENDU" "$brouillon/projet" "$brouillon/rendu.wav" > "$brouillon/rendu.txt" 2>&1
echo "       $(grep "^VSM_CLIP : " "$brouillon/lecture.txt" | head -1)"
read -r TETE ETAT <<< "$(transport lecture)"
DE="$(duree "$brouillon/export.wav")"; DR="$(duree "$brouillon/rendu.wav")"
echo "       Play : tête ${TETE:-?} s, ${ETAT:-?} ; export de l'application $DE s ; vsm-render ($RENDU) $DR s"
verdict "(b) Play : en lecture, la tête entre 0,8 et 1,6 s au relevé d'1,2 s (${TETE:-?} s, ${ETAT:-?})" \
    "$([ "${ETAT:-}" = "en lecture" ] && dans "${TETE:-x}" 0.8 1.6 && echo 1 || echo 0)"
verdict "(b) l'export de l'application : 5,0 s ($DE)" "$([ "$DE" = "5.000" ] && echo 1 || echo 0)"
verdict "(b) la même durée que vsm-render ($DR)" "$([ "$DE" != ABSENT ] && [ "$DE" = "$DR" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-lecture "$LIRE"
    course "$TEMOIN" t-export "800:exporter-audio:$brouillon/t-export.wav"
    read -r TT TE <<< "$(transport t-lecture)"
    TD="$(duree "$brouillon/t-export.wav")"
    echo "       témoin : Play — tête ${TT:-?} s, ${TE:-?} ; export $TD s"
    verdict "(b) témoin (D619) : arrêté à 0,000 ; l'export 2,0 s (l'hypothèse tranchée)" \
        "$([ "${TE:-}" = "arrêté" ] && [ "${TT:-}" = "0.000" ] && [ "$TD" = "2.000" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "CLIP-AUDIO-OUVERT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CLIP-AUDIO-OUVERT : un clip audio ouvert joue et s'exporte jusqu'au bout de son fichier, comme vsm-render le rend"
