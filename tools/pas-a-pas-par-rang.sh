#!/usr/bin/env bash
# D634 — EN SAISIE PAS À PAS, UNE FRAPPE DURE UNE CASE DE SON RANG : sur une piste qui a des grilles par rang (D622), une
# frappe saisie dans un rang à grille propre dure une case de cette grille — la durée qu'un clic lui donne —, sauf
# longueur d'insertion choisie (D625, D626) ; sa position reste la tête, et la tête avance d'UN PAS DE LA BARRE (la tête
# est un curseur que toutes les pièces partagent). Mesuré dans l'application, la mécanique de `pas-a-pas-longueur.sh`
# sur un projet ouvert ; les notes lues dans le projet enregistré ; le témoin à côté.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la barre à 1/16 = 120 ; une piste « Batterie » sur la TR-909, canal 10, qui
# porte une frappe 60@1920+120 pour que le rang 60 existe dans la carte ; la suite A (60) S (62) Entrée D (64)).
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D634) :
#   (a) le rang 60 à 1/4 : 60@0+480 62@120+120 64@360+120, la tête à 480 ;
#   (b) sans grille au rang 60 : 60@0+120 62@120+120 64@360+120 ;
#   (c) le rang 60 à 1/4 et « Insertion : 1/8 » : 60@0+240 62@120+240 64@360+240 ;
#   (d) le témoin (le binaire d'avant D634) : (a) → 60@0+120 — le défaut.
#
#   tools/pas-a-pas-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D633/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pas-a-pas-par-rang.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

INS="menu:Édition > Temps et durée > Longueur d'insertion > Insertion"
python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
b = f"{racine}/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
evts = []
for debut, h in [(1920, 60)]:   # une frappe de repère : le rang 60 existe dans la carte
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
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes. Une copie neuve du projet, un HOME neuf (D500, D626).
    local bin="$1" nom="$2" gestes="$3" n
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_MENU="Clavier d'ordinateur" VSM_GESTE_PISTE="cliquer:Pas à pas" VSM_DELAI=4600 \
        VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE_PISTE|GESTE|TOUCHE|VUE|MENU|CLIC|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ|JAMAIS|illisible)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les notes d'un projet enregistré : « hauteur@début+durée », triées — appariées dans l'ordre d'arrivée, comme
# l'application les relit (la leçon de `longueur-insertion.sh`).
notes() {
    python3 - "$1/midi/arrangement.mid" <<'PY'
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
PY
}
tete() { grep -o "VSM_TETE : tick -\?[0-9]*" "$brouillon/$1.txt" | tail -1 | grep -o "\-\?[0-9]*$"; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
egal() { [ "$1" = "$2" ] && echo 1 || echo 0; }

echo "=== D634 : en saisie pas à pas, une frappe dure une case de son rang ==="
SUITE="900:touche:x11:A;1400:touche:x11:S;1900:touche:return;2400:touche:x11:D"
GRILLE="300:carte-grille:60>1/4"
REPERE="60@1920+120"
course "$BIN" a "$GRILLE;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/a-projet"
course "$BIN" b "$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/b-projet"
course "$BIN" c "$GRILLE;500:$INS : 1/8;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/c-projet"

A="$(notes "$brouillon/a-projet")"; B="$(notes "$brouillon/b-projet")"; C="$(notes "$brouillon/c-projet")"
echo "       (a) $A ; tête $(tete a)"
echo "       (b) $B ; tête $(tete b)"
echo "       (c) $C ; tête $(tete c)"
verdict "(a) rang 60 à 1/4 : 60@0+480 62@120+120 64@360+120, tête 480" \
    "$( [ "$A" = "60@0+480 62@120+120 64@360+120 $REPERE" ] && [ "$(tete a)" = 480 ] && echo 1 || echo 0)"
verdict "(b) sans grille au rang 60 : 60@0+120 62@120+120 64@360+120 (rien ne change)" \
    "$(egal "$B" "60@0+120 62@120+120 64@360+120 $REPERE")"
verdict "(c) rang 60 à 1/4 et « Insertion : 1/8 » : 60@0+240 62@120+240 64@360+240" \
    "$(egal "$C" "60@0+240 62@120+240 64@360+240 $REPERE")"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a "$GRILLE;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/t-a-projet"
    T="$(notes "$brouillon/t-a-projet")"
    echo "       témoin : $T"
    verdict "(d) témoin (avant D634), (a) : 60@0+120 — le défaut" "$(egal "$T" "60@0+120 62@120+120 64@360+120 $REPERE")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "PAS-A-PAS-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PAS-A-PAS-PAR-RANG : la frappe saisie dure une case de son rang, la tête avance à la barre"
