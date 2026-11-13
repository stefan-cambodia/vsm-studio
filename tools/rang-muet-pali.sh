#!/usr/bin/env bash
# D638 — CE QU'UN RANG MUET TAIT SE VOIT PÂLI : dans le piano roll, les frappes d'un rang muet de la carte (D637) se dessinent
# comme une note muette, le nom de la pièce dit « — muet », et le relevé des losanges (`relever-losanges`) le dit, lu sur la
# même source que la peinture (`seTait`, `nomDuRang`).
#
# LES RÈGLES GARDÉES (une piste « Batterie » sur la TR-909, canal 10 : 36@0 et 42@480). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D638) :
#   (a) sans geste : aucune frappe « muette » ; les rangs nommés sans « — muet » ;
#   (b) `carte-muet:42` : la 42 « muette », la 36 non ; le rang 42 nommé « … — muet », le 36 inchangé ;
#   (c) la case décochée : plus rien de muet ;
#   (d) le témoin (le binaire d'avant D638) : aucune « muette », aucun « — muet » ;
#   (e) en barres aussi ; l'anglais (« — muted »).
#
#   tools/rang-muet-pali.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D637/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-rang-muet-pali.XXXXXX")"
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
for debut, h in [(0, 36), (480, 42)]:   # une grosse caisse, un charleston fermé
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
muettes() { grep "^VSM_LOSANGE : note .* ; muette$" "$brouillon/$1.txt" | sed -E 's/^VSM_LOSANGE : note ([0-9]+) .*/\1/' | tr '\n' ' ' | sed 's/ $//'; }
rang() { grep "^VSM_LOSANGE : rang $2 : " "$brouillon/$1.txt" | tail -1 | sed "s/^VSM_LOSANGE : rang $2 : //"; }
BARRES="900:menu:Affichage > Batterie en losanges (canal 10)"

echo "=== D638 : ce qu'un rang muet tait se voit pâli ==="
course "$BIN" a "1200:relever-losanges"
course "$BIN" b "700:carte-muet:42;1300:relever-losanges"
course "$BIN" c "700:carte-muet:42;1100:carte-muet:42;1500:relever-losanges"
course "$BIN" barres "700:carte-muet:42;$BARRES;1400:relever-losanges"
course "$BIN" en "700:carte-muet:42;1300:relever-losanges" VSM_LANGUE=en

for k in a b c barres en; do echo "       ($k) muettes [$(muettes $k)] ; rang 36 « $(rang $k 36) » ; rang 42 « $(rang $k 42) » ; $(grep -h "^VSM_LOSANGE : [lb]" "$brouillon/$k.txt" | tail -1)"; done

verdict "(a) sans geste : aucune frappe muette, les rangs sans « — muet »" \
    "$( [ -z "$(muettes a)" ] && [ -n "$(rang a 42)" ] && ! rang a 42 | grep -q "muet" && echo 1 || echo 0)"
verdict "(b) le rang 42 muet : la 42 « muette », la 36 non" "$( [ "$(muettes b)" = "42" ] && echo 1 || echo 0)"
verdict "(b) le rang 42 nommé « … — muet », le 36 inchangé" \
    "$( rang b 42 | grep -q " — muet$" && [ "$(rang b 36)" = "$(rang a 36)" ] && echo 1 || echo 0)"
verdict "(c) la case décochée : plus rien de muet" "$( [ -z "$(muettes c)" ] && [ "$(rang c 42)" = "$(rang a 42)" ] && echo 1 || echo 0)"
verdict "(e) en barres aussi : la 42 « muette »" \
    "$( [ "$(muettes barres)" = "42" ] && grep -q "^VSM_LOSANGE : barres" "$brouillon/barres.txt" && echo 1 || echo 0)"
verdict "(e) l'anglais : « — muted »" "$(rang en 42 | grep -q " — muted$" && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-b "700:carte-muet:42;1300:relever-losanges"
    echo "       témoin : muettes [$(muettes t-b)] ; rangs nommés : $(grep -c "^VSM_LOSANGE : rang " "$brouillon/t-b.txt") ; $(grep -h "^VSM_CARTE : muet 42" "$brouillon/t-b.txt" | head -1)"
    verdict "(d) témoin (avant D638) : le rang muet, rien de pâli ni de dit" \
        "$( [ -z "$(muettes t-b)" ] && ! grep -q "— muet" "$brouillon/t-b.txt" && grep -q "^VSM_CARTE : muet 42 → oui" "$brouillon/t-b.txt" && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "RANG-MUET-PALI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RANG-MUET-PALI : un rang muet se voit pâli, et son nom le dit"
