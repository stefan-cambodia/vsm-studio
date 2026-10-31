#!/usr/bin/env bash
# D622 — LA GRILLE PAR RANG DE LA CARTE DE BATTERIE (la colonne *Quantize* de la *Drum Map* de Cubase), mesurée dans
# l'application : la grille du rang 38 réglée par la MÊME liste que la souris (`carte-grille:38>1/4`), puis des clics du
# piano roll (`clic:pianoroll:fx,fy`, la géométrie lue par `relever-losanges`) et « Quantifier (100 %) » au menu ; les notes
# lues dans le projet enregistré.
#
# LE JEU : une piste « Batterie » sur la TR-909, canal 10, 480 ticks à la noire, la grille de la barre à 1/16 (120 ticks).
# Deux frappes de repère : 38 (caisse claire) au tick 960, 36 (grosse caisse) au tick 1 440 — elles donnent la place des
# ticks à l'écran et la hauteur des deux rangs. Le jeu « quantifier » ajoute deux frappes à 70, rangs 36 et 38.
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D622) :
#   (b) un clic au tick ~400 dans le rang 36 (global) → une frappe à 360, longue de 120 ; dans le rang 38 (1/4) → à 480,
#       longue de 480 ;
#   (c) les frappes à 70 choisies, « Quantifier » → 120 (rang 36) et 0 (rang 38) ;
#   (d) le projet porte `"drumGrid": [{"in": 38, "grid": "1/4"}]` ; relu, la colonne dit « 1/4 » ; Ctrl+Z → « globale » ;
#       le témoin (D621) : le verbe inconnu, (b) → 360 et 360 ; l'anglais.
#
#   tools/grille-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D621/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-grille-rang.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def projet(nom, frappes):
    b = f"{racine}/{nom}"; os.makedirs(f"{b}/midi", exist_ok=True)
    evts = []
    for debut, h in frappes:
        evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 120, 0, bytes([0x89, h, 0]))]
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
    for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
        corps += vlq(tick - t) + e; t = tick
    corps += b"\x00\xff\x2f\x00"
    open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                           "instrument": {"preferredPlugin": "vsm.tr909"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
              open(f"{b}/project.json", "w"), indent=1)
projet("projet", [(960, 38), (1440, 36)])
projet("quantifier", [(70, 36), (70, 38), (960, 38), (1440, 36)])
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu (`JEU`), un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/${JEU:-projet}" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=2600 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CLIC_POSITION|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les frappes d'un projet enregistré : « hauteur@début+durée », triées.
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
        # APPARIÉES DANS L'ORDRE D'ARRIVÉE, comme l'application les relit — CORRIGÉ APRÈS LE PREMIER ESSAI : le fichier
        # écrit au même tick le note-on d'une frappe AVANT le note-off de la précédente de même hauteur (le défaut que
        # D623 traite), et un appariement « dernier ouvert » en faisait une note de longueur nulle.
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D622 : la grille par rang de la carte de batterie ==="
course "$BIN" geometrie "1200:relever-losanges"
centre() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1 \2/p'; }
read -r X960 Y38 <<< "$(centre 38 960)"; read -r X1440 Y36 <<< "$(centre 36 1440)"
[ -n "${X960:-}" ] && [ -n "${X1440:-}" ] || { echo "REFUS : la géométrie des frappes de repère n'a pas été relevée"; exit 1; }
# LE TICK 400 À L'ÉCRAN, par les deux repères (la règle est linéaire) ; les rangs, par leurs frappes.
X400="$(python3 -c "import sys; a, b = float(sys.argv[1]), float(sys.argv[2]); print(f'{a + (400 - 960) * (b - a) / 480:.4f}')" "$X960" "$X1440")"
echo "       repères : 38@960 à x=$X960, 36@1440 à x=$X1440 ; le tick 400 à x=$X400 ; rangs y36=$Y36, y38=$Y38"
course "$BIN" pose36 "900:carte-grille:38>1/4;1300:clic:pianoroll:$X400,$Y36;1700:enregistrer:$brouillon/pose36-projet"
course "$BIN" pose38 "900:carte-grille:38>1/4;1300:clic:pianoroll:$X400,$Y38;1700:relever-carte;1900:enregistrer:$brouillon/pose38-projet"
QUANT="menu:Édition > Temps et durée > Quantifier (100 %)"
TOUT="menu:Édition > Sélection > Tout sélectionner"
JEU=quantifier course "$BIN" quant "900:carte-grille:38>1/4;1200:$TOUT;1500:$QUANT;1900:enregistrer:$brouillon/quant-projet"
course "$BIN" annule "900:carte-grille:38>1/4;1200:touche:ctrl + Z;1500:relever-carte"
course "$BIN" en "900:carte-grille:38>1/4;1200:relever-carte" VSM_LANGUE=en

F36="$(frappes "$brouillon/pose36-projet")"; F38="$(frappes "$brouillon/pose38-projet")"; FQ="$(frappes "$brouillon/quant-projet")"
echo "       rang 36 : $F36"
echo "       rang 38 : $F38"
echo "       quantifié : $FQ"
neuve() { echo "$1" | tr ' ' '\n' | grep -v "^38@960+120$\|^36@1440+120$" | tr '\n' ' ' | sed 's/ $//'; }
verdict "(b) le rang 36 (global) : une frappe à 360, longue de 120 ($(neuve "$F36"))" "$([ "$(neuve "$F36")" = "36@360+120" ] && echo 1 || echo 0)"
verdict "(b) le rang 38 (1/4) : une frappe à 480, longue de 480 ($(neuve "$F38"))" "$([ "$(neuve "$F38")" = "38@480+480" ] && echo 1 || echo 0)"
verdict "(c) « Quantifier » : la frappe de 70 du rang 36 → 120, celle du rang 38 → 0" \
    "$(echo " $FQ " | grep -q " 36@120+" && echo " $FQ " | grep -q " 38@0+" && echo 1 || echo 0)"
# LA STRUCTURE, pas la chaîne — CORRIGÉ APRÈS LE PREMIER ESSAI : le projet écrit ses clés dans l'ordre alphabétique.
G="$(python3 -c "import json, sys; print(json.load(open(sys.argv[1]))['tracks'][0].get('drumGrid') == [{'in': 38, 'grid': '1/4'}])" "$brouillon/pose38-projet/project.json" 2>/dev/null)"
echo "       drumGrid au projet : $(python3 -c "import json, sys; print(json.load(open(sys.argv[1]))['tracks'][0].get('drumGrid'))" "$brouillon/pose38-projet/project.json" 2>/dev/null)"
verdict "(d) le projet porte « drumGrid » : [{in 38, grid 1/4}]" "$([ "$G" = True ] && echo 1 || echo 0)"
verdict "(d) la colonne dit « 1/4 » pour 38, « globale » pour 36" \
    "$(grep -q "^VSM_CARTE : grille 38 → 1/4$" "$brouillon/pose38.txt" && grep -q "^VSM_CARTE : grille 36 → globale$" "$brouillon/pose38.txt" && echo 1 || echo 0)"
verdict "(d) Ctrl+Z : 38 revient à « globale »" "$(grep "^VSM_CARTE : grille 38 → " "$brouillon/annule.txt" | tail -1 | grep -q "globale$" && echo 1 || echo 0)"
verdict "(d) en anglais : 38 → « 1/4 », 36 → « global »" \
    "$(grep -q "^VSM_CARTE : grille 38 → 1/4$" "$brouillon/en.txt" && grep -q "^VSM_CARTE : grille 36 → global$" "$brouillon/en.txt" && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-pose38 "900:carte-grille:38>1/4;1300:clic:pianoroll:$X400,$Y38;1900:enregistrer:$brouillon/t-pose38-projet"
    TF="$(neuve "$(frappes "$brouillon/t-pose38-projet")")"
    ti="$(grep -c "geste inconnu" "$brouillon/t-pose38.txt")"
    echo "       témoin : $ti « geste inconnu » ; la frappe posée dans le rang 38 : $TF"
    verdict "(d) témoin (D621) : le verbe inconnu ; le rang 38 suit la barre — 360, longue de 120" \
        "$([ "$ti" -ge 1 ] && [ "$TF" = "38@360+120" ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "GRILLE-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "GRILLE-PAR-RANG : chaque rang de la carte pose et quantifie à sa grille, et un rang « global » suit la barre"
