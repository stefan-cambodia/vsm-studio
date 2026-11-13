#!/usr/bin/env bash
# D641 — AJUSTER LES VÉLOCITÉS DE LA QUANTITÉ CHOISIE (le *Velocity* de *MIDI ▸ Functions* de Cubase) : au sous-menu
# *Vélocité* du piano roll (et du menu *Édition*), *Ajuster les vélocités…* — ajouter ou retrancher, comprimer ou étendre
# autour de la moyenne, limiter ; un pas ; ce qui sort de 1 à 127 est borné ET COMPTÉ ; une valeur illisible refuse.
#
# LES RÈGLES GARDÉES (une piste « Synthé », Minimoog, canal 1 : quatre notes 60 aux vélocités 20, 64, 100, 120, toutes
# choisies ; la boîte remplie par `VSM_OPTIONS`, les vélocités relues dans l'export MIDI). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D641, (b) à (d)) :
#   (a) ajouter +20 → {40, 84, 120, 127}, « 1 vélocité(s) bornée(s) » ;
#   (b) comprimer ou étendre à 200 % → {1, 52, 124, 127}, « 2 » ;
#   (c) limiter 50-110 → {50, 64, 100, 110} ;
#   (d) une quantité « abc » : refusée et dite, aucun pas, {20, 64, 100, 120} ;
#   (e) le témoin (le binaire d'avant D641) : l'entrée absente ; (f) l'anglais.
#
#   tools/velocites-ajustees.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D640/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-velocites-ajustees.XXXXXX")"
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
for i, v in enumerate([20, 64, 100, 120]):
    debut = 480 * i
    evts += [(debut, 1, bytes([0x90, 60, v])), (debut + 240, 0, bytes([0x80, 60, 0]))]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Synth\xc3\xa9"; t = 0
for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
    corps += vlq(tick - t) + e; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "projet", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Synthé",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; $4 = une variable de plus (VSM_LANGUE=en). Une copie neuve, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3   # le reste : des variables de plus (VSM_LANGUE=en VSM_OPTIONS=…)
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" "$@" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE|CLIC_POSITION|OPTIONS) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les vélocités des NoteOn d'un .mid, triées (un multiensemble : le geste ne touche pas aux positions).
velocites() {
    python3 - "$1" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, i = [], 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; statut = 0
    while j < fin:
        while d[j] & 0x80: j += 1
        j += 1
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); n = 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        if statut & 0xF0 == 0x90 and d[j + 1] > 0: out.append(d[j + 1])
        j += taille
    i = fin
print(" ".join(map(str, sorted(out))) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
egal() { [ "$1" = "$2" ] && echo 1 || echo 0; }
TOUT="900:menu:Édition > Sélection > Tout sélectionner"
AJUSTER="1300:menu:Édition > Vélocité > Ajuster les vélocités..."
fin() { echo "1900:relever-etat;2100:relever-historique;2400:exporter-midi:$brouillon/$1.mid"; }
etat() { grep -h "^VSM_ETAT_PIANOROLL : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_ETAT_PIANOROLL : //'; }
pas() { grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/$1.txt" | tail -1 | sed -E 's/^VSM_HISTORIQUE_PAS : ([0-9]+) .*/\1/'; }

echo "=== D641 : ajuster les vélocités de la quantité choisie ==="
course "$BIN" a "$TOUT;$AJUSTER;$(fin a)" VSM_OPTIONS="mode=1;valeur=20"
course "$BIN" b "$TOUT;$AJUSTER;$(fin b)" VSM_OPTIONS="mode=2;valeur=200"
course "$BIN" c "$TOUT;$AJUSTER;$(fin c)" VSM_OPTIONS="mode=3;bas=50;haut=110"
course "$BIN" d "$TOUT;$AJUSTER;$(fin d)" VSM_OPTIONS="mode=1;valeur=abc"
course "$BIN" en "900:menu:Edit > Selection > Select all;1300:menu:Edit > Velocity > Adjust velocities...;$(fin en)" VSM_LANGUE=en "VSM_OPTIONS=mode=1;valeur=20"
for k in a b c d en; do echo "       ($k) {$(velocites "$brouillon/$k.mid")} ; $(pas $k) pas ; « $(etat $k) »"; done

verdict "(a) +20 → {40 84 120 127}, une bornée, dite" "$( [ "$(velocites "$brouillon/a.mid")" = "40 84 120 127" ] && etat a | grep -q "1 vélocité(s) bornée(s)" && echo 1 || echo 0)"
verdict "(b) 200 % → {1 52 124 127}, deux bornées" "$( [ "$(velocites "$brouillon/b.mid")" = "1 52 124 127" ] && etat b | grep -q "2 vélocité(s) bornée(s)" && echo 1 || echo 0)"
verdict "(c) limiter 50-110 → {50 64 100 110}" "$(egal "$(velocites "$brouillon/c.mid")" "50 64 100 110")"
verdict "(d) « abc » : refusé, dit, aucun pas, {20 64 100 120}" \
    "$( [ "$(velocites "$brouillon/d.mid")" = "20 64 100 120" ] && etat d | grep -q "ne se lit pas" && grep -q "^VSM_VELOCITE : REFUS" "$brouillon/d.txt" && [ "$(pas d)" != "$(pas a)" ] && echo 1 || echo 0)"
verdict "(f) l'anglais : « Adjust velocities... », le résultat de (a)" "$( [ "$(velocites "$brouillon/en.mid")" = "40 84 120 127" ] && etat en | grep -q "clamped" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-a "$TOUT;$AJUSTER;$(fin t-a)" VSM_OPTIONS="mode=1;valeur=20"
    echo "       témoin : {$(velocites "$brouillon/t-a.mid")} ; $(grep -h "Ajuster les vélocités" "$brouillon/t-a.txt" | head -1 | cut -c1-120)"
    verdict "(e) témoin (avant D641) : l'entrée absente, les vélocités d'origine" \
        "$( [ "$(velocites "$brouillon/t-a.mid")" = "20 64 100 120" ] && grep -q "^VSM_MENU : aucune entrée de menu au chemin « Édition > Vélocité > Ajuster les vélocités" "$brouillon/t-a.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "VELOCITES-AJUSTEES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "VELOCITES-AJUSTEES : la vélocité s'ajuste de la quantité choisie, ce qui sature se dit"
