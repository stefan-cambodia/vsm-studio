#!/usr/bin/env bash
# D625 — LA LONGUEUR D'INSERTION, DISTINCTE DE LA GRILLE (Édition ▸ Temps et durée ▸ Longueur d'insertion ; l'*Insert
# Length* des éditeurs de Cubase), mesurée dans l'application : la grille dit OÙ une note posée commence, la longueur
# d'insertion dit COMBIEN elle dure. Les frappes lues dans le projet enregistré ; le témoin (D624) à côté.
#
# LES RÈGLES GARDÉES (480 ticks par noire, la barre à 1/16 : 120 ticks ; une piste « Batterie » sur la TR-909, des frappes
# de repère à 960 et 1 440 ; un clic au tick ~400). L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D625) :
#   (a) sans rien choisir, rang 36 → 36@360+120 (le résultat de D622) ;
#   (b) « Insertion : 1/8 », rang 36 → 36@360+240 ;
#   (c) « Insertion : 1/8 » et le rang 38 à 1/4, rang 38 → 38@480+240 (le début au rang, la durée choisie) ;
#   (d) le projet enregistré porte "insertLength": "1/8" ; rouvert, l'entrée est cochée et un clic pose 240 ;
#       « Insertion : la grille » rechoisie puis enregistré : la clé n'y est plus ;
#   (e) la clé à « 1/7 » : écartée et dite, « Insertion : la grille » cochée ;
#   (f) un autre morceau ouvert ensuite n'en hérite pas ; l'anglais ; le témoin (D624) : libellé introuvable, 360+120.
#
#   tools/longueur-insertion.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D624/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-longueur-insertion.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def projet(nom, frappes):
    b = f"{racine}/{nom}"; os.makedirs(f"{b}/midi", exist_ok=True)
    evts = []
    for debut, h in frappes:
        evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 120, 0, bytes([0x89, h, 0]))]
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
    for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
        corps += vlq(tick - t) + e; t = tick
    corps += b"\x00\xff\x2f\x00"
    open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    json.dump({"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                           "instrument": {"preferredPlugin": "vsm.tr909"},
                           "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
              open(f"{b}/project.json", "w"), indent=1)
projet("projet", [(960, 38), (1440, 36)])
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Le projet `p-<nom>` posé par `copie`, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=2600 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|CLIC_POSITION|CARTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les frappes d'un projet enregistré : « hauteur@début+durée », triées.
frappes() {
    python3 - "$1/midi/arrangement.mid" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, ouvertes, i = [], {}, 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; statut = 0
    while j < fin:
        v = 0
        while True:
            c = d[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); n = 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        h, vel = d[j], (d[j + 1] if taille == 2 else 0)
        # APPARIÉES DANS L'ORDRE D'ARRIVÉE, comme l'application les relit — CORRIGÉ APRÈS LE PREMIER ESSAI : le fichier
        # écrit au même tick le note-on d'une frappe AVANT le note-off de la précédente de même hauteur (le défaut que
        # D623 traite), et un appariement « dernier ouvert » en faisait une note de longueur nulle.
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D625 : la longueur d'insertion, distincte de la grille ==="
copie() { rm -rf "$brouillon/p-$1"; cp -r "${2:-$brouillon/projet}" "$brouillon/p-$1"; }   # $1 = nom de course ; $2 = source
copie geometrie; course "$BIN" geometrie "1200:relever-losanges"
centre() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/geometrie.txt" | head -1 | sed -n 's/.* centre \([0-9.]*\),\([0-9.]*\) ;.*/\1 \2/p'; }
read -r X960 Y38 <<< "$(centre 38 960)"; read -r X1440 Y36 <<< "$(centre 36 1440)"
[ -n "${X960:-}" ] && [ -n "${X1440:-}" ] || { echo "REFUS : la géométrie des frappes de repère n'a pas été relevée"; exit 1; }
X400="$(python3 -c "import sys; a, b = float(sys.argv[1]), float(sys.argv[2]); print(f'{a + (400 - 960) * (b - a) / 480:.4f}')" "$X960" "$X1440")"
INS="menu:Édition > Temps et durée > Longueur d'insertion > Insertion"
cle() {   # la valeur de view.insertLength d'un projet enregistré : le libellé, « ABSENTE », ou « ILLISIBLE »
    python3 -c "
import json, sys
try: d = json.load(open(sys.argv[1] + '/project.json'))
except Exception: print('ILLISIBLE'); sys.exit(0)
print(d.get('view', {}).get('insertLength', 'ABSENTE'))" "$1"
}
cochee() { grep -h "^VSM_MENU_LISTE : .*Insertion : .*\[cochée\]" "$brouillon/$1.txt" | sed -n 's/.*\(Insertion : [^[]*\) \[cochée\].*/\1/p' | tr '\n' ';'; }

copie a;  course "$BIN" a "1300:clic:pianoroll:$X400,$Y36;1700:enregistrer:$brouillon/a-projet"
copie b;  course "$BIN" b "900:$INS : 1/8;1300:clic:pianoroll:$X400,$Y36;1600:lister-menus;1900:enregistrer:$brouillon/b-projet"
copie c;  course "$BIN" c "800:$INS : 1/8;1000:carte-grille:38>1/4;1300:clic:pianoroll:$X400,$Y38;1700:enregistrer:$brouillon/c-projet"
copie d "$brouillon/b-projet";  course "$BIN" d "1000:lister-menus;1300:clic:pianoroll:$X400,$Y38;1700:enregistrer:$brouillon/d-projet"
copie d2 "$brouillon/b-projet"; course "$BIN" d2 "900:$INS : la grille;1200:lister-menus;1500:enregistrer:$brouillon/d2-projet"
copie e "$brouillon/b-projet"
python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'; d = json.load(open(p)); d.setdefault('view', {})['insertLength'] = '1/7'; json.dump(d, open(p, 'w'), indent=1)" "$brouillon/p-e"
course "$BIN" e "1000:lister-menus"
copie f "$brouillon/b-projet"; course "$BIN" f "900:ouvrir-midi:$brouillon/projet/midi/arrangement.mid;1600:lister-menus"
copie en "$brouillon/b-projet"; course "$BIN" en "1000:lister-menus" VSM_LANGUE=en

A="$(frappes "$brouillon/a-projet")"; B="$(frappes "$brouillon/b-projet")"; C="$(frappes "$brouillon/c-projet")"; D="$(frappes "$brouillon/d-projet")"
echo "       (a) $A"; echo "       (b) $B — clé « $(cle "$brouillon/b-projet") » ; cochée : $(cochee b)"
echo "       (c) $C"; echo "       (d) rouvert : cochée : $(cochee d) ; $D ; la grille rechoisie : cochée : $(cochee d2), clé « $(cle "$brouillon/d2-projet") »"
grep -h "^VSM_INSERTION : \|^VSM_VUE_IGNOREE : " "$brouillon"/b.txt "$brouillon"/d2.txt "$brouillon"/e.txt | sed 's/^/       /'
echo "       (e) cochée : $(cochee e) ; (f) autre morceau : cochée : $(cochee f)"
ena="$(grep -ch "^VSM_MENU_LISTE : .*Insert length > Insert: \(the grid\|1/8 \[\)" "$brouillon/en.txt")"
contient() { case " $1 " in *" $2 "*) echo 1 ;; *) echo 0 ;; esac; }

verdict "(a) sans rien choisir : 36@360+120" "$(contient "$A" "36@360+120")"
verdict "(b) « Insertion : 1/8 » : 36@360+240" "$(contient "$B" "36@360+240")"
verdict "(b) l'entrée choisie est cochée, elle seule" "$([ "$(cochee b)" = "Insertion : 1/8;" ] && echo 1 || echo 0)"
verdict "(c) le rang 38 à 1/4 : 38@480+240 (le début au rang, la durée choisie)" "$(contient "$C" "38@480+240")"
verdict "(d) le projet porte \"insertLength\": \"1/8\"" "$([ "$(cle "$brouillon/b-projet")" = "1/8" ] && echo 1 || echo 0)"
verdict "(d) rouvert : « Insertion : 1/8 » cochée, et un clic pose 38@360+240" \
    "$([ "$(cochee d)" = "Insertion : 1/8;" ] && [ "$(contient "$D" "38@360+240")" = 1 ] && echo 1 || echo 0)"
verdict "(d) « Insertion : la grille » rechoisie : cochée, la clé n'est plus au projet" \
    "$([ "$(cochee d2)" = "Insertion : la grille;" ] && [ "$(cle "$brouillon/d2-projet")" = "ABSENTE" ] && echo 1 || echo 0)"
verdict "(e) « 1/7 » : écartée et dite, « la grille » cochée" \
    "$(grep -q "^VSM_VUE_IGNOREE : insertLength « 1/7 »" "$brouillon/e.txt" && [ "$(cochee e)" = "Insertion : la grille;" ] && echo 1 || echo 0)"
verdict "(f) un autre morceau ouvert ensuite : « la grille »" "$([ "$(cochee f)" = "Insertion : la grille;" ] && echo 1 || echo 0)"
verdict "(f) en anglais : « Insert length », « Insert: the grid », « Insert: 1/8 » ($ena)" "$([ "$ena" = 2 ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    copie t-b; course "$TEMOIN" t-b "900:$INS : 1/8;1300:clic:pianoroll:$X400,$Y36;1900:enregistrer:$brouillon/t-b-projet"
    T="$(frappes "$brouillon/t-b-projet")"; ti="$(grep -c "VSM_.*\(introuvable\|inconnu\|aucun\)" "$brouillon/t-b.txt")"
    echo "       témoin (D624) : $T ; $ti ligne(s) « introuvable »"
    verdict "(f) témoin (D624) : le libellé introuvable, 36@360+120" "$([ "$ti" -ge 1 ] && [ "$(contient "$T" "36@360+120")" = 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "LONGUEUR-INSERTION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LONGUEUR-INSERTION : la grille dit où, la longueur d'insertion dit combien, et le morceau la retient"
