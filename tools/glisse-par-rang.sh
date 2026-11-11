#!/usr/bin/env bash
# D627 — LE GLISSÉ D'UNE BATTERIE S'AIMANTE À LA GRILLE DU RANG QU'ON SAISIT (l'aimant de l'éditeur de batterie de
# Cubase : quand on déplace des notes, la valeur de quantification de la pièce sur laquelle on a CLIQUÉ mène), mesuré
# dans l'application par de VRAIS glissés (`glisser:pianoroll:…:lent`, partis du centre du losange) ; les frappes lues
# dans le projet enregistré ; le témoin à côté.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la barre à 1/16 : 120 ticks ; une piste « Batterie » sur la TR-909, canal
# 10, des frappes 38@960 et 36@1440 ; le rang 38 réglé à 1/4 par `carte-grille:38>1/4`). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D627) — chaque glissé fait ~350 ticks, et chaque arrivée tombe à 50 ticks au moins d'un bord d'arrondi :
#   (a) la frappe 36 (rang global) tirée → 36@1800 (un pas de 1/16) ;
#   (b) la frappe 38 tirée → 38@1440 (un pas de 1/4 ; à la grille de la barre, ce serait 38@1320) ;
#   (c) les deux choisies (Ctrl+A) : la 38 saisie → 38@1440 et 36@1920 ; la 36 saisie → 38@1320 et 36@1800 ;
#   (d) le témoin (le binaire d'avant D627, les mêmes gestes) : (b) → 38@1320 — le défaut ;
#   (e) sans grille au rang 38 : (b) → 38@1320 (une piste sans grilles par rang ne change pas).
#
#   tools/glisse-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D625/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-glisse-par-rang.XXXXXX")"
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
for debut, h in [(960, 38), (1440, 36)]:
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
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes. Une copie neuve du projet, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
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

echo "=== D627 : le glissé d'une batterie s'aimante à la grille du rang qu'on saisit ==="
course "$BIN" geometrie "1200:relever-losanges"
centre() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1 \2/p'; }
read -r X960 Y38 <<< "$(centre 38 960)"; read -r X1440 Y36 <<< "$(centre 36 1440)"
[ -n "${X960:-}" ] && [ -n "${X1440:-}" ] || { echo "REFUS : la géométrie des frappes n'a pas été relevée"; exit 1; }
x() { python3 -c "import sys; a, b, t = map(float, sys.argv[1:]); print(f'{a + (t - 960) * (b - a) / 480:.4f}')" "$X960" "$X1440" "$1"; }
X1310="$(x 1310)"; X1790="$(x 1790)"
TIRER38="glisser:pianoroll:$X960,$Y38:$X1310,$Y38:lent"     # la 38 saisie au centre de son losange, tirée de 350
TIRER36="glisser:pianoroll:$X1440,$Y36:$X1790,$Y36:lent"    # la 36, de même
GRILLE="700:carte-grille:38>1/4"
TOUT="1000:menu:Édition > Sélection > Tout sélectionner"   # le geste de `grille-par-rang`

course "$BIN" a  "$GRILLE;1300:$TIRER36;2400:enregistrer:$brouillon/a-projet"
course "$BIN" b  "$GRILLE;1300:$TIRER38;2400:enregistrer:$brouillon/b-projet"
course "$BIN" c1 "$GRILLE;$TOUT;1300:$TIRER38;2400:enregistrer:$brouillon/c1-projet"
course "$BIN" c2 "$GRILLE;$TOUT;1300:$TIRER36;2400:enregistrer:$brouillon/c2-projet"
course "$BIN" e  "1300:$TIRER38;2400:enregistrer:$brouillon/e-projet"

A="$(frappes "$brouillon/a-projet")"; B="$(frappes "$brouillon/b-projet")"
C1="$(frappes "$brouillon/c1-projet")"; C2="$(frappes "$brouillon/c2-projet")"; E="$(frappes "$brouillon/e-projet")"
echo "       géométrie : 38@960 en $X960,$Y38 ; 36@1440 en $X1440,$Y36"
echo "       (a) $A"; echo "       (b) $B"; echo "       (c) la 38 saisie : $C1 ; la 36 saisie : $C2"; echo "       (e) $E"
grep -h "^VSM_GLISSE : " "$brouillon/b.txt" | head -1 | sed 's/^/       /'

verdict "(a) la 36 (rang global) tirée : 36@1800 (un pas de 1/16)" "$(egal "$A" "38@960+120 36@1800+120")"
verdict "(b) la 38 (rang à 1/4) tirée : 38@1440 (un pas de 1/4)" "$(egal "$B" "36@1440+120 38@1440+120")"
verdict "(c) les deux choisies, la 38 saisie : 38@1440 et 36@1920" "$(egal "$C1" "38@1440+120 36@1920+120")"
verdict "(c) les deux choisies, la 36 saisie : 38@1320 et 36@1800" "$(egal "$C2" "38@1320+120 36@1800+120")"
verdict "(e) sans grille au rang 38 : 38@1320 (rien ne change)" "$(egal "$E" "38@1320+120 36@1440+120")"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-b "$GRILLE;1300:$TIRER38;2400:enregistrer:$brouillon/t-b-projet"
    T="$(frappes "$brouillon/t-b-projet")"
    echo "       témoin : $T"
    verdict "(d) témoin (avant D627), la 38 tirée : 38@1320 — le défaut" "$(egal "$T" "38@1320+120 36@1440+120")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "GLISSE-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "GLISSE-PAR-RANG : la frappe saisie mène, toute la sélection suit son pas"
