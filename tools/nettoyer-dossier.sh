#!/usr/bin/env bash
# D564.1 — NETTOYER LE DOSSIER DU PROJET (Fichier ▸ Nettoyer le dossier du projet… ; le *Cleanup* de Cubase), MESURÉ
# SUR LE DOSSIER ET SUR LA CORBEILLE.
#
# LES RÈGLES GARDÉES. Un projet à deux pistes audio (A joue audio/a.wav, B joue audio/b.wav), et un orphelin
# audio/orphelin.wav que rien ne désigne. Dans la course, on SUPPRIME la piste B : son fichier n'est plus désigné par le
# projet, mais un Ctrl+Z le rappellerait.
#   (1) confirmé : seul l'orphelin part — a.wav et b.wav restent ; le journal compte « 1 orphelin, 1 désigné, 1 gardé
#       pour l'historique » ;
#   (2) il est dans la CORBEILLE (Trash/files), avec sa fiche (Trash/info, `Path=` vers l'ancien emplacement) ;
#   (3) refusé : rien ne bouge ;
#   (4) sans orphelin : la boîte dit qu'il n'y a rien à nettoyer ; et le projet n'est pas « modifié » par le geste (aucun
#       pas).
#
#   tools/nettoyer-dossier.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-nettoyer.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
for nom, f in (("a", 440), ("b", 550), ("orphelin", 660)):
    with wave.open(f"{b}/{nom}.wav", "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<hh", int(8000 * math.sin(2 * math.pi * f * n / sr)), 0) for n in range(sr // 2)))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; $3 = HOME ; le reste : variables
    local nom="$1" projet="$2" maison="$3"
    shift 3
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
maison() { mktemp -d "$brouillon/home.XXXX"; }
fichiers() { (cd "$1/audio" 2>/dev/null && ls | sort | tr '\n' ' ' | sed 's/ $//') || echo "ABSENT"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
NETTOYER="menu:Fichier > Nettoyer le dossier du projet..."

echo "=== D564.1 : nettoyer le dossier du projet ==="
course import-a "$brouillon/P" "$(maison)" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/a.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/P"
course import-b "$brouillon/P" "$(maison)" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/b.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/P"
cp -r "$brouillon/P" "$brouillon/sans-orphelin"
cp "$brouillon/orphelin.wav" "$brouillon/P/audio/orphelin.wav"
for x in oui non; do cp -r "$brouillon/P" "$brouillon/$x"; done
pistes="$(python3 -c "
import json, sys
print(' '.join(t['name'] + '=' + t['audio']['file'] for t in json.load(open(sys.argv[1] + '/project.json'))['tracks'] if t.get('audio')))" "$brouillon/P")"
echo "       pistes audio : $pistes ; audio/ : [$(fichiers "$brouillon/P")]"
# LA PISTE DE b.wav : son rang dans la liste (la piste MIDI vide de l'ouverture compte).
rangb="$(python3 -c "
import json, sys
ts = json.load(open(sys.argv[1] + '/project.json'))['tracks']
print(next(i for i, t in enumerate(ts) if t.get('audio', {}).get('file', '').endswith('b.wav')))" "$brouillon/P")"
H="$(maison)"
course oui "$brouillon/oui" "$H" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:$rangb" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="900:menu:Piste > Supprimer la piste sélectionnée;1300:$NETTOYER"
course non "$brouillon/non" "$(maison)" VSM_DELAI=2400 VSM_CONFIRMER=non VSM_GESTE_APRES="1000:$NETTOYER"
course rien "$brouillon/sans-orphelin" "$(maison)" VSM_DELAI=2200 VSM_GESTE_APRES="1000:$NETTOYER"

fo="$(fichiers "$brouillon/oui")"; fn="$(fichiers "$brouillon/non")"
corbeille="$H/.local/share/Trash"
grep -h "^VSM_NETTOYAGE" "$brouillon/oui.txt" "$brouillon/non.txt" "$brouillon/rien.txt" | sed 's/^/       /'
echo "       après oui : [$fo] ; après non : [$fn] ; corbeille : [$(ls "$corbeille/files" 2>/dev/null | tr '\n' ' ')] [$(ls "$corbeille/info" 2>/dev/null | tr '\n' ' ')]"
fiche="$(cat "$corbeille/info/orphelin.wav.trashinfo" 2>/dev/null | tr '\n' ' ')"
echo "       fiche : $fiche"
titre="$(grep -h "^VSM_TITRE" "$brouillon/rien.txt" | head -1)"

verdict "(1) seul l'orphelin part ; b.wav, que Ctrl+Z rappellerait, reste" \
    "$([ "$fo" = "a.wav b.wav" ] && grep -q "^VSM_NETTOYAGE : 1 orphelin(s) \[audio/orphelin.wav\], 1 d.sign.(s), 1 gard.(s) pour l'historique" "$brouillon/oui.txt" \
        && grep -q "^VSM_NETTOYAGE : 1 . la corbeille, 0 " "$brouillon/oui.txt" && echo 1 || echo 0)"
verdict "(2) dans la corbeille, avec sa fiche" \
    "$([ -f "$corbeille/files/orphelin.wav" ] && echo "$fiche" | grep -q "Path=.*/oui/audio/orphelin.wav" && echo "$fiche" | grep -q "DeletionDate=" && echo 1 || echo 0)"
verdict "(3) refusé : rien ne bouge" "$([ "$fn" = "a.wav b.wav orphelin.wav" ] && grep -q "^VSM_NETTOYAGE : refus" "$brouillon/non.txt" && echo 1 || echo 0)"
verdict "(4) sans orphelin : « Rien à nettoyer », et le projet reste non modifié" \
    "$(grep -q "^VSM_BOITE : Nettoyer le dossier du projet : Rien à nettoyer" "$brouillon/rien.txt" && [ -n "$titre" ] && ! echo "$titre" | grep -q ' \*$' && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NETTOYER-DOSSIER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NETTOYER-DOSSIER : ce que rien ne désigne part à la corbeille, et rien de ce qu'un Ctrl+Z rappellerait"
