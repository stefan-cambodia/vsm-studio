#!/usr/bin/env bash
# D574.3 — LA PLAGE EN EFFET MIDI (Piste ▸ Effets MIDI ▸ Ajouter : Plage ; le *Range* des modificateurs MIDI de Cubase),
# MESURÉE PAR LE PROJET RELU APRÈS LE REPORT DANS LES NOTES (D31.5).
#
# LES RÈGLES GARDÉES. Trois notes : à 30, 60 et 90, de vélocités 20, 64 et 120. La chaîne de la piste porte l'effet
# « range », écrit au projet avant l'ouverture. L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D574) :
#   (1) le TÉMOIN à la plage entière (0-127) : reporté, les notes d'origine ;
#   (2) limiter les hauteurs à 48-72 : 54, 60, 66 — les vélocités intactes ;
#   (3) filtrer les hauteurs à 48-72 : 60 seule, et le report DIT « 2 note(s) écartée(s) hors de la plage » ;
#   (4) limiter les vélocités à 40-100 : 40, 64, 100 — les hauteurs intactes ;
#   (5) le menu propose « Ajouter : Plage » (« Add: Range ») ; ajouté, l'effet arrive à la plage entière ;
#   (6) un pas : Ctrl+Z après le report filtré rend les trois notes et la chaîne.
#
#   tools/plage-effet-midi.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-plage-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 240, 30, 20), (480, 720, 60, 64), (960, 1200, 90, 120)]
ev = []
for d, f, h, v in notes:
    ev.append((d, 1, bytes([0x90, h, v]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
plage = lambda mode, mini, maxi: [{"type": "range", "parameters": {"Mode": mode, "Min": mini, "Max": maxi}}]
for nom, chaine in (("plein", plage(0, 0, 127)), ("lim", plage(0, 48, 72)), ("fil", plage(1, 48, 72)),
                    ("vel", plage(2, 40, 100)), ("z", plage(1, 48, 72)), ("vide", [])):
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
notes() {   # « début:hauteur/vélocité » des notes d'un .mid, triées, ou « ABSENT »
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
            ouvertes.setdefault(x.note, []).append((t, x.velocity))
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            d, v = ouvertes[x.note].pop(0)
            out.append((d, x.note, v))
print(" ".join(f"{d}:{h}/{v}" for d, h, v in sorted(out)) or "RIEN")
PY2
}
chaine() {   # les effets MIDI de la piste d'un project.json (« type:Mode:Min:Max »), « (vide) », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
fx = d['tracks'][0].get('midiEffects', [])
p = lambda e, k: e.get('parameters', {}).get(k)
f = lambda v: '-' if v is None else f'{float(v):g}'
print(' '.join(e['type'] + ':' + ':'.join(f(p(e, k)) for k in ('Mode', 'Min', 'Max')) for e in fx) or '(vide)')" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REPORT="menu:Piste > Effets MIDI de la piste (1) > Reporter les effets MIDI dans les notes (définitif)"

echo "=== D574.3 : la plage en effet MIDI ==="
for k in plein lim fil vel; do
    course "report-$k" "$brouillon/$k" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
        VSM_GESTE_APRES="1200:$REPORT;1900:enregistrer:$brouillon/$k"
done
course annuler "$brouillon/z" VSM_DELAI=3000 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1200:$REPORT;1700:touche:ctrl + Z;2300:enregistrer:$brouillon/z"
course ajouter "$brouillon/vide" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:menu:Piste > Effets MIDI de la piste (0) > Ajouter : Plage;1900:enregistrer:$brouillon/vide"
course anglais "$brouillon/plein" VSM_DELAI=1800 VSM_VUE="sans-rapport" VSM_LANGUE=en VSM_MENU_LISTE=1

np="$(notes "$brouillon/plein/midi/arrangement.mid")"; nl="$(notes "$brouillon/lim/midi/arrangement.mid")"
nf="$(notes "$brouillon/fil/midi/arrangement.mid")"; nv="$(notes "$brouillon/vel/midi/arrangement.mid")"
nz="$(notes "$brouillon/z/midi/arrangement.mid")"; cz="$(chaine "$brouillon/z")"; cv="$(chaine "$brouillon/vide")"
dit="$(grep -h "^Effets MIDI report" "$brouillon/report-fil.txt" | head -1)"
fr="$(grep -h "^VSM_MENU_LISTE : Piste > Effets MIDI de la piste (0) > Ajouter : Plage" "$brouillon/ajouter.txt" | head -1)"
en="$(grep -h "^VSM_MENU_LISTE : .* > Add: Range" "$brouillon/anglais.txt" | head -1)"
echo "       plage entière : [$np]"
echo "       limiter les hauteurs 48-72 : [$nl]"
echo "       filtrer les hauteurs 48-72 : [$nf] ; le report : ${dit:-(rien)}"
echo "       limiter les vélocités 40-100 : [$nv]"
echo "       Ctrl+Z après le report filtré : [$nz] ; chaîne [$cz]"
echo "       ajouté par le menu : chaîne [$cv] ; menu fr [${fr#VSM_MENU_LISTE : }] ; en [${en#VSM_MENU_LISTE : }]"
ORIGINE="0:30/20 480:60/64 960:90/120"

verdict "(1) le témoin à la plage entière : les notes d'origine" "$([ "$np" = "$ORIGINE" ] && echo 1 || echo 0)"
verdict "(2) limiter les hauteurs à 48-72 : 54, 60, 66, les vélocités intactes" \
    "$([ "$nl" = "0:54/20 480:60/64 960:66/120" ] && echo 1 || echo 0)"
verdict "(3) filtrer les hauteurs : 60 seule, et « 2 note(s) écartée(s) hors de la plage » dit" \
    "$([ "$nf" = "480:60/64" ] && echo "$dit" | grep -q "2 note(s) écartée(s) hors de la plage" && echo 1 || echo 0)"
verdict "(4) limiter les vélocités à 40-100 : 40, 64, 100, les hauteurs intactes" \
    "$([ "$nv" = "0:30/40 480:60/64 960:90/100" ] && echo 1 || echo 0)"
verdict "(5) « Ajouter : Plage » (« Add: Range ») ; ajouté, l'effet arrive à la plage entière" \
    "$([ -n "$fr" ] && [ -n "$en" ] && [ "$cv" = "range:0:0:127" ] && echo 1 || echo 0)"
verdict "(6) un pas : Ctrl+Z après le report rend les trois notes et la chaîne" \
    "$([ "$nz" = "$ORIGINE" ] && [ "$cz" = "range:1:48:72" ] && grep -q "Effets MIDI report" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PLAGE-EFFET-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PLAGE-EFFET-MIDI : la plage limite ou filtre les notes jouées, dit ce qu'elle écarte, et se reporte"
