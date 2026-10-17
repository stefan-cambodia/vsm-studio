#!/usr/bin/env bash
# D560.3 — PLIER, DÉPLIER, INVERSER LES PLIS DE TOUTES LES PISTES (Piste ▸ Plier toutes les pistes (N), Déplier toutes
# les pistes (N), Inverser les plis (N) ; le *Track Folding* de Cubase : Fold / Unfold Tracks, Flip Fold States),
# MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Quatre pistes : « Dossier » (un dossier), « Kick » (dedans), « Basse » (déjà pliée), « Cachée »
# (masquée).
#   (1) le menu dit « Plier (3) », « Déplier (1) », « Inverser (4) » ;
#   (2) plier : les QUATRE pliées, la masquée comprise, et elle reste masquée ; dit ;
#   (3) inverser, depuis l'état de départ : chacune l'inverse de ce qu'elle était ;
#   (4) déplier après plier : aucune pliée ; et le menu, après plier, dit « Plier (0) » GRISÉE ;
#   (5) un pas : Ctrl+Z après plier rend l'état de départ, après un geste qui a eu lieu.
#
#   tools/plier-toutes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-plis.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    corps += b"\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
def entree(nom, **x):
    return dict({"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": nom, "mix": dict(mix),
                 "instrument": {"preferredPlugin": "vsm.minimoog"}}, **x)
noms = ("Dossier", "Kick", "Basse", "Cachée")
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 4, 480) + b"".join(piste(n, tempo=(i == 0)) for i, n in enumerate(noms)))
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [entree("Dossier", kind="folder", folderDepth=0), entree("Kick", folderDepth=1),
                      entree("Basse", folded=True), entree("Cachée", hidden=True)]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
plis() {   # « nom:P/M » de chaque piste (P pliée, M masquée), ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(t['name'] + ':' + ('P' if t.get('folded') else '') + ('M' if t.get('hidden') else '') for t in d['tracks']))" "$1/project.json"
}
entrees() {   # les trois entrées telles que le menu Piste les liste
    grep -h "^VSM_MENU_LISTE : Piste > \(Plier toutes\|Déplier toutes\|Inverser les plis\)" "$1" | sed 's/^VSM_MENU_LISTE : Piste > //' | tr '\n' ';'
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
PLIER="menu:Piste > Plier toutes les pistes (3)"

echo "=== D560.3 : plier, déplier, inverser les plis de toutes les pistes ==="
for x in p i d z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course plier "$brouillon/p" VSM_DELAI=2600 \
    VSM_GESTE_APRES="800:lister-menus;1100:$PLIER;1400:lister-menus;1800:enregistrer:$brouillon/p"
course inverser "$brouillon/i" VSM_DELAI=2400 \
    VSM_GESTE_APRES="1000:menu:Piste > Inverser les plis (4);1500:enregistrer:$brouillon/i"
course deplier "$brouillon/d" VSM_DELAI=2600 \
    VSM_GESTE_APRES="1000:$PLIER;1300:menu:Piste > Déplier toutes les pistes (4);1800:enregistrer:$brouillon/d"
course annuler "$brouillon/z" VSM_DELAI=2800 \
    VSM_GESTE_APRES="1000:$PLIER;1400:touche:ctrl + Z;1900:enregistrer:$brouillon/z"

depart="$(plis "$brouillon/base")"; pp="$(plis "$brouillon/p")"; pi="$(plis "$brouillon/i")"
pd="$(plis "$brouillon/d")"; pz="$(plis "$brouillon/z")"
mapfile -t menus < <(grep -h "^VSM_MENU_LISTE : Piste > \(Plier toutes\|Déplier toutes\|Inverser les plis\)" "$brouillon/plier.txt" | sed 's/^VSM_MENU_LISTE : Piste > //')
grep -h "^VSM_PLIS" "$brouillon/plier.txt" "$brouillon/inverser.txt" "$brouillon/deplier.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       départ : [$depart] ; plié : [$pp] ; inversé : [$pi] ; plié puis déplié : [$pd] ; Ctrl+Z : [$pz]"
echo "       menu avant : [${menus[0]:-}] [${menus[1]:-}] [${menus[2]:-}]"
echo "       menu après plier : [${menus[3]:-}] [${menus[4]:-}] [${menus[5]:-}]"

verdict "(1) le menu dit 3, 1 et 4" \
    "$([ "${menus[0]:-}" = "Plier toutes les pistes (3)" ] && [ "${menus[1]:-}" = "Déplier toutes les pistes (1)" ] \
        && [ "${menus[2]:-}" = "Inverser les plis (4)" ] && echo 1 || echo 0)"
verdict "(2) plier : les quatre pliées, la masquée comprise et toujours masquée ; dit" \
    "$([ "$pp" = "Dossier:P Kick:P Basse:P Cachée:PM" ] && grep -q "^VSM_PLIS : 3 piste(s) pli" "$brouillon/plier.txt" && echo 1 || echo 0)"
verdict "(3) inverser : chacune l'inverse de son départ" "$([ "$pi" = "Dossier:P Kick:P Basse: Cachée:PM" ] && echo 1 || echo 0)"
verdict "(4) déplier après plier : aucune pliée ; après plier, « Plier (0) » grisée" \
    "$([ "$pd" = "Dossier: Kick: Basse: Cachée:M" ] && echo "${menus[3]:-}" | grep -q "^Plier toutes les pistes (0) \[grisée\]" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend le départ, après un geste qui a eu lieu" \
    "$([ "$pz" = "$depart" ] && [ "$depart" = "Dossier: Kick: Basse:P Cachée:M" ] && grep -q "^VSM_PLIS : 3 piste(s) pli" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PLIER-TOUTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PLIER-TOUTES : toutes les pistes se plient, se déplient et s'inversent d'un geste, masquées comprises"
