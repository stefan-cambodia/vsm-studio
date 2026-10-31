#!/usr/bin/env bash
# D553.3 — FUSIONNER LE MIDI ENTRE LES LOCATEURS (Édition ▸ Fusionner le MIDI entre les locateurs, le *Merge MIDI in
# Loop* de Cubase), MESURÉ PAR LE PROJET RELU ET PAR LE .mid JOUÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM : une mesure = 1 920 ticks). Les locateurs de 2 · 1 à 4 · 1
# [1920, 5760). Quatre pistes :
#   « Basse » (vsm.sh101) : 40 à 0 (AVANT la plage), 38 à 1 680 (partie avant elle, encore tenue à 1 920), 40 à 1 920,
#     43 à 3 840, 45 à 5 280 tenue 960 (qui DÉBORDE la fin) ;
#   « Lead » (vsm.minimoog, transposée de −12) : 72 à 2 400, 74 à 4 320 ;
#   « Muette » (piste muette) : 60 à 2 400 ;  « Clip muet » (son clip muet) : 65 à 2 880.
# La piste choisie est « Basse ».
#   (1) le projet relu : une piste NEUVE, juste sous « Basse », avec sa machine (vsm.sh101), un clip [1920, 5760), et
#       pour notes exactement { 40@1920·480, 43@3840·480, 45@5280·480 (coupée à la fin), 60@2400·480 et 62@4320·480
#       (la transposition appliquée) } — ni la note d'avant, ni celle qui part avant la plage, ni les muettes ;
#   (2) le .mid JOUÉ : la piste neuve fait entendre ce multiensemble, au tick près, et les quatre pistes d'origine
#       jouent ce qu'elles jouaient (le témoin, exporté du projet non touché) ;
#   (3) le SOLO respecté : « Lead » en solo → seules ses deux notes, 60 et 62 ;
#   (4) un pas : Ctrl+Z → les quatre pistes d'avant ;
#   (5) rien d'entendu entre les locateurs (posés sur [7680, 9600)) : « Rien à fusionner » dit, aucun pas, aucune piste ;
#   (6) D610 (g) : « Basse » porte une carte de batterie qui ÉCHANGE 38 et 40 (D609.1) — la fusion fait entendre ce que
#       « Basse » fait entendre (38 à 1 920), et la piste fusionnée n'a PAS de carte (ses notes la portent déjà) ; le code
#       d'avant recopiait la carte, et la note fusionnée se rejouait 40.
#
#   tools/fusionner-midi.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fusionner-midi.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, notes, tempo=False):   # notes : (début, hauteur, durée)
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    ev = sorted([(d, 1, h) for d, h, l in notes] + [(d + l, 0, h) for d, h, l in notes])
    t = 0
    for tick, on, h in ev:
        corps += vlq(tick - t) + bytes([(0x90 if on else 0x80) | canal, h, 100 if on else 0]); t = tick
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def mix(muet=False, solo=False):
    return {"muted": muet, "pan": 0.0, "sends": [0.0, 0.0], "solo": solo, "volume": 0.8}
clip = lambda n, **x: dict({"start": 0, "length": 9600, "sourceStart": 0, "sourceLength": 9600, "name": n}, **x)
basse = [(0, 40, 480), (1680, 38, 480), (1920, 40, 480), (3840, 43, 480), (5280, 45, 960)]
def ecrire(nom, boucle, solo_lead=False, carte=False):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    mid = [piste("Basse", 0, basse, tempo=True), piste("Lead", 1, [(2400, 72, 480), (4320, 74, 480)]),
           piste("Muette", 2, [(2400, 60, 480)]), piste("Clip muet", 3, [(2880, 65, 480)])]
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, len(mid), 480) + b"".join(mid))
    e = lambda n, c, m, machine, clips, **x: dict({"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n,
        "mix": m, "instrument": {"preferredPlugin": machine}, "clips": clips}, **x)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                             "loop": {"enabled": False, "startTick": boucle[0], "endTick": boucle[1]}},
               "tracks": [e("Basse", 0, mix(), "vsm.sh101", [clip("B")],
                            **({"drumMap": [{"in": 38, "out": 40}, {"in": 40, "out": 38}]} if carte else {})),
                          e("Lead", 1, mix(solo=solo_lead), "vsm.minimoog", [clip("L")], transpose=-12),
                          e("Muette", 2, mix(muet=True), "vsm.minimoog", [clip("M")]),
                          e("Clip muet", 3, mix(), "vsm.minimoog", [clip("C", muted=True)])]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
ecrire("F", (1920, 5760))
ecrire("F-solo", (1920, 5760), solo_lead=True)
ecrire("F-vide", (7680, 9600))
ecrire("F-carte", (1920, 5760), carte=True)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
pistes() {   # « nom/machine/clips » de chaque piste du projet relu, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + '/' + (t.get('instrument') or {}).get('preferredPlugin', '?') + '/'
                 + ','.join(f\"{int(c.get('start', 0))}+{int(c.get('length', 0))}\" for c in t.get('clips', []))
                 for t in p['tracks']))
" "$1"
}
notes() {   # le multiensemble trié « début:hauteur:durée » d'une piste d'un .mid (par fragment de nom), ou ABSENT
    "$PY" - "$1" "$2" <<'PY2'
import sys
from collections import Counter
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
for tr in m.tracks:
    nom = next((x.name for x in tr if x.type == "track_name"), "")
    if sys.argv[2] not in nom:
        continue
    t, ouvertes, faites = 0, {}, Counter()
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            d = ouvertes[x.note].pop(0); faites[(d, x.note, t - d)] += 1
    print(" ".join(f"{d}:{h}:{l}" for (d, h, l), k in sorted(faites.items()) for _ in range(k)) or "RIEN"); sys.exit(0)
print("PISTE-ABSENTE")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement,piste:0"
FUSION="menu:Fusionner le MIDI entre les locateurs"
ATTENDU="1920:40:480 2400:60:480 3840:43:480 4320:62:480 5280:45:480"

echo "=== D553.3 : fusionner le MIDI entre les locateurs ==="
for x in Ff Fz; do cp -r "$brouillon/F" "$brouillon/$x"; done
course fusion "$brouillon/Ff" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$FUSION;1700:enregistrer:$brouillon/Ff;2100:exporter-midi:$brouillon/Ff.mid"
course temoin "$brouillon/F" VSM_DELAI=2000 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:exporter-midi:$brouillon/F.mid"
course solo "$brouillon/F-solo" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$FUSION;1700:enregistrer:$brouillon/F-solo"
course annuler "$brouillon/Fz" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$FUSION;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Fz"
course vide "$brouillon/F-vide" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$FUSION;1500:relever-historique;1800:enregistrer:$brouillon/F-vide"
course carte "$brouillon/F-carte" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$FUSION;1700:enregistrer:$brouillon/F-carte"

p="$(pistes "$brouillon/Ff/project.json")"; n="$(notes "$brouillon/Ff/midi/arrangement.mid" fusion)"
j="$(notes "$brouillon/Ff.mid" fusion)"; s="$(notes "$brouillon/F-solo/midi/arrangement.mid" fusion)"
z="$(pistes "$brouillon/Fz/project.json")"; v="$(pistes "$brouillon/F-vide/project.json")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/vide.txt" | tail -1)"
origines=1
for nom in Basse Lead Muette "Clip muet"; do
    a="$(notes "$brouillon/F.mid" "$nom")"; b="$(notes "$brouillon/Ff.mid" "$nom")"
    case "$a" in ABSENT|PISTE-ABSENTE) origines=0 ;; esac
    [ "$a" = "$b" ] || { origines=0; echo "       « $nom » joué : témoin [$a], après la fusion [$b]"; }
done
echo "       pistes [$p]"
echo "       notes de la fusion : projet relu [$n] ; .mid joué [$j] ; avec le solo de Lead [$s]"
echo "       Ctrl+Z [$z] ; plage vide [$v] ; ${hist:-historique non relevé}"
grep -h "^VSM_FUSION_MIDI" "$brouillon/fusion.txt" "$brouillon/solo.txt" "$brouillon/vide.txt" | sed 's/^/       /'

verdict "(1) le projet relu : une piste neuve sous la piste choisie, sa machine, un clip sur la plage, ce qui s'entend" \
    "$(echo "$p" | grep -q "^Basse/vsm.sh101/0+9600 | Basse — fusion (mesures 2-3)/vsm.sh101/1920+3840 | Lead/" \
       && [ "$n" = "$ATTENDU" ] && echo 1 || echo 0)"
verdict "(2) le .mid joué : la piste neuve fait entendre la fusion, les pistes d'origine ce qu'elles jouaient" \
    "$([ "$j" = "$ATTENDU" ] && [ "$origines" = 1 ] && echo 1 || echo 0)"
verdict "(3) le solo respecté : seules les notes de la piste en solo" "$([ "$s" = "2400:60:480 4320:62:480" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les quatre pistes d'avant" \
    "$([ "$(echo "$z" | tr '|' '\n' | wc -l)" = 4 ] && ! echo "$z" | grep -q fusion && echo 1 || echo 0)"
c="$(notes "$brouillon/F-carte/midi/arrangement.mid" fusion)"
cc="$(python3 -c "import json,sys; t=[t for t in json.load(open(sys.argv[1]))['tracks'] if 'fusion' in t['name']]; print('ABSENTE' if not t else ('carte ' + str(len(t[0].get('drumMap', []))) + ' entrée(s)'))" "$brouillon/F-carte/project.json" 2>/dev/null)"
echo "       (6) avec la carte 38 ↔ 40 sur « Basse » : notes de la fusion [$c] ; piste fusionnée : ${cc:-illisible}"
verdict "(6) D610 : la fusion fait entendre 38 (ce que « Basse » joue), et la piste fusionnée n'a pas de carte" \
    "$([ "$c" = "1920:38:480 2400:60:480 3840:43:480 4320:62:480 5280:45:480" ] && [ "$cc" = "carte 0 entrée(s)" ] && echo 1 || echo 0)"
verdict "(5) rien d'entendu : « Rien à fusionner » dit, aucun pas, aucune piste" \
    "$([ "$(echo "$v" | tr '|' '\n' | wc -l)" = 4 ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
       && grep -q "^VSM_BOITE : Rien à fusionner" "$brouillon/vide.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FUSIONNER-MIDI : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FUSIONNER-MIDI : ce que les pistes font entendre entre les locateurs tient sur une piste neuve"
