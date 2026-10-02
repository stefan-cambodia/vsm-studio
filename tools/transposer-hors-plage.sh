#!/usr/bin/env bash
# D536 — « TRANSPOSER » NE BORNE PLUS EN SILENCE : TOUT OU RIEN, ET C'EST DIT.
#
# LA RÈGLE GARDÉE. Deux notes choisies, Maj+↑ au piano roll (une octave), puis l'export :
#   (1) {64, 120} : le .mid exporté reste {64, 120} — rien n'a bougé, ni le 120 borné à 127
#       (l'ancien défaut), ni le 64 seul monté (un accord cassé en deux octaves) ;
#   (2) la ligne d'état le DIT (« … 1 note(s) sortiraient de la plage MIDI … rien n'a bougé »),
#       le journal aussi (VSM_TRANSPOSITION), et l'historique n'a AUCUN pas « Transposer » ;
#   (3) le TÉMOIN, {60, 64} : le même geste exporte {72, 76} et laisse son pas « Transposer +12 »
#       — sans lui, « rien n'a bougé » voudrait aussi dire « le geste n'a jamais eu lieu ».
# Chaque fichier se lit AVANT de juger, et les avertissements du journal sont relayés.
#
#   tools/transposer-hors-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-transposer.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/haut/midi" "$brouillon/temoin/midi"
python3 - "$brouillon" <<'PY'
import json, struct, sys
d = sys.argv[1]
def projet(dossier, hauteurs):
    evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Piano")]
    for h in hauteurs:
        evs.append((0, bytes([0x90, h, 100])))
    evs.append((480, bytes([0x80, hauteurs[0], 0])))
    for h in hauteurs[1:]:
        evs.append((0, bytes([0x80, h, 0])))
    def vlq(n):
        b = [n & 0x7F]; n >>= 7
        while n:
            b.append((n & 0x7F) | 0x80); n >>= 7
        return bytes(reversed(b))
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    open(f"{dossier}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                           "name": "Piano"}]}, open(f"{dossier}/project.json", "w"), indent=1)
projet(d + "/haut", [64, 120])
projet(d + "/temoin", [60, 64])
PY

course() {   # $1 = nom (et projet)
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/$nom" VSM_TAILLE="1600x1000" VSM_DELAI=4000 VSM_VUE="sans-rapport" \
        VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner" \
        VSM_GESTE_APRES="1200:touche:pianoroll:shift + cursor up;1600:exporter-midi:$brouillon/$nom.mid;1800:relever-etat;2000:relever-historique" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # le multiensemble des hauteurs jouées d'un .mid, trié — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
i, h = 14, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st = 0, 0
    while j < len(corps):
        while corps[j] & 0x80: j += 1
        j += 1
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: h.append(corps[j])
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(",".join(str(x) for x in sorted(h)) or "AUCUNE")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D536 : Transposer, tout ou rien ==="
course haut
course temoin
h1="$(hauteurs "$brouillon/haut.mid")"; h2="$(hauteurs "$brouillon/temoin.mid")"
etat="$(grep "^VSM_ETAT_PIANOROLL" "$brouillon/haut.txt" | tail -1)"
pas1="$(grep "^VSM_HISTORIQUE_PAS" "$brouillon/haut.txt" | tail -1)"
pas2="$(grep "^VSM_HISTORIQUE_PAS" "$brouillon/temoin.txt" | tail -1)"
grep "^VSM_TRANSPOSITION" "$brouillon/haut.txt" | sed 's/^/       /'
echo "       $etat"
echo "       {64,120} → {$h1} ; $pas1"
echo "       témoin {60,64} → {$h2} ; $pas2"
verdict "(1) {64, 120} reste {64, 120} : ni bornée, ni à moitié montée" "$([ "$h1" = "64,120" ] && echo 1 || echo 0)"
verdict "(2) la ligne d'état le dit : 1 note sortirait, rien n'a bougé" \
    "$(echo "$etat" | grep -q "1 note(s) sortiraient de la plage MIDI (0 à 127) — rien n'a bougé" && grep -q "^VSM_TRANSPOSITION : +12 refusée — 1 note(s)" "$brouillon/haut.txt" && echo 1 || echo 0)"
verdict "(2) aucun pas « Transposer » dans l'historique" "$([ -n "$pas1" ] && ! echo "$pas1" | grep -q "Transposer" && echo 1 || echo 0)"
verdict "(3) le témoin {60, 64} monte à {72, 76} et laisse son pas « Transposer +12 »" \
    "$([ "$h2" = "72,76" ] && echo "$pas2" | grep -q "Transposer +12" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TRANSPOSER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TRANSPOSER : tout ou rien, et c'est dit"
