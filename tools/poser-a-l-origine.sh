#!/usr/bin/env bash
# D628 — POSER UN FICHIER DE LA RÉSERVE À SON ORIGINE (le *Pool ▸ Insert into Project ▸ At Origin* de Cubase), mesuré dans
# l'application : la tête posée sur un repère (`repere-aller:`), une ligne de la réserve choisie (`reserve-choisir:`),
# « Poser à l'origine » pressé (`cliquer:`, le vrai bouton) ; le projet enregistré, l'historique et le relevé de la ligne.
#
# LE JEU : un projet à 120 BPM, 480 ticks à la noire ; « Voix » joue `audio/voix.wav` ; un repère au tick 1 920 (2 s) ; trois
# fichiers que rien ne désigne — `audio/bwf.wav` (44,1 kHz, bloc `bext`, référence 132 300 = 3 s), `audio/bwf48.wav` (48 kHz,
# référence 144 000 = 3 s), `audio/sans.wav` (44,1 kHz, sans bloc `bext`).
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D628), la tête au repère :
#   (a) bwf.wav : « Poser à l'origine » actif, « origine 3.000 s » ; pressé → une piste « bwf » sous « Voix », qui désigne
#       audio/bwf.wav, son clip au tick 2 880 (pas à la tête, 1 920) ; aucun fichier neuf ; 1 pas ;
#   (b) bwf48.wav : au tick 2 880 aussi (la fréquence du fichier, pas celle du moteur) ;
#   (c) sans.wav : « Poser à l'origine » grisé, « sans origine » ; « Poser à la tête » actif ;
#   (d) le témoin de la règle : bwf.wav par « Poser à la tête » → au tick 1 920 ;
#   (e) Ctrl+Z après (a) → la piste retirée ;
#   (f) le témoin (binaire d'avant D628) : pas de bouton, aucune piste ; l'anglais (« Insert at origin »).
#
#   tools/poser-a-l-origine.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D625/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-poser-origine.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys
racine = sys.argv[1]
def mtrk(corps):
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def son(chemin, sr, secondes, reference=None):
    """Un WAV mono 16 bits ; `reference` : un bloc `bext` (BWF) dont la TimeReference vaut ce compte d'échantillons."""
    data = b"".join(struct.pack("<h", int(3000 * math.sin(2 * math.pi * 220 * n / sr))) for n in range(int(secondes * sr)))
    corps = b"fmt " + struct.pack("<I", 16) + struct.pack("<HHIIHH", 1, 1, sr, sr * 2, 2, 16)
    if reference is not None:
        bext = bytearray(602)   # description 256, originator 32, originatorRef 32, date 10, heure 8 : 338 octets
        bext[338:346] = struct.pack("<II", reference & 0xFFFFFFFF, reference >> 32); bext[346:348] = struct.pack("<H", 1)
        corps += b"bext" + struct.pack("<I", len(bext)) + bytes(bext)
    corps += b"data" + struct.pack("<I", len(data)) + data
    open(chemin, "wb").write(b"RIFF" + struct.pack("<I", 4 + len(corps)) + b"WAVE" + corps)
b = f"{racine}/projet"; os.makedirs(f"{b}/midi", exist_ok=True); os.makedirs(f"{b}/audio", exist_ok=True)
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + mtrk(b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"))
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "origine", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 1920, "name": "Refrain"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voix", "mix": mix,
                       "audio": {"file": "audio/voix.wav", "sampleRate": 48000, "frames": 48000, "channels": 1}}]},
          open(f"{b}/project.json", "w"), indent=1)
son(f"{b}/audio/voix.wav", 48000, 1.0)
son(f"{b}/audio/bwf.wav", 44100, 1.0, reference=132300)
son(f"{b}/audio/bwf48.wav", 48000, 1.0, reference=144000)
son(f"{b}/audio/sans.wav", 44100, 1.0)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
rejouees=0
course_sure() {   # la course rejouée une fois si sa fenêtre est née dégénérée (D613), et dit
    course "$@"
    if grep -q "fenêtre 1[0-9][0-9]x1[0-9][0-9]$" "$brouillon/$2.txt"; then
        echo "       course « $2 » REJOUÉE : $(grep -o "fenêtre [0-9]*x[0-9]*" "$brouillon/$2.txt" | head -1) au premier essai"
        rejouees=$((rejouees + 1))
        course "$@"
    fi
}
pas() { grep "^VSM_HISTORIQUE_PAS" "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_HISTORIQUE_PAS : //'; }
piste_de() {   # « nom ; index ; début du premier clip ; nombre de pistes » de la piste (autre que « Voix ») qui désigne $2
    python3 - "$1/project.json" "$2" <<'PY2'
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except OSError:
    print("ABSENT"); sys.exit(0)
pistes = p.get("tracks", [])
for i, t in enumerate(pistes):
    if t.get("audio", {}).get("file") == sys.argv[2] and t.get("name") != "Voix":
        clips = t.get("clips", [])
        print(f"{t.get('name')} ; {i} ; {clips[0].get('start') if clips else '-'} ; {len(pistes)}"); sys.exit(0)
print(f"AUCUNE ; - ; - ; {len(pistes)}")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

F="Réserve des fichiers"
B="$brouillon"
ORIGINE="800:repere-aller:0;900:relever-tete;1100:reserve-choisir:audio/bwf.wav;1300:cliquer:fenetre:$F:Poser à l'origine"
echo "=== D628 : poser un fichier de la réserve à son origine ==="
AVANT="$(ls "$B/projet/audio" | tr '\n' ' ')"
course_sure "$BIN" pose "$ORIGINE;1700:relever-historique;2000:enregistrer:$B/pose-projet"
course_sure "$BIN" p48 "800:repere-aller:0;1100:reserve-choisir:audio/bwf48.wav;1300:cliquer:fenetre:$F:Poser à l'origine;1800:enregistrer:$B/p48-projet"
course_sure "$BIN" sans "800:repere-aller:0;1100:reserve-choisir:audio/sans.wav;1500:relever-historique"
course_sure "$BIN" tete "800:repere-aller:0;1100:reserve-choisir:audio/bwf.wav;1300:cliquer:fenetre:$F:Poser à la tête;1800:enregistrer:$B/tete-projet"
course_sure "$BIN" annule "$ORIGINE;1700:touche:ctrl + Z;2000:enregistrer:$B/annule-projet"
course_sure "$BIN" en "800:repere-aller:0;1100:reserve-choisir:audio/bwf.wav;1300:cliquer:fenetre:Pool:Insert at origin;1800:enregistrer:$B/en-projet" VSM_LANGUE=en

T="$(grep "^VSM_TETE" "$B/pose.txt" | head -1)"; C="$(grep "^VSM_RESERVE : choisie" "$B/pose.txt" | head -1)"
P="$(grep "^VSM_RESERVE : posé" "$B/pose.txt" | head -1)"; PJ="$(piste_de "$B/pose-projet" audio/bwf.wav)"
APRES="$(ls "$B/p-pose/audio" | tr '\n' ' ')"
echo "       $T"; echo "       $C"; echo "       $P"; echo "       projet enregistré : $PJ ; pas : $(pas pose)"
echo "       audio/ avant : $AVANT ; après : $APRES"
verdict "(a) la tête au repère (1920) ; bwf.wav choisi : « À l'origine actif (origine 3.000 s) »" \
    "$(echo "$T" | grep -q "^VSM_TETE : tick 1920," && echo "$C" | grep -q "À l'origine actif (origine 3.000 s)" && echo 1 || echo 0)"
verdict "(a) une piste « bwf » sous « Voix » (index 1), qui désigne audio/bwf.wav, son clip au tick 2880" \
    "$([ "$PJ" = "bwf ; 1 ; 2880 ; 2" ] && echo 1 || echo 0)"
verdict "(a) aucun fichier neuf dans audio/ ; 1 pas : « Poser un fichier de la réserve »" \
    "$([ "$AVANT" = "$APRES" ] && [ "$(pas pose)" = "1 : Poser un fichier de la réserve" ] && echo 1 || echo 0)"
P48="$(piste_de "$B/p48-projet" audio/bwf48.wav)"; C48="$(grep "^VSM_RESERVE : choisie" "$B/p48.txt" | head -1)"
echo "       48 kHz : $C48 ; $P48"
verdict "(b) bwf48.wav (48 kHz, 144 000) : au tick 2880 aussi" "$([ "$P48" = "bwf48 ; 1 ; 2880 ; 2" ] && echo 1 || echo 0)"
CS="$(grep "^VSM_RESERVE : choisie" "$B/sans.txt" | head -1)"
echo "       sans bloc bext : $CS ; pas : $(pas sans)"
verdict "(c) sans.wav : « Poser actif ; À l'origine grisé (sans origine) », aucun pas" \
    "$(echo "$CS" | grep -q "Poser actif ; À l'origine grisé (sans origine)" && [ "$(pas sans)" = "0 : " ] && echo 1 || echo 0)"
PT="$(piste_de "$B/tete-projet" audio/bwf.wav)"
echo "       à la tête : $PT"
verdict "(d) le témoin de la règle : bwf.wav par « Poser à la tête » → au tick 1920" "$([ "$PT" = "bwf ; 1 ; 1920 ; 2" ] && echo 1 || echo 0)"
PA="$(piste_de "$B/annule-projet" audio/bwf.wav)"
echo "       Ctrl+Z : $PA"
verdict "(e) Ctrl+Z après la pose : la piste retirée" "$([ "$PA" = "AUCUNE ; - ; - ; 1" ] && echo 1 || echo 0)"
PE="$(piste_de "$B/en-projet" audio/bwf.wav)"
echo "       anglais : $PE"
verdict "(f) en anglais : « Insert at origin » pressé dans « Pool » → au tick 2880" "$([ "$PE" = "bwf ; 1 ; 2880 ; 2" ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-pose "$ORIGINE;2000:enregistrer:$B/t-pose-projet"
    tc="$(grep -c "Poser à l'origine — cliqué" "$B/t-pose.txt")"; TP="$(piste_de "$B/t-pose-projet" audio/bwf.wav)"
    echo "       témoin : $tc clic(s) sur « Poser à l'origine » ; $TP"
    verdict "(f) témoin (avant D628) : pas de bouton, aucune piste" "$([ "$tc" = 0 ] && [ "$TP" = "AUCUNE ; - ; - ; 1" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "POSER-A-L-ORIGINE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "POSER-A-L-ORIGINE : un fichier de la réserve revient à l'instant qu'il porte, par la carte de tempo"
