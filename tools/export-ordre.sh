#!/usr/bin/env bash
# D616 — L'EXPORT SUIT L'ORDRE DE JEU QU'ON ENTEND, mesuré dans l'application : l'ordre posé et la case cochée par des
# verbes (`ordre:1,0`, `ordre-suit:oui`), puis l'export audio (`exporter-audio:`) et les stems (`exporter-stems:`) ; contre
# l'export du même projet aplati par le bouton de la fenêtre (« Aplatir », confirmé par `VSM_CONFIRMER=oui`).
#
# LE JEU : 480 ticks à la noire, 120 BPM, deux repères — « A » au tick 0, « B » au tick 1 920 (une mesure) —, une piste
# au Minimoog : un la (57) dans A, un mi (64) dans B ; l'ordre [B, A].
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D616) :
#   (a) l'export, case cochée, identique au bit à l'export du projet aplati [B, A] par le bouton ;
#   (b) le projet enregistré APRÈS l'export garde ses notes à leur place (57 au tick 0, 64 au tick 1 920) ;
#   (c) la case décochée : la ligne de temps (identique au bit à l'export sans ordre, différent de (a)) ; le témoin
#       (D615), case cochée : la ligne de temps aussi ;
#   (d) les stems suivent : chaque stem identique au bit au stem du projet aplati, et différent de celui de la ligne ;
#   (e) le message de l'export dit l'ordre suivi (« L'ordre de jeu suivi : B, A ») ; en anglais aussi.
# LE TÉMOIN DE L'IDENTITÉ AU BIT : deux courses sans ordre (`ligne`, `decoche`) — si deux exports d'une même ligne de
# temps différaient d'une course à l'autre, « identique » ne voudrait rien dire.
#
#   tools/export-ordre.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D615/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-export-ordre.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for debut, hauteur in ((0, 57), (1920, 64)):
    corps += vlq(debut - t) + bytes([0x90, hauteur, 100]) + vlq(960) + bytes([0x80, hauteur, 0]); t = debut + 960
corps += vlq(3840 - t) + b"\xff\x2f\x00"
os.makedirs(f"{b}/projet/midi", exist_ok=True)
open(f"{b}/projet/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "export-ordre", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "A"}, {"tick": 1920, "name": "B"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/projet/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3600 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|EXPORT|EXPORT_STEMS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# LE BOUTON « APLATIR » EST DANS UNE FENÊTRE FLOTTANTE (D613 : née parfois en 128 × 128) : sans la confirmation au
# journal, la course est REJOUÉE une fois, et c'est DIT.
rejouees=0
course_aplatie() {
    course "$@"
    if ! grep -q "^VSM_CONFIRMER : oui" "$brouillon/$2.txt"; then
        echo "       course « $2 » REJOUÉE : l'aplatissement n'a pas été confirmé au premier essai"
        rejouees=$((rejouees + 1))
        rm -rf "$brouillon/p-$2" "$brouillon/$2.wav" "$brouillon/$2-stems"
        course "$@"
    fi
}
# Deux fichiers WAV : « identique » (les données PCM au bit), « différent », ou ce qui manque — jamais un verdict sur
# un côté absent (CLAUDE.md, D215).
pcm() {
    python3 - "$1" "$2" <<'PY2'
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
if a is None or b is None:
    print("ABSENT " + ("" if a is not None else os.path.basename(sys.argv[1])) + " " + ("" if b is not None else os.path.basename(sys.argv[2])))
elif len(a) == 0:
    print("VIDE")
else:
    print("identique" if a == b else f"différent ({sum(x != y for x, y in zip(a, b)) + abs(len(a) - len(b))} octets sur {max(len(a), len(b))})")
PY2
}
# Les notes d'un projet enregistré : « hauteur@tick » dans l'ordre du fichier.
notes_de() {
    python3 - "$1/midi/arrangement.mid" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, i = [], 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; statut = 0
    while j < fin:
        v = 0
        while True:
            c = d[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if d[j] == 0xFF:   # méta : FF, type, longueur (VLQ), données
            k, n = j + 2, 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] in (0xF0, 0xF7):
            k, n = j + 1, 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        if statut & 0xF0 == 0x90 and d[j + 1] > 0: out.append(f"{d[j]}@{t}")
        j += taille
    i = fin
print(" ".join(out) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

B="$brouillon"
APLATIR="800:ordre:1,0;1100:cliquer:fenetre:Ordre de jeu:Aplatir (écrit le matériau)"
echo "=== D616 : l'export suit l'ordre de jeu qu'on entend ==="
course "$BIN" suit "800:ordre:1,0;900:ordre-suit:oui;1300:exporter-audio:$B/suit.wav;2400:enregistrer:$B/suit-projet"
course_aplatie "$BIN" aplati "$APLATIR;1700:exporter-audio:$B/aplati.wav;2600:enregistrer:$B/aplati-projet" VSM_CONFIRMER=oui
course "$BIN" ligne "1300:exporter-audio:$B/ligne.wav"
course "$BIN" decoche "800:ordre:1,0;900:ordre-suit:non;1300:exporter-audio:$B/decoche.wav"
course "$BIN" stems-suit "800:ordre:1,0;900:ordre-suit:oui;1300:exporter-stems:$B/stems-suit"
course_aplatie "$BIN" stems-aplati "$APLATIR;1700:exporter-stems:$B/stems-aplati" VSM_CONFIRMER=oui
course "$BIN" stems-ligne "1300:exporter-stems:$B/stems-ligne"
course "$BIN" en "800:ordre:1,0;900:ordre-suit:oui;1300:exporter-audio:$B/en.wav" VSM_LANGUE=en

echo "       notes : la session aplatie par le bouton [$(notes_de "$B/aplati-projet")] ; après l'export suivi [$(notes_de "$B/suit-projet")]"
TEM="$(pcm "$B/ligne.wav" "$B/decoche.wav")"
echo "       témoin de l'identité : deux exports de la ligne de temps, deux courses — $TEM"
verdict "(témoin) deux exports de la même ligne de temps sont identiques au bit" "$([ "$TEM" = identique ] && echo 1 || echo 0)"
verdict "(témoin) le bouton a bien aplati [B, A] : 64 au tick 0, 57 au tick 1 920 ($(notes_de "$B/aplati-projet"))" \
    "$([ "$(notes_de "$B/aplati-projet")" = "64@0 57@1920" ] && echo 1 || echo 0)"
A="$(pcm "$B/suit.wav" "$B/aplati.wav")"; AL="$(pcm "$B/suit.wav" "$B/ligne.wav")"
verdict "(a) case cochée : l'export identique au bit à celui du projet aplati ($A) ; contre la ligne : $AL" \
    "$([ "$A" = identique ] && [ "${AL%% *}" = différent ] && echo 1 || echo 0)"
verdict "(b) le projet enregistré après l'export garde ses notes : 57 au tick 0, 64 au tick 1 920 ($(notes_de "$B/suit-projet"))" \
    "$([ "$(notes_de "$B/suit-projet")" = "57@0 64@1920" ] && echo 1 || echo 0)"
C="$(pcm "$B/decoche.wav" "$B/ligne.wav")"; CA="$(pcm "$B/decoche.wav" "$B/aplati.wav")"
verdict "(c) case décochée : la ligne de temps ($C) ; contre l'aplati : $CA" \
    "$([ "$C" = identique ] && [ "${CA%% *}" = différent ] && echo 1 || echo 0)"

nst=0; nsi=0; nsd=0
for f in "$B"/stems-aplati/*.wav; do
    [ -f "$f" ] || continue
    nst=$((nst + 1)); n="$(basename "$f")"
    [ "$(pcm "$B/stems-suit/$n" "$f")" = identique ] && nsi=$((nsi + 1))
    [ "$(pcm "$B/stems-suit/$n" "$B/stems-ligne/$n")" = identique ] || nsd=$((nsd + 1))
done
echo "       stems : $nst au projet aplati ; $nsi identiques au bit à ceux de l'ordre suivi ; $nsd différents de la ligne"
echo "       $(ls "$B/stems-suit" 2>/dev/null | tr '\n' ' ')"
verdict "(d) les stems suivent : $nsi/$nst identiques à ceux du projet aplati, $nsd/$nst différents de la ligne" \
    "$([ "$nst" -gt 0 ] && [ "$nsi" = "$nst" ] && [ "$nsd" = "$nst" ] && [ "$(ls "$B/stems-suit" | wc -l)" = "$(ls "$B/stems-aplati" | wc -l)" ] && echo 1 || echo 0)"

M="$(grep "^VSM_EXPORT : " "$B/suit.txt" | tail -1)"; MS="$(grep "^VSM_EXPORT_STEMS : " "$B/stems-suit.txt" | tail -1)"
MD="$(grep "^VSM_EXPORT : " "$B/decoche.txt" | tail -1)"; ME="$(grep "^VSM_EXPORT : " "$B/en.txt" | tail -1)"
echo "       $M"
echo "       $MS"
verdict "(e) le message dit « L'ordre de jeu suivi : B, A » (le mixage, les stems), et rien case décochée" \
    "$(echo "$M" | grep -q "L'ordre de jeu suivi : B, A" && echo "$MS" | grep -q "L'ordre de jeu suivi : B, A" \
       && ! echo "$MD" | grep -q "ordre de jeu suivi" && echo 1 || echo 0)"
verdict "(e) en anglais : « Play order followed: B, A »" "$(echo "$ME" | grep -q "Play order followed: B, A" && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-suit "800:ordre:1,0;900:ordre-suit:oui;1300:exporter-audio:$B/t-suit.wav"
    T="$(pcm "$B/t-suit.wav" "$B/ligne.wav")"
    echo "       témoin (D615), case cochée : contre la ligne de temps — $T"
    verdict "(c) témoin (D615), case cochée : la ligne de temps ($T)" "$([ "$T" = identique ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "EXPORT-ORDRE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "EXPORT-ORDRE : la case cochée, l'export rend l'ordre qu'on entend, et le projet garde son matériau"
