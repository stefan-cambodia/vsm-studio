#!/usr/bin/env bash
# D567.2 — AGRANDIR LES PISTES CHOISIES (Affichage ▸ Hauteur des pistes ▸ Agrandir les pistes choisies ; le *Zoom
# Selected Tracks* de Cubase), MESURÉ PAR LE PROJET RELU (`height`) ET PAR LE RELEVÉ DE L'ARRANGEMENT.
#
# LES RÈGLES GARDÉES. Quatre pistes à la hauteur normale (56 px) ; B et C choisies (B, puis Maj+Alt+↓).
#   (1) A et D à la hauteur minimale (24 px) ; B et C à la même hauteur, plus grande que 56 et au plus 400 ; c'est
#       celle que le journal annonce ;
#   (2) la hauteur des pistes agrandies remplit l'arrangement : (2 × la leur) + 2 × 24 ne dépasse pas sa hauteur, et
#       n'en laisse pas plus d'un rang de 24 px ;
#   (3) le témoin : le même geste, A seule choisie — A agrandie, B, C, D à 24 ;
#   (4) un pas : Ctrl+Z rend les quatre à 56, après un geste qui a eu lieu.
#
#   tools/agrandir-pistes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-agrandir.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += bytes([0x00, 0x90, 60, 100, 0x8f, 0x00, 0x80, 60, 0]) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 4, 480) + piste("A", tempo=True) + piste("B") + piste("C") + piste("D"))
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "name": n, "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0}} for c, n in enumerate("ABCD")]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_ARRANGEMENT=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # « nom:hauteur » de chaque piste (56 quand le fichier ne l'écrit pas), ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{t['name']}:{int(t.get('height', 56))}\" for t in d['tracks']))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Affichage > Hauteur des pistes > Agrandir les pistes choisies"
CHOISIR="700:touche:shift + alt + cursor down"

echo "=== D567.2 : agrandir les pistes choisies ==="
for x in deux une annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course deux "$brouillon/deux" VSM_DELAI=2600 VSM_VUE="sans-rapport,arrangement,piste:1" \
    VSM_GESTE_APRES="$CHOISIR;1200:$GESTE;1700:relever-arrangement;1900:enregistrer:$brouillon/deux"
course une "$brouillon/une" VSM_DELAI=2600 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1900:enregistrer:$brouillon/une"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement,piste:1" \
    VSM_GESTE_APRES="$CHOISIR;1200:$GESTE;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"

hd="$(hauteurs "$brouillon/deux")"; hu="$(hauteurs "$brouillon/une")"; hz="$(hauteurs "$brouillon/annule")"
annonce="$(sed -n 's/^VSM_HAUTEURS : 2 piste(s) choisie(s) à \([0-9]*\) px.*/\1/p' "$brouillon/deux.txt" | head -1)"
zone="$(grep -h "^VSM_ARRANGEMENT : pistes visibles" "$brouillon/deux.txt" | tail -1 | sed -n 's/.*zone \([0-9]*\) px.*/\1/p')"
grep -h "^VSM_HAUTEURS\|^VSM_ARRANGEMENT : pistes" "$brouillon/deux.txt" | sed 's/^/       /'
echo "       deux [$hd] ; une [$hu] ; Ctrl+Z [$hz] ; annoncé ${annonce:-?} px, zone ${zone:-?} px"
b="$(printf '%s\n' "$hd" | tr ' ' '\n' | sed -n 's/^B://p')"; c="$(printf '%s\n' "$hd" | tr ' ' '\n' | sed -n 's/^C://p')"

verdict "(1) A et D à 24 ; B et C à la même hauteur, entre 57 et 400, celle que le journal annonce" \
    "$([ -n "$b" ] && [ "$b" = "$c" ] && [ "$b" -gt 56 ] && [ "$b" -le 400 ] && [ "$b" = "${annonce:-x}" ] \
       && [ "$hd" = "A:24 B:$b C:$b D:24" ] && echo 1 || echo 0)"
verdict "(2) les pistes remplissent l'arrangement, à un rang près" \
    "$([ -n "$zone" ] && [ -n "$b" ] && [ $((2 * b + 48)) -le "$zone" ] && [ $((zone - 2 * b - 48)) -lt 24 ] && echo 1 || echo 0)"
verdict "(3) le témoin, A seule choisie : A agrandie, les autres à 24" \
    "$(printf '%s\n' "$hu" | grep -qE '^A:([0-9]+) B:24 C:24 D:24$' && [ "$(printf '%s\n' "$hu" | sed -n 's/^A:\([0-9]*\) .*/\1/p')" -gt 56 ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les quatre à 56, après un geste qui a eu lieu" \
    "$([ "$hz" = "A:56 B:56 C:56 D:56" ] && grep -q "^VSM_HAUTEURS : 2 piste(s) choisie(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AGRANDIR-PISTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AGRANDIR-PISTES : les pistes choisies se partagent l'arrangement, les autres au plus petit"
