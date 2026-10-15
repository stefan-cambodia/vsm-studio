#!/usr/bin/env bash
# D554.3 — LES PÉDALES EN DURÉES DE NOTES (piano roll ▸ Temps et durée ▸ Pédale en durées de notes ; le *Pedals to Note
# Length* de Cubase), MESURÉ PAR LE .mid JOUÉ ET PAR L'EXPORT.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM). Projet P, machine MACHINE : la pédale enfoncée de 0 à 1 440
# (CC 64 à 127, puis à 0), sous elle 60 [0, 240), 64 [480, 720), 67 [960, 1200) ; après elle, 72 [1920, 2160).
#   (1) le geste : le .mid joué dit « 0-1440:60 480-1440:64 960-1440:67 1920-2160:72 » et ne porte plus aucun CC 64 ;
#       « 3 note(s) allongée(s), 2 CC 64 retiré(s) » dit ;
#   (2) L'EXPORT NE CHANGE PAS : écart maximal entre l'export converti et celui du projet d'origine sous 1e-4 — le
#       graphe faisait déjà sonner la pédale ainsi (D25.1). Deux TÉMOINS le rendent lisible : le projet d'origine
#       exporté deux fois (la machine est-elle déterministe ? sinon l'écart ne veut rien dire) et le projet SANS ses
#       CC 64 (la pédale s'entend-elle ? un écart sous 1e-2 dirait qu'on ne mesure rien) ;
#   (3) un pas : Ctrl+Z → le .mid du témoin, CC 64 compris ;
#   (4) LA RE-FRAPPE (projet R) : 60 [0, 240) et 60 [960, 1200) sous la même pédale → « 0-960:60 960-1440:60 » — une
#       note ne se chevauche pas elle-même ; l'écart d'export qui en résulte est PUBLIÉ (le graphe, lui, garde la
#       première voix jusqu'au relâchement), pas jugé ;
#   (5) une piste sans pédale : l'entrée « Pédale en durées de notes (0 appui(s)) » grisée, au menu Édition.
#
#   tools/pedales-en-durees.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
MACHINE="${VSM_MACHINE:-vsm.juno106}"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pedales.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" "$MACHINE" <<'PY'
import json, os, struct, sys
b, machine = sys.argv[1], sys.argv[2]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def ecrire(nom, notes, pedale):
    ev = []
    for d, f, h in notes:
        ev.append((d, 2, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
    for t, v in pedale:
        ev.append((t, 1, bytes([0xB0, 64, v])))
    ev.sort(key=lambda e: (e[0], e[1]))   # à tick égal : fins, pédale, attaques
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano"
    t = 0
    for tick, _, m in ev:
        corps += vlq(tick - t) + m; t = tick
    corps += b"\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Piano", "mix": mix,
                           "instrument": {"preferredPlugin": machine},
                           "clips": [{"start": 0, "length": 3840, "sourceStart": 0, "sourceLength": 3840, "name": "P"}]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
P = [(0, 240, 60), (480, 720, 64), (960, 1200, 67), (1920, 2160, 72)]
ecrire("P", P, [(0, 127), (1440, 0)])
ecrire("P-sans", P, [])
ecrire("R", [(0, 240, 60), (960, 1200, 60)], [(0, 127), (1440, 0)])
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « début-fin:hauteur » des notes d'un .mid, dans l'ordre, puis « | CC64:n », ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out, cc64 = [], 0
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "control_change" and x.control == 64:
            cc64 += 1
        elif x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            out.append((ouvertes[x.note].pop(0), t, x.note))
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(out)) + f" | CC64:{cc64}")
PY2
}
ecart() {   # l'écart maximal entre deux exports, ou « ABSENT » si l'un manque ou se tait
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(b) == 0 or float(np.max(np.abs(a))) == 0.0 or float(np.max(np.abs(b))) == 0.0:
    print("ABSENT"); sys.exit(0)
n = min(len(a), len(b))
print(f"{float(np.max(np.abs(a[:n] - b[:n]))):.3g}")
PY2
}
moins() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[1]) < float(sys.argv[2]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="pianoroll:Pédale en durées de notes (1 appui(s))"   # le libellé porte le compte d'appuis

echo "=== D554.3 : les pédales en durées de notes ($MACHINE) ==="
for x in Pc Pz Rc; do cp -r "$brouillon/${x:0:1}" "$brouillon/$x"; done
course convertir "$brouillon/Pc" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/Pc.mid;1800:enregistrer:$brouillon/Pc"
course temoin "$brouillon/P" VSM_DELAI=2000 VSM_GESTE_APRES="1200:exporter-midi:$brouillon/P.mid"
course annuler "$brouillon/Pz" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:exporter-midi:$brouillon/Pz.mid"
course refrappe "$brouillon/Rc" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/Rc.mid;1800:enregistrer:$brouillon/Rc"
course vide "$brouillon/P-sans" VSM_DELAI=1500 VSM_MENU_LISTE=1
for x in P P-sans Pc R Rc; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done
course export-P2 "$brouillon/P" VSM_DELAI=1500 VSM_EXPORT="$brouillon/P2.wav"

c="$(notes "$brouillon/Pc.mid")"; t="$(notes "$brouillon/P.mid")"; z="$(notes "$brouillon/Pz.mid")"; r="$(notes "$brouillon/Rc.mid")"
e="$(ecart "$brouillon/P.wav" "$brouillon/Pc.wav")"; e2="$(ecart "$brouillon/P.wav" "$brouillon/P2.wav")"
es="$(ecart "$brouillon/P.wav" "$brouillon/P-sans.wav")"; er="$(ecart "$brouillon/R.wav" "$brouillon/Rc.wav")"
v="$(grep -h "^VSM_MENU_LISTE : Édition > Temps et durée > Pédale en durées de notes" "$brouillon/vide.txt" | head -1)"
grep -h "^VSM_PEDALES" "$brouillon/convertir.txt" "$brouillon/refrappe.txt" | sed 's/^/       /'
echo "       converti [$c] ; témoin [$t] ; Ctrl+Z [$z] ; re-frappe [$r]"
echo "       export, écart au témoin : converti $e ; témoin rejoué $e2 ; sans pédale $es ; re-frappe $er (publié)"
echo "       sans pédale, le menu : [${v#VSM_MENU_LISTE : }]"

verdict "(1) la pédale devient des durées, les CC 64 partent, et c'est dit" \
    "$([ "$c" = "0-1440:60 480-1440:64 960-1440:67 1920-2160:72 | CC64:0" ] \
       && grep -q "^VSM_PEDALES : 3 note(s) allongée(s), 2 CC 64 retiré(s)" "$brouillon/convertir.txt" && echo 1 || echo 0)"
verdict "(2) l'export ne change pas (sous 1e-4), la machine est déterministe et la pédale s'entend (au-dessus de 1e-2)" \
    "$(moins "$e" 1e-4 && moins "$e2" 1e-4 && ! moins "$es" 1e-2 && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le témoin, pédale comprise" "$([ "$z" = "$t" ] && echo "$t" | grep -q "CC64:2$" && echo 1 || echo 0)"
verdict "(4) la re-frappe : la première note s'arrête où la même hauteur revient" \
    "$([ "$r" = "0-960:60 960-1440:60 | CC64:0" ] && echo 1 || echo 0)"
verdict "(5) une piste sans pédale : l'entrée dit 0 et se grise" \
    "$(echo "$v" | grep -q "Pédale en durées de notes (0 appui(s)) \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PEDALES-EN-DUREES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PEDALES-EN-DUREES : ce que la pédale tenait s'écrit en durées, et l'export ne bouge pas"
