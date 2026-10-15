#!/usr/bin/env bash
# D555.2 — SUPPRIMER LES CONTRÔLEURS D'UNE PISTE (Piste ▸ Supprimer les contrôleurs MIDI de la piste (N) ; le *Delete
# Controllers* de Cubase), MESURÉ PAR LE .mid JOUÉ ET PAR LE MENU.
#
# LES RÈGLES GARDÉES. Une piste : deux notes, et CC 0 (banque), CC 1, CC 7, un pli, une pression de canal, un
# changement de programme.
#   (1) le TÉMOIN : le .mid joué porte les deux notes, 3 CC, 1 pli, 1 pression, 1 programme ;
#   (2) le geste : les deux notes intactes, 0 CC, 0 pli, 0 pression — et le programme GARDÉ, le 5 et non un autre ;
#       « 3 CC (dont 1 de banque), 1 pli(s), 1 pression(s) de canal, 0 pression(s) polyphonique(s) » dit. LE NUMÉRO, et
#       pas le compte : une piste sans changement de programme en reçoit un à l'export, tiré de sa machine
#       (vsm.juno106 → 51) — compté, le mutant qui efface les programmes passait au vert ;
#   (3) le MENU, lu avant et après : « (5) » actif, puis « (0) [grisée] » — le fichier relu ne dit pas tout (D553.1) ;
#   (4) un pas : Ctrl+Z → le .mid du témoin.
#
#   tools/controleurs-retires.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-controleurs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
ev = [(0, 0, bytes([0xB0, 0, 1])), (0, 1, bytes([0xC0, 5])), (0, 2, bytes([0xB0, 7, 100])), (0, 3, bytes([0x90, 60, 100])),
      (240, 2, bytes([0xB0, 1, 64])), (240, 2, bytes([0xE0, 0x00, 0x50])), (240, 2, bytes([0xD0, 80])),
      (480, 0, bytes([0x80, 60, 0])), (960, 3, bytes([0x90, 64, 100])), (1440, 0, bytes([0x80, 64, 0]))]
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

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
contenu() {   # « notes début-fin:hauteur | CC n | pli n | pression n | programme numéros » d'un .mid, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
notes, cc, pb, cp, pc = [], 0, 0, 0, []
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            notes.append((ouvertes[x.note].pop(0), t, x.note))
        elif x.type == "control_change": cc += 1
        elif x.type == "pitchwheel": pb += 1
        elif x.type == "aftertouch": cp += 1
        elif x.type == "program_change": pc.append(str(x.program))
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(notes)) + f" | CC {cc} | pli {pb} | pression {cp} | programme {','.join(pc) or '-'}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Supprimer les contrôleurs MIDI de la piste"
VUE="sans-rapport,arrangement,piste:0"

echo "=== D555.2 : supprimer les contrôleurs d'une piste ==="
course retirer VSM_DELAI=2800 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="900:lister-menus;1200:$GESTE;1500:lister-menus;1800:exporter-midi:$brouillon/retire.mid"
course temoin VSM_DELAI=2000 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:exporter-midi:$brouillon/temoin.mid"
course annuler VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:exporter-midi:$brouillon/annule.mid"

r="$(contenu "$brouillon/retire.mid")"; t="$(contenu "$brouillon/temoin.mid")"; z="$(contenu "$brouillon/annule.mid")"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Piste > Supprimer les contrôleurs MIDI de la piste" "$brouillon/retirer.txt" | sed 's/^VSM_MENU_LISTE : //')
grep -h "^VSM_CONTROLEURS_RETIRES" "$brouillon/retirer.txt" | sed 's/^/       /'
echo "       témoin [$t]"
echo "       après  [$r] ; Ctrl+Z [$z]"
for x in "${m[@]}"; do echo "       menu : $x"; done

verdict "(1) le témoin : deux notes, 3 CC, 1 pli, 1 pression, le programme 5" \
    "$([ "$t" = "0-480:60 960-1440:64 | CC 3 | pli 1 | pression 1 | programme 5" ] && echo 1 || echo 0)"
verdict "(2) les contrôleurs partent, les notes et le programme restent, et c'est dit" \
    "$([ "$r" = "0-480:60 960-1440:64 | CC 0 | pli 0 | pression 0 | programme 5" ] \
       && grep -q "^VSM_CONTROLEURS_RETIRES : 3 CC (dont 1 de banque), 1 pli(s), 1 pression(s) de canal, 0 pression(s) polyphonique(s)" "$brouillon/retirer.txt" && echo 1 || echo 0)"
verdict "(3) le menu : cinq avant, zéro et grisée après" \
    "$([ "${#m[@]}" = 2 ] && echo "${m[0]}" | grep -q "(5)$" && echo "${m[1]}" | grep -q "(0) \[grisée\]$" && echo 1 || echo 0)"
# (4) LE GESTE A EU LIEU dans la course d'annulation : sans cela, un geste qui ne fait rien rendrait aussi le témoin
# (vu sur le binaire d'avant D555, où ce contrôle passait au vert).
verdict "(4) un pas : Ctrl+Z rend le témoin" \
    "$([ "$z" = "$t" ] && [ "$t" != ABSENT ] && grep -q "^VSM_CONTROLEURS_RETIRES : 3 CC" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CONTROLEURS-RETIRES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CONTROLEURS-RETIRES : la piste perd ses contrôleurs, garde ses notes et son programme"
