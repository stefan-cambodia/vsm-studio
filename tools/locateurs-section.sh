#!/usr/bin/env bash
# D542.1 — LES LOCATEURS SUR UNE SECTION, PAR LES DEUX RÈGLES, MESURÉS PAR project.json.
#
# LA RÈGLE GARDÉE. Repères A à 0, B à 1 920, C à 3 840 ; le matériau sonne jusqu'à 5 760.
#   (1) la tête dans B, « Locateurs sur cette section » par la règle de l'ARRANGEMENT : la boucle
#       enregistrée vaut [1 920, 3 840), active ;
#   (2) la même par la règle du PIANO ROLL ;
#   (3) la tête dans C : [3 840, 5 760) — la dernière section va jusqu'à la fin de ce qu'on
#       entend (D540) ;
#   (4) Ctrl+Z après le geste : la boucle d'avant (inactive, [0, 0)) ;
#   (5) un projet dont le premier repère est à 1 920, la tête à 0 : l'entrée est GRISÉE (le
#       journal le dit), la boucle intacte ;
#   (6) le journal dit la section (`VSM_LOCATEURS`).
#
#   tools/locateurs-section.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-sections.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n:
        o.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(o))
def projet(nom, reperes):
    d = f"{b}/{nom}"; os.makedirs(f"{d}/midi", exist_ok=True)
    evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Piano"),
           (0, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0])),
           (4800, bytes([0x90, 67, 100])), (480, bytes([0x80, 67, 0]))]
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                  + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "markers": [{"tick": t, "name": n} for t, n in reperes],
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                           "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
projet("abc", [(0, "A"), (1920, "B"), (3840, "C")])
projet("tardif", [(1920, "B")])
PY

course() {   # $1 = nom ; $2 = projet ; $3 = position ; $4 = menu contextuel ; $5 = VSM_GESTE_APRES
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/$2" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_POSITION="$3" \
        VSM_MENU_CONTEXTE="$4" VSM_DELAI=3000 VSM_GESTE_APRES="$5" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
boucle() {   # « active:début:fin » de la boucle d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    l = json.load(open(sys.argv[1]))['transport']['loop']
except Exception:
    print('ABSENT'); sys.exit(0)
print(f\"{'active' if l.get('enabled') else 'inactive'}:{l.get('startTick', 0)}:{l.get('endTick', 0)}\")
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D542.1 : les locateurs sur une section ==="
course arrangement abc 2 "regle:Locateurs sur cette section" \
    "1500:enregistrer:$brouillon/arrangement-pose;1800:touche:ctrl + Z;2100:enregistrer:$brouillon/arrangement-annule"
course pianoroll abc 2 "regle-pianoroll:Locateurs sur cette section" "1500:enregistrer:$brouillon/pianoroll-pose"
course derniere abc 3 "regle:Locateurs sur cette section" "1500:enregistrer:$brouillon/derniere-pose"
course grisee tardif 1 "regle:Locateurs sur cette section" "1500:enregistrer:$brouillon/grisee-pose"
a="$(boucle "$brouillon/arrangement-pose/project.json")"; u="$(boucle "$brouillon/arrangement-annule/project.json")"
p="$(boucle "$brouillon/pianoroll-pose/project.json")"; d="$(boucle "$brouillon/derniere-pose/project.json")"
g="$(boucle "$brouillon/grisee-pose/project.json")"
grep -h "^VSM_LOCATEURS" "$brouillon"/arrangement.txt "$brouillon"/derniere.txt | sed 's/^/       /'
echo "       arrangement {$a} ; Ctrl+Z {$u} ; piano roll {$p} ; dernière {$d} ; avant le premier repère {$g}"
verdict "(1) par la règle de l'arrangement : [1 920, 3 840), active" "$([ "$a" = "active:1920:3840" ] && echo 1 || echo 0)"
verdict "(2) par la règle du piano roll : la même" "$([ "$p" = "active:1920:3840" ] && echo 1 || echo 0)"
verdict "(3) la dernière section : [3 840, 5 760)" "$([ "$d" = "active:3840:5760" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z : la boucle d'avant" "$([ "$u" = "inactive:0:0" ] && echo 1 || echo 0)"
verdict "(5) avant le premier repère : l'entrée grisée, la boucle intacte" \
    "$(grep -q "« Locateurs sur cette section » est GRISÉE" "$brouillon/grisee.txt" && [ "$g" = "inactive:0:0" ] && echo 1 || echo 0)"
verdict "(6) le journal dit la section" \
    "$(grep -q "^VSM_LOCATEURS : section « B » \[1920, 3840), boucle active" "$brouillon/arrangement.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "SECTIONS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SECTIONS : la boucle se pose sur une section, par les deux règles, et s'annule"
