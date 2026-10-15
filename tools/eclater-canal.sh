#!/usr/bin/env bash
# D555.3 — ÉCLATER UNE PISTE PAR CANAL (Piste ▸ Éclater par canal (N canaux) ; le *Dissolve Part ▸ Separate Channels*
# de Cubase), MESURÉ PAR LE PROJET RELU ET PAR LE .mid JOUÉ.
#
# LES RÈGLES GARDÉES. Une piste « Mêlée » (vsm.juno106) dont les notes portent trois canaux — 1 : 48 et 50 ; 3 : 60 ;
# 10 : 36, 38 et 42 —, un CC 7 sur le canal 1 et un pli sur le canal 3 ; une piste « Basse » après elle.
#   (1) le geste : 4 pistes — « Mêlée » (canal 1), « Mêlée — canal 3 », « Mêlée — canal 10 », « Basse » —, les trois
#       premières en vsm.juno106 ; « 2 piste(s) créée(s), canaux 1, 3, 10 » dit ;
#   (2) le .mid JOUÉ : chaque pièce fait entendre exactement les notes de son canal (multiensembles), le CC 7 sur
#       « Mêlée », le pli sur « Mêlée — canal 3 » ; « Basse » inchangée. Les noms s'y lisent « Mêlée - canal 3 » :
#       l'export écrit les noms de piste en Latin-1, où « ê » existe et le tiret cadratin non (vu au premier passage) ;
#   (3) le TÉMOIN : le .mid joué avant le geste porte les six notes sur la seule « Mêlée » ;
#   (4) un pas : Ctrl+Z → les deux pistes d'avant ;
#   (5) une piste d'un seul canal (« Basse » choisie) : « Éclater par canal (1 canaux) » grisée.
#
# L'EXPORT AUDIO N'EST PAS JUGÉ, et c'est voulu : le graphe tient le volume de canal (CC 7, D329) PAR PISTE, si bien que
# sur la piste d'origine le CC 7 du canal 1 réglait aussi les canaux 3 et 10 ; éclatée, chaque piste n'obéit plus qu'aux
# contrôleurs de son canal — ce que le fichier demandait. Le critère de réception ne promet que le projet et le .mid.
#
#   tools/eclater-canal.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-eclater-canal.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, ev, tempo=False):
    ev = sorted(ev, key=lambda e: (e[0], e[1]))
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for tick, _, m in ev:
        corps += vlq(tick - t) + m; t = tick
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def note(d, canal, h):
    return [(d, 2, bytes([0x90 | canal, h, 100])), (d + 240, 0, bytes([0x80 | canal, h, 0]))]
melee = note(0, 0, 48) + note(480, 0, 50) + note(240, 2, 60) + note(0, 9, 36) + note(480, 9, 38) + note(720, 9, 42) \
    + [(0, 1, bytes([0xB0, 7, 100])), (240, 1, bytes([0xE2, 0x00, 0x50]))]
basse = note(0, 1, 40) + note(960, 1, 43)
os.makedirs(f"{b}/base/midi", exist_ok=True)
pistes = [piste("Mêlée", melee, tempo=True), piste("Basse", basse)]
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + b"".join(pistes))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
e = lambda n, c: {"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                  "instrument": {"preferredPlugin": "vsm.juno106"},
                  "clips": [{"start": 0, "length": 1920, "sourceStart": 0, "sourceLength": 1920, "name": n}]}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [e("Mêlée", 0), e("Basse", 1)]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
pistes() {   # « nom/machine » de chaque piste du projet relu, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + '/' + (t.get('instrument') or {}).get('preferredPlugin', '?') for t in p['tracks']))
" "$1"
}
joue() {   # pour chaque piste d'un .mid : « nom: hauteurs triées ; CC n ; pli n », ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    nom = next((x.name for x in tr if x.type == "track_name"), "")
    try:
        nom = nom.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        pass
    h = sorted(x.note for x in tr if x.type == "note_on" and x.velocity > 0)
    cc = sum(1 for x in tr if x.type == "control_change" and x.control == 7)
    pb = sum(1 for x in tr if x.type == "pitchwheel")
    if h or cc or pb:
        out.append(f"{nom}: {','.join(map(str, h))} ; CC7 {cc} ; pli {pb}")
print(" | ".join(out) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Éclater par canal"

echo "=== D555.3 : éclater une piste par canal ==="
for x in E Z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course eclater "$brouillon/E" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1700:enregistrer:$brouillon/E;2100:exporter-midi:$brouillon/E.mid"
course temoin "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="1200:exporter-midi:$brouillon/temoin.mid"
course annuler "$brouillon/Z" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Z"
course seul "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement,piste:1" VSM_GESTE_APRES="1200:lister-menus"

p="$(pistes "$brouillon/E/project.json")"; j="$(joue "$brouillon/E.mid")"; t="$(joue "$brouillon/temoin.mid")"
z="$(pistes "$brouillon/Z/project.json")"
s="$(grep -h "^VSM_MENU_LISTE : Piste > Éclater par canal" "$brouillon/seul.txt" | head -1)"
grep -h "^VSM_ECLATER_CANAL" "$brouillon/eclater.txt" | sed 's/^/       /'
echo "       pistes [$p]"
echo "       joué   [$j]"
echo "       témoin [$t]"
echo "       Ctrl+Z [$z] ; une piste d'un canal : [${s#VSM_MENU_LISTE : }]"

verdict "(1) une piste par canal, le plus bas sur l'originale, la machine partout, et c'est dit" \
    "$([ "$p" = "Mêlée/vsm.juno106 | Mêlée — canal 3/vsm.juno106 | Mêlée — canal 10/vsm.juno106 | Basse/vsm.juno106" ] \
       && grep -q "^VSM_ECLATER_CANAL : 2 piste(s) créée(s), canaux 1, 3, 10" "$brouillon/eclater.txt" && echo 1 || echo 0)"
verdict "(2) le .mid joué : chaque pièce ses notes et ses contrôleurs, la basse intacte" \
    "$([ "$j" = "Mêlée: 48,50 ; CC7 1 ; pli 0 | Mêlée - canal 3: 60 ; CC7 0 ; pli 1 | Mêlée - canal 10: 36,38,42 ; CC7 0 ; pli 0 | Basse: 40,43 ; CC7 0 ; pli 0" ] && echo 1 || echo 0)"
verdict "(3) le témoin : les six notes sur la seule « Mêlée »" \
    "$([ "$t" = "Mêlée: 36,38,42,48,50,60 ; CC7 1 ; pli 1 | Basse: 40,43 ; CC7 0 ; pli 0" ] && echo 1 || echo 0)"
# (4) LE GESTE A EU LIEU dans la course d'annulation (sur le binaire d'avant D555, ce contrôle passait au vert).
verdict "(4) un pas : Ctrl+Z rend les deux pistes" \
    "$([ "$z" = "Mêlée/vsm.juno106 | Basse/vsm.juno106" ] && grep -q "^VSM_ECLATER_CANAL : 2 piste" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(5) une piste d'un seul canal : l'entrée dit 1 et se grise" \
    "$(echo "$s" | grep -q "Éclater par canal (1 canaux) \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ECLATER-CANAL : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ECLATER-CANAL : une piste par canal, chacune avec ce qu'elle jouait"
