#!/usr/bin/env bash
# D621 — LE NIVEAU QU'ON VOIT EN LECTURE ET CELUI QU'ON EXPORTE, mesurés dans l'application : une piste audio (un fichier
# stéréo de 3 s, −10,00 dBFS de crête, fader 1,0), lue par le bouton Play et relevée par `VSM_VUMETRES` (la tranche et le
# master, au moment de la photo) ; puis exportée (`exporter-audio:`, D589.3), la crête lue dans le WAV.
#
# L'HYPOTHÈSE ÉCRITE AVANT LA MESURE (ROADMAP-daw, D621) :
#   (a) au centre, la loi par défaut : la tranche −10,00 (avant le panoramique, D4.7), le master −13,01, l'export −13,01 —
#       le master et l'export à 0,1 dB près ; RÉFUTÉE si le master s'écarte de l'export de plus de 0,5 dB ;
#   (b) à fond à gauche : la tranche, le master et l'export à −10,00.
#   (c) MESURÉE, L'HYPOTHÈSE TENANT : le point de mesure s'écrit où on le lit — l'infobulle de la tranche dit « avant le
#       panoramique », celle du master « ce que l'export rend », en français et en anglais ; le témoin (D620) : aucune.
#
#   tools/niveau-lecture-export.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
TEMOIN="${2:-./build/temoin-D620/Vintage Synth MIDI Studio}"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-niveau.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
racine = sys.argv[1]; sr = 48000
for nom, pan in (("centre", 0.0), ("gauche", -1.0)):
    b = f"{racine}/{nom}"
    os.makedirs(f"{b}/audio", exist_ok=True); os.makedirs(f"{b}/midi", exist_ok=True)
    with wave.open(f"{b}/audio/a.wav", "wb") as w:   # −10,00 dBFS : 10 362 / 32 768
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<hh", *([int(round(10362 * math.sin(2 * math.pi * 220 * n / sr)))] * 2))
                               for n in range(3 * sr)))
    t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
    open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(t)) + t)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "A",
                           "mix": {"muted": False, "pan": pan, "sends": [], "solo": False, "volume": 1.0},
                           "audio": {"file": "audio/a.wav", "sampleRate": sr, "frames": 3 * sr, "channels": 2}}]},
              open(f"{b}/project.json", "w"), indent=1)
PY

course() {   # $1 = jeu ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local jeu="$1" nom="$2" gestes="$3"
    shift 3
    cp -r "$brouillon/$jeu" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3200 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "${BINAIRE:-$BIN}" > "$brouillon/$nom.txt" 2>&1
}
vumetre() { grep "^VSM_VUMETRE : $2 : " "$brouillon/$1.txt" | tail -1 | sed -n "s/^VSM_VUMETRE : $2 : \([-0-9.inf]*\) dBFS.*/\1/p"; }
crete() {
    python3 - "$1" <<'PY2'
import math, os, struct, sys
p = sys.argv[1]
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None; ext = b""
while i + 8 <= len(d):
    nom, t = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24]); ext = d[i + 8 + 24:i + 8 + 26]
    if nom == b"data": data = d[i + 8:i + 8 + t]
    i += 8 + t + (t & 1)
if fmt is None or data is None: print("ABSENT"); sys.exit(0)
tag, bits = fmt[0], fmt[5]
if tag == 0xFFFE: tag = int.from_bytes(ext, "little")
m = 0.0
if tag == 3:
    for (v,) in struct.iter_unpack("<f", data[: len(data) // 4 * 4]): m = max(m, abs(v))
else:
    n = bits // 8; plein = float(1 << (bits - 1))
    for k in range(0, len(data) - n + 1, n): m = max(m, abs(int.from_bytes(data[k:k + n], "little", signed=True)) / plein)
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.2f}")
PY2
}
pres() { python3 -c "import sys; a, b, t = sys.argv[1:4]; sys.exit(0 if a not in ('', '-inf', 'ABSENT') and b not in ('', '-inf', 'ABSENT') and abs(float(a) - float(b)) <= float(t) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D621 : le niveau qu'on voit en lecture et celui qu'on exporte ==="
for jeu in centre gauche; do
    course "$jeu" "lecture-$jeu" "1000:cliquer:Play;2200:relever-ordre" VSM_VUMETRES=1
    course "$jeu" "export-$jeu" "800:exporter-audio:$brouillon/export-$jeu.wav"
done
for jeu in centre gauche; do
    echo "       $jeu : tranche $(vumetre "lecture-$jeu" "tranche 0") ; master $(vumetre "lecture-$jeu" master) ; export $(crete "$brouillon/export-$jeu.wav") dBFS ; $(grep "^VSM_ORDRE : relevé" "$brouillon/lecture-$jeu.txt" | grep -o "tête [0-9.]* s ; [a-zé ]*" | head -1)"
done
TC="$(vumetre lecture-centre "tranche 0")"; MC="$(vumetre lecture-centre master)"; EC="$(crete "$brouillon/export-centre.wav")"
TG="$(vumetre lecture-gauche "tranche 0")"; MG="$(vumetre lecture-gauche master)"; EG="$(crete "$brouillon/export-gauche.wav")"
verdict "(témoin) les deux lectures jouaient au relevé" \
    "$(grep -q "^VSM_ORDRE : relevé .* ; en lecture" "$brouillon/lecture-centre.txt" && grep -q "^VSM_ORDRE : relevé .* ; en lecture" "$brouillon/lecture-gauche.txt" && echo 1 || echo 0)"
verdict "(a) au centre, la tranche −10,00 ($TC), avant le panoramique" "$(pres "$TC" -10.00 0.1 && echo 1 || echo 0)"
verdict "(a) au centre, l'export −13,01 ($EC)" "$(pres "$EC" -13.01 0.1 && echo 1 || echo 0)"
verdict "(a) au centre, le master et l'export à 0,1 dB près ($MC / $EC)" "$(pres "$MC" "$EC" 0.1 && echo 1 || echo 0)"
verdict "(b) à fond à gauche : la tranche, le master et l'export à −10,00 ($TG / $MG / $EG)" \
    "$(pres "$TG" -10.00 0.1 && pres "$MG" -10.00 0.1 && pres "$EG" -10.00 0.1 && echo 1 || echo 0)"
pres "$MC" "$EC" 0.5 || echo "       L'HYPOTHÈSE EST RÉFUTÉE : le master s'écarte de l'export de plus de 0,5 dB"

course centre bulles-en "500:relever-ordre" VSM_VUMETRES=1 VSM_LANGUE=en
infobulle() { grep "^VSM_VUMETRE : infobulle $2 : " "$brouillon/$1.txt" | tail -1 | sed "s/^VSM_VUMETRE : infobulle $2 : //"; }
echo "       infobulle de la tranche : « $(infobulle lecture-centre "tranche 0") »"
echo "       infobulle du master     : « $(infobulle lecture-centre master) »"
echo "       en anglais : « $(infobulle bulles-en "tranche 0" | cut -c1-60)… » ; « $(infobulle bulles-en master | cut -c1-60)… »"
verdict "(c) la tranche dit « après le fader, AVANT le panoramique » ; le master « ce que l'export rend » ; les trois marques de la peinture" \
    "$(infobulle lecture-centre "tranche 0" | grep -q "après le fader, AVANT le panoramique" && infobulle lecture-centre master | grep -q "ce que l'export rend" \
       && infobulle lecture-centre "tranche 0" | grep -q "La barre pâle est la crête, la barre pleine le niveau efficace, le trait la crête retenue" && echo 1 || echo 0)"
verdict "(c) en anglais : « after the fader, BEFORE the panner » ; « what the export renders »" \
    "$(infobulle bulles-en "tranche 0" | grep -q "after the fader, BEFORE the panner" && infobulle bulles-en master | grep -q "what the export renders" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    BINAIRE="$TEMOIN" course centre t-bulles "500:relever-ordre" VSM_VUMETRES=1
    echo "       témoin (D620) : $(grep -c "^VSM_VUMETRE : infobulle" "$brouillon/t-bulles.txt") ligne(s) d'infobulle ; $(grep -c "^VSM_VUMETRE : tranche 0 :" "$brouillon/t-bulles.txt") relevé(s) de la tranche"
    verdict "(c) témoin (D620) : les vumètres relevés, aucune infobulle" \
        "$([ "$(grep -c "^VSM_VUMETRE : infobulle" "$brouillon/t-bulles.txt")" = 0 ] && grep -q "^VSM_VUMETRE : tranche 0 :" "$brouillon/t-bulles.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi

echo
if [ "$rates" -gt 0 ]; then echo "NIVEAU-LECTURE-EXPORT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NIVEAU-LECTURE-EXPORT : le master qu'on voit et le fichier qu'on exporte disent le même niveau"
