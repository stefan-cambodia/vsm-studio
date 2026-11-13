#!/usr/bin/env bash
# D639 — SOUS UN ORDRE DE CARTE, ↑ ET ↓ CHANGENT UNE FRAPPE DE RANGÉE (le déplacement de pièce de l'éditeur de batterie de
# Cubase) : sous un ordre (le repli qui le suit, D636.2), ↑ fait passer chaque frappe choisie à la rangée du dessus, ↓ à
# celle du dessous ; Maj n'y ajoute rien ; tout ou rien (une frappe au bord refuse le geste, et c'est dit) ; un pas. Sans
# ordre, ↑ et ↓ transposent comme avant.
#
# LES RÈGLES GARDÉES (une piste « Batterie », TR-909, canal 10 : 36@0, 38@480, 42@960 ; l'ordre 42, 36, 38 posé par deux
# `carte-monter:42` ; le piano roll replié ; une frappe choisie par un clic sur son losange). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D639) :
#   (a) ↑ sur la 38 : 36@0 36@480 42@960 ; un pas « Changer de rangée » ;
#   (b) ↓ sur la 42 : 36@0 38@480 36@960 ;
#   (c) ↑ sur la 42 (la rangée du haut) : rien ne bouge, aucun pas, la ligne d'état le dit ;
#   (d) Maj+↑ sur la 38 : comme (a) ;
#   (e) sans ordre (replié par hauteur) : ↑ sur la 38 → 39@480 ;
#   (f) le témoin (le binaire d'avant D639) : (a) → 39@480.
#
#   tools/fleches-de-rangee.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D637/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fleches-de-rangee.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
b = f"{racine}/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
evts = []
for debut, h in [(0, 36), (480, 38), (960, 42)]:   # la piste de `ordre-de-la-carte`
    evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 120, 0, bytes([0x89, h, 0]))]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
    corps += vlq(tick - t) + e; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "projet", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; $4 = une variable de plus (VSM_LANGUE=en). Une copie neuve, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" plus="${4:-VSM_RIEN=1}" n
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=4000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" "$plus" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE|CLIC_POSITION|RANGEE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les frappes d'un projet enregistré : « hauteur@début+durée », triées (le lecteur de `longueur-insertion.sh`).
frappes() {
    python3 - "$1/midi/arrangement.mid" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, ouvertes, i = [], {}, 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; statut = 0
    while j < fin:
        v = 0
        while True:
            c = d[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); n = 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        h, vel = d[j], (d[j + 1] if taille == 2 else 0)
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
egal() { [ "$1" = "$2" ] && echo 1 || echo 0; }
REPLIER="500:cliquer:Replier"
ORDRE="800:carte-monter:42;1300:carte-monter:42"
centre() { grep "^VSM_LOSANGE : note $2 tick $3 : " "$brouillon/$1.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1,\2/p'; }
pas() { grep -h "^VSM_HISTORIQUE_PAS" "$brouillon/$1.txt" | tail -1 | grep -o "Changer de rangée" | wc -l; }

echo "=== D639 : sous un ordre de carte, ↑ et ↓ changent une frappe de rangée ==="
course "$BIN" geo "$REPLIER;$ORDRE;1800:relever-losanges"
course "$BIN" geo-sans "$REPLIER;1200:relever-losanges"
C38="$(centre geo 38 480)"; C42="$(centre geo 42 960)"; S38="$(centre geo-sans 38 480)"
[ -n "$C38" ] && [ -n "$C42" ] && [ -n "$S38" ] || { echo "REFUS : la géométrie des frappes n'a pas été relevée"; exit 1; }
fin() { echo "2900:relever-historique;3100:relever-etat;3300:enregistrer:$brouillon/$1-projet"; }
course "$BIN" a "$REPLIER;$ORDRE;2000:clic:pianoroll:$C38;2400:touche:pianoroll:cursor up;$(fin a)"
course "$BIN" b "$REPLIER;$ORDRE;2000:clic:pianoroll:$C42;2400:touche:pianoroll:cursor down;$(fin b)"
course "$BIN" c "$REPLIER;$ORDRE;2000:clic:pianoroll:$C42;2400:touche:pianoroll:cursor up;$(fin c)"
course "$BIN" d "$REPLIER;$ORDRE;2000:clic:pianoroll:$C38;2400:touche:pianoroll:shift + cursor up;$(fin d)"
course "$BIN" e "$REPLIER;1600:clic:pianoroll:$S38;2000:touche:pianoroll:cursor up;$(fin e)"

for k in a b c d e; do eval "F_$k=\"\$(frappes \"\$brouillon/$k-projet\")\""; done
for k in a b c d e; do eval "echo \"       ($k) \$F_$k ; \$(pas $k) pas « Changer de rangée »\""; done
ETAT_C="$(grep -h "^VSM_ETAT_PIANOROLL : " "$brouillon/c.txt" | tail -1)"
echo "       (c) $ETAT_C"

verdict "(a) ↑ sur la 38 : 36@0 36@480 42@960, un pas" "$( [ "$F_a" = "36@0+120 36@480+120 42@960+120" ] && [ "$(pas a)" = 1 ] && echo 1 || echo 0)"
verdict "(b) ↓ sur la 42 : 36@0 38@480 36@960" "$(egal "$F_b" "36@0+120 38@480+120 36@960+120")"
verdict "(c) ↑ sur la 42 (rangée du haut) : rien ne bouge, aucun pas, et c'est dit" \
    "$( [ "$F_c" = "36@0+120 38@480+120 42@960+120" ] && [ "$(pas c)" = 0 ] && echo "$ETAT_C" | grep -q "rangée du haut" && echo 1 || echo 0)"
verdict "(d) Maj+↑ sur la 38 : comme (a)" "$(egal "$F_d" "$F_a")"
verdict "(e) sans ordre : ↑ sur la 38 → 39@480 (la transposition d'avant)" "$(egal "$F_e" "36@0+120 39@480+120 42@960+120")"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a "$REPLIER;$ORDRE;2000:clic:pianoroll:$C38;2400:touche:pianoroll:cursor up;$(fin t-a)"
    T="$(frappes "$brouillon/t-a-projet")"
    echo "       témoin : $T"
    verdict "(f) témoin (avant D639), (a) : 39@480 — la transposition" "$(egal "$T" "36@0+120 39@480+120 42@960+120")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "FLECHES-DE-RANGEE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FLECHES-DE-RANGEE : sous un ordre de carte, une frappe change de rangée, tout ou rien"
