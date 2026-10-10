#!/usr/bin/env bash
# D547.1 — LE VERROU REFUSE LA SUPPRESSION, MESURÉ PAR LE PROJET RELU.
#
# LA RÈGLE GARDÉE. Deux pistes MIDI (480 ticks par noire) : « Verrouillée » (verrou de piste, D16.5) porte A ;
# « Libre » porte B, verrouillé lui-même (D546.3), et C, libre.
#   (1) tout choisi, Suppr : le projet relu garde A et B, C est parti ; le refus est DIT (« Montage refusé :
#       2 clips verrouillés sont restés en place ») ;
#   (2) le TÉMOIN, le même projet sans aucun verrou : les trois partent ;
#   (3) un pas : Suppr puis Ctrl+Z → A, B et C ;
#   (4) une suppression ENTIÈREMENT refusée (A seul choisi) ne laisse aucun pas d'historique, et se dit.
#
#   tools/verrou-suppression.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-verrou-suppr.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import copy, json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, canal, notes, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90 | canal, 60, 100]); corps += vlq(480) + bytes([0x80 | canal, 60, 0])
        t = debut + 480
    return corps + b"\x00\xff\x2f\x00"
pistes = [piste("Verrouillée", 0, [0], tempo=True), piste("Libre", 1, [0, 1920])]
mid = b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480)
for p in pistes:
    mid += b"MTrk" + struct.pack(">I", len(p)) + p
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
projet = {"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
          "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "locked": True, "mix": mix,
                      "instrument": {"preferredPlugin": "vsm.minimoog"}, "name": "Verrouillée",
                      "clips": [{"start": 0, "length": 960, "sourceStart": 0, "sourceLength": 960, "name": "A"}]},
                     {"channel": 1, "color": "#FF6BD0FF", "effects": [], "mix": mix,
                      "instrument": {"preferredPlugin": "vsm.minimoog"}, "name": "Libre",
                      "clips": [{"start": 0, "length": 960, "sourceStart": 0, "sourceLength": 960, "name": "B",
                                 "locked": True},
                                {"start": 1920, "length": 960, "sourceStart": 1920, "sourceLength": 960, "name": "C"}]}]}
for nom, p in (("base", projet), ("libre", None)):
    if p is None:
        p = copy.deepcopy(projet)
        p["tracks"][0].pop("locked"); p["tracks"][1]["clips"][0].pop("locked")
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(mid)
    json.dump(p, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début » des clips d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = sorted((x.get('name', '?'), int(x.get('start', 0))) for t in p['tracks'] for x in t.get('clips', []))
print(' '.join(f'{n}:{d}' for n, d in c) or 'AUCUN')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D547.1 : le verrou refuse la suppression ==="
for x in suppr annule seul; do cp -r "$brouillon/base" "$brouillon/$x"; done
course supprimer "$brouillon/suppr" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:enregistrer:$brouillon/suppr"
course temoin "$brouillon/libre" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:enregistrer:$brouillon/libre"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,tout-choisir" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1700:touche:ctrl + Z;2200:enregistrer:$brouillon/annule"
course refuser "$brouillon/seul" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,premier-clip:0" \
    VSM_GESTE_APRES="1200:touche:arrangement:delete;1600:relever-historique;1800:enregistrer:$brouillon/seul"

cs="$(clips "$brouillon/suppr/project.json")"; ct="$(clips "$brouillon/libre/project.json")"
ca="$(clips "$brouillon/annule/project.json")"; cr="$(clips "$brouillon/seul/project.json")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/refuser.txt" | tail -1)"
echo "       clips relus : Suppr [$cs] ; témoin sans verrou [$ct] ; Suppr puis Ctrl+Z [$ca] ; A seul [$cr]"
grep -h "^VSM_BOITE : Montage refusé" "$brouillon/supprimer.txt" "$brouillon/refuser.txt" | sed 's/^/       /' | cut -c1-120
echo "       ${hist:-historique non relevé}"
verdict "(1) A et B restent, C part, le refus est dit" \
    "$([ "$cs" = "A:0 B:0" ] && grep -q "^VSM_BOITE : Montage refusé : 2 clips verrouillés sont restés en place" "$brouillon/supprimer.txt" \
        && echo 1 || echo 0)"
verdict "(2) le témoin sans verrou : les trois partent" "$([ "$ct" = "AUCUN" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend A, B et C" "$([ "$ca" = "A:0 B:0 C:1920" ] && echo 1 || echo 0)"
verdict "(4) entièrement refusée : aucun pas, le refus dit, rien de parti" \
    "$([ "$cr" = "A:0 B:0 C:1920" ] && [ "${hist%% :*}" = "VSM_HISTORIQUE_PAS" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place" "$brouillon/refuser.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "VERROU-SUPPRESSION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "VERROU-SUPPRESSION : un clip verrouillé ne se supprime pas, le refus est dit, sans pas pour rien"
