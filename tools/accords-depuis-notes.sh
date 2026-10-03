#!/usr/bin/env bash
# D543.1 — CRÉER LA LIGNE D'ACCORDS D'APRÈS LES NOTES, MESURÉ PAR project.json.
#
# LA RÈGLE GARDÉE. Une piste de quatre mesures : do-mi-sol, la-do-mi-sol, si-ré-sol (si à la basse),
# puis une note seule.
#   (1) Édition ▸ « Créer les accords d'après les notes (piste active) » : la ligne enregistrée vaut
#       « 0:C 1920:Am7 3840:G/B », la quatrième mesure comptée sans accord net (journal) ;
#   (2) Ctrl+Z : la ligne d'avant (vide) ;
#   (3) le geste rejoué sur la ligne qu'il vient d'écrire : rien à changer, aucun pas (le journal et
#       l'historique le disent).
#
#   tools/accords-depuis-notes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-accords-notes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/quatre/midi"
python3 - "$brouillon/quatre" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n:
        o.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(o))
mesures = [[60, 64, 67], [57, 60, 64, 67], [59, 62, 67], [72]]
evs = []
for m, hauteurs in enumerate(mesures):
    for h in hauteurs:
        evs.append((m * 1920, 1, bytes([0x90, h, 100])))
        evs.append((m * 1920 + 1920, 0, bytes([0x80, h, 0])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano", 0
for tick, _, o in evs:
    corps += vlq(tick - t) + o; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/quatre" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_DELAI=3500 \
        VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|grisée)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
ligne() {   # « tick:symbole … » de la ligne d'accords d'un project.json, « (vide) » ou « ABSENT »
    python3 -c "
import json, sys
try:
    j = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{a['tick']}:{a['chord']}\" for a in j.get('chords', [])) or '(vide)')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
M="menu:Édition > Créer les accords d'après les notes (piste active)"

echo "=== D543.1 : la ligne d'accords d'après les notes ==="
course creer "1200:$M;1600:enregistrer:$brouillon/cree;2000:touche:ctrl + Z;2400:enregistrer:$brouillon/annule;\
2800:touche:ctrl + Y;3000:$M;3200:relever-historique"
c="$(ligne "$brouillon/cree/project.json")"; u="$(ligne "$brouillon/annule/project.json")"
grep "^VSM_ACCORDS_DEPUIS_NOTES" "$brouillon/creer.txt" | sed 's/^/       /'
echo "       créée {$c} ; Ctrl+Z {$u}"
verdict "(1) la ligne « 0:C 1920:Am7 3840:G/B », une mesure sans accord net" \
    "$([ "$c" = "0:C 1920:Am7 3840:G/B" ] && grep -q "^VSM_ACCORDS_DEPUIS_NOTES : « Piano » — 3 accord(s) sur 4 mesure(s), 1 sans accord net" "$brouillon/creer.txt" && echo 1 || echo 0)"
verdict "(2) Ctrl+Z : la ligne d'avant (vide)" "$([ "$u" = "(vide)" ] && echo 1 || echo 0)"
pas="$(grep "^VSM_HISTORIQUE_PAS" "$brouillon/creer.txt" | tail -1 | cut -d: -f2 | tr -d ' ')"
echo "       pas d'historique après rétablissement et second geste : ${pas:-?}"
verdict "(3) le geste rejoué sur sa propre ligne : aucun pas de plus" "$([ "${pas:-}" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ACCORDS D'APRÈS LES NOTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ACCORDS D'APRÈS LES NOTES : la ligne se lit dans les notes, et s'annule"
