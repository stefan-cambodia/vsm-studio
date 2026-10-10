#!/usr/bin/env bash
# D548 — LES GESTES À LA TÊTE DE LECTURE, ET LE VERROU AU CLAVIER, MESURÉS PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4).
#   Projet L : une piste, A [0, 1920) VERROUILLÉ, B [1920, 3840) ; tout choisi, la tête à 1 · 3 (960).
#   (1) D548.1 — Ctrl+E : A reste entier (deux clips relus), « Montage refusé : 1 clip verrouillé est resté en
#       place » ; le TÉMOIN sans verrou : A coupé (trois clips) ;
#   (2) D548.1 — Ctrl+J : A et B restent deux, le refus dit ; le témoin sans verrou : un seul clip ;
#   Projet M : piste 0, A [1920, 3840) et B [5760, 7680) ; piste 1, C [3840, 5760) ; tout choisi.
#   (3) D548.2 — « Déplacer à la tête de lecture », la tête à 5 · 1 (7 680) : A 7 680, C 9 600, B 11 520 ;
#       Ctrl+Z → A 1 920 ;
#   (4) D548.3 — « Rogner le début à la tête de lecture », la tête à 2 · 3 (2 880) : A [2 880, 3 840), B et C
#       inchangés, « 1 clip(s) rogné(s), 2 ne contiennent pas la tête » ;
#   (5) D548.3 — « Rogner la fin… » à 2 880 : A [1 920, 2 880) ; B reste à 5 760 — EN CHAÎNE (D547.2), B recule
#       à 4 800, C (une autre piste) ne bouge pas ;
#   (6) rien à rogner (la tête à 9 · 1) : « Rien à rogner » dit, aucun pas d'historique ;
#   (7) rien à déplacer (la tête à 2 · 1, où A commence déjà) : « Rien à déplacer » dit, aucun pas — le geste
#       était MUET, et c'est `annulation-des-menus.py` qui l'a trouvé.
#
#   tools/a-la-tete.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-a-la-tete.XXXXXX")"
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
        corps += vlq(debut - t) + bytes([0x90 | canal, 60, 100]); corps += vlq(1440) + bytes([0x80 | canal, 60, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def ecrire(nom, pistes_mid, pistes_json):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes_mid), 480) + b"".join(pistes_mid))
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": pistes_json}, open(f"{b}/{nom}/project.json", "w"), indent=1)
clip = lambda nom, d, **x: dict({"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": nom}, **x)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
entree = lambda nom, canal, clips: {"channel": canal, "color": "#FF6B9BFF", "effects": [], "name": nom, "mix": mix,
                                    "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}
ecrire("L", [piste("Partie", 0, [0, 1920], tempo=True)],
       [entree("Partie", 0, [clip("A", 0, locked=True), clip("B", 1920)])])
ecrire("L-libre", [piste("Partie", 0, [0, 1920], tempo=True)], [entree("Partie", 0, [clip("A", 0), clip("B", 1920)])])
ecrire("M", [piste("Haut", 0, [1920, 5760], tempo=True), piste("Bas", 1, [3840])],
       [entree("Haut", 0, [clip("A", 1920), clip("B", 5760)]), entree("Bas", 1, [clip("C", 3840)])])
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:longueur » des clips d'un project.json (triés), ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = sorted((x.get('name', '?'), int(x.get('start', 0)), int(x.get('length', 0))) for t in p['tracks'] for x in t.get('clips', []))
print(' '.join(f'{n}:{d}:{l}' for n, d, l in c) or 'AUCUN')
" "$1"
}
compte() { [ "$1" = ABSENT ] && echo ABSENT || echo "$1" | wc -w; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REFUS="^VSM_BOITE : Montage refusé : 1 clip verrouillé est resté en place"
VUE="sans-rapport,arrangement,tout-choisir"

echo "=== D548 : les gestes à la tête de lecture, et le verrou au clavier ==="
for x in Le Lj; do cp -r "$brouillon/L" "$brouillon/$x"; done
for x in tLe tLj; do cp -r "$brouillon/L-libre" "$brouillon/$x"; done
for x in Md Mz Mrd Mrf Mrfc Mrien Mdeja; do cp -r "$brouillon/M" "$brouillon/$x"; done

course couper "$brouillon/Le" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=1.3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + E;1700:enregistrer:$brouillon/Le"
course couper-temoin "$brouillon/tLe" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=1.3 \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + E;1700:enregistrer:$brouillon/tLe"
course joindre "$brouillon/Lj" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + J;1700:enregistrer:$brouillon/Lj"
course joindre-temoin "$brouillon/tLj" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:touche:arrangement:ctrl + J;1700:enregistrer:$brouillon/tLj"
course deplacer "$brouillon/Md" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=5 \
    VSM_MENU_CONTEXTE="clip-midi:Déplacer à la tête de lecture" VSM_GESTE_APRES="1500:enregistrer:$brouillon/Md"
course deplacer-annule "$brouillon/Mz" VSM_DELAI=3000 VSM_VUE="$VUE" VSM_POSITION=5 \
    VSM_MENU_CONTEXTE="clip-midi:Déplacer à la tête de lecture" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/Mz"
course rogner-debut "$brouillon/Mrd" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2.3 \
    VSM_MENU_CONTEXTE="clip-midi:Rogner le début à la tête de lecture" VSM_GESTE_APRES="1500:enregistrer:$brouillon/Mrd"
course rogner-fin "$brouillon/Mrf" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2.3 \
    VSM_MENU_CONTEXTE="clip-midi:Rogner la fin à la tête de lecture" VSM_GESTE_APRES="1500:enregistrer:$brouillon/Mrf"
course rogner-fin-chaine "$brouillon/Mrfc" VSM_DELAI=2500 VSM_MENU="Montage en chaîne dans l'arrangement" VSM_VUE="$VUE" \
    VSM_POSITION=2.3 VSM_MENU_CONTEXTE="clip-midi:Rogner la fin à la tête de lecture" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/Mrfc"
course rien "$brouillon/Mrien" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=9 \
    VSM_MENU_CONTEXTE="clip-midi:Rogner le début à la tête de lecture" \
    VSM_GESTE_APRES="1300:relever-historique;1500:enregistrer:$brouillon/Mrien"

course deja "$brouillon/Mdeja" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2 \
    VSM_MENU_CONTEXTE="clip-midi:Déplacer à la tête de lecture" \
    VSM_GESTE_APRES="1300:relever-historique;1500:enregistrer:$brouillon/Mdeja"

ce="$(clips "$brouillon/Le/project.json")"; tce="$(clips "$brouillon/tLe/project.json")"
cj="$(clips "$brouillon/Lj/project.json")"; tcj="$(clips "$brouillon/tLj/project.json")"
md="$(clips "$brouillon/Md/project.json")"; mz="$(clips "$brouillon/Mz/project.json")"
rd="$(clips "$brouillon/Mrd/project.json")"; rf="$(clips "$brouillon/Mrf/project.json")"
rfc="$(clips "$brouillon/Mrfc/project.json")"; rr="$(clips "$brouillon/Mrien/project.json")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/rien.txt" | tail -1)"
echo "       Ctrl+E, A verrouillé [$ce] ; témoin [$tce]"
echo "       Ctrl+J, A verrouillé [$cj] ; témoin [$tcj]"
echo "       à la tête 5 · 1 [$md] ; puis Ctrl+Z [$mz]"
echo "       rogner le début à 2 · 3 [$rd] ; la fin [$rf] ; la fin en chaîne [$rfc]"
grep -h "^VSM_A_LA_TETE" "$brouillon/rogner-debut.txt" "$brouillon/rien.txt" | sed 's/^/       /'
echo "       rien à rogner [$rr] ; ${hist:-historique non relevé}"

verdict "(1) Ctrl+E : le clip verrouillé reste entier, le refus est dit ; le témoin le coupe" \
    "$([ "$ce" = "A:0:1920 B:1920:1920" ] && grep -q "$REFUS" "$brouillon/couper.txt" && [ "$(compte "$tce")" = 3 ] && echo 1 || echo 0)"
verdict "(2) Ctrl+J : le clip verrouillé ne se joint pas, le refus est dit ; le témoin joint" \
    "$([ "$cj" = "A:0:1920 B:1920:1920" ] && grep -q "$REFUS" "$brouillon/joindre.txt" && [ "$(compte "$tcj")" = 1 ] && echo 1 || echo 0)"
verdict "(3) déplacer à la tête : le premier à 7 680, les écarts gardés ; Ctrl+Z rend la place" \
    "$([ "$md" = "A:7680:1920 B:11520:1920 C:9600:1920" ] && [ "$mz" = "A:1920:1920 B:5760:1920 C:3840:1920" ] && echo 1 || echo 0)"
verdict "(4) rogner le début : A seul, à 2 880 ; le compte dit les deux autres" \
    "$([ "$rd" = "A:2880:960 B:5760:1920 C:3840:1920" ] \
        && grep -q "^VSM_A_LA_TETE : rogner le début, 1 clip(s) rogné(s), 2 ne contiennent pas la tête" "$brouillon/rogner-debut.txt" \
        && echo 1 || echo 0)"
verdict "(5) rogner la fin : A finit à 2 880 ; en chaîne, B recule à 4 800, C reste" \
    "$([ "$rf" = "A:1920:960 B:5760:1920 C:3840:1920" ] && [ "$rfc" = "A:1920:960 B:4800:1920 C:3840:1920" ] && echo 1 || echo 0)"
verdict "(6) rien à rogner : dit, aucun pas, rien de changé" \
    "$([ "$rr" = "A:1920:1920 B:5760:1920 C:3840:1920" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Rien à rogner" "$brouillon/rien.txt" && echo 1 || echo 0)"
md2="$(clips "$brouillon/Mdeja/project.json")"; hist2="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/deja.txt" | tail -1)"
echo "       déjà à la tête [$md2] ; ${hist2:-historique non relevé}"
verdict "(7) rien à déplacer : dit, aucun pas, rien de changé" \
    "$([ "$md2" = "A:1920:1920 B:5760:1920 C:3840:1920" ] && [ "$(echo "$hist2" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Rien à déplacer" "$brouillon/deja.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "A-LA-TETE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "A-LA-TETE : les clips vont et se rognent à la tête, et le verrou tient au clavier"
