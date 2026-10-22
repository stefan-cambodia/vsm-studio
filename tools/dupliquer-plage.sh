#!/usr/bin/env bash
# D568.1 — DUPLIQUER ENTRE LES LOCATEURS (Édition ▸ Dupliquer entre les locateurs (toutes les pistes) ; le *Duplicate
# Time* de Live), MESURÉ PAR L'EXPORT MIDI RELU ET PAR LE PROJET.
#
# LES RÈGLES GARDÉES. Le projet de `copier-plage.sh` : deux pistes, « Clips » (un clip identité par mesure) et « Sans
# clip », une note par mesure (60 62 64 et 48 50 52) ; les locateurs sur la mesure 2 [1 920, 3 840).
#   (1) dupliquer : la mesure 2 s'entend deux fois, le reste une mesure plus tard, sur les deux pistes — et les
#       locateurs passent sur la copie, [3 840, 5 760) ;
#   (2) le presse-papiers n'est pas pris : « Coller la plage… » reste grisée ;
#   (3) dupliquer deux fois : la mesure 2 trois fois (la seconde duplique la copie, que les locateurs désignent) ;
#   (4) un pas : Ctrl+Z rend le .mid d'avant et les locateurs d'avant, après un geste qui a eu lieu.
#
#   tools/dupliquer-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-dupliquer.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, canal, hauteurs, tempo=False):
    evs = ([(0, b"\xff\x51\x03\x07\xa1\x20")] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for i, h in enumerate(hauteurs):
        debut = i * 1920
        evs += [(debut - t, bytes([0x90 | canal, h, 100])), (480, bytes([0x80 | canal, h, 0]))]
        t = debut + 480
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480)
    + piste("Clips", 0, [60, 62, 64], tempo=True) + piste("Sans clip", 1, [48, 50, 52]))
clip = lambda s: {"sourceStart": s, "sourceLength": 1920, "start": s, "length": 1920, "color": "#FF6B9BFF"}
def entree(nom, canal, **extra):
    e = {"channel": canal, "color": "#FF6B9BFF", "effects": [], "instrument": {"preferredPlugin": "vsm.minimoog"},
         "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}, "name": nom}
    e.update(extra)
    return e
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "loop": {"enabled": False, "startTick": 1920, "endTick": 3840}},
           "tracks": [entree("Clips", 0, clips=[clip(0), clip(1920), clip(3840)]), entree("Sans clip", 1)]},
          open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES
    local nom="$1"
    cp -r "$brouillon/base" "$brouillon/$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/$nom" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=3500 VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « canal:tick:hauteur » de chaque note d'un .mid, triées — ou « ABSENT »
    "$racine/analyse/.venv/bin/python" - "$1" <<'PY'
import sys
import mido
try:
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
jouees = []
for piste in m.tracks:
    t = 0
    for msg in piste:
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0: jouees.append((msg.channel, t, msg.note))
print(" ".join(f"{c + 1}:{t}:{h}" for c, t, h in sorted(jouees)) or "AUCUNE")
PY
}
boucle() { python3 -c "
import json, sys
try:
    l = json.load(open(sys.argv[1] + '/project.json'))['transport']['loop']
    print(f\"{l['startTick']}:{l['endTick']}\")
except Exception:
    print('ABSENT')" "$1"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Dupliquer entre les locateurs (toutes les pistes)"
AVANT="1:0:60 1:1920:62 1:3840:64 2:0:48 2:1920:50 2:3840:52"

echo "=== D568.1 : dupliquer entre les locateurs ==="
course une "1200:$GESTE;1600:exporter-midi:$brouillon/une.mid;1800:lister-menus;2100:enregistrer:$brouillon/une"
course deux "1200:$GESTE;1600:$GESTE;2000:exporter-midi:$brouillon/deux.mid"
course annule "1200:$GESTE;1600:touche:ctrl + Z;2000:exporter-midi:$brouillon/annule.mid;2300:enregistrer:$brouillon/annule"

u="$(notes "$brouillon/une.mid")"; d="$(notes "$brouillon/deux.mid")"; z="$(notes "$brouillon/annule.mid")"
bu="$(boucle "$brouillon/une")"; bz="$(boucle "$brouillon/annule")"
coller="$(grep -h "^VSM_MENU_LISTE : Édition > Coller la plage" "$brouillon/une.txt" | head -1)"
grep -h "^VSM_PLAGE" "$brouillon/une.txt" "$brouillon/deux.txt" | sed 's/^/       /'
echo "       une fois  : $u — locateurs $bu"
echo "       deux fois : $d"
echo "       Ctrl+Z    : $z — locateurs $bz"
echo "       ${coller:-(entrée « Coller la plage » non relevée)}"

verdict "(1) la mesure 2 deux fois, le reste une mesure plus tard ; les locateurs sur la copie" \
    "$([ "$u" = "1:0:60 1:1920:62 1:3840:62 1:5760:64 2:0:48 2:1920:50 2:3840:50 2:5760:52" ] && [ "$bu" = "3840:5760" ] && echo 1 || echo 0)"
verdict "(2) le presse-papiers n'est pas pris : « Coller la plage… » grisée" \
    "$(echo "$coller" | grep -q "\[grisée\]" && echo 1 || echo 0)"
verdict "(3) deux fois : la mesure 2 trois fois" \
    "$([ "$d" = "1:0:60 1:1920:62 1:3840:62 1:5760:62 1:7680:64 2:0:48 2:1920:50 2:3840:50 2:5760:50 2:7680:52" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend le .mid et les locateurs d'avant, après un geste qui a eu lieu" \
    "$([ "$z" = "$AVANT" ] && [ "$bz" = "1920:3840" ] && grep -q "^VSM_PLAGE : dupliquée \[1920, 3840)" "$brouillon/annule.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DUPLIQUER-PLAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DUPLIQUER-PLAGE : la plage se répète après elle-même, les locateurs sur la copie, le presse-papiers intact"
