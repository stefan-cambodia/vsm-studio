#!/usr/bin/env bash
# D618 — POSER UN FICHIER DE LA RÉSERVE DANS LE PROJET (le *Pool ▸ Insert into Project ▸ At Cursor* de Cubase), mesuré dans
# l'application : la tête posée sur un repère (`repere-aller:`), une ligne de la réserve choisie (`reserve-choisir:`, D608),
# « Poser à la tête » pressé (`cliquer:`, le vrai bouton) ; le projet enregistré, les stems exportés (`exporter-stems:`,
# D616), l'historique et la réserve relue.
#
# LE JEU : « Voix » joue `audio/voix.wav` et garde la prise `audio/prise2.wav` ; `audio/orphelin.wav` (2 s, stéréo, −10 dBFS
# de crête) n'est désigné par rien ; `audio/texte.wav` est du texte ; un repère au tick 1 920 (2 s à 120 BPM). Un second
# jeu, « manque », où « Basse » désigne `audio/absent.wav`, absent.
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D618) :
#   (a) une piste « orphelin » sous « Voix », qui désigne `audio/orphelin.wav`, son clip au tick 1 920 ; la ligne « joué » ;
#       aucun fichier neuf dans `audio/` ; 1 pas « Poser un fichier de la réserve » ; son stem silencieux (sous −130 dBFS)
#       avant 1,9 s et au-dessus de −40 dBFS après 2,1 s ;
#   (b) la prise `audio/prise2.wav` se pose de même ; Ctrl+Z → la piste retirée, `orphelin.wav` « gardé pour l'historique » ;
#   (c) une ligne MANQUANT : « Poser » grisé ; `audio/texte.wav` : refusé et dit, aucune piste, aucun pas ;
#   (d) le témoin (D617) : pas de bouton, aucune piste ; l'anglais.
#
#   tools/poser-depuis-reserve.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D617/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-poser.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
racine = sys.argv[1]
def mtrk(corps):
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
tempo = mtrk(b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00")
vide = mtrk(b"\x00\xff\x2f\x00")
sr = 48000
def son(chemin, secondes, canaux=1, crete=3000):
    with wave.open(chemin, "wb") as w:
        w.setnchannels(canaux); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<" + "h" * canaux, *([int(crete * math.sin(2 * math.pi * 220 * n / sr))] * canaux))
                               for n in range(int(secondes * sr))))
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
def source(nom, frames, canaux=1):
    return {"file": f"audio/{nom}", "sampleRate": sr, "frames": frames, "channels": canaux}
def projet(dossier, pistes, markers):
    os.makedirs(f"{dossier}/midi", exist_ok=True); os.makedirs(f"{dossier}/audio", exist_ok=True)
    open(f"{dossier}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + tempo + vide * (len(pistes) - 1))   # UN bloc MIDI par piste, le tempo dans le premier
    open(f"{dossier}/midi/prises.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + vide)
    json.dump({"format": "vsm-project", "version": 1, "title": "poser", "midi": {"file": "midi/arrangement.mid"},
               "markers": markers,
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": pistes}, open(f"{dossier}/project.json", "w"), indent=1)
b = f"{racine}/projet"
voix = {"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Voix", "mix": mix,
        "audio": source("voix.wav", sr),
        "takes": [{"name": "Prise 1", "startTick": 0, "endTick": 1920, "midiTrack": -1, "audio": source("voix.wav", sr)},
                  {"name": "Prise 2", "startTick": 0, "endTick": 1920, "midiTrack": -1, "audio": source("prise2.wav", sr // 2)}],
        "activeTake": 0}
projet(b, [voix], [{"tick": 1920, "name": "Refrain"}])
son(f"{b}/audio/voix.wav", 1.0); son(f"{b}/audio/prise2.wav", 0.5)
son(f"{b}/audio/orphelin.wav", 2.0, canaux=2, crete=10362)   # −10 dBFS de crête
open(f"{b}/audio/texte.wav", "w").write("ceci n'est pas un fichier audio\n")
m = f"{racine}/manque"
projet(m, [{"kind": "audio", "channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Basse", "mix": mix,
            "audio": source("absent.wav", 2 * sr, 2)}], [])
son(f"{m}/audio/orphelin.wav", 0.5)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu (`JEU`), un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/${JEU:-projet}" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3400 VSM_GESTE_APRES="$gestes" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# LA FENÊTRE DÉGÉNÉRÉE (D607, D613) : une course dont la réserve est relevée sous 200 × 200 est REJOUÉE une fois, et c'est DIT.
rejouees=0
course_sure() {
    course "$@"
    if grep -q "fenêtre 1[0-9][0-9]x1[0-9][0-9]$" "$brouillon/$2.txt"; then
        echo "       course « $2 » REJOUÉE : $(grep -o "fenêtre [0-9]*x[0-9]*" "$brouillon/$2.txt" | head -1) au premier essai"
        rejouees=$((rejouees + 1))
        rm -rf "$brouillon/p-$2"
        course "$@"
    fi
}
ligne() { grep "^VSM_RESERVE : $2 ; " "$brouillon/$1.txt" | tail -1; }
etat_de() { echo "$1" | awk -F' ; ' '{print $NF}'; }
pas() { grep "^VSM_HISTORIQUE_PAS" "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_HISTORIQUE_PAS : //'; }
# La piste d'un projet enregistré qui désigne `$2` : « nom ; index ; début du premier clip ; nombre de pistes ».
piste_de() {
    python3 - "$1/project.json" "$2" <<'PY2'
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except OSError:
    print("ABSENT"); sys.exit(0)
pistes = p.get("tracks", [])
for i, t in enumerate(pistes):
    if t.get("audio", {}).get("file") == sys.argv[2] and t.get("name") != "Voix":
        clips = t.get("clips", [])
        print(f"{t.get('name')} ; {i} ; {clips[0].get('start') if clips else '-'} ; {len(pistes)}"); sys.exit(0)
print(f"AUCUNE ; - ; - ; {len(pistes)}")
PY2
}
# La crête d'un WAV en dBFS entre deux instants (en secondes) ; « -inf » pour un silence, « ABSENT » s'il manque.
crete() {
    python3 - "$1" "$2" "$3" <<'PY2'
import math, os, struct, sys
p, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None; ext = b""
while i + 8 <= len(d):
    nom, taille = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24]); ext = d[i + 8 + 24:i + 8 + 26]
    if nom == b"data": data = d[i + 8:i + 8 + taille]
    i += 8 + taille + (taille & 1)
if fmt is None or data is None: print("ABSENT"); sys.exit(0)
tag, canaux, sr, bits = fmt[0], fmt[1], fmt[2], fmt[5]
if tag == 0xFFFE: tag = int.from_bytes(ext, "little")   # WAVE_FORMAT_EXTENSIBLE : le sous-format
n = bits // 8; trame = n * canaux
data = data[int(t0 * sr) * trame:int(t1 * sr) * trame]
m = 0.0
if tag == 3:
    for (v,) in struct.iter_unpack("<f", data[: len(data) // 4 * 4]): m = max(m, abs(v))
else:
    plein = float(1 << (bits - 1))
    for k in range(0, len(data) - n + 1, n):
        m = max(m, abs(int.from_bytes(data[k:k + n], "little", signed=True)) / plein)
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.2f}")
PY2
}
au_dessus() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v not in ('', '-inf', 'ABSENT') and float(v) > float(sys.argv[2]) else 1)" "$@" 2>/dev/null; }
# SILENCIEUX = SOUS −130 dBFS : l'export en 24 bits porte le tramage TPDF de ± 1 LSB (D14.4), −138,47 dBFS (D617).
silencieux() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v == '-inf' or (v != 'ABSENT' and float(v) < -130) else 1)" "$1" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

F="Réserve des fichiers"
B="$brouillon"
POSER="800:repere-aller:0;900:relever-tete;1100:reserve-choisir:audio/orphelin.wav;1300:cliquer:fenetre:$F:Poser à la tête"
echo "=== D618 : poser un fichier de la réserve dans le projet ==="
AVANT="$(ls "$B/projet/audio" | tr '\n' ' ')"
course_sure "$BIN" pose "$POSER;1700:relever-reserve;1800:relever-historique;1900:exporter-stems:$B/stems-pose;2300:enregistrer:$B/pose-projet"
course_sure "$BIN" prise "800:repere-aller:0;1100:reserve-choisir:audio/prise2.wav;1300:cliquer:fenetre:$F:Poser à la tête;1700:relever-reserve;2000:enregistrer:$B/prise-projet"
course_sure "$BIN" annule "$POSER;1700:touche:ctrl + Z;2100:relever-reserve;2200:relever-historique;2400:enregistrer:$B/annule-projet"
course_sure "$BIN" illisible "1100:reserve-choisir:audio/texte.wav;1300:cliquer:fenetre:$F:Poser à la tête;1700:relever-historique;1900:relever-reserve"
JEU=manque course_sure "$BIN" manque "900:cliquer:fenetre:Audio non chargé:OK;1200:reserve-choisir:audio/absent.wav"
course_sure "$BIN" en "800:repere-aller:0;1100:reserve-choisir:audio/orphelin.wav;1300:cliquer:fenetre:Pool:Insert at playhead;1700:relever-reserve;1800:relever-historique" VSM_LANGUE=en

T="$(grep "^VSM_TETE" "$B/pose.txt" | head -1)"; C="$(grep "^VSM_RESERVE : choisie" "$B/pose.txt" | head -1)"
P="$(grep "^VSM_RESERVE : posé" "$B/pose.txt" | head -1)"; L="$(ligne pose audio/orphelin.wav)"
echo "       $T"
echo "       $C"
echo "       $P"
echo "       $L"
PJ="$(piste_de "$B/pose-projet" audio/orphelin.wav)"
echo "       projet enregistré : $PJ"
APRES="$(ls "$B/p-pose/audio" | tr '\n' ' ')"
echo "       audio/ avant : $AVANT ; après : $APRES"
STEM="$(ls "$B"/stems-pose/*orphelin*.wav 2>/dev/null | head -1)"
S0="$(crete "$STEM" 0 1.9)"; S1="$(crete "$STEM" 2.1 4.0)"
echo "       stem « $(basename "${STEM:-aucun}") » : 0 à 1,9 s $S0 dBFS ; 2,1 à 4 s $S1 dBFS"
verdict "(a) la tête au repère (tick 1920) ; la ligne choisie : « Poser » actif" \
    "$(echo "$T" | grep -q "^VSM_TETE : tick 1920," && echo "$C" | grep -q "Poser actif" && echo 1 || echo 0)"
verdict "(a) une piste « orphelin » sous « Voix » (index 1), qui désigne audio/orphelin.wav, son clip au tick 1920" \
    "$([ "$PJ" = "orphelin ; 1 ; 1920 ; 2" ] && echo 1 || echo 0)"
# CORRIGÉ APRÈS LE PREMIER ESSAI : la ligne disait « 44.1 kHz » (la fréquence du moteur) pour un fichier à 48 kHz.
verdict "(a) la ligne « joué », décrite comme le fichier (2.00 s, 48 kHz stéréo) ; aucun fichier neuf dans audio/" \
    "$([ "$(etat_de "$L")" = "joué" ] && echo "$L" | grep -q " ; 2.00 s ; 48 kHz stéréo ; " && [ "$AVANT" = "$APRES" ] && echo 1 || echo 0)"
verdict "(a) 1 pas : « Poser un fichier de la réserve » ($(pas pose))" "$([ "$(pas pose)" = "1 : Poser un fichier de la réserve" ] && echo 1 || echo 0)"
verdict "(a) le stem : silencieux avant 1,9 s ($S0), au-dessus de −40 dBFS après 2,1 s ($S1)" \
    "$(silencieux "$S0" && au_dessus "$S1" -40 && echo 1 || echo 0)"
PP="$(piste_de "$B/prise-projet" audio/prise2.wav)"
echo "       la prise : $PP ; $(etat_de "$(ligne prise audio/prise2.wav)")"
verdict "(b) la prise en réserve se pose de même : « prise2 », index 1, au tick 1920, la ligne « joué »" \
    "$([ "$PP" = "prise2 ; 1 ; 1920 ; 2" ] && [ "$(etat_de "$(ligne prise audio/prise2.wav)")" = "joué" ] && echo 1 || echo 0)"
UA="$(piste_de "$B/annule-projet" audio/orphelin.wav)"; UL="$(ligne annule audio/orphelin.wav)"
echo "       Ctrl+Z : $UA ; orphelin.wav $(etat_de "$UL") ; pas : $(pas annule)"
verdict "(b) Ctrl+Z : la piste retirée (1 piste), orphelin.wav « gardé pour l'historique »" \
    "$([ "$UA" = "AUCUNE ; - ; - ; 1" ] && [ "$(etat_de "$UL")" = "gardé pour l'historique" ] && echo 1 || echo 0)"
I="$(grep "^VSM_RESERVE : poser REFUS" "$B/illisible.txt" | head -1)"
echo "       illisible : $I ; pas : $(pas illisible)"
verdict "(c) audio/texte.wav : refusé et dit, aucun pas" \
    "$(echo "$I" | grep -q "audio/texte.wav illisible" && [ "$(pas illisible)" = "0 : " ] && echo 1 || echo 0)"
CM="$(grep "^VSM_RESERVE : choisie" "$B/manque.txt" | head -1)"
echo "       manquant : $CM"
verdict "(c) une ligne MANQUANT : « Poser » grisé" "$(echo "$CM" | grep -q "Relier actif ; Poser grisé" && echo 1 || echo 0)"
EP="$(grep "^VSM_RESERVE : posé" "$B/en.txt" | head -1)"
echo "       anglais : $EP ; $(pas en)"
verdict "(d) en anglais : « Insert at playhead » pressé dans « Pool », la piste posée, la ligne « playing »" \
    "$(echo "$EP" | grep -q "audio/orphelin.wav sur la piste « orphelin » (index 1), au tick 1920" && [ "$(etat_de "$(ligne en audio/orphelin.wav)")" = playing ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-pose "$POSER;1700:relever-reserve;2000:enregistrer:$B/t-pose-projet"
    tc="$(grep -c "Poser à la tête — cliqué" "$B/t-pose.txt")"; TP="$(piste_de "$B/t-pose-projet" audio/orphelin.wav)"
    echo "       témoin : $tc clic(s) sur « Poser à la tête » ; $TP ; orphelin.wav $(etat_de "$(ligne t-pose audio/orphelin.wav)")"
    verdict "(d) témoin (D617) : pas de bouton, aucune piste, orphelin.wav inutilisé" \
        "$([ "$tc" = 0 ] && [ "$TP" = "AUCUNE ; - ; - ; 1" ] && [ "$(etat_de "$(ligne t-pose audio/orphelin.wav)")" = "inutilisé" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "POSER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "POSER : un fichier de la réserve se pose à la tête sur une piste neuve, en place, en un pas"
