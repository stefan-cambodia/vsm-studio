#!/usr/bin/env bash
# D571.1 — GROUPER DES CLIPS (Édition ▸ Grouper / Dégrouper les clips choisis, Ctrl+G et Ctrl+U ; le *Group / Ungroup*
# de Cubase), MESURÉ PAR LE PROJET RELU APRÈS DE VRAIS CLICS (`clic:arrangement:…`, le `mouseDown` du composant).
#
# LES RÈGLES GARDÉES. Deux pistes MIDI : P0 porte A [0, 1 920) et B [1 920, 3 840) ; P1 porte C [0, 1 920) et
# D [3 840, 5 760).
#   (1) clic sur A, Maj+clic sur C, Grouper : A et C portent le groupe 1, B et D aucun ; la touche dite au menu, l'entrée
#       grisée tant qu'un seul clip est choisi ;
#   (2) un clic sur A SEUL, puis Suppr : A et C partent, B et D restent — le témoin sans groupe : A seul part ;
#   (3) Maj+clic retire le groupe ENTIER : B, Maj+A (A et C entrent), Maj+C (A et C sortent), Suppr : B seul part ;
#   (4) une copie naît hors du groupe : A cliqué, Ctrl+D — deux copies sans groupe, A et C toujours groupés ;
#   (5) Dégrouper (A cliqué) : plus aucun groupe écrit ; un pas — Ctrl+Z rend le groupe 1 à A et C.
#
#   tools/grouper-clips.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-groupe.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, canal, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    for i in range(12):   # douze noires, de 0 à 5 760
        corps += bytes([0x00, 0x90 | canal, 60, 100]) + bytes([0x83, 0x60]) + bytes([0x80 | canal, 60, 0])
    return b"MTrk" + struct.pack(">I", len(corps + b"\x00\xff\x2f\x00")) + corps + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("P0", 0, tempo=True) + piste("P1", 1))
def clips(*defs):
    return [{"start": s, "length": 1920, "sourceStart": s, "sourceLength": 1920, "name": n} for n, s in defs]
def p(nom, canal, c):
    return {"channel": canal, "color": "#FF6B9BFF", "effects": [], "name": nom, "clips": c,
            "instrument": {"preferredPlugin": "vsm.minimoog"},
            "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0}}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [p("P0", 0, clips(("A", 0), ("B", 1920))), p("P1", 1, clips(("C", 0), ("D", 3840)))]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|CLIC_POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
etat() {   # « P0[A:1 B] P1[C:1 D] » : les clips de chaque piste, leur groupe s'il y en a un, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(t['name'] + '[' + ' '.join(c.get('name', '?') + (':' + str(c['group']) if 'group' in c else '')
                                          for c in t.get('clips', [])) + ']' for t in d['tracks']))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GROUPER="menu:Édition > Grouper les clips choisis"
DEGROUPER="menu:Édition > Dégrouper les clips choisis"

echo "=== D571.1 : grouper des clips ==="
course geometrie "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"
declare -A OU
while read -r nom pos; do OU[$nom]="$pos"; done < <(python3 -c "
import re, sys
for ligne in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.match(r'VSM_CLIP_ECRAN : piste \d+ \"(\w)\" x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', ligne)
    if m:
        n = m.group(1); x1, x2, y1, y2, w, h = (float(v) for v in m.groups()[1:])
        print(n, f'{(x1 + x2) / 2 / w:.5f},{(y1 + y2) / 2 / h:.5f}')
" "$brouillon/geometrie.txt")
for n in A B C D; do [ -n "${OU[$n]:-}" ] || { echo "  RATÉ la géométrie : le clip $n n'est pas à l'écran (relever-clips)"; exit 1; }; done
echo "       à l'écran : A ${OU[A]}, B ${OU[B]}, C ${OU[C]}, D ${OU[D]}"
CA="clic:arrangement:${OU[A]}"; CB="clic:arrangement:${OU[B]}"; CC="clic:arrangement:${OU[C]}"

cp -r "$brouillon/base" "$brouillon/groupe"
course grouper "$brouillon/groupe" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1200:lister-menus;1400:$CC:maj;1600:lister-menus;1800:$GROUPER;2200:enregistrer:$brouillon/groupe"
for x in supprime temoin retire copie degroupe annule; do cp -r "$brouillon/groupe" "$brouillon/$x"; done
rm -rf "$brouillon/temoin"; cp -r "$brouillon/base" "$brouillon/temoin"
course supprimer "$brouillon/supprime" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1300:touche:arrangement:delete;1800:enregistrer:$brouillon/supprime"
course temoin "$brouillon/temoin" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1300:touche:arrangement:delete;1800:enregistrer:$brouillon/temoin"
course retirer "$brouillon/retire" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CB;1200:$CA:maj;1400:$CC:maj;1700:touche:arrangement:delete;2200:enregistrer:$brouillon/retire"
course copier "$brouillon/copie" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1300:touche:arrangement:ctrl + D;1800:enregistrer:$brouillon/copie"
course degrouper "$brouillon/degroupe" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1300:$DEGROUPER;1800:enregistrer:$brouillon/degroupe"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CA;1300:$DEGROUPER;1700:touche:ctrl + Z;2200:enregistrer:$brouillon/annule"

eg="$(etat "$brouillon/groupe")"; es="$(etat "$brouillon/supprime")"; et="$(etat "$brouillon/temoin")"
er="$(etat "$brouillon/retire")"; ec="$(etat "$brouillon/copie")"; ed="$(etat "$brouillon/degroupe")"; ea="$(etat "$brouillon/annule")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Édition > Grouper les clips choisis" "$brouillon/grouper.txt" | sed 's/^VSM_MENU_LISTE : Édition > //')
for x in "${m[@]}"; do echo "       menu : $x"; done
grep -h "^VSM_CLIPS_GROUPE" "$brouillon"/{grouper,degrouper,annuler}.txt | sed 's/^/       /'
echo "       groupé [$eg] ; un clic puis Suppr [$es] ; témoin sans groupe [$et]"
echo "       Maj retire le groupe [$er] ; Ctrl+D [$ec] ; dégroupé [$ed] ; Ctrl+Z [$ea]"

verdict "(1) A et C groupés (1), B et D non ; la touche au menu, grisée sur un seul clip" \
    "$([ "$eg" = "P0[A:1 B] P1[C:1 D]" ] && [ "${#m[@]}" = 2 ] && echo "${m[0]}" | grep -q "\[touche .*G\] \[grisée\]" \
       && ! echo "${m[1]}" | grep -q "grisée" && grep -q "^VSM_CLIPS_GROUPE : 2 clip(s) dans le groupe 1" "$brouillon/grouper.txt" && echo 1 || echo 0)"
verdict "(2) un clic sur A seul, Suppr : A et C partent ; le témoin sans groupe ne perd que A" \
    "$([ "$es" = "P0[B] P1[D]" ] && [ "$et" = "P0[B] P1[C D]" ] && echo 1 || echo 0)"
verdict "(3) Maj+clic retire le groupe entier : B seul part" "$([ "$er" = "P0[A:1] P1[C:1 D]" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+D : deux copies hors du groupe, A et C toujours groupés" \
    "$(python3 -c "
import sys
e = sys.argv[1]
jetons = e.replace('[', ' ').replace(']', ' ').split()   # deux noms de piste et six clips
print(1 if e.count(':1') == 2 and 'A:1' in jetons and 'C:1' in jetons and len(jetons) == 8 else 0)" "$ec")"
verdict "(5) dégroupé : plus aucun groupe ; Ctrl+Z rend le groupe 1 à A et C" \
    "$([ "$ed" = "P0[A B] P1[C D]" ] && [ "$ea" = "P0[A:1 B] P1[C:1 D]" ] \
       && grep -q "^VSM_CLIPS_GROUPE : 2 clip(s) dégroupé(s)" "$brouillon/degrouper.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "GROUPER-CLIPS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "GROUPER-CLIPS : un clic choisit le groupe, les gestes le suivent, une copie en sort, un pas le défait"
