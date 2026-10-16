#!/usr/bin/env bash
# D558.3 — LA TÊTE D'UNE ATTAQUE À L'AUTRE (Transport ▸ Tête : attaque suivante / précédente, Ctrl+Alt+N et Ctrl+Alt+B ;
# le *Locate Next / Previous Hitpoint* de Cubase), MESURÉ PAR LE JOURNAL DE LA TÊTE.
#
# LES RÈGLES GARDÉES. Le fichier de D543.3 et D544.1 : quatre clics à 0,260 · 0,490 · 0,770 · 1,010 s dans deux secondes
# de silence, importé à la tête (120 BPM : les attaques sonnent aux ticks 250 · 470 · 739 · 970, ±1 — ce que le banc
# de D544.1 mesure au .mid).
#   (1) suivante, cinq fois depuis 0 : 250, 470, 739, 970, puis « aucune après la tête », qui reste à 970 ;
#   (2) précédente, deux fois depuis 1 920 : 970, 739 ; et depuis 0 : « aucune avant » ;
#   (3) le clip qui BOUCLE (sa fenêtre de deux secondes jouée deux fois) : depuis 960, 970 puis 2 170 et 2 390 — les
#       attaques du second tour, que D544.1 ne place pas ;
#   (4) une piste MIDI : « n'est pas une piste audio », la tête ne bouge pas.
#
#   tools/tete-attaque.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-attaque.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
echantillons = [0.0] * (2 * sr)
for instant in (0.260, 0.490, 0.770, 1.010):
    debut = round(instant * sr)
    for n in range(int(0.010 * sr)):
        echantillons[debut + n] += 0.8 * math.sin(2 * math.pi * 3000 * n / sr) * math.exp(-n / (0.0012 * sr))
trames = b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in echantillons)
with wave.open(f"{b}/clics.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
os.makedirs(f"{b}/base/midi", exist_ok=True)
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": transport, "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
# Une piste MIDI seule, pour (4).
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/midi/midi", exist_ok=True)
open(f"{b}/midi/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": transport,
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/midi/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# Les destinations successives de la tête, dans l'ordre (« aucune » pour un geste qui ne trouve rien).
sauts() {
    grep -h "^VSM_TETE_ATTAQUE : " "$brouillon/$1.txt" \
        | sed -n -e 's/^VSM_TETE_ATTAQUE : [0-9-]* -> \([0-9]*\) ticks.*/\1/p' -e 's/^VSM_TETE_ATTAQUE : aucune.*/aucune/p' \
        | tr '\n' ' ' | sed 's/ $//'
}
# Vrai si chaque terme de $1 vaut celui de $2 à ±1 (« aucune » doit y être tel quel).
proches() {
    python3 -c "
import sys
a, b = sys.argv[1].split(), sys.argv[2].split()
ok = len(a) == len(b) and all(x == y if 'aucune' in (x, y) else abs(int(x) - int(y)) <= 1 for x, y in zip(a, b))
sys.exit(0 if ok else 1)" "$1" "$2"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
N="touche:ctrl + alt + N"
B="touche:ctrl + alt + B"

echo "=== D558.3 : la tête d'une attaque à l'autre ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_VUE="sans-rapport" VSM_IMPORT_AUDIO="$brouillon/clics.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
cp -r "$brouillon/base" "$brouillon/boucle"
python3 - "$brouillon/boucle/project.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
pistes = [t for t in d["tracks"] if t.get("clips")]
c = pistes[0]["clips"][0]
fenetre = c.get("sourceLength") or c["length"]
c["sourceLength"] = fenetre          # la fenêtre : le clip tel qu'importé
c["length"] = 2 * fenetre            # joué deux fois (D5.2)
json.dump(d, open(sys.argv[1], "w", encoding="utf-8"), indent=1)
print(f"       clip bouclé : fenêtre {fenetre} ticks, jouée sur {2 * fenetre}")
PY
# LA PISTE AUDIO EST LA SECONDE : le projet vide reçoit à l'ouverture une piste MIDI sans nom (rang 0), et l'import pose
# la piste audio après elle — le premier passage de ce banc visait la première, et lisait « «  » n'est pas une piste audio ».
VUE="sans-rapport,arrangement,piste:1"
course suivante "$brouillon/base" VSM_DELAI=3200 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1000:$N;1300:$N;1600:$N;1900:$N;2200:$N"
course precedente "$brouillon/base" VSM_DELAI=2600 VSM_VUE="$VUE,tete-temps:4" VSM_GESTE_APRES="1000:$B;1400:$B"
course depuis-zero "$brouillon/base" VSM_DELAI=2000 VSM_VUE="$VUE" VSM_GESTE_APRES="1000:$B;1400:relever-tete"
course boucle "$brouillon/boucle" VSM_DELAI=2800 VSM_VUE="$VUE,tete-temps:2" \
    VSM_GESTE_APRES="1000:menu:Transport > Tête : attaque suivante;1400:$N;1800:$N"
course midi "$brouillon/midi" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement,piste:0" VSM_GESTE_APRES="1000:$N;1400:relever-tete"

s="$(sauts suivante)"; p="$(sauts precedente)"; z="$(sauts depuis-zero)"; l="$(sauts boucle)"
grep -h "^VSM_TETE_ATTAQUE" "$brouillon/suivante.txt" | head -1 | sed 's/^/       /'
echo "       suivante ×5 : [$s] ; précédente ×2 depuis 1 920 : [$p] ; précédente depuis 0 : [$z]"
echo "       clip bouclé, depuis 960 : [$l]"
grep -h "^VSM_TETE_ATTAQUE" "$brouillon/midi.txt" | sed 's/^/       /'

verdict "(1) suivante : 250, 470, 739, 970, puis aucune — et la tête reste" \
    "$(proches "$s" "250 470 739 970 aucune" && grep -q "aucune après la tête, qui reste à $(echo "$s" | cut -d' ' -f4) " "$brouillon/suivante.txt" && echo 1 || echo 0)"
verdict "(2) précédente : 970, 739 depuis 1 920 ; aucune depuis 0, la tête à 0" \
    "$(proches "$p" "970 739" && [ "$z" = aucune ] && grep -q "^VSM_TETE : tick 0" "$brouillon/depuis-zero.txt" && echo 1 || echo 0)"
verdict "(3) le clip qui boucle : 970, puis 2 170 et 2 390, au second tour" \
    "$(proches "$l" "970 2170 2390" && echo 1 || echo 0)"
verdict "(4) une piste MIDI : dit, la tête ne bouge pas" \
    "$(grep -q "^VSM_TETE_ATTAQUE : « Partie » n'est pas une piste audio" "$brouillon/midi.txt" && grep -q "^VSM_TETE : tick 0" "$brouillon/midi.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-ATTAQUE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-ATTAQUE : la tête se pose d'une attaque à l'autre, à chaque tour d'une boucle"
