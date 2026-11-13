#!/usr/bin/env bash
# D636 — LES RANGS DANS L'ORDRE DE LA CARTE (l'ordre des pièces de la *Drum Map* de Cubase) : ▲ et ▼ sur chaque ligne de
# la carte de batterie déplacent la ligne d'un rang ; l'ordre va au modèle (`Track::drumOrder`), au projet (`"drumOrder"`)
# et à l'historique (un pas par appui) ; le piano roll REPLIÉ range ses rangées dans cet ordre. Sans ordre posé, rien ne
# bouge : la carte par numéro croissant, le repli par hauteur décroissante.
#
# LES RÈGLES GARDÉES (une piste « Batterie » sur la TR-909, canal 10 : 36@0, 38@480, 42@960). L'ATTENDU ÉCRIT AVANT LE CODE
# (ROADMAP-daw, D636) — la carte relue par `relever-carte`, le repli par la position des frappes (`relever-losanges`, y) :
#   (a) sans geste : la carte 36 38 42 ; replié 42 38 36 de haut en bas ; pas de "drumOrder" au projet ;
#   (b) ▲ de 42 deux fois : la carte 42 36 38 ; replié 42 36 38 ; "drumOrder": [42, 36, 38] ; deux pas « Ordre de la carte » ;
#   (c) puis Ctrl+Z : la carte 36 42 38 ;
#   (d) le projet de (b) rouvert : la carte et le repli 42 36 38 ;
#   (e) sous l'ordre de (b), un clic dans la rangée du milieu à la mesure 2 pose une frappe 36 (la rangée qu'on voit) ;
#   (f) le témoin (le binaire d'avant D636) : le verbe inconnu, le repli 42 38 36 ;
#   (h) l'infobulle de ▲ en anglais.
#
#   tools/ordre-de-la-carte.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D635/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-ordre-de-la-carte.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
racine = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
b = f"{racine}/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
evts = []
for debut, h in [(0, 36), (480, 38), (960, 42)]:   # grosse caisse, caisse claire, charleston fermé
    evts += [(debut, 1, bytes([0x99, h, 110])), (debut + 120, 0, bytes([0x89, h, 0]))]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie"; t = 0
for tick, _, e in sorted(evts, key=lambda x: (x[0], x[1])):
    corps += vlq(tick - t) + e; t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "projet", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; $4 = une variable de plus (VSM_LANGUE=en). Une copie neuve, un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" plus="${4:-VSM_RIEN=1}" n
    rm -rf "$brouillon/p-$nom"; cp -r "$brouillon/projet" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3000 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" "$plus" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|GLISSE|TOUCHE|VUE|MENU|CARTE|CLIC_POSITION|CLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les frappes d'un projet enregistré : « hauteur@début+durée », triées (le lecteur de `longueur-insertion.sh`).
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
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY2
}
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
egal() { [ "$1" = "$2" ] && echo 1 || echo 0; }
# L'ordre de la carte, tel que le relevé l'écrit : le numéro de chaque ligne « VSM_CARTE : <n>  <note> … → … », dans l'ordre.
carte() { grep -E "^VSM_CARTE : [0-9]+ " "$brouillon/$1.txt" | tail -3 | sed -E 's/^VSM_CARTE : ([0-9]+) .*/\1/' | tr '\n' ' ' | sed 's/ $//'; }
# L'ordre du repli, de haut en bas : les frappes triées par leur y à l'écran (une hauteur par rangée).
repli() { grep "^VSM_LOSANGE : note " "$brouillon/$1.txt" | sed -E 's/^VSM_LOSANGE : note ([0-9]+) tick [0-9]+ : centre [0-9.]+,([0-9.]+) .*/\2 \1/' \
            | sort -n | awk '{ if (!($2 in vu)) { vu[$2] = 1; printf "%s%s", (n++ ? " " : ""), $2 } }'; }
ordre_projet() { python3 -c "import json, sys; t = json.load(open(sys.argv[1] + '/project.json'))['tracks'][0]; print(' '.join(map(str, t.get('drumOrder', []))) or 'aucun')" "$1" 2>/dev/null || echo ABSENT; }
REPLIER="500:cliquer:Replier"
MONTER="800:carte-monter:42;1300:carte-monter:42"

echo "=== D636 : les rangs dans l'ordre de la carte ==="
course "$BIN" a "$REPLIER;900:relever-carte;1300:relever-losanges;1700:enregistrer:$brouillon/a-projet"
course "$BIN" b "$REPLIER;$MONTER;1800:relever-carte;2100:relever-losanges;2300:relever-historique;2600:enregistrer:$brouillon/b-projet"
course "$BIN" c "$REPLIER;$MONTER;1900:touche:ctrl + Z;2400:relever-carte"
# (d) : le projet de (b), rouvert dans une course neuve
rm -rf "$brouillon/projet-b-sauf"; cp -r "$brouillon/projet" "$brouillon/projet-b-sauf"
if [ -f "$brouillon/b-projet/project.json" ]; then rm -rf "$brouillon/projet"; cp -r "$brouillon/b-projet" "$brouillon/projet"; fi
course "$BIN" d "$REPLIER;900:relever-carte;1300:relever-losanges"
rm -rf "$brouillon/projet"; mv "$brouillon/projet-b-sauf" "$brouillon/projet"

A_C="$(carte a)"; A_R="$(repli a)"; A_P="$(ordre_projet "$brouillon/a-projet")"
B_C="$(carte b)"; B_R="$(repli b)"; B_P="$(ordre_projet "$brouillon/b-projet")"
B_H="$(grep -h "^VSM_HISTORIQUE" "$brouillon/b.txt" | grep -o "Ordre de la carte" | wc -l)"
C_C="$(carte c)"; D_C="$(carte d)"; D_R="$(repli d)"
echo "       (a) carte $A_C ; repli $A_R ; projet $A_P"
echo "       (b) carte $B_C ; repli $B_R ; projet $B_P ; $B_H pas « Ordre de la carte »"
echo "       (c) après Ctrl+Z : carte $C_C"
echo "       (d) rouvert : carte $D_C ; repli $D_R"

# (e) : sous l'ordre de (b), un clic dans la rangée du MILIEU, à la mesure 2 (tick 1920) ; x tiré de la géométrie de (b).
geo() { grep "^VSM_LOSANGE : note $1 tick $2 : " "$brouillon/b.txt" | head -1 | sed -E 's/.* centre ([0-9.]+),([0-9.]+) ;.*/\1 \2/'; }
read -r X0 _ <<< "$(geo 36 0)"; read -r X480 _ <<< "$(geo 38 480)"
# CORRIGÉ AVANT LA MESURE DU BINAIRE NEUF : le y de la frappe 36 visait la 36 quel que soit l'ordre, et (e) passait sur le
# témoin. La rangée du MILIEU, c'est la médiane des trois y relevés — 36 sous l'ordre de (b), 38 sans ordre.
Y36="$(grep "^VSM_LOSANGE : note " "$brouillon/b.txt" | sed -E 's/.* centre [0-9.]+,([0-9.]+) ;.*/\1/' | sort -n | sed -n 2p)"
E=""
if [ -n "${X0:-}" ] && [ -n "${X480:-}" ] && [ -n "${Y36:-}" ]; then
    X1920="$(python3 -c "import sys; a, b = map(float, sys.argv[1:]); print(f'{a + 4 * (b - a):.4f}')" "$X0" "$X480")"
    rm -rf "$brouillon/projet-sauf"; cp -r "$brouillon/projet" "$brouillon/projet-sauf"
    rm -rf "$brouillon/projet"; cp -r "$brouillon/b-projet" "$brouillon/projet"
    course "$BIN" e "$REPLIER;1000:clic:pianoroll:$X1920,$Y36;1500:enregistrer:$brouillon/e-projet"
    rm -rf "$brouillon/projet"; mv "$brouillon/projet-sauf" "$brouillon/projet"
    E="$(frappes "$brouillon/e-projet")"
    echo "       (e) clic en $X1920,$Y36 (la rangée du milieu) : $E"
else
    echo "       (e) la géométrie de (b) manque : le clic n'est PAS joué"
fi
course "$BIN" en "500:carte-monter:42;1200:relever-carte" VSM_LANGUE=en
EN="$(grep -h "^VSM_CARTE : infobulle : " "$brouillon/en.txt" | tail -1)"
echo "       (h) ${EN:-aucune infobulle relevée}"

verdict "(a) sans geste : carte 36 38 42, repli 42 38 36, pas d'ordre au projet" \
    "$( [ "$A_C" = "36 38 42" ] && [ "$A_R" = "42 38 36" ] && [ "$A_P" = "aucun" ] && echo 1 || echo 0)"
verdict "(b) ▲ de 42 deux fois : carte 42 36 38" "$(egal "$B_C" "42 36 38")"
verdict "(b) le repli suit : 42 36 38 de haut en bas" "$(egal "$B_R" "42 36 38")"
verdict "(b) le projet porte drumOrder [42, 36, 38]" "$(egal "$B_P" "42 36 38")"
verdict "(b) deux pas « Ordre de la carte »" "$(egal "$B_H" "2")"
verdict "(c) Ctrl+Z défait un pas : carte 36 42 38" "$(egal "$C_C" "36 42 38")"
verdict "(d) rouvert : carte et repli 42 36 38" "$( [ "$D_C" = "42 36 38" ] && [ "$D_R" = "42 36 38" ] && echo 1 || echo 0)"
verdict "(e) un clic dans la rangée du milieu pose une frappe 36 à la mesure 2" "$(echo "$E" | grep -q "36@1920+" && echo 1 || echo 0)"
verdict "(h) l'infobulle de ▲ en anglais" "$(echo "$EN" | grep -q "Move this row up: the folded piano roll follows the order of the map." && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-b "$REPLIER;$MONTER;1800:relever-carte;2100:relever-losanges"
    T_R="$(repli t-b)"; T_V="$(grep -c "VSM_GESTE_APRES : carte-monter:42 .*refus" "$brouillon/t-b.txt")"
    echo "       témoin : repli $T_R ; carte $(carte t-b) ; $(grep -h "carte-monter" "$brouillon/t-b.txt" | head -1)"
    verdict "(f) témoin (avant D636) : le verbe inconnu, le repli 42 38 36" "$( [ "$T_R" = "42 38 36" ] && [ "$T_V" -ge 1 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "ORDRE-DE-LA-CARTE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ORDRE-DE-LA-CARTE : la carte se range, le fichier le garde, et le piano roll replié suit"
