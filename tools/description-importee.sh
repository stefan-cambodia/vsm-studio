#!/usr/bin/env bash
# D619 — UN FICHIER IMPORTÉ SE DÉCRIT COMME IL EST, mesuré dans l'application : un fichier importé sur une piste neuve
# (`VSM_IMPORT_AUDIO`, D24.5 — `placeSampleOnTrack`, le chemin du menu, du dépôt et du dépôt à un tick), le projet
# enregistré (`enregistrer:`), la réserve relue (`relever-reserve`) et l'export du mixage (`exporter-audio:`, D589.3).
#
# LE JEU : un projet SANS PISTE ; `son48.wav` (2 s, stéréo, 48 kHz) et `son44.wav` (2 s, stéréo, 44,1 kHz). Le moteur
# des bancs tourne à 44,1 kHz (vu en D618).
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D619) :
#   (a) `son48.wav` importé → `project.json` : 48 000 Hz, 96 000 trames, 2 canaux ; le témoin (D618) : 44 100 Hz,
#       88 200 trames ; la réserve « 48 kHz stéréo » (témoin : « 44.1 kHz ») ;
#   (b) la longueur du clip, en ticks, égale à celle du témoin ;
#   (c) l'export identique au bit à celui du témoin (deux courses du témoin d'abord) ;
#   (d) `son44.wav` importé : la même description chez le neuf et le témoin.
#
#   tools/description-importee.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D618/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$TEMOIN" ] || { echo "REFUS : le témoin $TEMOIN est absent — la mesure (b), (c) et (d) se fait CONTRE lui"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-description.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
def son(chemin, sr, secondes=2.0, crete=10362):
    with wave.open(chemin, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<hh", *([int(crete * math.sin(2 * math.pi * 220 * n / sr))] * 2))
                               for n in range(int(secondes * sr))))
son(f"{b}/son48.wav", 48000); son(f"{b}/son44.wav", 44100)
tempo = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/vide/midi", exist_ok=True)
open(f"{b}/vide/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                   + b"MTrk" + struct.pack(">I", len(tempo)) + tempo)
json.dump({"format": "vsm-project", "version": 1, "title": "vide", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/vide/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = fichier importé. Une copie du projet vide, un HOME neuf.
    local bin="$1" nom="$2" fichier="$3" n
    cp -r "$brouillon/vide" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_IMPORT_AUDIO="$brouillon/$fichier" \
        VSM_GESTE_APRES="1200:relever-reserve;1500:exporter-audio:$brouillon/$nom.wav;2000:enregistrer:$brouillon/$nom-projet" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|VUE|IMPORT[A-Z_]*) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC)|VSM_EXPORT : ÉCHEC"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
# La description et le clip de la piste importée : « fréquence trames canaux longueur-du-clip ».
description() {
    python3 - "$brouillon/$1-projet/project.json" <<'PY2'
import json, sys
# LA PISTE AUDIO, où qu'elle soit — CORRIGÉ APRÈS LE PREMIER ESSAI : le projet « vide » ouvre une piste MIDI vide (le
# bloc de tempo de son fichier MIDI), la piste importée est la DEUXIÈME ; lire `tracks[0]` rendait « None », et deux
# contrôles comparaient « None » à « None » au vert.
try:
    pistes = [t for t in json.load(open(sys.argv[1]))["tracks"] if t.get("kind") == "audio"]
except (OSError, KeyError):
    print("ABSENT"); sys.exit(0)
if len(pistes) != 1: print(f"ABSENT ({len(pistes)} piste(s) audio)"); sys.exit(0)
a = pistes[0].get("audio", {}); c = pistes[0].get("clips", [])
v = [a.get("sampleRate"), a.get("frames"), a.get("channels"), c[0].get("length") if c else None]
print(" ".join(str(int(x)) for x in v) if all(isinstance(x, (int, float)) for x in v) else "ABSENT (champ manquant)")
PY2
}
format_reserve() { grep "^VSM_RESERVE : audio/$2 ; " "$brouillon/$1.txt" | tail -1 | awk -F' ; ' '{print $3}'; }
# Deux WAV : « identique » (les données PCM au bit), « différent (…) », ou ce qui manque (D215 : jamais un verdict sur
# un côté absent).
pcm() {
    python3 - "$brouillon/$1.wav" "$brouillon/$2.wav" <<'PY2'
import os, sys
def donnees(p):
    if not os.path.isfile(p) or os.path.getsize(p) == 0: return None
    d = open(p, "rb").read(); i = 12
    while i + 8 <= len(d):
        nom, taille = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
        if nom == b"data": return d[i + 8:i + 8 + taille]
        i += 8 + taille + (taille & 1)
    return None
a, b = donnees(sys.argv[1]), donnees(sys.argv[2])
if a is None or b is None: print("ABSENT")
elif len(a) == 0: print("VIDE")
else: print("identique" if a == b else f"différent ({sum(x != y for x, y in zip(a, b)) + abs(len(a) - len(b))} octets sur {max(len(a), len(b))})")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D619 : un fichier importé se décrit comme il est ==="
course "$TEMOIN" t48a son48.wav
course "$TEMOIN" t48b son48.wav
course "$BIN" n48 son48.wav
course "$TEMOIN" t44 son44.wav
course "$BIN" n44 son44.wav

for c in t48a n48 t44 n44; do echo "       $c : description [$(description $c)] ; réserve « $(format_reserve $c "son${c:1:2}.wav") »"; done
TEM="$(pcm t48a t48b)"
echo "       témoin de l'identité : deux exports du témoin — $TEM"
read -r NF NT NC NL <<< "$(description n48)"; read -r TF TT TC TL <<< "$(description t48a)"
verdict "(témoin) deux exports du témoin, deux courses : identiques au bit" "$([ "$TEM" = identique ] && echo 1 || echo 0)"
verdict "(a) son48.wav : 48000 Hz, 96000 trames, 2 canaux ($NF $NT $NC) ; le témoin 44100 Hz, 88200 trames ($TF $TT)" \
    "$([ "$NF" = 48000 ] && [ "$NT" = 96000 ] && [ "$NC" = 2 ] && [ "$TF" = 44100 ] && [ "$TT" = 88200 ] && echo 1 || echo 0)"
verdict "(a) la réserve : « $(format_reserve n48 son48.wav) » (témoin « $(format_reserve t48a son48.wav) »)" \
    "$([ "$(format_reserve n48 son48.wav)" = "48 kHz stéréo" ] && [ "$(format_reserve t48a son48.wav)" = "44.1 kHz stéréo" ] && echo 1 || echo 0)"
entier() { [[ "${1:-}" =~ ^[0-9]+$ ]]; }
verdict "(b) la longueur du clip égale à celle du témoin ($NL et $TL ticks)" "$(entier "$NL" && [ "$NL" = "$TL" ] && echo 1 || echo 0)"
E="$(pcm n48 t48a)"
verdict "(c) l'export identique au bit à celui du témoin ($E)" "$([ "$E" = identique ] && echo 1 || echo 0)"
verdict "(d) son44.wav : la même description chez le neuf et le témoin ($(description n44) / $(description t44))" \
    "$([ "$(description n44)" = "$(description t44)" ] && entier "$(description n44 | cut -d' ' -f1)" && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "DESCRIPTION-IMPORTEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DESCRIPTION-IMPORTEE : un fichier importé est déclaré à sa fréquence et à ses trames, et le son n'a pas bougé d'un bit"
