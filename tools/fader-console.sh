#!/usr/bin/env bash
# La garde de D342 : LE FADER DE LA CONSOLE RESTE RÉGLABLE, ET SON ÉCHELLE SE VOIT.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. chaque tranche garde AU MOINS 40 px de course pour ses 66 dB, soit au
#      plus 1,70 dB au pixel. Avant D342 : 10 px et 6,60 dB au pixel — un fader
#      qu'aucun geste ne peut régler, et que rien ne disait ;
#   2. l'échelle en décibels est RÉSERVÉE dès que la tranche fait 120 px de
#      large (le relevé la donne en pixels) ;
#   3. et elle est DESSINÉE — c'est le repère du 0 dB, un trait ambre de huit
#      pixels à gauche du fader, COMPTÉ SUR LA PHOTO. Cette troisième règle
#      existe parce que le défaut a été payé : une soustraction débordante
#      (`y - INT_MIN`) sautait toutes les graduations pendant que le relevé
#      annonçait « échelle 26 px ». Un relevé dit ce qu'on a RÉSERVÉ, la photo
#      dit ce qu'on a PEINT.
#
# COMMENT. Le projet de démarrage suffit (une piste, une tranche) : aucune
# donnée extérieure, aucune campagne, deux secondes de course. Rend 0 si tout
# tient, 1 sinon, 2 si le binaire manque.
#
#   tools/fader-console.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fader-console.XXXXXX")"
maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
trap 'rm -rf "$brouillon"' EXIT

env HOME="$maison" VSM_TAILLE="1280x742" VSM_MIXEUR=1 VSM_DELAI=2500 \
    VSM_CAPTURE="$brouillon/console.png" \
    timeout 40 "$BIN" > "$brouillon/journal.txt" 2>&1
rc=$?

j="$(cat "$brouillon/journal.txt")"
rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D342 : la course du fader de console et son échelle ==="
tranches="$(grep -c '^VSM_MIXEUR : piste ' <<<"$j")"
verdict "le relevé VSM_MIXEUR existe ($tranches tranche(s))" \
        "$([ "$tranches" -ge 1 ] && echo 1 || echo 0)"

if [ "$tranches" -ge 1 ]; then
    # LE PIRE CAS DÉCIDE, pas la moyenne : une seule tranche inutilisable suffit
    # à rendre la console inutilisable pour la piste qu'elle porte.
    pire="$(grep '^VSM_MIXEUR : piste ' <<<"$j" \
            | sed -n 's/.*course \([0-9.]*\) px (\([0-9.]*\) dB\/px).*/\1 \2/p' \
            | sort -n | head -1)"
    course="${pire%% *}"
    parpx="${pire##* }"
    verdict "course minimale $course px (règle : >= 40)" \
            "$(awk -v c="$course" 'BEGIN { print (c >= 40) ? 1 : 0 }')"
    verdict "au plus $parpx dB au pixel (règle : <= 1,70)" \
            "$(awk -v d="$parpx" 'BEGIN { print (d <= 1.70) ? 1 : 0 }')"
    # L'échelle n'est réservée que sur une tranche assez large : on ne l'exige
    # que là où la règle la demande.
    larges="$(grep '^VSM_MIXEUR : piste ' <<<"$j" | sed -n 's/.*tranche \([0-9]*\)x.*/\1/p' \
              | awk '$1 >= 120' | wc -l)"
    echelles="$(grep '^VSM_MIXEUR : piste ' <<<"$j" | sed -n 's/.*échelle \([0-9]*\) px.*/\1/p' \
                | awk '$1 > 0' | wc -l)"
    verdict "échelle réservée sur les $larges tranche(s) de 120 px ou plus (relevé : $echelles)" \
            "$([ "$echelles" -ge "$larges" ] && echo 1 || echo 0)"
fi

# LA PHOTO, ET NON LE RELEVÉ : le repère du 0 dB est un trait ambre COURT
# (huit pixels) ; le capuchon du fader en porte un LONG (plus de cent), et
# c'est ce qui les distingue sans rien deviner.
reperes="$(python3 - "$brouillon/console.png" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print(-1); raise SystemExit(0)
im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
W, H = im.size
def ambre(c):
    r, g, b = c
    return r > 170 and 120 < g < 200 and b < 120
n = 0
for y in range(H // 2, H):
    x = 0
    while x < W:
        if ambre(px[x, y]):
            debut = x
            while x < W and ambre(px[x, y]):
                x += 1
            if 5 <= x - debut <= 12:
                n += 1
        else:
            x += 1
print(n)
PY
)"
verdict "repère(s) du 0 dB peint(s) sur la photo : $reperes (règle : >= 1 par tranche)" \
        "$([ "$reperes" -ge "$tranches" ] && [ "$reperes" -ge 1 ] && echo 1 || echo 0)"
verdict "code de sortie 0 (relevé : $rc)" "$([ "$rc" -eq 0 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
