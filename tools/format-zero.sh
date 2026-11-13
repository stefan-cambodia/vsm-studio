#!/usr/bin/env bash
# D643 — UN FICHIER MIDI DE FORMAT 0, OUVERT PUIS RÉÉCRIT. Mesure de l'hypothèse écrite avant elle (ROADMAP-daw, D643) :
#   (H1) exporté (*Exporter MIDI…*) : quel en-tête (format, nombre de pistes) ?
#   (H2) enregistré comme projet : l'en-tête de son `midi/arrangement.mid` ; rouvert : ses pistes (noms, canaux, notes) ;
#   (H3) le témoin : le même contenu en format 1.
# Le premier tour a MESURÉ (H1 tient, H2 sans perte) ; devenu garde, il juge l'attendu du remède (ROADMAP-daw, D643) :
#   (a) la source de format 0, exportée : format 0, 1 piste, « 1:60 10:36 1:64 1:67 10:38 » ;
#   (b) son projet : `midi/arrangement.mid` de format 1 (2 pistes), « midiFormat »: 0 au project.json ;
#   (c) rouvert, réexporté : format 0, 1 piste, les mêmes notes ; (e) l'export entre les locateurs aussi ;
#   (d) la source de format 1 : format 1 partout, pas de clé « midiFormat » ;
#   (f) le témoin (le binaire d'avant D643) : l'export du format 0 en « format 0, 2 pistes » — le défaut.
#
#   tools/format-zero.sh [binaire] [témoin d'avant D643] [témoin d'avant D644]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D641/Vintage Synth MIDI Studio}"      # (f) : le binaire d'avant D643
TEMOIN644="${3:-./build/temoin-D643/Vintage Synth MIDI Studio}"   # (i) : le binaire d'avant D644
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-format-zero.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp -r "$brouillon"/*.txt "$brouillon"/*.mid "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

# Deux fichiers, le même contenu : une mélodie au canal 1 (60, 64, 67) et une batterie au canal 10 (36, 38).
python3 - "$brouillon" <<'PY'
import struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(evts, nom=b""):
    corps = b"\x00\xff\x51\x03\x07\xa1\x20" if nom == b"tempo" else b""
    if nom and nom != b"tempo": corps += b"\x00\xff\x03" + bytes([len(nom)]) + nom
    t = 0
    for tick, e in sorted(evts, key=lambda x: (x[0], x[1][0] & 0xF0 == 0x90)):
        corps += vlq(tick - t) + e; t = tick
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
melodie = []
for i, h in enumerate([60, 64, 67]):
    melodie += [(480 * i, bytes([0x90, h, 100])), (480 * i + 400, bytes([0x80, h, 0]))]
batterie = []
for i, h in enumerate([36, 38]):
    batterie += [(960 * i, bytes([0x99, h, 110])), (960 * i + 120, bytes([0x89, h, 0]))]
# Format 0 : UNE piste, tempo compris.
seule = piste(melodie + batterie + [(0, b"\xff\x51\x03\x07\xa1\x20")])
open(f"{b}/zero.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + seule)
# Format 1 : le tempo, puis les deux parties.
un = piste([], b"tempo") + piste(melodie, b"Melodie") + piste(batterie, b"Batterie")
open(f"{b}/un.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + un)
PY

entete() {   # « format F, N piste(s), M note(s) par piste »
    python3 - "$1" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
fmt, n = int.from_bytes(d[8:10], "big"), int.from_bytes(d[10:12], "big")
i, notes = 14, []
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; statut = 0; c = []
    while j < fin:
        while d[j] & 0x80: j += 1
        j += 1
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); m = 0
            while True:
                x = d[k]; k += 1; m = (m << 7) | (x & 0x7F)
                if not x & 0x80: break
            j = k + m; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        if statut & 0xF0 == 0x90 and d[j + 1] > 0: c.append(f"{(statut & 0x0F) + 1}:{d[j]}")
        j += taille
    notes.append("[" + " ".join(c) + "]"); i = fin
print(f"format {fmt}, {n} piste(s) déclarée(s), {len(notes)} écrite(s) ; notes par piste (canal:hauteur) : {' '.join(notes)}")
PY2
}
pistes() {   # les pistes d'un projet enregistré : nom, canal, machine
    python3 -c "import json, sys
t = json.load(open(sys.argv[1] + '/project.json'))['tracks']
print(' | '.join(f\"{x.get('name')} (canal {x.get('channel', 0) + 1}, {x.get('instrument', {}).get('preferredPlugin', '-')})\" for x in t))" "$1" 2>/dev/null || echo ABSENT
}
course() {   # $1 = nom ; $2 = gestes ; le reste : variables. Un HOME neuf. Le binaire : BINAIRE, ou celui du banc.
    local nom="$1" gestes="$2"; shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_DELAI=4500 \
        VSM_GESTE_APRES="$gestes" "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "${BINAIRE:-$BIN}" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|refus|inconnu|ATTENTION)" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /"
}

echo "=== D643 : un fichier MIDI de format 0, ouvert puis réécrit (mesure) ==="
for src in zero un; do
    course "$src" "800:ouvrir-midi:$brouillon/$src.mid;1800:exporter-midi:$brouillon/$src-export.mid;2400:enregistrer:$brouillon/$src-projet"
    course "$src-rouvert" "1500:enregistrer:$brouillon/$src-projet2" VSM_PROJET="$brouillon/$src-projet"
    echo "--- source $src.mid : $(entete "$brouillon/$src.mid")"
    echo "    (H1) l'export        : $(entete "$brouillon/$src-export.mid")"
    echo "    (H2) le projet       : $(entete "$brouillon/$src-projet/midi/arrangement.mid")"
    echo "         ses pistes      : $(pistes "$brouillon/$src-projet")"
    echo "         rouvert, réenregistré : $(entete "$brouillon/$src-projet2/midi/arrangement.mid")"
    echo "         ses pistes      : $(pistes "$brouillon/$src-projet2")"
done

# (c), (e) : le projet né du format 0, des locateurs posés (0 → 1920, tout le morceau), rouvert, réexporté.
python3 - "$brouillon/zero-projet/project.json" <<'PY3'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d.setdefault("transport", {})["loop"] = {"enabled": False, "startTick": 0, "endTick": 1920}
json.dump(d, open(p, "w"), indent=1)
PY3
course rouvert-export "1500:exporter-midi:$brouillon/rouvert.mid;2000:exporter-midi-locateurs:$brouillon/rouvert-boucle.mid" VSM_PROJET="$brouillon/zero-projet"
cle() { python3 -c "import json, sys; print(json.load(open(sys.argv[1] + '/project.json')).get('midiFormat', 'absente'))" "$1" 2>/dev/null || echo ABSENT; }
echo "    (c) rouvert, réexporté  : $(entete "$brouillon/rouvert.mid")"
echo "    (e) entre les locateurs : $(entete "$brouillon/rouvert-boucle.mid")"
echo "    clé midiFormat : source 0 → $(cle "$brouillon/zero-projet") ; source 1 → $(cle "$brouillon/un-projet")"

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
ZERO="format 0, 1 piste(s) déclarée(s), 1 écrite(s) ; notes par piste (canal:hauteur) : [1:60 10:36 1:64 1:67 10:38]"
UN="format 1, 2 piste(s) déclarée(s), 2 écrite(s) ; notes par piste (canal:hauteur) : [1:60 1:64 1:67] [10:36 10:38]"
verdict "(a) la source de format 0, exportée : un vrai format 0, une piste, les notes dans l'ordre du temps" "$( [ "$(entete "$brouillon/zero-export.mid")" = "$ZERO" ] && echo 1 || echo 0)"
verdict "(b) son projet : un fichier interne de format 1, et midiFormat 0 au project.json" \
    "$( [ "$(entete "$brouillon/zero-projet/midi/arrangement.mid")" = "$UN" ] && [ "$(cle "$brouillon/zero-projet")" = 0 ] && echo 1 || echo 0)"
verdict "(c) rouvert puis réexporté : de nouveau un format 0, une piste" "$( [ "$(entete "$brouillon/rouvert.mid")" = "$ZERO" ] && echo 1 || echo 0)"
verdict "(d) la source de format 1 : format 1 partout, pas de clé midiFormat" \
    "$( [ "$(entete "$brouillon/un-export.mid")" = "$UN" ] && [ "$(entete "$brouillon/un-projet/midi/arrangement.mid")" = "$UN" ] && [ "$(cle "$brouillon/un-projet")" = absente ] && echo 1 || echo 0)"
verdict "(e) l'export entre les locateurs d'un projet né du format 0 : un format 0, une piste" "$( [ "$(entete "$brouillon/rouvert-boucle.mid")" = "$ZERO" ] && echo 1 || echo 0)"
# D644 : l'export au format 0 SUR DEMANDE, sur la source de format 1 ; ce qu'il ne porte pas, dit.
course un-zero "800:ouvrir-midi:$brouillon/un.mid;1800:exporter-midi-0:$brouillon/un-zero.mid"
course un-muet "800:ouvrir-midi:$brouillon/un.mid;1300:touche:pianoroll:ctrl + A;1600:cliquer:Muet;2100:exporter-midi-0:$brouillon/un-muet.mid"
course en "800:ouvrir-midi:$brouillon/un.mid;1800:menu:File > Export MIDI as type 0 (single track)..." VSM_LANGUE=en VSM_FICHIER="$brouillon/en.mid"
echo "    (g) format 1 exporté au format 0 : $(entete "$brouillon/un-zero.mid")"
echo "        $(grep -h "^Export MIDI — format 0" "$brouillon/un-zero.txt" | head -1)"
echo "    (h) trois notes muettes        : $(grep -h "^Export MIDI — format 0" "$brouillon/un-muet.txt" | head -1) ; $(grep -h "^VSM_BOITE" "$brouillon/un-muet.txt" | grep -o "Ce que le format 0 ne porte pas" | head -1)"
echo "    (j) en anglais                 : $(entete "$brouillon/en.mid")"
verdict "(g) la source de format 1, « au format 0 » : format 0, une piste, les cinq notes" "$( [ "$(entete "$brouillon/un-zero.mid")" = "$ZERO" ] && echo 1 || echo 0)"
verdict "(g) rien à dire, rien de dit (aucune boîte)" "$(grep -q "Ce que le format 0 ne porte pas" "$brouillon/un-zero.txt" && echo 0 || echo 1)"
verdict "(h) trois notes muettes : dites au journal et dans une boîte" \
    "$(grep -q "^Export MIDI — format 0 : une seule piste ; 3 note(s) muette(s)" "$brouillon/un-muet.txt" && grep -q "Ce que le format 0 ne porte pas" "$brouillon/un-muet.txt" && echo 1 || echo 0)"
verdict "(j) l'anglais : « Export MIDI as type 0 (single track)... »" "$( [ "$(entete "$brouillon/en.mid")" = "$ZERO" ] && echo 1 || echo 0)"
if [ -x "$TEMOIN644" ]; then
    BINAIRE="$TEMOIN644" course t-un-zero "800:ouvrir-midi:$brouillon/un.mid;1800:exporter-midi-0:$brouillon/t-un-zero.mid"
    verdict "(i) témoin (avant D644) : le verbe inconnu, rien d'écrit" \
        "$(grep -q "VSM_GESTE_APRES : exporter-midi-0:.*refus" "$brouillon/t-un-zero.txt" && [ ! -f "$brouillon/t-un-zero.mid" ] && echo 1 || echo 0)"
fi
if [ -x "$TEMOIN" ]; then
    BINAIRE="$TEMOIN" course t-zero "800:ouvrir-midi:$brouillon/zero.mid;1800:exporter-midi:$brouillon/t-zero-export.mid"
    echo "    témoin : $(entete "$brouillon/t-zero-export.mid")"
    verdict "(f) témoin (avant D643) : « format 0 » sur deux pistes — le défaut" "$(entete "$brouillon/t-zero-export.mid" | grep -q "^format 0, 2 piste" && echo 1 || echo 0)"
else
    echo "    témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
echo
if [ "$rates" -gt 0 ]; then echo "FORMAT-ZERO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FORMAT-ZERO : un format 0 se réécrit en vrai format 0, et le fichier du projet reste un format 1"
