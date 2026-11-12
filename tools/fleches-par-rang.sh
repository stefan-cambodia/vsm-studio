#!/usr/bin/env bash
# D632 — LES FLÈCHES D'UNE BATTERIE AVANCENT AU PAS DE SES RANGS : sur une piste qui a des grilles par rang (D622), ← et →
# déplacent la sélection du plus petit pas COMMUN aux grilles des rangs choisis (un rang « global » compte pour la grille
# de la barre) — le seul pas qui garde chaque frappe sur la grille de son rang ; aucun rang propre : la barre, au tick
# près ; un pas commun plus long qu'une mesure : la barre, et la ligne d'état le DIT. Mesuré dans l'application (une
# frappe choisie par un clic sur son losange, les deux par « Tout sélectionner », la flèche par
# `touche:pianoroll:cursor right`), les frappes lues dans le projet enregistré, le témoin à côté.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la barre à 1/16 : 120 ticks ; une piste « Batterie » sur la TR-909, canal 10,
# des frappes 38@960 et 36@1440). L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D632) :
#   (a) rang 38 à 1/4, la 38 seule, → : 38@1440 (à la grille de la barre : 38@1080) ;
#   (b) rang 38 à 1/4, la 36 seule (rang global), → : 36@1560 ;
#   (c) rang 38 à 1/4, les deux, → : 38@1440 et 36@1920 (PPCM de 480 et 120) ;
#   (d) rang 38 à 1/4T, rang 36 à 1/8, les deux, → : 38@1920 et 36@2400 (PPCM de 320 et 240 : 960) ;
#   (e) rang 38 à 1/4, la 38 seule, Maj+→ : 38@2880 (quatre pas) ;
#   (f) rang 38 à 1/4., rang 36 à 1/4T, les deux, → : 38@1080 et 36@1560 (PPCM 2 880 > une mesure : la barre), la phrase
#       dite par `relever-etat` ;
#   (g) sans grille au rang 38, la 38 seule, → : 38@1080 (rien ne change) ;
#   (h) le témoin (le binaire d'avant D632) : (a) → 38@1080 — le défaut ;
#   (i) l'anglais : (f) dit sa phrase en anglais.
#
#   tools/fleches-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D630/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fleches-par-rang.XXXXXX")"
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

echo "=== D632 : les flèches d'une batterie avancent au pas de ses rangs ==="
course "$BIN" geometrie "1200:relever-losanges"
centre() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1,\2/p'; }
C38="$(centre 38 960)"; C36="$(centre 36 1440)"
[ -n "$C38" ] && [ -n "$C36" ] || { echo "REFUS : la géométrie des frappes n'a pas été relevée"; exit 1; }
LA38="1000:clic:pianoroll:$C38"; LA36="1000:clic:pianoroll:$C36"
TOUT="1000:menu:Édition > Sélection > Tout sélectionner"
TOUT_EN="1000:touche:pianoroll:ctrl + A"   # « Tout sélectionner » par sa touche : le menu change de nom en anglais
DROITE="1400:touche:pianoroll:cursor right"; MAJ_DROITE="1400:touche:pianoroll:shift + cursor right"
g() { local r=""; for x in "$@"; do r="$r${r:+;}700:carte-grille:$x"; done; echo "$r"; }
fin() { echo "1800:relever-etat;2200:enregistrer:$brouillon/$1-projet"; }

course "$BIN" a "$(g '38>1/4');$LA38;$DROITE;$(fin a)"
course "$BIN" b "$(g '38>1/4');$LA36;$DROITE;$(fin b)"
course "$BIN" c "$(g '38>1/4');$TOUT;$DROITE;$(fin c)"
course "$BIN" d "$(g '38>1/4T' '36>1/8');$TOUT;$DROITE;$(fin d)"
course "$BIN" e "$(g '38>1/4');$LA38;$MAJ_DROITE;$(fin e)"
course "$BIN" f "$(g '38>1/4.' '36>1/4T');$TOUT;$DROITE;$(fin f)"
course "$BIN" g "$LA38;$DROITE;$(fin g)"
course "$BIN" en "$(g '38>1/4.' '36>1/4T');$TOUT_EN;$DROITE;$(fin en)" VSM_LANGUE=en

for k in a b c d e f g en; do eval "F_$k=\"\$(frappes \"\$brouillon/$k-projet\")\""; done
etat() { grep -h "^VSM_ETAT_PIANOROLL : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_ETAT_PIANOROLL : //'; }
echo "       géométrie : 38@960 en $C38 ; 36@1440 en $C36"
for k in a b c d e f g en; do eval "echo \"       ($k) \$F_$k\""; done
echo "       (f) état : $(etat f)"; echo "       (en) état : $(etat en)"
grep -h "^VSM_CLIC_POSITION : " "$brouillon/a.txt" | head -1 | sed 's/^/       /'

verdict "(a) rang 38 à 1/4, la 38 seule, → : 38@1440" "$(egal "$F_a" "36@1440+120 38@1440+120")"
verdict "(b) rang 38 à 1/4, la 36 seule (rang global), → : 36@1560" "$(egal "$F_b" "38@960+120 36@1560+120")"
verdict "(c) rang 38 à 1/4, les deux, → : 38@1440 et 36@1920 (PPCM 480)" "$(egal "$F_c" "38@1440+120 36@1920+120")"
verdict "(d) 38 à 1/4T et 36 à 1/8, les deux, → : 38@1920 et 36@2400 (PPCM 960)" "$(egal "$F_d" "38@1920+120 36@2400+120")"
verdict "(e) rang 38 à 1/4, la 38 seule, Maj+→ : 38@2880 (quatre pas)" "$(egal "$F_e" "36@1440+120 38@2880+120")"
verdict "(f) 38 à 1/4. et 36 à 1/4T, les deux, → : 38@1080 et 36@1560 (au-delà de la mesure : la barre)" "$(egal "$F_f" "38@1080+120 36@1560+120")"
verdict "(f) la ligne d'état le dit" "$(etat f | grep -q "ne se rencontrent pas dans la mesure" && echo 1 || echo 0)"
verdict "(g) sans grille au rang 38, la 38 seule, → : 38@1080 (rien ne change)" "$(egal "$F_g" "38@1080+120 36@1440+120")"
verdict "(i) l'anglais : les frappes de (f), et la phrase" "$( [ "$F_en" = "$F_f" ] && etat en | grep -q "do not meet within the bar" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a "$(g '38>1/4');$LA38;$DROITE;$(fin t-a)"
    T="$(frappes "$brouillon/t-a-projet")"
    echo "       témoin : $T"
    verdict "(h) témoin (avant D632), (a) : 38@1080 — le défaut" "$(egal "$T" "38@1080+120 36@1440+120")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "FLECHES-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FLECHES-PAR-RANG : les flèches avancent au pas commun des rangs choisis"
