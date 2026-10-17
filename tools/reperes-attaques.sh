#!/usr/bin/env bash
# D560.4 — POSER DES REPÈRES AUX ATTAQUES (clic droit sur un clip audio ▸ Poser des repères aux attaques ; le *Create
# Markers from Hitpoints* de Cubase), MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Le fichier de D543.3 et D558.3 : quatre clics à 0,260 · 0,490 · 0,770 · 1,010 s dans deux secondes
# de silence, importé (120 BPM : ils sonnent aux ticks 250 · 470 · 739 · 970, ±1). Deux repères d'avant : « Repère 1 »
# à 470 (sur une attaque) et « Repère 2 » à 3 000 (ailleurs).
#   (1) posés : trois repères neufs à 250 · 739 · 970, nommés « Repère 3 · 4 · 5 » (les premiers numéros LIBRES) ; celui
#       de 470 sauté et compté ; dit ;
#   (2) rejoué sur le projet qui les porte : rien de posé, et la boîte le dit (« Chaque attaque porte déjà un repère ») ;
#   (3) le clip qui BOUCLE (sa fenêtre jouée deux fois) : huit repères, ceux du second tour compris (2 170 · 2 390 ·
#       2 659 · 2 890) ;
#   (4) un clip MUET choisi compte (le choix est explicite) ; un clip À L'ENVERS est écarté, compté, et la boîte le dit ;
#   (5) un pas : Ctrl+Z rend les deux repères d'avant, après un geste qui a eu lieu.
#
#   tools/reperes-attaques.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-reperes-attaques.XXXXXX")"
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
with wave.open(f"{b}/clics.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in echantillons))
os.makedirs(f"{b}/base/midi", exist_ok=True)
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
reperes() {   # « tick=nom » des repères d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{m['tick']}={m['name'].replace(' ', '_')}\" for m in d.get('markers', [])) or 'AUCUN')" "$1/project.json"
}
# Vrai si les repères $1 valent « tick=nom » de $2, les ticks à ±1.
memes() {
    python3 -c "
import sys
a, b = sys.argv[1].split(), sys.argv[2].split()
def lu(x):
    t, n = x.split('=', 1); return int(t), n
try:
    ok = len(a) == len(b) and all(abs(lu(x)[0] - lu(y)[0]) <= 1 and lu(x)[1] == lu(y)[1] for x, y in zip(a, b))
except ValueError:
    ok = False
sys.exit(0 if ok else 1)" "$1" "$2"
}
modifier() {   # $1 = dossier ; $2 = code Python sur d (le projet) et c (le clip audio)
    python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])][0]
exec(sys.argv[2])
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$1" "$2"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="clip-audio:Poser des repères aux attaques"

echo "=== D560.4 : poser des repères aux attaques ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/clics.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in boucle muet envers; do cp -r "$brouillon/base" "$brouillon/$x"; done
modifier "$brouillon/base" "d['markers'] = [{'tick': 470, 'name': 'Repère 1'}, {'tick': 3000, 'name': 'Repère 2'}]"
modifier "$brouillon/boucle" "f = c.get('sourceLength') or c['length']; c['sourceLength'] = f; c['length'] = 2 * f"
modifier "$brouillon/muet" "c['muted'] = True"
modifier "$brouillon/envers" "c['reversed'] = True"
for x in p z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course poser "$brouillon/p" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/p"
cp -r "$brouillon/p" "$brouillon/p2"
course rejouer "$brouillon/p2" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/p2"
course boucle "$brouillon/boucle" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/boucle"
course muet "$brouillon/muet" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/muet"
course envers "$brouillon/envers" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/envers"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/z"

rb="$(reperes "$brouillon/base")"; rp="$(reperes "$brouillon/p")"; rp2="$(reperes "$brouillon/p2")"
rl="$(reperes "$brouillon/boucle")"; rm_="$(reperes "$brouillon/muet")"; re="$(reperes "$brouillon/envers")"; rz="$(reperes "$brouillon/z")"
for x in poser rejouer boucle muet envers; do grep -h "^VSM_REPERES_ATTAQUES\|^VSM_BOITE : Poser des repères" "$brouillon/$x.txt" | sed "s/^/       ($x) /"; done
echo "       d'avant : [$rb]"
echo "       posés : [$rp] ; rejoués : [$rp2]"
echo "       boucle : [$rl]"
echo "       muet : [$rm_] ; à l'envers : [$re] ; Ctrl+Z : [$rz]"

verdict "(1) posés : 250 · 739 · 970, « Repère 3 · 4 · 5 », celui de 470 sauté et compté" \
    "$(memes "$rp" "250=Repère_3 470=Repère_1 739=Repère_4 970=Repère_5 3000=Repère_2" \
        && grep -q "^VSM_REPERES_ATTAQUES : 3 rep.re(s) pos.(s) \[.*\], 1 d.j. l." "$brouillon/poser.txt" && echo 1 || echo 0)"
verdict "(2) rejoué : rien de posé, la boîte le dit" \
    "$([ "$rp2" = "$rp" ] && grep -q "^VSM_REPERES_ATTAQUES : 0 rep.re(s) pos.(s) \[\], 4 d.j. l." "$brouillon/rejouer.txt" \
        && grep -q "^VSM_BOITE : Poser des repères aux attaques : Chaque attaque porte déjà un repère (4)" "$brouillon/rejouer.txt" && echo 1 || echo 0)"
verdict "(3) le clip qui boucle : huit repères, le second tour compris" \
    "$(memes "$rl" "250=Repère_1 470=Repère_2 739=Repère_3 970=Repère_4 2170=Repère_5 2390=Repère_6 2659=Repère_7 2890=Repère_8" && echo 1 || echo 0)"
verdict "(4) un clip muet compte ; à l'envers : écarté, compté, dit" \
    "$(memes "$rm_" "250=Repère_1 470=Repère_2 739=Repère_3 970=Repère_4" && [ "$re" = AUCUN ] \
        && grep -q "^VSM_REPERES_ATTAQUES : 0 .*, 1 . l'envers .cart.(s)" "$brouillon/envers.txt" \
        && grep -q "^VSM_BOITE : Poser des repères aux attaques : .*1 clip(s) à l'envers écarté(s)" "$brouillon/envers.txt" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend les deux repères d'avant, après un geste qui a eu lieu" \
    "$([ "$rz" = "$rb" ] && [ "$rb" = "470=Repère_1 3000=Repère_2" ] && grep -q "^VSM_REPERES_ATTAQUES : 3 " "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REPERES-ATTAQUES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REPERES-ATTAQUES : un repère naît à chaque attaque des clips choisis, à chaque tour d'une boucle"
