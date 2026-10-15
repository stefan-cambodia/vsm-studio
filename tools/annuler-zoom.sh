#!/usr/bin/env bash
# D557.3 — ANNULER ET RÉTABLIR UN ZOOM DE L'ARRANGEMENT (Affichage ▸ Annuler le zoom, Alt+Z ; Rétablir le zoom,
# Maj+Alt+Z ; le *Undo Zoom / Redo Zoom* de Cubase), MESURÉ PAR LE RELEVÉ DE LA FENÊTRE.
#
# LES RÈGLES GARDÉES. Un projet de seize mesures, ouvert cadré (V0). Dans UNE course : deux zooms avant au clavier de
# l'arrangement (V1 = V0 × 1,25, V2 = V1 × 1,25), puis « tout voir » DEUX fois (Ctrl+0) — la seconde ne change rien —,
# puis annuler par le menu, deux fois par Alt+Z, une quatrième fois de trop, rétablir par Maj+Alt+Z, et un zoom
# arrière neuf.
#   (1) à l'ouverture, les deux entrées sont grisées : la vue d'ouverture n'est pas un geste ;
#   (2) annuler revient à V2, puis V1, puis V0 — la fenêtre relue après chaque geste est celle d'avant le zoom qu'il
#       défait. V2 et non le cadrage : le second « tout voir », qui ne changeait rien, n'a rien empilé ;
#   (3) la quatrième annulation est DITE (« aucun zoom à annuler ») et ne bouge rien ;
#   (4) rétablir revient à V1 ; les deux entrées sont alors actives ;
#   (5) un zoom neuf vide le « rétablir » : l'entrée est grisée.
# La fenêtre se lit en mesures (`VSM_ARRANGEMENT : fenêtre N mesure(s)`), au dixième : V1 = V0 / 1,25 et V2 = V0 /
# 1,5625 se distinguent à seize mesures (16,7 · 13,3 · 10,7).
#
#   tools/annuler-zoom.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-annuler-zoom.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
ev = []
for mesure in range(16):   # une noire au début de chaque mesure, seize mesures
    d = mesure * 1920
    ev.append((d, 1, bytes([0x90, 60, 100]))); ev.append((d + 480, 0, bytes([0x80, 60, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D557.3 : annuler et rétablir un zoom de l'arrangement ==="
env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" \
    VSM_VUE="sans-rapport,arrangement" VSM_DELAI=4000 \
    VSM_GESTE_APRES="900:lister-menus;1000:relever-arrangement;1200:touche:arrangement:=;1400:touche:arrangement:=;1600:touche:ctrl + 0;1800:touche:ctrl + 0;2000:menu:Affichage > Annuler le zoom;2200:touche:alt + Z;2400:touche:alt + Z;2600:touche:alt + Z;2800:touche:shift + alt + Z;3000:lister-menus;3200:touche:arrangement:-;3400:lister-menus" \
    VSM_CAPTURE="$brouillon/zoom.png" timeout 90 "$BIN" > "$brouillon/zoom.txt" 2>&1
J="$brouillon/zoom.txt"
grep -E "VSM_(GESTE_APRES|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$J" | sed 's/^/        journal : /' >&2

# Les fenêtres, dans l'ordre du journal : V0 est la DERNIÈRE relue avant le premier zoom avant (un relevé du démarrage
# peut précéder la mise en page) ; puis celle que chaque annulation ou rétablissement relit juste après sa ligne.
mapfile -t f < <(python3 - "$J" <<'PY2'
import re, sys
v0, apres, attend = None, [], False
zoome = False
for ligne in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"VSM_ARRANGEMENT : fenêtre ([0-9.]+) mesure", ligne)
    if "touche:arrangement:= à 1200 ms" in ligne:
        zoome = True
    if m and not zoome:
        v0 = m.group(1)
    elif m and attend:
        apres.append(m.group(1)); attend = False
    if ligne.startswith("VSM_ZOOM : annulé") or ligne.startswith("VSM_ZOOM : rétabli"):
        attend = True
print(v0 or "")
for x in apres:
    print(x)
PY2
)
mapfile -t z < <(grep -h "^VSM_ZOOM : " "$J" | sed 's/^VSM_ZOOM : //')
mapfile -t menus < <(grep -h "^VSM_MENU_LISTE : Affichage > \(Annuler\|Rétablir\) le zoom" "$J" | sed 's/^VSM_MENU_LISTE : Affichage > //')
for x in "${z[@]}"; do echo "       zoom : $x"; done
echo "       fenêtres relues : ${f[*]}"
for x in "${menus[@]}"; do echo "       menu : $x"; done

# V0, et ce qu'en font deux zooms avant de 1,25 : la fenêtre en mesures se divise d'autant.
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= 0.15 else 1)" "$1" "$2" 2>/dev/null; }
v0="${f[0]:-}"
v1="$(python3 -c "print(round(float('${v0:-0}') / 1.25, 2))")"
v2="$(python3 -c "print(round(float('${v0:-0}') / 1.5625, 2))")"
echo "       attendu : V0 $v0, V1 $v1, V2 $v2"

verdict "(1) à l'ouverture, les deux entrées grisées" \
    "$(echo "${menus[0]:-}" | grep -q "^Annuler le zoom .*\[grisée\]$" && echo "${menus[1]:-}" | grep -q "^Rétablir le zoom .*\[grisée\]$" && echo 1 || echo 0)"
verdict "(2) annuler revient à V2, V1, puis V0 — le second « tout voir » n'a rien empilé" \
    "$([ "${#f[@]}" -ge 5 ] && proche "${f[1]}" "$v2" && proche "${f[2]}" "$v1" && proche "${f[3]}" "$v0" \
       && echo "${z[0]:-}" | grep -q "^annulé" && echo 1 || echo 0)"
verdict "(3) la quatrième annulation est dite et ne bouge rien" \
    "$(echo "${z[3]:-}" | grep -q "^aucun zoom à annuler$" && echo 1 || echo 0)"
verdict "(4) rétablir revient à V1, et les deux entrées sont actives" \
    "$([ "${#f[@]}" -ge 5 ] && proche "${f[4]}" "$v1" && echo "${z[4]:-}" | grep -q "^rétabli" \
       && echo "${menus[2]:-}" | grep -q "^Annuler le zoom \[touche [^]]*\]$" && echo "${menus[3]:-}" | grep -q "^Rétablir le zoom \[touche [^]]*\]$" && echo 1 || echo 0)"
verdict "(5) un zoom neuf vide le rétablir" \
    "$(echo "${menus[5]:-}" | grep -q "^Rétablir le zoom .*\[grisée\]$" && echo "${menus[4]:-}" | grep -q "^Annuler le zoom \[touche [^]]*\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ANNULER-ZOOM : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ANNULER-ZOOM : la vue d'avant chaque zoom se reprend, et se rend"
