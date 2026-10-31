#!/usr/bin/env bash
# D609 — LA CARTE DE BATTERIE (la note de sortie de la *Drum Map* de Cubase), mesurée dans l'application : la carte posée
# par la fenêtre (par la même liste que la souris), son relevé, le fichier enregistré, l'export MIDI, Ctrl+Z ; l'anglais ;
# le témoin.
#
# LE JEU : une piste « Toms » sur la TR-909, canal 10, une partie de toms General MIDI — 41, 43, 45, 47, une noire
# chacun. La 909 n'a rien en 41 ni en 43 ; la carte 41 → 45, 43 → 47 les lui fait jouer.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D609, attendu (d)) :
#   la carte posée par la fenêtre (deux entrées), son relevé ; le fichier enregistré la porte ; l'export MIDI garde 41
#   et 43 et nomme « carte de batterie » ; Ctrl+Z la retire ; l'anglais ; la photo ; le témoin (D608) : l'entrée inconnue.
#
#   tools/carte-batterie.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D608/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-carte.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"
os.makedirs(f"{b}/midi", exist_ok=True)
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x04Toms"
t = 0
for i, h in enumerate((41, 43, 45, 47)):
    corps += vlq(i * 480 - t) + bytes([0x99, h, 110]) + vlq(240) + bytes([0x89, h, 0]); t = i * 480 + 240
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "carte", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Toms",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
# Les hauteurs des NoteOn d'un fichier MIDI, dans l'ordre (le multiensemble suffit : la carte ne touche pas au temps).
hauteurs_mid() {
    python3 - "$1" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
i, out = 14, []
while i < len(d):
    longueur = int.from_bytes(d[i + 4:i + 8], "big"); p, fin, statut = i + 8, i + 8 + longueur, 0
    while p < fin:
        while d[p] & 0x80: p += 1
        p += 1
        if d[p] == 0xFF: l = d[p + 2]; p += 3 + l; continue
        if d[p] & 0x80: statut = d[p]; p += 1
        if statut & 0xF0 == 0x90 and d[p + 1] > 0: out.append(d[p])
        p += 2 if statut & 0xF0 in (0x80, 0x90, 0xA0, 0xB0, 0xE0) else 1
    i = fin
print(",".join(str(h) for h in sorted(out)))
PY
}

echo "=== D609 : la carte de batterie ==="
POSER="1200:carte:41>45;1400:carte:43>47;1600:relever-carte"
course "$BIN" pose "$POSER;1800:enregistrer:$brouillon/enregistre;2100:exporter-midi:$brouillon/export.mid"
course "$BIN" annule "1200:carte:41>45;1500:touche:ctrl + Z;1900:relever-carte"
course "$BIN" en "$POSER" VSM_LANGUE=en

grep "^VSM_CARTE" "$brouillon/pose.txt" | sed 's/^/       /'
L41="$(grep "^VSM_CARTE : 41 " "$brouillon/pose.txt" | tail -1)"; L43="$(grep "^VSM_CARTE : 43 " "$brouillon/pose.txt" | tail -1)"
verdict "(d) la fenêtre : 41 → tom grave (45), 43 → tom médium (47)" \
    "$(echo "$L41" | grep -q "→ tom grave (45)$" && echo "$L43" | grep -q "→ tom médium (47)$" && echo 1 || echo 0)"
verdict "(d) 45 et 47 se jouent eux-mêmes" \
    "$(grep -q "^VSM_CARTE : 45 .*→ elle-même$" "$brouillon/pose.txt" && grep -q "^VSM_CARTE : 47 .*→ elle-même$" "$brouillon/pose.txt" && echo 1 || echo 0)"
J="$brouillon/enregistre/project.json"
CARTE="$( [ -s "$J" ] && python3 -c "import json,sys; t=json.load(open(sys.argv[1]))['tracks'][0]; print(sorted((e['in'], e['out']) for e in t.get('drumMap', [])))" "$J")"
echo "       au fichier : $CARTE"
verdict "(d) le fichier enregistré porte la carte [(41, 45), (43, 47)]" "$([ "$CARTE" = "[(41, 45), (43, 47)]" ] && echo 1 || echo 0)"
H="$( [ -s "$brouillon/export.mid" ] && hauteurs_mid "$brouillon/export.mid")"
echo "       export MIDI : $H"
verdict "(d) l'export MIDI garde les notes ÉCRITES (41, 43, 45, 47)" "$([ "$H" = "41,43,45,47" ] && echo 1 || echo 0)"
verdict "(d) l'export nomme la carte (« carte de batterie (2 note(s) déplacée(s)) »)" \
    "$(grep -q "carte de batterie (2 note(s) déplacée(s))" "$brouillon/pose.txt" && echo 1 || echo 0)"
A="$(grep "^VSM_CARTE : piste" "$brouillon/annule.txt" | tail -1)"; A41="$(grep "^VSM_CARTE : 41 " "$brouillon/annule.txt" | tail -1)"
echo "       Ctrl+Z : $A ; $A41"
verdict "(d) Ctrl+Z retire l'entrée : 0 note déplacée, 41 → elle-même" \
    "$(echo "$A" | grep -q " ; 0 note(s) déplacée(s)$" && echo "$A41" | grep -q "→ elle-même$" && echo 1 || echo 0)"
E="$(grep "^VSM_CARTE : 41 " "$brouillon/en.txt" | tail -1)"; E45="$(grep "^VSM_CARTE : 45 " "$brouillon/en.txt" | tail -1)"
echo "       en anglais : $E ; $E45"
verdict "(d) en anglais : la sortie nommée en anglais, « itself »" \
    "$(echo "$E" | grep -q "(45)$" && ! echo "$E" | grep -q "tom grave" && echo "$E45" | grep -q "→ itself$" && echo 1 || echo 0)"
PHOTO="$(ls "$brouillon"/pose-*-Carte*.png 2>/dev/null | head -1)"
verdict "(d) la fenêtre photographiée ($( [ -n "$PHOTO" ] && basename "$PHOTO"))" "$([ -n "$PHOTO" ] && [ -s "$PHOTO" ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-pose "$POSER"
    tl="$(grep -c "^VSM_CARTE" "$brouillon/t-pose.txt")"; ti="$(grep -c "geste inconnu" "$brouillon/t-pose.txt")"
    echo "       témoin : $tl relevé(s) de carte ; $ti « geste inconnu »"
    verdict "(d) témoin (D608) : aucune carte, les verbes inconnus" "$([ "$tl" = 0 ] && [ "$ti" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "CARTE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CARTE : la note écrite se joue à sa note de sortie, le matériau ne bouge pas"
