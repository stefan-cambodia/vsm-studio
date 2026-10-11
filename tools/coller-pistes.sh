#!/usr/bin/env bash
# D551.1 — COLLER SUR PLUSIEURS PISTES (H14) ET COLLER UN CLIP MIDI AILLEURS (H15), MESURÉ PAR LE PROJET RELU ET PAR
# LE .mid DE CE QUI EST JOUÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4). Projet P : « Haut », une note la3 (57) à 0, clip A [0, 1920) ;
# « Bas », une note mi4 (64) à 0, clip C [0, 1920). La tête à 3 · 1 (3 840).
#   (1) H14 — tout choisi, Ctrl+C, la piste courante « Haut », Ctrl+V : chaque clip recollé sur SA piste relative
#       — A' sur « Haut » à 3 840, C' sur « Bas » à 3 840 (Cubase), et non les deux sur « Haut » ;
#   (2) H15 — le clip A seul, Ctrl+C, la piste courante « Bas », Ctrl+V : ce qui JOUE sur « Bas » à partir de 3 840
#       est la note de A (57), pas la matière de « Bas » (64) ;
#   (3) un pas : (1) puis Ctrl+Z → les deux clips d'origine, rien d'autre ;
#   (4) D551.2 — tout choisi, Ctrl+C, « Coller à l'origine » : chaque clip recollé sur SA piste, à SA place (0) ;
#   (5) tout choisi, Ctrl+C, la piste courante « Bas », Ctrl+V : le clip de « Haut » va sur « Bas », celui de « Bas »
#       n'a pas de piste sous elle — posé 1, et « 1 sans piste où se poser » DIT.
#
#   tools/coller-pistes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-coller-pistes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, hauteur, notes, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90 | canal, hauteur, 100]); corps += vlq(1440) + bytes([0x80 | canal, hauteur, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/P/midi", exist_ok=True)
open(f"{b}/P/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Haut", 0, 57, [0], tempo=True) + piste("Bas", 1, 64, [0]))
# UNE note par piste, DANS son clip : une note hors de tout clip borné étend le clip le plus proche (D337), et la
# mesure de H15 lirait alors autre chose que la fenêtre collée.
clip = lambda n: {"start": 0, "length": 1920, "sourceStart": 0, "sourceLength": 1920, "name": n}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
entree = lambda n, c, k: {"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                          "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip(k)]}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Haut", 0, "A"), entree("Bas", 1, "C")]}, open(f"{b}/P/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « piste:début » des clips de chaque piste, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + ':' + ','.join(str(int(c.get('start', 0))) for c in sorted(t.get('clips', []), key=lambda c: c.get('start', 0))) for t in p['tracks']))
" "$1"
}
joue() {   # les hauteurs jouées sur la piste « $2 » à partir du tick $3, d'après un .mid exporté, ou « ABSENT »
    "$PY" - "$1" "$2" "$3" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
for tr in m.tracks:
    nom = next((x.name for x in tr if x.type == "track_name"), "")
    if nom != sys.argv[2]:
        continue
    t, h = 0, []
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0 and t >= int(sys.argv[3]):
            h.append(x.note)
    print(",".join(str(n) for n in h) or "RIEN"); sys.exit(0)
print("PISTE-ABSENTE")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D551.1 : coller sur plusieurs pistes, coller un clip MIDI ailleurs ==="
for x in Pm Pa Pz; do cp -r "$brouillon/P" "$brouillon/$x"; done
course multi "$brouillon/Pm" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,tout-choisir,piste:0" VSM_POSITION=3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + C;1500:touche:arrangement:ctrl + V;2000:enregistrer:$brouillon/Pm"
course ailleurs "$brouillon/Pa" VSM_DELAI=3200 VSM_VUE="sans-rapport,arrangement,premier-clip:0,piste:1" VSM_POSITION=3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + C;1500:touche:arrangement:ctrl + V;2000:enregistrer:$brouillon/Pa;2300:exporter-midi:$brouillon/Pa.mid"
for x in Po Pd; do cp -r "$brouillon/P" "$brouillon/$x"; done
course origine "$brouillon/Po" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,tout-choisir,piste:1" VSM_POSITION=3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + C;1500:menu:Coller à l'origine;2000:enregistrer:$brouillon/Po"
course deborde "$brouillon/Pd" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,tout-choisir,piste:1" VSM_POSITION=3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + C;1500:touche:arrangement:ctrl + V;2000:enregistrer:$brouillon/Pd"
course annuler "$brouillon/Pz" VSM_DELAI=3200 VSM_VUE="sans-rapport,arrangement,tout-choisir,piste:0" VSM_POSITION=3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + C;1500:touche:arrangement:ctrl + V;1900:touche:ctrl + Z;2400:enregistrer:$brouillon/Pz"

m="$(clips "$brouillon/Pm/project.json")"; a="$(clips "$brouillon/Pa/project.json")"; z="$(clips "$brouillon/Pz/project.json")"
j="$(joue "$brouillon/Pa.mid" Bas 3840)"; o="$(clips "$brouillon/Po/project.json")"; dd="$(clips "$brouillon/Pd/project.json")"
echo "       tout collé depuis « Haut » [$m] ; A collé sur « Bas » [$a], joué sur « Bas » dès 3 840 : $j ; Ctrl+Z [$z]"

verdict "(1) H14 : chaque clip recollé sur sa piste relative" "$([ "$m" = "Haut:0,3840 | Bas:0,3840" ] && echo 1 || echo 0)"
verdict "(2) H15 : le clip collé sur une autre piste y joue SES notes" "$([ "$j" = "57" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend les deux clips d'origine" "$([ "$z" = "Haut:0 | Bas:0" ] && echo 1 || echo 0)"
echo "       à l'origine [$o] ; débordant depuis « Bas » [$dd]"
grep -h "^VSM_COLLER\|^VSM_BOITE : Coller" "$brouillon/deborde.txt" | sed 's/^/       /'
verdict "(4) coller à l'origine : chacun sur sa piste, à sa place" "$([ "$o" = "Haut:0,0 | Bas:0,0" ] && echo 1 || echo 0)"
verdict "(5) ce qui déborde sous la dernière piste n'est pas posé, et c'est dit" \
    "$([ "$dd" = "Haut:0 | Bas:0,3840" ] && grep -q "^VSM_BOITE : Coller : 1 clip(s) posé(s) ; 0 refusé(s)" "$brouillon/deborde.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COLLER-PISTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COLLER-PISTES : chaque clip se recolle sur sa piste, et un clip MIDI emporte ses notes"
