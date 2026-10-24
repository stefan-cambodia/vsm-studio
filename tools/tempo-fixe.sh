#!/usr/bin/env bash
# D595 — LE TEMPO FIXE (Transport ▸ Tempo fixe (la carte de tempo ignorée), Transport ▸ Régler le tempo fixe… ; le
# bouton *Tempo Track / Fixed* de Cubase), MESURÉ PAR LA DURÉE DES RENDUS : `vsm-render` et l'export de l'application,
# chacun contre son témoin du MÊME code (la carte jouée).
#
# LE JEU : 480 ticks à la noire, 4/4 ; 120 BPM à 0, 60 BPM au tick 1 920 ; une piste MIDI, une note de 240 ticks sur
# chaque temps de 0 à 3 360 (la dernière finit au tick 3 600).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D595) :
#   (2) vsm-render, 48 kHz, queue de 2 s : 360 000 trames sur la carte (5,5 s + 2), 276 000 au tempo fixe de 120
#       (3,75 s + 2) — seul le champ `fixedTempo` change entre les deux projets ;
#   (3) l'application, la tête à 0 : « Tempo fixe » coché → project.json porte 120, et son export dure ce que dure celui
#       de vsm-render (à un bloc près, 512 trames) ; décoché → le champ absent, la carte intacte (deux changements) ;
#       CORRIGÉ APRÈS LA MESURE (25/10, 05 h 54), ET DIT : l'export de l'application ne finit pas où finit celui de
#       vsm-render — il s'arrête à la FIN DE LA MESURE (tick 3 840) plus 2 s, à 44,1 kHz (352 800 trames sur la carte,
#       8,0 s, le binaire de D591 compris), quand vsm-render s'arrête à la fin de la dernière note : l'attendu comparait
#       deux règles de fin. Le contrôle juge désormais ce que le tempo fixe CHANGE dans l'export de l'application : la
#       fin de mesure passe de 6,0 s (carte) à 4,0 s (fixe à 120) — l'export raccourcit de 2,0 s, à un bloc près ;
#       Ctrl+Z → absent ; « Régler le tempo fixe… » à 90 → 90 ; la tête au tick 2 400 (mes. 2, temps 2) → 60 ;
#   (4) en anglais, Transport > Fixed tempo (tempo track ignored).
#
#   tools/tempo-fixe.sh [binaire de l'application] [vsm-render]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
RENDU="${2:-./build/tools/vsm-render}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent — compiler vsm-render d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (soundfile) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tempo-fixe.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import copy, json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = []
for debut in range(0, 3361, 480):
    evs.append((debut, 1, bytes([0x90, 60, 100])))
    evs.append((debut + 240, 0, bytes([0x80, 60, 0])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets; t = tick
corps += b"\x00\xff\x2f\x00"
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
projet = {"format": "vsm-project", "version": 1, "title": "tempo", "midi": {"file": "midi/arrangement.mid"},
          "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}, {"bpm": 60.0, "tick": 1920}],
                        "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                      "instrument": {"preferredPlugin": "vsm.minimoog"}}]}
for nom, fixe in (("base", None), ("r-fixe", 120.0)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    p = copy.deepcopy(projet)
    if fixe: p["transport"]["fixedTempo"] = {"bpm": fixe}
    json.dump(p, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" n
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    local motif="VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT|POSITION|OPTIONS|TEMPO_FIXE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
fixe() {   # le tempo fixe d'un projet enregistré (« absent » s'il n'y est pas) et le nombre de changements de la carte
    python3 -c "
import json, sys
try:
    t = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))['transport']
except Exception:
    print('ABSENT'); sys.exit(0)
f = t.get('fixedTempo')
print(('%g' % f['bpm'] if f else 'absent') + '/' + str(len(t.get('tempoChanges', []))))" "$1"
}
trames() { "$PY" -c "
import sys
import soundfile as sf
try:
    print(sf.info(sys.argv[1]).frames)
except Exception:
    print('ABSENT')" "$1"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D595 : le tempo fixe ==="
# (2) LE RENDU HORS-LIGNE : deux projets qui ne diffèrent QUE par `fixedTempo`.
for x in base r-fixe; do "$RENDU" "$brouillon/$x" "$brouillon/$x.wav" --sample-rate 48000 --quiet > "$brouillon/rendu-$x.txt" 2>&1; done
rc="$(trames "$brouillon/base.wav")"; rf="$(trames "$brouillon/r-fixe.wav")"
echo "       vsm-render : carte $rc trames, tempo fixe $rf trames"

# (3) L'APPLICATION.
for x in a-fixe a-carte a-off a-z a-regle a-tete; do cp -r "$brouillon/base" "$brouillon/$x"; done
T="menu:Transport > Tempo fixe (la carte de tempo ignorée)"
course a-fixe "$brouillon/a-fixe" VSM_DELAI=3500 VSM_GESTE_APRES="900:$T;1300:enregistrer:$brouillon/a-fixe;1700:exporter-audio:$brouillon/a-fixe.wav"
course a-carte "$brouillon/a-carte" VSM_DELAI=3000 VSM_GESTE_APRES="1300:exporter-audio:$brouillon/a-carte.wav"
course a-off "$brouillon/a-off" VSM_DELAI=2500 VSM_GESTE_APRES="900:$T;1300:$T;1800:enregistrer:$brouillon/a-off"
course a-z "$brouillon/a-z" VSM_DELAI=2500 VSM_GESTE_APRES="900:$T;1300:touche:ctrl + Z;1800:enregistrer:$brouillon/a-z"
course a-regle "$brouillon/a-regle" VSM_DELAI=2500 VSM_OPTIONS="bpm=90" \
    VSM_GESTE_APRES="900:menu:Transport > Régler le tempo fixe…;1500:enregistrer:$brouillon/a-regle"
course a-tete "$brouillon/a-tete" VSM_DELAI=2500 VSM_POSITION=2.2 \
    VSM_GESTE_APRES="700:relever-tete;900:$T;1300:relever-tete;1700:enregistrer:$brouillon/a-tete"
course a-en "$brouillon/base" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="1000:lister-menus"

af="$(trames "$brouillon/a-fixe.wav")"; ac="$(trames "$brouillon/a-carte.wav")"
echo "       l'application : export carte $ac trames, tempo fixe $af trames"
echo "       enregistré : coché [$(fixe "$brouillon/a-fixe")] ; décoché [$(fixe "$brouillon/a-off")] ; Ctrl+Z [$(fixe "$brouillon/a-z")] ; réglé [$(fixe "$brouillon/a-regle")] ; tête à 2 400 [$(fixe "$brouillon/a-tete")]"
grep -h "^VSM_TEMPO_FIXE\|^VSM_TETE" "$brouillon"/a-fixe.txt "$brouillon"/a-tete.txt "$brouillon"/a-regle.txt | sed 's/^/       /'
en="$(grep -c "^VSM_MENU_LISTE : Transport > Fixed tempo (tempo track ignored)" "$brouillon/a-en.txt")"
echo "       en anglais : $en entrée(s)"

proche() { python3 -c "import sys; sys.exit(0 if abs(int(sys.argv[1]) - int(sys.argv[2])) <= int(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
verdict "(2) vsm-render : 360 000 trames sur la carte" "$([ "$rc" = 360000 ] && echo 1 || echo 0)"
verdict "(2) vsm-render : 276 000 trames au tempo fixe" "$([ "$rf" = 276000 ] && echo 1 || echo 0)"
verdict "(3) coché : 120 enregistré, la carte intacte" "$([ "$(fixe "$brouillon/a-fixe")" = "120/2" ] && echo 1 || echo 0)"
verdict "(3) l'export de l'application : 2,0 s plus court au tempo fixe, à un bloc près (corrigé après la mesure)" \
    "$("$PY" -c "
import sys
import soundfile as sf
try:
    c, f = sf.info(sys.argv[1]), sf.info(sys.argv[2])
    sys.exit(0 if c.samplerate == f.samplerate and abs((c.frames - f.frames) - 2 * c.samplerate) <= 512 else 1)
except Exception:
    sys.exit(1)" "$brouillon/a-carte.wav" "$brouillon/a-fixe.wav" && echo 1 || echo 0)"
# D589 : « absent » ne prouve rien sans l'allumage dans la même course — le témoin le rendait vert (25/10).
verdict "(3) décoché : absent, la carte intacte" \
    "$([ "$(fixe "$brouillon/a-off")" = "absent/2" ] && grep -q "^VSM_TEMPO_FIXE : allum" "$brouillon/a-off.txt" \
        && grep -q "^VSM_TEMPO_FIXE : .teint" "$brouillon/a-off.txt" && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : absent" "$([ "$(fixe "$brouillon/a-z")" = "absent/2" ] && grep -q "^VSM_TEMPO_FIXE : allum" "$brouillon/a-z.txt" && echo 1 || echo 0)"
verdict "(3) réglé à 90" "$([ "$(fixe "$brouillon/a-regle")" = "90/2" ] && echo 1 || echo 0)"
verdict "(3) la tête au tick 2 400 : 60" "$([ "$(fixe "$brouillon/a-tete")" = "60/2" ] && echo 1 || echo 0)"
verdict "(4) en anglais : Transport > Fixed tempo (tempo track ignored)" "$([ "$en" = 1 ] && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "TEMPO-FIXE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TEMPO-FIXE : la carte ignorée le temps qu'on l'allume, dans la lecture, l'export et le rendu, et rendue intacte"
