#!/usr/bin/env bash
# D560.1 — LES CANAUX RECOMPTÉS À L'OUVERTURE : un projet écrit avant D559 déclare « channels: 2 » pour un fichier mono
# (l'import l'écrivait en dur), et « Gauche et droite échangées » s'y offrait sur un clip qui n'a rien à échanger.
#
# LES RÈGLES GARDÉES. Un fichier mono et un fichier stéréo, importés puis enregistrés ; le mono réécrit à « 2 », comme
# l'aurait fait l'import d'avant D559.
#   (1) ouvert : le journal le dit (`VSM_PROJET_CORRIGE`, « déclarait 2 … en a 1 ») et l'entrée est GRISÉE ;
#   (2) le projet ne s'ouvre PAS « modifié » — le titre sans « * » : ce n'est pas un geste de l'utilisateur ;
#   (3) réenregistré, il écrit « channels: 1 » ;
#   (4) le témoin : le stéréo déclaré 2 n'est pas touché (aucune ligne), son entrée reste active.
#
#   tools/canaux-recomptes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-recompte.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.5 * math.sin(2 * math.pi * 440 * n / sr) for n in range(sr)]
with wave.open(f"{b}/mono.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in la))
with wave.open(f"{b}/stereo.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in la))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
for nom in ("mono", "stereo"):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                       + b"MTrk" + struct.pack(">I", len(vide)) + vide)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": []}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
canaux() {   # les « channels » des pistes audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(str(t['audio'].get('channels')) for t in d['tracks'] if t.get('audio')) or 'AUCUNE')" "$1/project.json"
}
declarer_deux() {   # réécrit à 2 les canaux de la piste audio — ce qu'écrivait l'import d'avant D559
    python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
for t in d['tracks']:
    if t.get('audio'): t['audio']['channels'] = 2
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$1"
}
entree() {   # l'entrée « Gauche et droite échangées » telle que le menu du clip la liste
    grep -h "^VSM_MENU_CONTEXTE : clip-audio = " "$1" | head -1 | tr '|' '\n' | grep "Gauche et droite échangées" | sed 's/^ *//; s/ *$//'
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D560.1 : les canaux recomptés à l'ouverture ==="
course import-mono "$brouillon/mono" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/mono.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/mono"
course import-stereo "$brouillon/stereo" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/stereo.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/stereo"
importe="$(canaux "$brouillon/mono")"
declarer_deux "$brouillon/mono"
ancien="$(canaux "$brouillon/mono")"
cp -r "$brouillon/mono" "$brouillon/mono-r"
course ouvrir-mono "$brouillon/mono" VSM_DELAI=2000 VSM_MENU_CONTEXTE="clip-audio:?"
course reenregistrer "$brouillon/mono-r" VSM_DELAI=2000 VSM_GESTE_APRES="1000:enregistrer:$brouillon/mono-r"
course ouvrir-stereo "$brouillon/stereo" VSM_DELAI=2000 VSM_MENU_CONTEXTE="clip-audio:?"

grep -h "^VSM_PROJET_CORRIGE" "$brouillon/ouvrir-mono.txt" "$brouillon/ouvrir-stereo.txt" | sed 's/^/       /'
em="$(entree "$brouillon/ouvrir-mono.txt")"; es="$(entree "$brouillon/ouvrir-stereo.txt")"
titre="$(grep -h "^VSM_TITRE" "$brouillon/ouvrir-mono.txt" | head -1)"
apres="$(canaux "$brouillon/mono-r")"
echo "       importé : [$importe] ; réécrit « à l'ancienne » : [$ancien] ; réenregistré : [$apres]"
echo "       mono : [$em] ; stéréo : [$es] ; $titre"

verdict "(1) ouvert : dit au journal, l'entrée grisée" \
    "$(grep -q "^VSM_PROJET_CORRIGE : .*d.clarait 2 canal(aux), le fichier en a 1" "$brouillon/ouvrir-mono.txt" \
        && echo "$em" | grep -q "\[grisee\]" && [ "$ancien" = 2 ] && echo 1 || echo 0)"
verdict "(2) le projet ne s'ouvre pas « modifié »" \
    "$([ -n "$titre" ] && ! echo "$titre" | grep -q ' \*$' && echo 1 || echo 0)"
verdict "(3) réenregistré : « channels: 1 »" "$([ "$apres" = 1 ] && echo 1 || echo 0)"
verdict "(4) le témoin stéréo : rien de corrigé, l'entrée active" \
    "$(! grep -q "^VSM_PROJET_CORRIGE" "$brouillon/ouvrir-stereo.txt" && [ -n "$es" ] && ! echo "$es" | grep -q "\[grisee\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CANAUX-RECOMPTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CANAUX-RECOMPTES : un projet d'avant D559 retrouve les canaux de son fichier à l'ouverture, sans s'ouvrir modifié"
