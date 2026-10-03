#!/usr/bin/env bash
# D535.3 — COPIER UNE PLAGE SUR TOUTES LES PISTES, LA COLLER EN INSÉRANT — MESURÉ PAR L'EXPORT MIDI.
#
# LA RÈGLE GARDÉE. Deux pistes : « Clips » (canal 1, un clip identité par mesure — ceux que pose
# l'ouverture d'un MIDI, D333) et « Sans clip » (canal 2). Une note par mesure sur chacune. Les
# locateurs sur la mesure 2, la tête à la mesure 3.
#   (1) « Coller la plage… » est GRISÉ tant que rien n'est copié (le journal le dit) ;
#   (2) copier, coller : le .mid exporté, relu en couples canal:tick:hauteur, fait entendre la
#       mesure 2 deux fois et le reste une mesure plus tard, sur les deux pistes ;
#   (3) Ctrl+Z : le .mid d'avant, au multiensemble près ;
#   (4) couper la mesure 2 : ce qui suivait recule d'une mesure ;
#   (5) le journal dit ce qui a été copié et collé (`VSM_PLAGE`).
#
#   tools/copier-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-plage.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/deux/midi"
python3 - "$brouillon/deux" <<'PY'
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
data = b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480)
data += piste("Clips", 0, [60, 62, 64], tempo=True)
data += piste("Sans clip", 1, [48, 50, 52])
open(f"{d}/midi/arrangement.mid", "wb").write(data)
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
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/deux" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_POSITION=3 \
        VSM_DELAI=4500 VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « canal:tick:hauteur » de chaque note d'un .mid, triées — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
i, jouees = 14, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st, t = 0, 0, 0
    while j < len(corps):
        dt = 0
        while corps[j] & 0x80: dt = (dt << 7) | (corps[j] & 0x7F); j += 1
        dt = (dt << 7) | corps[j]; j += 1; t += dt
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: jouees.append((st & 0x0F, t, corps[j]))
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(" ".join(f"{c + 1}:{t}:{h}" for c, t, h in sorted(jouees)) or "AUCUNE")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
AVANT="1:0:60 1:1920:62 1:3840:64 2:0:48 2:1920:50 2:3840:52"

echo "=== D535.3 : copier une plage sur toutes les pistes, la coller en insérant ==="
course coller "1200:menu:Coller la plage à la tête de lecture (en insérant);\
1500:menu:Copier entre les locateurs (toutes les pistes);1800:menu:Coller la plage à la tête de lecture (en insérant);\
2200:exporter-midi:$brouillon/colle.mid;2600:touche:ctrl + Z;3000:exporter-midi:$brouillon/annule.mid"
grep "^VSM_PLAGE\|grisée" "$brouillon/coller.txt" | sed 's/^/       /'
c="$(notes "$brouillon/colle.mid")"; u="$(notes "$brouillon/annule.mid")"
echo "       collé  : $c"
echo "       Ctrl+Z : $u"
verdict "(1) « Coller » grisé avant toute copie" \
    "$(grep -q "VSM_MENU : « Coller la plage à la tête de lecture (en insérant) » est grisée" "$brouillon/coller.txt" && echo 1 || echo 0)"
verdict "(2) la mesure 2 deux fois, le reste une mesure plus tard, sur les deux pistes" \
    "$([ "$c" = "1:0:60 1:1920:62 1:3840:62 1:5760:64 2:0:48 2:1920:50 2:3840:50 2:5760:52" ] && echo 1 || echo 0)"
verdict "(3) Ctrl+Z rend le .mid d'avant" "$([ "$u" = "$AVANT" ] && echo 1 || echo 0)"
verdict "(5) le journal : copiée [1920, 3840), collée au tick 3840 sur 2 pistes" \
    "$(grep -q "^VSM_PLAGE : copiée \[1920, 3840) — 2 piste(s)" "$brouillon/coller.txt" \
       && grep -q "^VSM_PLAGE : collée au tick 3840 — 2 piste(s)" "$brouillon/coller.txt" && echo 1 || echo 0)"

course couper "1200:menu:Couper entre les locateurs (toutes les pistes);1600:exporter-midi:$brouillon/coupe.mid"
k="$(notes "$brouillon/coupe.mid")"
echo "       coupé  : $k"
verdict "(4) couper la mesure 2 : ce qui suivait recule d'une mesure" \
    "$([ "$k" = "1:0:60 1:1920:64 2:0:48 2:1920:52" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PLAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PLAGE : la plage se copie, se colle en insérant, se coupe et s'annule"
