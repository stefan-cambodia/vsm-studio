#!/usr/bin/env bash
# D566.3 — LIER LES TRANCHES CHOISIES (Mixage ▸ Lier les tranches choisies (Q-Link) ; le *Q-Link* de la MixConsole de
# Cubase), MESURÉ PAR LE PROJET RELU, LE TÉMOIN SANS LIAISON À CÔTÉ.
#
# LES RÈGLES GARDÉES. Quatre pistes : A 0,5 (−6,02 dB) au centre, B 1 (0 dB) à −0,5, C 0,25 (−12,04 dB) à +0,8, D 0,5
# au centre. B et C choisies (B, puis Maj+Alt+↓) ; A et D hors du choix.
#   (1) la case cochée, le fader de B posé à −6 dB (la saisie, `valeur:`) : C suit du MÊME ÉCART, à −18,04 dB ; A et D
#       ne bougent pas ; le témoin sans la case : B seule ;
#   (2) le panoramique de B posé au centre (+0,5) : C borné à +1 ; le témoin : B seule ;
#   (3) la LISTE DES PISTES, un glissé du curseur de volume de B (le chemin de la souris, un seul geste) : C suit de
#       l'écart que B a pris, en dB, au centième ; le témoin : B seule ;
#   (4) une piste HORS du choix (A) touchée, la case cochée : A bouge seule ;
#   (5) un seul pas : Ctrl+Z rend B ET C, après un geste qui a eu lieu ;
#   (6) la case est RETENUE : cochée au premier lancement, cochée au second sous le même HOME ; un HOME neuf : décochée.
#
#   tools/lier-tranches.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-lier.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 4, 480) + piste("A", tempo=True) + piste("B") + piste("C") + piste("D"))
mix = {"A": (0.5, 0.0), "B": (1.0, -0.5), "C": (0.25, 0.8), "D": (0.5, 0.0)}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "name": n,
                       "mix": {"muted": False, "solo": False, "volume": mix[n][0], "pan": mix[n][1]},
                       "instrument": {"preferredPlugin": "vsm.minimoog"}} for c, n in enumerate("ABCD")]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet (ou « - ») ; $3 = HOME (ou « - » : neuf) ; le reste : variables
    local nom="$1" projet="$2" maison="$3"
    shift 3
    [ "$maison" = "-" ] && maison="$(mktemp -d "$brouillon/home.XXXX")"
    local avec=()
    [ "$projet" != "-" ] && avec=(VSM_PROJET="$projet")
    env HOME="$maison" "${avec[@]}" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # LES AVERTISSEMENTS DU JOURNAL SE RELAIENT (D147).
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|VALEUR_POSEE|GLISSE) : .*(inconnu|introuvable|aucun|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" \
        "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
tranches() {   # « nom:dB/pan » de chaque piste, ou « ABSENT »
    python3 -c "
import json, math, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
def db(g): return '-inf' if g <= 0 else '%.2f' % (20 * math.log10(g))
print(' '.join(f\"{t['name']}:{db(t['mix']['volume'])}/{t['mix']['pan']:g}\" for t in d['tracks']))" "$1"
}
champ() {   # $1 = relevé « A:x/y … » ; $2 = piste ; $3 = 1 (dB) ou 2 (panoramique)
    printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2://p" | cut -d/ -f"$3"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
LIER="menu:Mixage > Lier les tranches choisies (Q-Link)"
CHOISIR="700:touche:shift + alt + cursor down"
VUE="sans-rapport,piste:1"

echo "=== D566.3 : lier les tranches choisies ==="
for x in vol vol0 pan pan0 liste liste0 hors z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course vol "$brouillon/vol" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1000:$LIER;1300:valeur:mixeur.volume#2=-6;1900:enregistrer:$brouillon/vol"
course vol0 "$brouillon/vol0" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1300:valeur:mixeur.volume#2=-6;1900:enregistrer:$brouillon/vol0"
course pan "$brouillon/pan" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1000:$LIER;1300:valeur:mixeur.pan#2=0;1900:enregistrer:$brouillon/pan"
course pan0 "$brouillon/pan0" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1300:valeur:mixeur.pan#2=0;1900:enregistrer:$brouillon/pan0"
course liste "$brouillon/liste" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1000:$LIER;1300:glisser:pistes.volume#2:0.6,0.5:0.35;1900:enregistrer:$brouillon/liste"
course liste0 "$brouillon/liste0" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1300:glisser:pistes.volume#2:0.6,0.5:0.35;1900:enregistrer:$brouillon/liste0"
course hors "$brouillon/hors" - VSM_DELAI=2600 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1000:$LIER;1300:valeur:mixeur.volume#1=-20;1900:enregistrer:$brouillon/hors"
course annuler "$brouillon/z" - VSM_DELAI=2800 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="$CHOISIR;1000:$LIER;1300:valeur:mixeur.volume#2=-6;1700:touche:ctrl + Z;2200:enregistrer:$brouillon/z"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course retenue1 - "$maison" VSM_DELAI=1500 VSM_VUE="sans-rapport" VSM_GESTE_APRES="600:$LIER;1000:lister-menus"
course retenue2 - "$maison" VSM_DELAI=1500 VSM_VUE="sans-rapport" VSM_GESTE_APRES="800:lister-menus"
course neuve - - VSM_DELAI=1500 VSM_VUE="sans-rapport" VSM_GESTE_APRES="800:lister-menus"

for x in base vol vol0 pan pan0 liste liste0 hors z; do eval "t_$x=\"\$(tranches \"\$brouillon/\$x\")\""; done
for x in base vol vol0 pan pan0 liste liste0 hors z; do eval "printf '       %-7s [%s]\n' $x \"\$t_$x\""; done
coche() { grep -h "^VSM_MENU_LISTE : Mixage > Lier les tranches choisies" "$brouillon/$1.txt" | head -1 | grep -q "\[cochée\]" && echo oui || echo non; }
r1="$(coche retenue1)"; r2="$(coche retenue2)"; rn="$(coche neuve)"
echo "       case : premier lancement $r1, second $r2, HOME neuf $rn"
grep -h "^VSM_VALEUR_POSEE\|^VSM_GLISSE\|^VSM_CONSOLE : tranches" "$brouillon/vol.txt" "$brouillon/liste.txt" "$brouillon/z.txt" 2>/dev/null \
    | sed 's/^/       /'

# (1) C suit B de −6 dB : −12,04 − 6 = −18,04 ; A et D au départ.
verdict "(1) saisie à la console : B −6 dB, C −18,04 dB (même écart), A et D intactes ; le témoin : B seule" \
    "$([ "$t_vol" = "A:-6.02/0 B:-6.00/-0.5 C:-18.04/0.8 D:-6.02/0" ] \
       && [ "$t_vol0" = "A:-6.02/0 B:-6.00/-0.5 C:-12.04/0.8 D:-6.02/0" ] && echo 1 || echo 0)"
verdict "(2) panoramique : B au centre (+0,5), C borné à +1 ; le témoin : B seule" \
    "$([ "$t_pan" = "A:-6.02/0 B:0.00/0 C:-12.04/1 D:-6.02/0" ] \
       && [ "$t_pan0" = "A:-6.02/0 B:0.00/0 C:-12.04/0.8 D:-6.02/0" ] && echo 1 || echo 0)"
bl="$(champ "$t_liste" B 1)"; cl="$(champ "$t_liste" C 1)"; bl0="$(champ "$t_liste0" B 1)"; cl0="$(champ "$t_liste0" C 1)"
echo "       liste : B $bl dB, C $cl dB (écart B $(awk -v b="$bl" 'BEGIN{printf "%.2f", b-0}'), C $(awk -v c="$cl" 'BEGIN{printf "%.2f", c+12.04}')) ; témoin B $bl0, C $cl0"
verdict "(3) glissé dans la liste des pistes : C suit de l'écart de B, au centième de dB ; le témoin : B seule" \
    "$(awk -v b="$bl" -v c="$cl" -v b0="$bl0" -v c0="$cl0" 'BEGIN { e = b - 0; f = c + 12.04;
        print (b != "" && b + 0 < -0.5 && (e - f < 0.011 && f - e < 0.011) && b0 == b && c0 == "-12.04") ? 1 : 0 }')"
verdict "(4) A, hors du choix, touchée la case cochée : A seule" \
    "$([ "$t_hors" = "A:-20.00/0 B:0.00/-0.5 C:-12.04/0.8 D:-6.02/0" ] && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend B et C, après un geste qui a eu lieu" \
    "$([ "$t_z" = "$t_base" ] && grep -q "^VSM_VALEUR_POSEE : mixeur.volume#2=-6" "$brouillon/annuler.txt" \
       && grep -q "^VSM_CONSOLE : tranches choisies liées" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(6) retenue : cochée, cochée au relancement ; un HOME neuf la trouve décochée" \
    "$([ "$r1" = oui ] && [ "$r2" = oui ] && [ "$rn" = non ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LIER-TRANCHES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LIER-TRANCHES : les tranches choisies suivent du même écart, et la piste hors du choix bouge seule"
