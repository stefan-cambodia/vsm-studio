#!/usr/bin/env bash
# D633 — REDIMENSIONNER UNE FRAPPE À LA GRILLE DE SON RANG, DEPUIS LA NOTE QU'ON TIENT. Une piste de batterie dessinée en
# BARRES (Affichage ▸ « Batterie en losanges » décoché, D615) se redimensionne par ses bords :
#   D633.1 — le bord tiré s'aimante à la grille du rang de la frappe TENUE (`snapTickDuRang`) ; un rang « global » : la
#            barre, au bit près ;
#   D633.2 — l'écart se prend sur la note TENUE, plus sur la première de la sélection (`dragSnapshot_.front()`).
# Mesuré dans l'application par de VRAIS glissés lents (`glisser:pianoroll:…:lent`), le bord saisi à un millième de la
# surface à l'intérieur de la barre ; les frappes lues dans le projet enregistré ; le témoin à côté.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la barre à 1/16 : 120 ticks ; une piste « Batterie » sur la TR-909, canal 10,
# des frappes LONGUES 38@960+240 et 36@1440+240). L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D633) :
#   (a) rang 38 à 1/4, le bord droit de la 38 tiré vers ~1 530 : 38@960+480 (à la barre : 38@960+600) ;
#   (b) sans grille au rang 38, le même geste : 38@960+600 ;
#   (c) les deux choisies, sans grille, le bord droit de la 36 tiré vers ~1 790 : 38@960+360 et 36@1440+360 ;
#   (d) rang 36 à 1/4, le bord GAUCHE de la 36 tiré vers ~1 040 : 36@960+720 (à la barre : 36@1080+600) ;
#   (e) rang 38 à 1/4, les deux choisies, le bord droit de la 38 tiré vers ~1 530 : 38@960+480 et 36@1440+480 ;
#   (f) le témoin (le binaire d'avant D633) : (a) → 38@960+600 ; (c) → 38@960+840 et 36@1440+840 (le défaut D633.2).
#
#   tools/redimensionner-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D632/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-redimensionner-par-rang.XXXXXX")"
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
    evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 240, 0, bytes([0x89, h, 0]))]
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
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" "$plus" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE|CLIC_POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
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

echo "=== D633 : redimensionner une frappe à la grille de son rang, depuis la note qu'on tient ==="
BARRES="900:menu:Affichage > Batterie en losanges (canal 10)"   # décoché : les frappes en barres (D615)
course "$BIN" geometrie "$BARRES;1300:relever-losanges"
grep -q "^VSM_LOSANGE : barres" "$brouillon/geometrie.txt" || { echo "REFUS : les frappes ne sont pas dessinées en barres"; grep "^VSM_LOSANGE" "$brouillon/geometrie.txt" | head -3; exit 1; }
champ() {   # $1 = hauteur ; $2 = tick ; $3 = centre|fin → « fx fy » (le centre en y dans les deux cas)
    local l; l="$(grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1)"
    local x y; y="$(echo "$l" | sed -n 's/.* centre [0-9.]*,\([0-9.]*\) ;.*/\1/p')"
    if [ "$3" = fin ]; then x="$(echo "$l" | sed -n 's/.* ; fin \([0-9.]*\).*/\1/p')"; else x="$(echo "$l" | sed -n 's/.* centre \([0-9.]*\),.*/\1/p')"; fi
    echo "$x $y"
}
read -r D38 Y38 <<< "$(champ 38 960 centre)"; read -r F38 _ <<< "$(champ 38 960 fin)"
read -r D36 Y36 <<< "$(champ 36 1440 centre)"; read -r F36 _ <<< "$(champ 36 1440 fin)"
[ -n "${D38:-}" ] && [ -n "${F38:-}" ] && [ -n "${D36:-}" ] && [ -n "${F36:-}" ] || { echo "REFUS : la géométrie des barres n'a pas été relevée"; exit 1; }
x() { python3 -c "import sys; a, b, t = map(float, sys.argv[1:]); print(f'{a + (t - 960) * (b - a) / 480:.4f}')" "$D38" "$D36" "$1"; }
dedans() { python3 -c "import sys; print(f'{float(sys.argv[1]) + float(sys.argv[2]):.4f}')" "$1" "$2"; }
DROIT38="$(dedans "$F38" -0.001)"; DROIT36="$(dedans "$F36" -0.001)"; GAUCHE36="$(dedans "$D36" 0.001)"
TIRER38="1300:glisser:pianoroll:$DROIT38,$Y38:$(x 1530),$Y38:lent"     # le bord droit de la 38 vers ~1 530
TIRER36="1300:glisser:pianoroll:$DROIT36,$Y36:$(x 1790),$Y36:lent"     # le bord droit de la 36 vers ~1 790
GAUCHE="1300:glisser:pianoroll:$GAUCHE36,$Y36:$(x 1040),$Y36:lent"     # le bord gauche de la 36 vers ~1 040
TOUT="1100:menu:Édition > Sélection > Tout sélectionner"
fin() { echo "2400:enregistrer:$brouillon/$1-projet"; }

course "$BIN" a "700:carte-grille:38>1/4;$BARRES;$TIRER38;$(fin a)"
course "$BIN" b "$BARRES;$TIRER38;$(fin b)"
course "$BIN" c "$BARRES;$TOUT;$TIRER36;$(fin c)"
course "$BIN" d "700:carte-grille:36>1/4;$BARRES;$GAUCHE;$(fin d)"
course "$BIN" e "700:carte-grille:38>1/4;$BARRES;$TOUT;$TIRER38;$(fin e)"

for k in a b c d e; do eval "F_$k=\"\$(frappes \"\$brouillon/$k-projet\")\""; done
echo "       géométrie (barres) : 38 de $D38 à $F38 (y $Y38) ; 36 de $D36 à $F36 (y $Y36)"
for k in a b c d e; do eval "echo \"       ($k) \$F_$k\""; done

verdict "(a) rang 38 à 1/4, bord droit de la 38 vers ~1530 : 38@960+480" "$(egal "$F_a" "38@960+480 36@1440+240")"
verdict "(b) sans grille au rang 38, le même geste : 38@960+600 (la barre)" "$(egal "$F_b" "38@960+600 36@1440+240")"
verdict "(c) les deux, bord droit de la 36 vers ~1790 : 38@960+360 et 36@1440+360 (la note tenue mène)" "$(egal "$F_c" "38@960+360 36@1440+360")"
verdict "(d) rang 36 à 1/4, bord gauche de la 36 vers ~1040 : 36@960+720" "$(egal "$F_d" "36@960+720 38@960+240")"
verdict "(e) rang 38 à 1/4, les deux, bord droit de la 38 vers ~1530 : 38@960+480 et 36@1440+480" "$(egal "$F_e" "38@960+480 36@1440+480")"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a "700:carte-grille:38>1/4;$BARRES;$TIRER38;$(fin t-a)"
    course "$TEMOIN" t-c "$BARRES;$TOUT;$TIRER36;$(fin t-c)"
    TA="$(frappes "$brouillon/t-a-projet")"; TC="$(frappes "$brouillon/t-c-projet")"
    echo "       témoin : (a) $TA ; (c) $TC"
    verdict "(f) témoin (avant D633), (a) : 38@960+600 — la barre" "$(egal "$TA" "38@960+600 36@1440+240")"
    verdict "(f) témoin (avant D633), (c) : 38@960+840 et 36@1440+840 — l'écart pris sur la première" "$(egal "$TC" "38@960+840 36@1440+840")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "REDIMENSIONNER-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REDIMENSIONNER-PAR-RANG : le bord tenu s'aimante à la grille de son rang, et la note tenue mène l'écart"
