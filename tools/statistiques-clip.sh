#!/usr/bin/env bash
# D562.1 — LES STATISTIQUES D'UN CLIP AUDIO (clic droit ▸ Statistiques du clip… ; l'*Audio ▸ Statistics* de Cubase),
# MESURÉES CONTRE UN LECTEUR INDÉPENDANT (soundfile et numpy sur le même fichier).
#
# LES RÈGLES GARDÉES. Un fichier de deux secondes : la PREMIÈRE un la faible (0,1), la SECONDE un la fort (0,5) sur un
# décalage de +0,25 à gauche, et son opposé à droite, sans décalage. Le clip ne joue que la seconde (sa fenêtre), et son
# gain est à ×2.
#   (1) la boîte s'ouvre, et le journal dit les deux canaux ;
#   (2) crête, valeur efficace (dBFS) et moyenne de CHAQUE canal égales à celles que numpy lit sur la SECONDE du fichier
#       — à 0,05 dB et 0,001 : c'est la fenêtre jouée, pas le fichier ;
#   (3) le gain du clip n'y entre pas (×2 = +6 dB ne s'y voit pas), et la boîte le dit ;
#   (4) lecture seule : le titre reste sans « * ».
#
#   tools/statistiques-clip.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-statistiques.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = []
for n in range(2 * sr):
    s = math.sin(2 * math.pi * 440 * n / sr)
    g, d = (0.1 * s, -0.1 * s) if n < sr else (0.25 + 0.5 * s, -0.5 * s)
    trames.append(struct.pack("<hh", int(g * 32767), int(d * 32767)))
with wave.open(f"{b}/deux.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(trames))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
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

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D562.1 : les statistiques d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/deux.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
# LA FENÊTRE : la seconde seconde seulement (120 BPM : 960 ticks par seconde), au gain ×2.
python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
for t in d['tracks']:
    for c in t.get('clips', []):
        if t.get('audio'):   # le départ DANS LE FICHIER d'un clip audio est « sourceStartSeconds » (la première
                             # écriture de ce banc ne posait que « sourceStart », en ticks : le clip jouait la première seconde)
            c['start'] = 960; c['length'] = 960; c['sourceStart'] = 960; c['sourceLength'] = 960
            c['sourceStartSeconds'] = 1.0; c['gain'] = 2.0
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$brouillon/base"
course stats "$brouillon/base" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio:Statistiques du clip…"

ligne="$(grep -h "^VSM_STATISTIQUES" "$brouillon/stats.txt" | head -1)"
boite="$(grep -h "^VSM_BOITE : Statistiques du clip" "$brouillon/stats.txt" | head -1)"
titre="$(grep -h "^VSM_TITRE" "$brouillon/stats.txt" | head -1)"
echo "       $ligne"
echo "       ${boite:0:240}"
attendu="$("$PY" - "$brouillon/deux.wav" <<'PY2'
import sys
import numpy as np
import soundfile as sf
x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
z = x[sr:2 * sr]
db = lambda v: 20 * np.log10(max(v, 1e-12))
out = []
for c in range(2):
    out += [f"{db(np.abs(z[:, c]).max()):.2f}", f"{db(np.sqrt(np.mean(z[:, c] ** 2))):.2f}", f"{z[:, c].mean():.4f}"]
print(" ".join(out))
PY2
)"
lu="$(echo "$ligne" | sed -n 's/.*gauche \([^ ]*\) \([^ ]*\) \([^ ]*\) ; droite \([^ ]*\) \([^ ]*\) \([^ ]*\) (.*/\1 \2 \3 \4 \5 \6/p')"
echo "       numpy, la seconde seconde (crête efficace moyenne, g puis d) : [$attendu] ; l'application : [$lu]"

verdict "(1) la boîte s'ouvre, le journal dit les deux canaux" "$([ -n "$boite" ] && [ -n "$lu" ] && echo 1 || echo 0)"
verdict "(2) les six valeurs, celles de la fenêtre jouée (0,05 dB, 0,001)" \
    "$(python3 -c "
import sys
try:
    a = list(map(float, sys.argv[1].split())); b = list(map(float, sys.argv[2].split()))
except ValueError:
    sys.exit(1)
ok = len(a) == 6 == len(b) and all(abs(a[i] - b[i]) <= (0.001 if i % 3 == 2 else 0.05) for i in range(6))
sys.exit(0 if ok else 1)" "$attendu" "$lu" && echo 1 || echo 0)"
verdict "(3) le gain du clip n'y entre pas, et la boîte le dit" \
    "$(echo "$boite" | grep -q "Le gain du clip n'y entre pas" && echo 1 || echo 0)"
verdict "(4) lecture seule : pas d'astérisque au titre" "$([ -n "$titre" ] && ! echo "$titre" | grep -q ' \*$' && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "STATISTIQUES-CLIP : $rates contrôle(s) raté(s)"; exit 1; fi
echo "STATISTIQUES-CLIP : crête, valeur efficace et décalage de la fenêtre jouée, ceux qu'un lecteur indépendant lit"
