#!/usr/bin/env bash
# D545.1 — LA COURBE DE GAIN D'UN CLIP AUDIO, MESURÉE PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Un sinus de 1 kHz de 4 s, crête 0,5, importé sur une piste audio (120 BPM : une seconde
# fait deux temps). Les gestes passent par le menu du clip, « ici » étant la tête de lecture (VSM_POSITION).
#   (1) « Ajouter un point de gain ici » à 1 s (mes. 1 · 3) puis à 3 s (mes. 2 · 3), « Gain de ce point… » à
#       3 s : -inf. Le journal dit la courbe « 1.000:1.000 3.000:0.000 », et le projet relu la porte ;
#   (2) l'EXPORT, rapporté au TÉMOIN (l'export sans courbe, plat — 0,354 et non 0,5 : la loi de panoramique
#       de la piste, −3 dB) : par tranches de 100 ms, la crête vaut 1 × le témoin avant 1 s, 0,5 × (±0,04)
#       autour de 2 s, et le silence (sous −60 dBFS) après 3 s. Écrit d'abord en valeurs ABSOLUES (0,5 ;
#       0,25) : la première course a montré la bonne FORME à 0,354 près ;
#   (3) la courbe TIENT AU MATÉRIAU : le clip coupé à 2 s (« Couper à la tête de lecture ») rend le même
#       export que le clip entier, à 10⁻⁴ près, HORS de ±20 ms autour de la coupe ; sur la jonction, une
#       coupe change le son d'elle-même — le témoin, coupé SANS courbe, s'écarte de 0,31 de l'export d'avant
#       sur ±2 ms (le fondu de sécurité de D33.2, posé aux deux bords neufs) —, et la courbe y rend
#       exactement cet écart-là multiplié par son gain (0,5 à 2 s), à 5 % près ;
#   (4) un pas : un point posé puis Ctrl+Z → l'export d'avant au bit près, et le projet relu sans courbe ;
#   (5) la courbe se DESSINE : la photo de l'arrangement (regardée à la main) et la ligne `VSM_COURBE_GAIN`.
# Chaque fichier se lit AVANT de juger (deux .wav présents et non vides avant toute comparaison).
#
#   tools/courbe-de-gain.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-courbe-gain.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.wav "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = b"".join(struct.pack("<hh", v, v) for v in
                  (int(0.5 * 32767 * math.sin(2 * math.pi * 1000.0 * n / sr)) for n in range(4 * sr)))
with wave.open(f"{b}/la1k.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU_CONTEXTE|MENU|OPTIONS|EXPORT|IMPORT|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
geste() {   # $1 = nom ; $2 = projet de départ ; $3 = position ; $4 = entrée du menu du clip ; le reste : variables
    local nom="$1" depart="$2" position="$3" entree="$4"
    shift 4
    cp -r "$brouillon/$depart" "$brouillon/$nom"
    course "$nom" "$brouillon/$nom" VSM_DELAI=2500 VSM_POSITION="$position" \
        VSM_MENU_CONTEXTE="clip-audio-tous:$entree" "$@" VSM_GESTE_APRES="1500:enregistrer:$brouillon/$nom"
}
enveloppe() {   # la crête par tranches de 100 ms de 0,1 à 3,9 s, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
m = np.abs(x).max(axis=1)
print(" ".join(f"{m[int(t * sr):int((t + 0.1) * sr)].max():.4f}" for t in [k / 10 for k in range(1, 39)]))
PY2
}
jonction() {   # $1 témoin entier, $2 témoin coupé, $3 courbe entière, $4 courbe coupée : « hors_t hors_c dans_t dans_c »
    "$PY" - "$1" "$2" "$3" "$4" <<'PY4'
import sys
import numpy as np
import soundfile as sf
try:
    b, sr = sf.read(sys.argv[1], always_2d=True); t, _ = sf.read(sys.argv[2], always_2d=True)
    c, _ = sf.read(sys.argv[3], always_2d=True); k, _ = sf.read(sys.argv[4], always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if not (len(b) == len(t) == len(c) == len(k)) or len(b) == 0:
    print("LONGUEURS"); sys.exit(0)
dt, dc = np.abs(b - t).max(axis=1), np.abs(c - k).max(axis=1)
hors = np.ones(len(b), bool); hors[int(1.98 * sr):int(2.02 * sr)] = False
print(f"{dt[hors].max():.2e} {dc[hors].max():.2e} {dt[~hors].max():.4f} {dc[~hors].max():.4f}")
PY4
}
ecart() {   # le plus grand écart échantillon par échantillon entre deux .wav, ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY3'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(a) != len(b):
    print(f"LONGUEURS {len(a)} {len(b)}"); sys.exit(0)
print(f"{np.abs(a - b).max():.2e}")
PY3
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D545.1 : la courbe de gain d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la1k.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
geste p1 base 1.3 "Ajouter un point de gain ici"
geste p2 p1 2.3 "Ajouter un point de gain ici"
geste courbe p2 2.3 "Gain de ce point…" VSM_OPTIONS="gain=-inf"
geste coupe courbe 2.1 "Couper à la tête de lecture"
geste coupe-temoin base 2.1 "Couper à la tête de lecture"
cp -r "$brouillon/base" "$brouillon/annule"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_POSITION=1.3 \
    VSM_MENU_CONTEXTE="clip-audio-tous:Ajouter un point de gain ici" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
for x in base courbe coupe coupe-temoin annule; do
    course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"
done

grep -h "^VSM_COURBE_GAIN" "$brouillon/p1.txt" "$brouillon/p2.txt" "$brouillon/courbe.txt" | sed 's/^/       /'
courbe_projet="$(python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in p['tracks'] if t.get('kind') == 'audio' for c in t.get('clips', [])]
print(' '.join(f\"{q['seconds']:.3f}:{q['gain']:.3f}\" for q in c[0].get('gainEnvelope', [])) if c else 'PAS-DE-CLIP')
" "$brouillon/courbe/project.json")"
echo "       projet relu : [$courbe_projet]"
verdict "(1) deux points, le second à -inf : le journal et le projet relu disent « 1:1 3:0 »" \
    "$(grep -q "^VSM_COURBE_GAIN : « la1k » : 1.000:1.000 3.000:0.000$" "$brouillon/courbe.txt" \
        && [ "$courbe_projet" = "1.000:1.000 3.000:0.000" ] && echo 1 || echo 0)"

eb="$(enveloppe "$brouillon/base.wav")"; ec="$(enveloppe "$brouillon/courbe.wav")"
echo "       crêtes par 100 ms (0,1 → 3,9 s) : témoin [$eb]"
echo "                                          courbe [$ec]"
verdict "(2) l'export suit la courbe : 1 × le témoin avant 1 s, 0,5 × vers 2 s, le silence après 3 s ; le témoin plat" \
    "$(python3 -c "
import sys
try:
    b = [float(v) for v in sys.argv[1].split()]; c = [float(v) for v in sys.argv[2].split()]
except ValueError:
    sys.exit(1)
r = [x / y for x, y in zip(c, b)] if b and min(b) > 0 else []
ok = len(b) == len(c) == 38 and max(b) - min(b) <= 0.005 \
     and all(abs(v - 1.0) <= 0.02 for v in r[:8]) \
     and abs(r[19] - 0.5) <= 0.04 and all(v < 0.001 for v in c[30:])
sys.exit(0 if ok else 1)" "$eb" "$ec" && echo 1 || echo 0)"

jn="$(jonction "$brouillon/base.wav" "$brouillon/coupe-temoin.wav" "$brouillon/courbe.wav" "$brouillon/coupe.wav")"
echo "       coupe à 2 s (hors ±20 ms : témoin, courbe ; sur la jonction : témoin, courbe) : $jn"
verdict "(3) la courbe tient au matériau : coupé à 2 s, le même export hors de la jonction ; sur elle, l'écart de la coupe × 0,5" \
    "$(python3 -c "
import sys
try:
    ht, hc, jt, jc = (float(v) for v in sys.argv[1].split())
except ValueError:
    sys.exit(1)
sys.exit(0 if ht <= 1e-4 and hc <= 1e-4 and jt > 0 and abs(jc - 0.5 * jt) <= 0.05 * jt else 1)" "$jn" && echo 1 || echo 0)"

pa="$(stat -c %s "$brouillon/base.wav" 2>/dev/null || echo 0)"; pz="$(stat -c %s "$brouillon/annule.wav" 2>/dev/null || echo 0)"
identiques=0
[ "$pa" -gt 44 ] && [ "$pz" -gt 44 ] && cmp -s "$brouillon/base.wav" "$brouillon/annule.wav" && identiques=1
annule_projet="ABSENT"
[ -s "$brouillon/annule/project.json" ] && annule_projet="$(grep -c '"gainEnvelope"' "$brouillon/annule/project.json"; true)"
echo "       Ctrl+Z : exports identiques au bit $identiques ; « gainEnvelope » au projet relu : $annule_projet"
verdict "(4) un pas : Ctrl+Z rend l'export d'avant au bit près, et le projet sans courbe" \
    "$([ "$identiques" = 1 ] && [ "$annule_projet" = 0 ] && echo 1 || echo 0)"

# (5) LA PHOTO DE L'ARRANGEMENT — la vue par défaut montre le piano roll : la première forme de ce contrôle
# photographiait la mauvaise vue, et « une photo prise » ne disait rien de la courbe.
# Et le projet se rouvre défilé là où il a été enregistré (la tête à 3 s) : « Zoom : tout voir » d'abord.
course photo "$brouillon/courbe" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_MENU_CONTEXTE="clip-audio-tous:Zoom : tout voir"
verdict "(5) la courbe se dessine : la photo de l'ARRANGEMENT est prise (à regarder)" \
    "$([ -s "$brouillon/photo.png" ] && echo 1 || echo 0)"
echo "       photo : $brouillon/photo.png (VSM_GARDER pour la garder)"

echo
if [ "$rates" -gt 0 ]; then echo "COURBE-DE-GAIN : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COURBE-DE-GAIN : la courbe de gain s'entend dans l'export, tient au matériau, en un pas"
