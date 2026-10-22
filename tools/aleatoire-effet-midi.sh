#!/usr/bin/env bash
# D575.3 — L'ALÉATOIRE EN EFFET MIDI (Piste ▸ Effets MIDI ▸ Ajouter : Aléatoire ; le *Random* des modificateurs MIDI
# de Cubase, rendu REPRODUCTIBLE), MESURÉ PAR LE PROJET RELU APRÈS LE REPORT DANS LES NOTES (D31.5).
#
# LES RÈGLES GARDÉES. Seize notes, une par noire à partir de 240. La chaîne porte l'effet « random », *Position*, écrit
# au projet avant l'ouverture. L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D575) :
#   (1) le TÉMOIN à 0..0 : reporté, les notes d'origine ;
#   (2) −30..+30, graine 7 : tous les départs dans ±30 de l'origine, la durée gardée, au moins un qui bouge ;
#   (3) le MÊME tirage dans un AUTRE lancement : note pour note (le tirage ne dépend pas des identifiants, que le
#       chargement redonne) — et différent de l'origine : deux reports qui ne font rien sont identiques aussi, et la
#       première version de ce contrôle passait ainsi sur le binaire de D574 ;
#   (4) une autre graine (8) : un autre tirage ;
#   (5) le menu propose « Ajouter : Aléatoire » (« Add: Random ») ; ajouté, l'effet arrive à 0..0 (il ne fait rien tant
#       qu'on ne le règle pas) ;
#   (6) un pas : Ctrl+Z après le report rend les notes et la chaîne.
#
#   tools/aleatoire-effet-midi.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-aleatoire-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(240 + 480 * i, 480 + 480 * i, 60 + i % 5) for i in range(16)]
ev = []
for d, f, h in notes:
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
hasard = lambda mini, maxi, graine: [{"type": "random", "parameters": {"Cible": 0, "Min": mini, "Max": maxi, "Graine": graine}}]
for nom, chaine in (("nul", hasard(0, 0, 7)), ("g7", hasard(-30, 30, 7)), ("g7bis", hasard(-30, 30, 7)),
                    ("g8", hasard(-30, 30, 8)), ("z", hasard(-30, 30, 7)), ("vide", [])):
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
chaine() {   # les effets MIDI de la piste d'un project.json (« type:Cible:Min:Max:Graine »), « (vide) », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
fx = d['tracks'][0].get('midiEffects', [])
p = lambda e, k: e.get('parameters', {}).get(k)
f = lambda v: '-' if v is None else f'{float(v):g}'
print(' '.join(e['type'] + ':' + ':'.join(f(p(e, k)) for k in ('Cible', 'Min', 'Max', 'Graine')) for e in fx) or '(vide)')" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REPORT="menu:Piste > Effets MIDI de la piste (1) > Reporter les effets MIDI dans les notes (définitif)"

echo "=== D575.3 : l'aléatoire en effet MIDI ==="
for k in nul g7 g7bis g8; do
    course "report-$k" "$brouillon/$k" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
        VSM_GESTE_APRES="1200:$REPORT;1900:enregistrer:$brouillon/$k"
done
course annuler "$brouillon/z" VSM_DELAI=3000 VSM_VUE="sans-rapport,piste:0" VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="1200:$REPORT;1700:touche:ctrl + Z;2300:enregistrer:$brouillon/z"
course ajouter "$brouillon/vide" VSM_DELAI=2600 VSM_VUE="sans-rapport,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:menu:Piste > Effets MIDI de la piste (0) > Ajouter : Aléatoire;1900:enregistrer:$brouillon/vide"
course anglais "$brouillon/nul" VSM_DELAI=1800 VSM_VUE="sans-rapport" VSM_LANGUE=en VSM_MENU_LISTE=1

n0="$(notes "$brouillon/nul/midi/arrangement.mid")"; n7="$(notes "$brouillon/g7/midi/arrangement.mid")"
n7b="$(notes "$brouillon/g7bis/midi/arrangement.mid")"; n8="$(notes "$brouillon/g8/midi/arrangement.mid")"
nz="$(notes "$brouillon/z/midi/arrangement.mid")"; cz="$(chaine "$brouillon/z")"; cv="$(chaine "$brouillon/vide")"
fr="$(grep -h "^VSM_MENU_LISTE : Piste > Effets MIDI de la piste (0) > Ajouter : Aléatoire" "$brouillon/ajouter.txt" | head -1)"
en="$(grep -h "^VSM_MENU_LISTE : .* > Add: Random" "$brouillon/anglais.txt" | head -1)"
ORIGINE="$(python3 -c "print(' '.join(f'{240 + 480 * i}-{480 + 480 * i}:{60 + i % 5}' for i in range(16)))")"
echo "       0..0 : [$n0]"
echo "       −30..+30, graine 7 : [$n7]"
echo "       le même, autre lancement : [$n7b]"
echo "       graine 8 : [$n8]"
echo "       Ctrl+Z après le report : chaîne [$cz] ; ajouté par le menu : [$cv]"
echo "       menu fr [${fr#VSM_MENU_LISTE : }] ; en [${en#VSM_MENU_LISTE : }]"
dans30="$(python3 -c "
import sys
o = sys.argv[1].split(); t = sys.argv[2].split()
if len(o) != 16 or len(t) != 16: print(0); sys.exit(0)
def p(x):
    d, r = x.split('-'); f, h = r.split(':'); return int(d), int(f), int(h)
bouge = 0
for a, b in zip(sorted(o, key=lambda x: (p(x)[2], p(x)[0])), sorted(t, key=lambda x: (p(x)[2], p(x)[0]))):
    (d0, f0, h0), (d1, f1, h1) = p(a), p(b)
    if h0 != h1 or abs(d1 - d0) > 30 or f1 - d1 != f0 - d0: print(0); sys.exit(0)
    bouge += d1 != d0
print(1 if bouge > 0 else 0)" "$ORIGINE" "$n7")"

verdict "(1) le témoin à 0..0 : les notes d'origine" "$([ "$n0" = "$ORIGINE" ] && echo 1 || echo 0)"
verdict "(2) −30..+30 : tous les départs dans ±30, la durée gardée, au moins un qui bouge" "$dans30"
verdict "(3) le même tirage dans un autre lancement, note pour note — et un tirage qui a eu lieu" \
    "$([ -n "$n7" ] && [ "$n7" = "$n7b" ] && [ "$n7" != ABSENT ] && [ "$n7" != "$ORIGINE" ] && echo 1 || echo 0)"
verdict "(4) une autre graine, un autre tirage" "$([ "$n8" != "$n7" ] && [ "$n8" != ABSENT ] && echo 1 || echo 0)"
verdict "(5) « Ajouter : Aléatoire » (« Add: Random ») ; ajouté, à 0..0" \
    "$([ -n "$fr" ] && [ -n "$en" ] && [ "$cv" = "random:0:0:0:1" ] && echo 1 || echo 0)"
verdict "(6) un pas : Ctrl+Z après le report rend les notes et la chaîne" \
    "$([ "$nz" = "$ORIGINE" ] && [ "$cz" = "random:0:-30:30:7" ] && grep -q "Effets MIDI report" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ALEATOIRE-EFFET-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ALEATOIRE-EFFET-MIDI : l'aléatoire tire dans ses bornes, le même à chaque lancement, et se reporte"
