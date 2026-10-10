#!/usr/bin/env bash
# D550.1 — CHOISIR DES CLIPS PAR LA TÊTE ET LES LOCATEURS (Édition ▸ Sélectionner dans l'arrangement ▸ …), MESURÉ PAR
# LE RELEVÉ DE LA SÉLECTION ET PAR LE PROJET RELU APRÈS UN GESTE QUI AGIT SUR ELLE.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4 : une mesure = 1 920). Projet P : piste « Haut », A [0, 1920) et
# B [3840, 5760) ; piste « Bas », C [1920, 3840) et D [5760, 7680) ; les locateurs de 2 · 3 à 3 · 3 [2880, 4800).
#   (1) « Sélectionner entre les locateurs » : les clips qui TOUCHENT la plage — C et B (« Bas:1920 Haut:3840 ») ;
#   (2) « Sélectionner du début à la tête », la tête à 2 · 3 (2 880) : ceux qui COMMENCENT avant elle — A et C ;
#       la tête à 2 · 1 (1 920), où C COMMENCE : A seul (le bord) ;
#   (3) « Sélectionner de la tête à la fin », la tête à 2 · 3 : ceux qui FINISSENT après elle — C, B, D ;
#   (4) « Inverser la sélection de l'arrangement » après (3) : A seul ;
#   (5) « Ne rien sélectionner dans l'arrangement » après « tout choisir » : 0 ;
#   (6) LE GESTE QUI AGIT : (1) puis Suppr, le projet relu — A et D restent, C et B sont partis ;
#   (7) sans locateurs : l'entrée « entre les locateurs » est GRISÉE, et le menu le dit.
#
#   tools/choisir-par-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-choisir-plage.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
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
def ecrire(nom, boucle):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 2, 480) + piste("Haut", 0, [0, 3840], tempo=True) + piste("Bas", 1, [1920, 5760]))
    clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    entree = lambda n, c, clips: {"channel": c, "color": "#FF6B9BFF", "effects": [], "name": n, "mix": mix,
                                  "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}
    transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                 "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
    if boucle:
        transport["loop"] = {"enabled": False, "startTick": 2880, "endTick": 4800}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": transport,
               "tracks": [entree("Haut", 0, [clip("A", 0), clip("B", 3840)]),
                          entree("Bas", 1, [clip("C", 1920), clip("D", 5760)])]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
ecrire("P", True)
ecrire("P-sans", False)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releve() {   # la sélection relevée par « relever-selection » : « N : piste:début … », ou « NON RELEVÉE »
    local l
    l="$(grep -h "^VSM_SELECTION_CLIPS : [0-9]* :" "$brouillon/$1.txt" | tail -1)"
    [ -n "$l" ] && echo "${l#VSM_SELECTION_CLIPS : }" || echo "NON RELEVÉE"
}
clips() {
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(sorted(x.get('name', '?') for t in p['tracks'] for x in t.get('clips', []))) or 'AUCUN')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
ENTRE="menu:Sélectionner entre les locateurs"
DEBUT="menu:Sélectionner du début à la tête"
FIN="menu:Sélectionner de la tête à la fin"

echo "=== D550.1 : choisir des clips par la tête et les locateurs ==="
cp -r "$brouillon/P" "$brouillon/Psuppr"
course entre "$brouillon/P" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$ENTRE;1600:relever-selection"
course debut "$brouillon/P" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2.3 VSM_GESTE_APRES="1200:$DEBUT;1600:relever-selection"
course debut-bord "$brouillon/P" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2 VSM_GESTE_APRES="1200:$DEBUT;1600:relever-selection"
course fin "$brouillon/P" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_POSITION=2.3 VSM_GESTE_APRES="1200:$FIN;1600:relever-selection"
course inverser "$brouillon/P" VSM_DELAI=2800 VSM_VUE="$VUE" VSM_POSITION=2.3 \
    VSM_GESTE_APRES="1200:$FIN;1500:menu:Inverser la sélection de l'arrangement;1900:relever-selection"
course rien "$brouillon/P" VSM_DELAI=2800 VSM_VUE="$VUE,tout-choisir" \
    VSM_GESTE_APRES="1200:menu:Ne rien sélectionner dans l'arrangement;1600:relever-selection"
course supprimer "$brouillon/Psuppr" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$ENTRE;1500:touche:arrangement:delete;2000:enregistrer:$brouillon/Psuppr"
course sans "$brouillon/P-sans" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$ENTRE;1600:relever-selection"

e="$(releve entre)"; d="$(releve debut)"; db="$(releve debut-bord)"; f="$(releve fin)"
i="$(releve inverser)"; r="$(releve rien)"; s="$(clips "$brouillon/Psuppr/project.json")"; sl="$(releve sans)"
echo "       entre les locateurs [$e] ; du début à la tête 2 · 3 [$d], 2 · 1 [$db] ; de la tête à la fin [$f]"
echo "       inversé [$i] ; rien [$r] ; après Suppr, il reste [$s] ; sans locateurs [$sl]"
grep -h "est grisée" "$brouillon/sans.txt" | head -1 | sed 's/^/       /'

verdict "(1) entre les locateurs : les clips qui touchent la plage" "$([ "$e" = "2 : Bas:1920 Haut:3840" ] && echo 1 || echo 0)"
verdict "(2) du début à la tête : ceux qui commencent avant elle — un clip qui y commence n'en est pas" \
    "$([ "$d" = "2 : Bas:1920 Haut:0" ] && [ "$db" = "1 : Haut:0" ] && echo 1 || echo 0)"
verdict "(3) de la tête à la fin : ceux qui finissent après elle" "$([ "$f" = "3 : Bas:1920 Bas:5760 Haut:3840" ] && echo 1 || echo 0)"
verdict "(4) inverser : le reste" "$([ "$i" = "1 : Haut:0" ] && echo 1 || echo 0)"
verdict "(5) ne rien sélectionner : 0" "$([ "$r" = "0 :" ] && echo 1 || echo 0)"
verdict "(6) le geste qui agit : Suppr après « entre les locateurs » retire C et B" "$([ "$s" = "A D" ] && echo 1 || echo 0)"
# L'application écrit « … est grisée, rien n'a été fait » : le banc cherchait d'abord « GRISÉE », et tombait.
verdict "(7) sans locateurs : l'entrée est grisée et le menu le dit, rien n'est choisi" \
    "$(grep -q "^VSM_MENU : « Sélectionner entre les locateurs » est grisée" "$brouillon/sans.txt" && [ "$sl" = "0 :" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CHOISIR-PAR-PLAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CHOISIR-PAR-PLAGE : les clips se choisissent par la tête et les locateurs, aux règles de Cubase"
