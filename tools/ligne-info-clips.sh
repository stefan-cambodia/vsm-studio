#!/usr/bin/env bash
# D598 — LA LIGNE D'INFORMATION DES CLIPS (Affichage ▸ Ligne d'information des clips ; l'*Info Line* de la fenêtre de
# projet de Cubase), MESURÉE PAR LE PROJET ENREGISTRÉ APRÈS UNE VALEUR TAPÉE (`saisir:`), le relevé `relever-info-clips`
# lu sur ce que la bande affiche.
#
# LE JEU (480 ticks à la noire, 4/4, 120 BPM) : la piste 0 MIDI, le clip A de 0 à 3 840 ; la piste 1 MIDI, le clip B de
# 1 920 à 3 840 ; la piste 2 AUDIO, le clip C de 0 à 1 920. Un clip se choisit par sa piste : « choisir:N », puis
# Édition ▸ Sélectionner dans l'arrangement ▸ Les clips des pistes choisies (N).
# L'ARRANGEMENT AU CENTRE (`VSM_VUE=…,arrangement`) : la bande ne se pose qu'au-dessus de lui ; sans lui, au premier
# essai, le menu cochait la ligne et le relevé disait « montrée non », sept contrôles rouges pour une vue oubliée.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D598, corrigé avant le code à 06 h 18) :
#   (1) éteinte d'origine : « montrée non » ;
#   (2) allumée, A choisi : « 1 clip(s) ; début 1.1 ; longueur 3840 ; gain — » (un clip MIDI n'a pas de gain) ;
#   (3) début « 3.1 » → A à 3 840, et Ctrl+Z → 0 (le relevé d'après la frappe lu dans la MÊME course) ;
#   (4) longueur « 1920 » → A long de 1 920 ;
#   (5) C choisi, gain « -6 » → le gain de C à 0,501 ;
#   (6) A et B choisis : « 2 clip(s) ; début 1.1 », puis début « 2.1 » → A à 1 920, B à 3 840 ;
#   (7) début « abc » → rien ne bouge, et le refus se dit ;
#   (8) en anglais View > Clip info line.
#
#   tools/ligne-info-clips.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-info-clips.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon/base" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
os.makedirs(f"{b}/midi", exist_ok=True); os.makedirs(f"{b}/audio", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(notes, tempo=False):
    evs = []
    for debut, h in notes:
        evs.append((debut, 1, bytes([0x90, h, 90]))); evs.append((debut + 240, 0, bytes([0x80, h, 0])))
    evs.sort(key=lambda e: (e[0], e[1]))
    corps, t = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b""), 0
    for tick, _, o in evs:
        corps += vlq(tick - t) + o; t = tick
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mid = (b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + piste([(t, 60) for t in range(0, 3840, 480)], tempo=True)
       + piste([(t, 64) for t in range(1920, 3840, 480)]) + piste([]))
open(f"{b}/midi/arrangement.mid", "wb").write(mid)
sr = 48000
with wave.open(f"{b}/audio/sons.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", x, x) for x in
                           (int(3000 * math.sin(2 * math.pi * 440 * n / sr)) for n in range(2 * sr))))
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
def clip(debut, longueur):
    return {"start": debut, "length": longueur, "sourceStart": debut, "sourceLength": longueur, "color": "#FF6B9BFF"}
c = clip(0, 1920); c["sourceStart"] = 0
json.dump({"format": "vsm-project", "version": 1, "title": "info-clips", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Lead", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip(0, 3840)]},
                      {"channel": 1, "color": "#FF6B9BFF", "effects": [], "name": "Basse", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip(1920, 1920)]},
                      {"kind": "audio", "channel": 2, "color": "#FF6B9BFF", "effects": [], "name": "Voix", "mix": mix,
                       "audio": {"file": "audio/sons.wav", "sampleRate": sr, "frames": 2 * sr, "channels": 2},
                       "clips": [c]}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; le reste : variables. Chaque course part d'une copie du jeu, un HOME neuf.
    local nom="$1" n
    shift
    cp -r "$brouillon/base" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    local motif="VSM_(GESTE_APRES|MENU|TOUCHE|SAISIE|GESTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « début+longueur×gain » de chaque clip, piste par piste, du projet enregistré d'une course, ou ABSENT
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join('%d+%d%s' % (c['start'], c['length'], ('x%.3f' % c['gain']) if 'gain' in c else '')
               for t in d['tracks'] for c in t.get('clips', [])))" "$1"
}
releve() { grep -h "^VSM_INFO_CLIPS : montr" "$brouillon/$1.txt" | sed -n "${2:-1}p"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

V="menu:Affichage > Ligne d'information des clips"
S1="menu:Édition > Sélectionner dans l'arrangement > Les clips des pistes choisies (1)"
S2="menu:Édition > Sélectionner dans l'arrangement > Les clips des pistes choisies (2)"
echo "=== D598 : la ligne d'information des clips ==="
course off VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-info-clips"
course info VSM_DELAI=2200 VSM_GESTE_APRES="500:$V;800:choisir:0;1100:$S1;1500:relever-info-clips"
course debut VSM_DELAI=2600 VSM_GESTE_APRES="500:$V;800:choisir:0;1100:$S1;1400:saisir:info-clips.debut=3.1;1700:relever-info-clips;2000:enregistrer:$brouillon/p-debut"
course z VSM_DELAI=3000 VSM_GESTE_APRES="500:$V;800:choisir:0;1100:$S1;1400:saisir:info-clips.debut=3.1;1700:relever-info-clips;2000:touche:ctrl + Z;2400:enregistrer:$brouillon/p-z"
course long VSM_DELAI=2600 VSM_GESTE_APRES="500:$V;800:choisir:0;1100:$S1;1400:saisir:info-clips.longueur=1920;1700:relever-info-clips;2000:enregistrer:$brouillon/p-long"
course gain VSM_DELAI=2600 VSM_GESTE_APRES="500:$V;800:choisir:2;1100:$S1;1400:saisir:info-clips.gain=-6;1700:relever-info-clips;2000:enregistrer:$brouillon/p-gain"
course deux VSM_DELAI=2800 VSM_GESTE_APRES="500:$V;800:choisir:0,1;1100:$S2;1400:relever-info-clips;1700:saisir:info-clips.debut=2.1;2000:relever-info-clips;2300:enregistrer:$brouillon/p-deux"
course abc VSM_DELAI=2600 VSM_GESTE_APRES="500:$V;800:choisir:0;1100:$S1;1400:saisir:info-clips.debut=abc;1700:relever-info-clips;2000:enregistrer:$brouillon/p-abc"
course anglais VSM_LANGUE=en VSM_DELAI=1500 VSM_GESTE_APRES="900:lister-menus"

echo "       éteinte : $(releve off)"
echo "       A choisi : $(releve info)"
for c in debut z long gain deux abc; do printf '       %-6s [%s]  %s\n' "$c" "$(clips "$brouillon/p-$c")" "$(releve $c)"; done
echo "       deux, avant la frappe : $(releve deux 1)"
grep -h "VSM_INFO_CLIPS : refus" "$brouillon/abc.txt" | head -1 | sed 's/^/       /'
en="$(grep -c "^VSM_MENU_LISTE : View > Clip info line" "$brouillon/anglais.txt")"
echo "       en anglais : $en entrée(s)"

dit() { releve "$1" "${3:-1}" | grep -qF -- "$2" && echo 1 || echo 0; }
verdict "(1) éteinte d'origine : « montrée non »" "$(dit off "montrée non")"
verdict "(2) A choisi : 1 clip(s) ; début 1.1 ; longueur 3840 ; gain —" "$(dit info "1 clip(s) ; début 1.1 ; longueur 3840 ; gain —")"
verdict "(3) début 3.1 : A à 3 840" "$([ "$(clips "$brouillon/p-debut")" = "3840+3840 1920+1920 0+1920" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : A à 0, après « début 3.1 » dans la même course" \
    "$([ "$(clips "$brouillon/p-z")" = "0+3840 1920+1920 0+1920" ] && [ "$(dit z "début 3.1")" = 1 ] && echo 1 || echo 0)"
verdict "(4) longueur 1920 : A long de 1 920" "$([ "$(clips "$brouillon/p-long")" = "0+1920 1920+1920 0+1920" ] && echo 1 || echo 0)"
verdict "(5) C : gain −6 dB → 0,501" "$([ "$(clips "$brouillon/p-gain")" = "0+3840 1920+1920 0+1920x0.501" ] && echo 1 || echo 0)"
verdict "(6) A et B : 2 clip(s) ; début 1.1, puis 2.1 → 1 920 et 3 840" \
    "$([ "$(dit deux "2 clip(s) ; début 1.1" 1)" = 1 ] && [ "$(clips "$brouillon/p-deux")" = "1920+3840 3840+1920 0+1920" ] && echo 1 || echo 0)"
verdict "(7) « abc » refusé et dit, rien ne bouge" \
    "$([ "$(clips "$brouillon/p-abc")" = "0+3840 1920+1920 0+1920" ] && grep -q "VSM_INFO_CLIPS : refus" "$brouillon/abc.txt" && echo 1 || echo 0)"
verdict "(8) en anglais : View > Clip info line" "$([ "$en" = 1 ] && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "LIGNE-INFO-CLIPS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LIGNE-INFO-CLIPS : les clips choisis se lisent et se règlent en tapant, et un refus se dit"
