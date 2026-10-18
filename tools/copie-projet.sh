#!/usr/bin/env bash
# D563.3 — ENREGISTRER UNE COPIE (Fichier ▸ Enregistrer une copie… ; le *Back up Project* de Cubase), MESURÉ SUR LES
# DEUX DOSSIERS RELUS.
#
# LES RÈGLES GARDÉES. Un projet « P » enregistré, un fichier audio, un repère ; on en pose un second.
#   (1) la copie : « Copie » porte les deux repères et le fichier audio ;
#   (2) la session reste : le titre dit toujours « P », AVEC son astérisque (la copie n'enregistre pas P) ;
#   (3) un Ctrl+S qui suit écrit dans « P » (deux repères), et la copie n'en reçoit rien de plus ;
#   (4) la copie vers « P » lui-même : refusée, et dite.
#
#   tools/copie-projet.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-copie.XXXXXX")"
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
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 0, "name": "Début"}], "tracks": []}, open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
etat() {   # « repères audio » d'un dossier de projet
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
COPIE="menu:Fichier > Enregistrer une copie..."
VUE="sans-rapport,tete-temps:4"

echo "=== D563.3 : enregistrer une copie ==="
course import "$brouillon/P" VSM_DELAI=2500 VSM_VUE="sans-rapport" VSM_IMPORT_AUDIO="$brouillon/la.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/P"
cp -r "$brouillon/P" "$brouillon/P2"
course copie "$brouillon/P" VSM_DELAI=2400 VSM_VUE="$VUE" VSM_FICHIER="$brouillon/Copie" \
    VSM_GESTE_APRES="900:touche:insert;1400:$COPIE"
course copie-puis-s "$brouillon/P2" VSM_DELAI=2800 VSM_VUE="$VUE" VSM_FICHIER="$brouillon/Copie2" \
    VSM_GESTE_APRES="900:touche:insert;1400:$COPIE;1900:touche:ctrl + S"
course sur-soi "$brouillon/P" VSM_DELAI=2200 VSM_VUE="$VUE" VSM_FICHIER="$brouillon/P" \
    VSM_GESTE_APRES="900:touche:insert;1400:$COPIE"

ec="$(etat "$brouillon/Copie")"; ep="$(etat "$brouillon/P")"; ec2="$(etat "$brouillon/Copie2")"; ep2="$(etat "$brouillon/P2")"
titre="$(grep -h "^VSM_TITRE" "$brouillon/copie.txt" | head -1)"
grep -h "^VSM_COPIE" "$brouillon/copie.txt" "$brouillon/sur-soi.txt" | sed 's/^/       /'
echo "       Copie : [$ec] ; P (non enregistré) : [$ep] ; après Ctrl+S — P2 : [$ep2], Copie2 : [$ec2]"
echo "       $titre"

verdict "(1) la copie porte les deux repères et le fichier audio" "$([ "$ec" = "2 audio" ] && echo 1 || echo 0)"
verdict "(2) la session reste dans « P », toujours modifiée" \
    "$(echo "$titre" | grep -q -- "-- P \*$" && [ "$ep" = "1 audio" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+S écrit dans « P2 », la copie déjà faite" "$([ "$ep2" = "2 audio" ] && [ "$ec2" = "2 audio" ] && echo 1 || echo 0)"
verdict "(4) vers le dossier du projet lui-même : refusée, et dite" \
    "$(grep -q "^VSM_COPIE : refus" "$brouillon/sur-soi.txt" && grep -q "^VSM_BOITE : Enregistrer une copie : C'est le dossier du projet" "$brouillon/sur-soi.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COPIE-PROJET : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COPIE-PROJET : une copie du projet et de ses médias s'écrit ailleurs, la session reste où elle est"
