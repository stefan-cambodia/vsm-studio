#!/usr/bin/env bash
# D630 — POSER PLUSIEURS FICHIERS DE LA RÉSERVE D'UN GESTE (la sélection multiple du Pool de Cubase et *Insert into
# Project*), mesuré dans l'application : deux lignes choisies (`reserve-choisir:` puis `reserve-choisir-aussi:`, un
# Ctrl+clic par la même sélection de liste), « Poser à la tête » ou « Poser à l'origine » pressé (`cliquer:`) ; le projet
# enregistré, l'historique.
#
# LE JEU : celui de `poser-a-l-origine.sh` — « Voix » ; `audio/bwf.wav` et `audio/bwf48.wav` à l'origine 3 s ;
# `audio/sans.wav` sans origine ; la tête au repère 1 920.
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D630) :
#   (a) bwf.wav puis bwf48.wav : « choisies 2 ; Relier grisé ; Poser actif ; À l'origine actif » ; « Poser à la tête » →
#       « bwf » index 1 et « bwf48 » index 2, à 1 920 ; 1 pas « Poser des fichiers de la réserve » ; Ctrl+Z → retirées ;
#   (b) les mêmes, « Poser à l'origine » → les deux à 2 880 ;
#   (c) bwf.wav et sans.wav : « Poser actif ; À l'origine grisé » ; « Poser à l'origine » pressé : aucun pas, aucune piste ;
#   (e) le témoin (le binaire de D629) : le verbe inconnu, une ligne choisie, une piste posée ;
#   (f) l'anglais : « Insert at playhead » dans « Pool », deux pistes.
#
#   tools/poser-plusieurs.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D629/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-poser-plusieurs.XXXXXX")"
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
    case "$nom" in t-*|mixte) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
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
DEUX="800:repere-aller:0;1000:reserve-choisir:audio/bwf.wav;1150:reserve-choisir-aussi:audio/bwf48.wav"
echo "=== D630 : poser plusieurs fichiers de la réserve d'un geste ==="
course_sure "$BIN" tete2 "$DEUX;1400:cliquer:fenetre:$F:Poser à la tête;1800:relever-historique;2100:enregistrer:$B/tete2-projet"
course_sure "$BIN" origine2 "$DEUX;1400:cliquer:fenetre:$F:Poser à l'origine;1800:relever-historique;2100:enregistrer:$B/origine2-projet"
course_sure "$BIN" annule2 "$DEUX;1400:cliquer:fenetre:$F:Poser à la tête;1800:touche:ctrl + Z;2100:enregistrer:$B/annule2-projet"
course_sure "$BIN" mixte "800:repere-aller:0;1000:reserve-choisir:audio/bwf.wav;1150:reserve-choisir-aussi:audio/sans.wav;1400:cliquer:fenetre:$F:Poser à l'origine;1800:relever-historique;2100:enregistrer:$B/mixte-projet"
course_sure "$BIN" en "$DEUX;1400:cliquer:fenetre:Pool:Insert at playhead;1800:relever-historique;2100:enregistrer:$B/en-projet" VSM_LANGUE=en

C="$(grep "^VSM_RESERVE : choisies" "$B/tete2.txt" | tail -1)"
T1="$(piste_de "$B/tete2-projet" audio/bwf.wav)"; T2="$(piste_de "$B/tete2-projet" audio/bwf48.wav)"
echo "       $C"; grep "^VSM_RESERVE : posé" "$B/tete2.txt" | sed 's/^/       /'
echo "       à la tête : $T1 | $T2 ; pas : $(pas tete2)"
verdict "(a) « choisies 2 … ; Relier grisé ; Poser actif ; À l'origine actif »" \
    "$(echo "$C" | grep -q "^VSM_RESERVE : choisies 2 (audio/bwf48.wav) ; Relier grisé ; Poser actif ; À l'origine actif$" && echo 1 || echo 0)"
verdict "(a) « Poser à la tête » : bwf (index 1) et bwf48 (index 2), à 1920, 3 pistes" \
    "$([ "$T1" = "bwf ; 1 ; 1920 ; 3" ] && [ "$T2" = "bwf48 ; 2 ; 1920 ; 3" ] && echo 1 || echo 0)"
verdict "(a) 1 pas : « Poser des fichiers de la réserve »" "$([ "$(pas tete2)" = "1 : Poser des fichiers de la réserve" ] && echo 1 || echo 0)"
A1="$(piste_de "$B/annule2-projet" audio/bwf.wav)"; A2="$(piste_de "$B/annule2-projet" audio/bwf48.wav)"
echo "       Ctrl+Z : $A1 | $A2"
verdict "(a) Ctrl+Z : les deux pistes retirées d'un coup" "$([ "$A1" = "AUCUNE ; - ; - ; 1" ] && [ "$A2" = "AUCUNE ; - ; - ; 1" ] && echo 1 || echo 0)"
O1="$(piste_de "$B/origine2-projet" audio/bwf.wav)"; O2="$(piste_de "$B/origine2-projet" audio/bwf48.wav)"
echo "       à l'origine : $O1 | $O2 ; pas : $(pas origine2)"
verdict "(b) « Poser à l'origine » : les deux à 2880, 1 pas" \
    "$([ "$O1" = "bwf ; 1 ; 2880 ; 3" ] && [ "$O2" = "bwf48 ; 2 ; 2880 ; 3" ] && [ "$(pas origine2)" = "1 : Poser des fichiers de la réserve" ] && echo 1 || echo 0)"
CM="$(grep "^VSM_RESERVE : choisies" "$B/mixte.txt" | tail -1)"; M1="$(piste_de "$B/mixte-projet" audio/bwf.wav)"
echo "       mixte : $CM ; $M1 ; pas : $(pas mixte)"
verdict "(c) bwf.wav et sans.wav : « Poser actif ; À l'origine grisé » ; pressé : aucun pas, aucune piste" \
    "$(echo "$CM" | grep -q "Poser actif ; À l'origine grisé$" && grep -q "Poser à l'origine — GRISÉ" "$B/mixte.txt" \
       && [ "$(pas mixte)" = "0 : " ] && [ "$M1" = "AUCUNE ; - ; - ; 1" ] && echo 1 || echo 0)"
E1="$(piste_de "$B/en-projet" audio/bwf.wav)"; E2="$(piste_de "$B/en-projet" audio/bwf48.wav)"
echo "       anglais : $E1 | $E2 ; pas : $(pas en)"
verdict "(f) en anglais : « Insert at playhead » dans « Pool » → deux pistes à 1920" \
    "$([ "$E1" = "bwf ; 1 ; 1920 ; 3" ] && [ "$E2" = "bwf48 ; 2 ; 1920 ; 3" ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-tete2 "$DEUX;1400:cliquer:fenetre:$F:Poser à la tête;2100:enregistrer:$B/t-tete2-projet"
    tI="$(grep -c "geste inconnu\|inconnu" "$B/t-tete2.txt")"; X1="$(piste_de "$B/t-tete2-projet" audio/bwf.wav)"; X2="$(piste_de "$B/t-tete2-projet" audio/bwf48.wav)"
    echo "       témoin (D629) : $tI ligne(s) « inconnu » ; $X1 | $X2"
    verdict "(e) témoin (D629) : le verbe inconnu, une seule piste posée (bwf)" \
        "$([ "$tI" -ge 1 ] && [ "$X1" = "bwf ; 1 ; 1920 ; 2" ] && [ "$X2" = "AUCUNE ; - ; - ; 2" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "POSER-PLUSIEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "POSER-PLUSIEURS : plusieurs lignes de la réserve se posent d'un geste, chacune sur sa piste, en un pas"
