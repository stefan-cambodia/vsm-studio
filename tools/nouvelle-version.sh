#!/usr/bin/env bash
# D563.1 — ENREGISTRER UNE NOUVELLE VERSION (Fichier ▸ Enregistrer une nouvelle version, Ctrl+Alt+S ; le *Save New
# Version* de Cubase), MESURÉ SUR LES DOSSIERS RELUS.
#
# LES RÈGLES GARDÉES. Un projet « Morceau », un fichier audio, un repère ; on en pose un second, puis Ctrl+Alt+S.
#   (1) « Morceau-02 » naît à côté, avec les DEUX repères et le fichier audio (médias compris) ;
#   (2) « Morceau » n'est pas touché : un seul repère ;
#   (3) la session y est passée : le titre dit « Morceau-02 », sans astérisque ;
#   (4) depuis « Morceau-02 », « Morceau-03 » existant déjà : « Morceau-04 », le premier numéro LIBRE — jamais
#       d'écrasement ; et par le MENU.
#
#   tools/nouvelle-version.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-version.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
with wave.open(f"{b}/la.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(8000 * math.sin(2 * math.pi * 440 * n / sr)), 0) for n in range(sr)))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/Morceau/midi", exist_ok=True)
open(f"{b}/Morceau/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                    + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 0, "name": "Début"}], "tracks": []}, open(f"{b}/Morceau/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
etat() {   # « repères audio » d'un dossier de projet : le nombre de repères, et « audio » si son fichier est là
    python3 -c "
import json, os, sys
d = sys.argv[1]
try:
    p = json.load(open(d + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
fichiers = [t['audio']['file'] for t in p['tracks'] if t.get('audio')]   # « file » (la première écriture disait « path »)
present = all(os.path.exists(os.path.join(d, f)) for f in fichiers) and fichiers
print(len(p.get('markers', [])), 'audio' if present else 'sans-audio')" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D563.1 : enregistrer une nouvelle version ==="
course import "$brouillon/Morceau" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/Morceau"
course version "$brouillon/Morceau" VSM_DELAI=2600 VSM_VUE="sans-rapport,tete-temps:4" \
    VSM_GESTE_APRES="1000:touche:insert;1500:touche:ctrl + alt + S"
mkdir -p "$brouillon/Morceau-03"
course menu "$brouillon/Morceau-02" VSM_DELAI=2200 VSM_GESTE_APRES="1000:menu:Fichier > Enregistrer une nouvelle version"

e1="$(etat "$brouillon/Morceau")"; e2="$(etat "$brouillon/Morceau-02")"; e4="$(etat "$brouillon/Morceau-04")"
titre="$(grep -h "^VSM_TITRE" "$brouillon/version.txt" | head -1)"
grep -h "^VSM_NOUVELLE_VERSION" "$brouillon/version.txt" "$brouillon/menu.txt" | sed 's/^/       /'
echo "       Morceau : [$e1] ; Morceau-02 : [$e2] ; Morceau-04 : [$e4] ; Morceau-03 laissé vide : [$(ls "$brouillon/Morceau-03" | wc -l) fichier(s)]"
echo "       $titre"

verdict "(1) « Morceau-02 » : les deux repères et le fichier audio" "$([ "$e2" = "2 audio" ] && echo 1 || echo 0)"
verdict "(2) « Morceau » n'est pas touché : un repère" "$([ "$e1" = "1 audio" ] && echo 1 || echo 0)"
verdict "(3) la session y est passée : « Morceau-02 », sans astérisque" \
    "$(echo "$titre" | grep -q -- "-- Morceau-02$" && echo 1 || echo 0)"
verdict "(4) par le menu, « Morceau-03 » pris : « Morceau-04 », et « Morceau-03 » intact" \
    "$([ "$e4" = "2 audio" ] && [ "$(ls "$brouillon/Morceau-03" | wc -l)" = 0 ] \
        && grep -q "^VSM_NOUVELLE_VERSION : « Morceau-02 » → « Morceau-04 »" "$brouillon/menu.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NOUVELLE-VERSION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NOUVELLE-VERSION : le projet passe au dossier suivant, médias compris, sans toucher au précédent ni rien écraser"
