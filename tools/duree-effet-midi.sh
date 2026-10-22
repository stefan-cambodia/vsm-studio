#!/usr/bin/env bash
# D573.3 — LA DURÉE EN EFFET MIDI (Piste ▸ Effets MIDI ▸ Ajouter : Durée ; la *Length Compression* des modificateurs
# MIDI de Cubase), MESURÉE PAR LE PROJET RELU APRÈS LE REPORT DANS LES NOTES (D31.5).
#
# LES RÈGLES GARDÉES. Une piste de quatre croches : un do à 0, un ré à 480, deux mi répétés à 960 et 1 200 (le premier
# finit où le second commence). La chaîne de la piste porte l'effet « length », écrit au projet avant l'ouverture.
#   (1) le TÉMOIN au rapport 1 : reporté, les notes reviennent telles quelles (0-240, 480-720, 960-1200, 1200-1440) —
#       sans quoi « les durées changent » ne prouverait pas que c'est l'effet ;
#   (2) au rapport 0,5 : toutes à 120 ticks, les départs intacts ;
#   (3) au rapport 2 : le do et le ré à 480, le PREMIER MI arrêté au départ du second (960-1200, le legato), le second à
#       480 ; et la chaîne vidée par le report ;
#   (4) le menu propose « Ajouter : Durée » (« Add: Length » en anglais) ; ajouté par lui, l'effet arrive au rapport 1
#       (le projet relu le porte, de type « length ») ;
#   (5) un pas : Ctrl+Z après le report rend les notes d'origine et la chaîne.
#
#   tools/duree-effet-midi.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-duree-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 240, 60), (480, 720, 62), (960, 1200, 64), (1200, 1440, 64)]
ev = []
for d, f, h in notes:
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
for nom, chaine in (("r1", [{"type": "length", "parameters": {"Ratio": 1.0}}]),
                    ("r05", [{"type": "length", "parameters": {"Ratio": 0.5}}]),
                    ("r2", [{"type": "length", "parameters": {"Ratio": 2.0}}]),
                    ("z", [{"type": "length", "parameters": {"Ratio": 2.0}}]),
                    ("vide", [])):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
             "instrument": {"preferredPlugin": "vsm.juno106"}}
    if chaine: piste["midiEffects"] = chaine
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [piste]}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|CONFIRMER) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « début-fin:hauteur » des notes d'un .mid, triées, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            out.append((ouvertes[x.note].pop(0), t, x.note))
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(out)) or "RIEN")
PY2
}
chaine() {   # les effets MIDI de la piste d'un project.json (« type:Ratio »), « (vide) », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
fx = d['tracks'][0].get('midiEffects', [])
r = lambda e: e.get('parameters', {}).get('Ratio')
print(' '.join(e['type'] + ':' + ('-' if r(e) is None else f'{float(r(e)):g}') for e in fx) or '(vide)')" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REPORT="menu:Piste > Effets MIDI de la piste (1) > Reporter les effets MIDI dans les notes (définitif)"

echo "=== D573.3 : la durée en effet MIDI ==="
for k in r1 r05 r2; do
    course "report-$k" "$brouillon/$k" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
        VSM_GESTE_APRES="1200:$REPORT;1900:enregistrer:$brouillon/$k"
done
course annuler "$brouillon/z" VSM_DELAI=3000 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1200:$REPORT;1700:touche:ctrl + Z;2300:enregistrer:$brouillon/z"
course ajouter "$brouillon/vide" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:menu:Piste > Effets MIDI de la piste (0) > Ajouter : Durée;1900:enregistrer:$brouillon/vide"
course anglais "$brouillon/r1" VSM_DELAI=1800 VSM_VUE="sans-rapport" VSM_LANGUE=en VSM_MENU_LISTE=1

n1="$(notes "$brouillon/r1/midi/arrangement.mid")"; n05="$(notes "$brouillon/r05/midi/arrangement.mid")"
n2="$(notes "$brouillon/r2/midi/arrangement.mid")"; nz="$(notes "$brouillon/z/midi/arrangement.mid")"
c2="$(chaine "$brouillon/r2")"; cz="$(chaine "$brouillon/z")"; cv="$(chaine "$brouillon/vide")"
fr="$(grep -h "^VSM_MENU_LISTE : Piste > Effets MIDI de la piste (0) > Ajouter : Durée" "$brouillon/ajouter.txt" | head -1)"
en="$(grep -h "^VSM_MENU_LISTE : .* > Add: Length" "$brouillon/anglais.txt" | head -1)"
echo "       rapport 1 : [$n1]"
echo "       rapport 0,5 : [$n05]"
echo "       rapport 2 : [$n2] ; chaîne après le report [$c2]"
echo "       Ctrl+Z après le report : [$nz] ; chaîne [$cz]"
echo "       ajouté par le menu : chaîne [$cv] ; menu fr [${fr#VSM_MENU_LISTE : }] ; en [${en#VSM_MENU_LISTE : }]"
ORIGINE="0-240:60 480-720:62 960-1200:64 1200-1440:64"

verdict "(1) le témoin au rapport 1 : les notes d'origine" "$([ "$n1" = "$ORIGINE" ] && echo 1 || echo 0)"
verdict "(2) au rapport 0,5 : toutes à 120 ticks, les départs intacts" \
    "$([ "$n05" = "0-120:60 480-600:62 960-1080:64 1200-1320:64" ] && echo 1 || echo 0)"
verdict "(3) au rapport 2 : 480, 480, le premier mi au départ du second (legato), 480 ; la chaîne vidée" \
    "$([ "$n2" = "0-480:60 480-960:62 960-1200:64 1200-1680:64" ] && [ "$c2" = "(vide)" ] && echo 1 || echo 0)"
verdict "(4) « Ajouter : Durée » (« Add: Length ») ; ajouté, l'effet arrive au rapport 1" \
    "$([ -n "$fr" ] && [ -n "$en" ] && [ "$cv" = "length:1" ] && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z après le report rend les notes d'origine et la chaîne" \
    "$([ "$nz" = "$ORIGINE" ] && [ "$cz" = "length:2" ] && grep -q "Effets MIDI report" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DUREE-EFFET-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DUREE-EFFET-MIDI : la durée des notes jouées suit le rapport, s'arrête à la note répétée, et se reporte"
