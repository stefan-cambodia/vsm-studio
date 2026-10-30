#!/usr/bin/env bash
# D601 — LA LECTURE ET L'ÉCRITURE DE TOUTES LES PISTES (les R et W globaux de la console de Cubase), LA SUSPENSION PAR
# FAMILLE (le *Suspend Read* du panneau d'automation), LE PANNEAU QUI PÂLIT CE QUI NE JOUE PAS — mesurés par les menus,
# le relevé des tranches, les projets enregistrés, ce que `vsm-render` et l'export de l'application en rendent, et la
# photo de l'onglet « Automation ».
#
# LE JEU (celui de D599) : 480 ticks à la noire, 120 BPM, une note tenue au do 6 (1 046 Hz) sur le générateur de test par
# piste ; volume statique 1,0.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D601, et ses deux corrections d'avant la mesure) :
#   (1) trois pistes A, B, C, une courbe de volume plate à 0,25 chacune ; C à R éteint, A en Touch, B en Trim :
#       « Éteindre la lecture de toutes les pistes (2) » → trois R éteints, deux W désarmés, « Lecture éteinte sur 2
#       piste(s) » ; piste par piste (`vsm-render --stems` sur le projet enregistré après le geste) : A et B +12,0 dB
#       (± 0,1), C 0,0 dB ; Ctrl+Z → R allumé, allumé, éteint, W touch et trim ; « Allumer… (1) » → trois R allumés ;
#       « Désarmer le W… (2) » → « W » partout ; à la fin, les deux entrées à zéro grisées ;
#   (2) une piste, volume plat à 0,25 et panoramique plat à −1 : « Suspendre le volume » → le fader à 0,0 dB, l'export
#       de l'application et `vsm-render` +12,0 dB (± 0,1) et toujours à gauche (le canal droit 40 dB sous le gauche),
#       `vsm-render` le dit, le projet enregistré porte `"automationSuspended": ["volume"]` ; Ctrl+Z le rétablit ;
#       « Suspendre le panoramique » à la place → les deux canaux à 1 dB l'un de l'autre ; un projet sans suspension
#       n'en écrit rien ;
#   (3) le panneau, une courbe de machine (la coupure du Minimoog) : R allumé « lue », R éteint « tue », et l'encre de la
#       lane sous 45 % de celle de la courbe lue ;
#   (4) en anglais, les entrées et le sous-menu nommés.
# LES TÉMOINS : le binaire de D600 (`build/temoin-D600`, application et `vsm-render`) — ses menus n'ont pas les entrées
# (`VSM_MENU : aucune entrée…`), son `vsm-render` ignore le champ (0,0 dB), son relevé ne dit ni « lue » ni « tue ».
#
#   tools/automation-globale.sh [binaire] [binaire témoin] [vsm-render] [vsm-render témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D600/Vintage Synth MIDI Studio}"
RENDU="${3:-./build/tools/vsm-render}"
RENDU_TEMOIN="${4:-./build/temoin-D600/vsm-render}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-globale.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def midi(noms):
    pistes = []
    for i, nom in enumerate(noms):
        corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if i == 0 else b"") + b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
        corps += vlq(0) + bytes([0x90 | i, 84, 100]) + vlq(7680) + bytes([0x80 | i, 84, 0]) + b"\x00\xff\x2f\x00"
        pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(noms), 480) + b"".join(pistes)
def plat(parametre, valeur):
    return {"parameter": parametre, "points": [{"tick": 0, "value": valeur}, {"tick": 7680, "value": valeur}]}
def piste(i, nom, courbes, machine="vsm.testtone", mode="", lecture=True):
    p = {"channel": i, "color": "#FF6B9BFF", "effects": [], "name": nom,
         "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 1.0},
         "instrument": {"preferredPlugin": machine}, "automation": courbes}
    if mode: p["automationMode"] = mode
    if not lecture: p["automationRead"] = False
    return p
def projet(nom, pistes, noms, extra=None):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(midi(noms))
    p = {"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
         "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                       "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}, "tracks": pistes}
    if extra: p.update(extra)
    json.dump(p, open(f"{b}/{nom}/project.json", "w"), indent=1)
v = [plat("mix.volume", 0.25)]
projet("trois", [piste(0, "A", v, mode="touch"), piste(1, "B", v, mode="trim"), piste(2, "C", v, lecture=False)],
       ["A", "B", "C"])
vp = [plat("mix.volume", 0.25), plat("mix.pan", -1.0)]
projet("susp", [piste(0, "Partie", vp)], ["Partie"])
projet("susp-main", [piste(0, "Partie", vp)], ["Partie"], {"automationSuspended": ["volume"]})   # pour les vsm-render
coupure = [{"parameter": "filter.1.cutoff", "points": [{"tick": 0, "value": 300.0}, {"tick": 3840, "value": 4000.0},
                                                        {"tick": 7680, "value": 800.0}]}]
projet("panneau-lu", [piste(0, "Partie", coupure, machine="vsm.minimoog")], ["Partie"])
projet("panneau-tu", [piste(0, "Partie", coupure, machine="vsm.minimoog", lecture=False)], ["Partie"])
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = dossier du projet ; le reste : variables
    local bin="$1" nom="$2" projet="$3" n
    shift 3
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|MENU|TOUCHE|BOITE|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }
# Les relevés, piste par piste : « R/W » de chaque piste au k-ième relevé (« éteint/W touch éteint/W … »).
etats() {   # $1 = fichier ; $2 = rang du relevé (un relevé = une ligne par piste, dans l'ordre)
    python3 - "$1" "$2" <<'PY'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_AUTOMATION_TRANCHE : piste ")]
pistes = sorted({int(re.match(r"VSM_AUTOMATION_TRANCHE : piste (\d+)", l).group(1)) for l in lignes})
n, k = len(pistes), int(sys.argv[2])
bloc = lignes[(k - 1) * n:k * n] if n else []
if len(bloc) < n or n == 0: print("ABSENT"); sys.exit(0)
def f(motif, l):
    m = re.search(motif, l)
    return m.group(1) if m else "?"
print(" ".join(f(r"; R (\S+) ;", l) + "/" + f(r"» : (W(?: \w+)?) ; t", l).replace(" ", "_") for l in bloc))
PY
}
champ() {   # $1 fichier ; $2 rang (piste 0) ; $3 motif
    python3 - "$1" "$2" "$3" <<'PY'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_AUTOMATION_TRANCHE : piste 0 ")]
k = int(sys.argv[2])
if len(lignes) < k: print("ABSENT"); sys.exit(0)
m = re.search(sys.argv[3], lignes[k - 1])
print(m.group(1) if m else "?")
PY
}
etat() { grep "^VSM_ETAT_PIANOROLL" "$1" | sed -n "${2}p"; }
niveaux() {   # efficace gauche et droite d'un WAV (PCM ou flottant), en dBFS
    python3 - "$1" <<'PY'
import math, struct, sys
try:
    d = open(sys.argv[1], "rb").read()
except Exception:
    print("? ?"); sys.exit(0)
if d[:4] != b"RIFF": print("? ?"); sys.exit(0)
i, fmt, donnees = 12, None, None
while i + 8 <= len(d):
    nom, taille = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24])
    elif nom == b"data": donnees = d[i + 8:i + 8 + taille]
    i += 8 + taille + (taille & 1)
code, canaux, bits = fmt[0], fmt[1], fmt[5]
if code == 0xFFFE: code = 3 if bits == 32 and b"\x03\x00\x00\x00\x00\x00\x10\x00" in d[:200] else 1
if code == 3 and bits == 32: v = struct.unpack("<%df" % (len(donnees) // 4), donnees); e = 1.0
elif code == 1 and bits == 16: v = struct.unpack("<%dh" % (len(donnees) // 2), donnees); e = 32768.0
elif code == 1 and bits == 32: v = struct.unpack("<%di" % (len(donnees) // 4), donnees); e = 2147483648.0
elif code == 1 and bits == 24:
    v = [int.from_bytes(donnees[k:k + 3], "little", signed=True) for k in range(0, len(donnees) - 2, 3)]; e = 8388608.0
else: print("? ?"); sys.exit(0)
g = v[0::canaux]; dr = v[1::canaux] if canaux > 1 else v[0::canaux]
def db(x):
    s = math.sqrt(sum((y / e) ** 2 for y in x) / max(1, len(x)))
    return "%.2f" % (20 * math.log10(s)) if s > 0 else "-999"
print(db(g), db(dr))
PY
}
total() { python3 -c "import math,sys; a,b=float(sys.argv[1]),float(sys.argv[2]); print('%.2f' % (10*math.log10((10**(a/10)+10**(b/10))/2)))" "$@" 2>/dev/null || echo "?"; }
ecart() { python3 -c "import sys; print('%.2f' % (float(sys.argv[1]) - float(sys.argv[2])))" "$@" 2>/dev/null || echo "?"; }
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }
stems() {   # $1 = dossier du projet ; $2 = dossier de sortie ; $3 = vsm-render — rend « dB(A) dB(B) dB(C) »
    rm -rf "$2"; mkdir -p "$2"
    "$3" "$1" "$2" --stems "$2" --tail 0 > "$2.txt" 2>&1
    # CORRIGÉ APRÈS LE PREMIER ESSAI : les stems s'appellent « 01 - A.wav » — un `for f in $(ls …)` les coupait en trois
    # mots, et rendait neuf « ? » ; le glob entre guillemets les garde entiers (et dans l'ordre des pistes).
    local f
    for f in "$2"/*.wav; do [ -f "$f" ] || continue; set -- $(niveaux "$f"); total "$1" "$2"; done | tr '\n' ' '
}
trois_nombres() { [[ "$1" =~ ^(-?[0-9]+\.[0-9]+\ ){3}$ ]]; }
encre() {   # l'encre SARCELLE de la lane d'automation : Σ max(0, vert − rouge), dans la boîte des pixels sarcelle de $2
    # CORRIGÉ APRÈS LE PREMIER ESSAI, ET DIT : l'instrument écrit avant la mesure (Σ de l'écart au fond, canal le plus
    # écarté) comptait aussi la grille, les graduations et la tête de lecture, identiques dans les deux photos — 650 656
    # contre 319 079, 49 % ; la photo montrait pourtant la courbe nettement pâlie. La sarcelle seule (le gris, la grille
    # et l'ambre y valent zéro) rend 327 420 contre 113 422 : 34,6 %, l'encre de 0,35 posée. Le seuil (45 %) n'a pas bougé.
    python3 - "$1" "$2" <<'PY'
import sys
from PIL import Image
try:
    im = Image.open(sys.argv[1]).convert("RGB"); ref = Image.open(sys.argv[2]).convert("RGB")
except Exception:
    print(-1); sys.exit(0)
w, h = ref.size
pr, px = ref.load(), im.load()
# LA BOÎTE : les pixels nettement sarcelle (g − r > 60) de la photo de RÉFÉRENCE (la courbe lue), sous la moitié haute —
# la lane du dock ; la même boîte pour les deux photos.
xs, ys = [], []
for y in range(h // 2, h):
    for x in range(w):
        r, g, b = pr[x, y]
        if g - r > 60 and b - r > 40: xs.append(x); ys.append(y)
if not xs: print(-1); sys.exit(0)
x0, x1, y0, y1 = max(0, min(xs) - 4), min(w, max(xs) + 5), max(0, min(ys) - 4), min(h, max(ys) + 5)
print(sum(max(0, px[x, y][1] - px[x, y][0]) for x in range(x0, x1) for y in range(y0, y1)))
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D601 : la lecture de toutes les pistes, la suspension par famille, le panneau qui pâlit ==="
# (1) toutes les pistes
cp_projet trois trois-a
course "$BIN" trois "$brouillon/trois-a" VSM_DELAI=3600 VSM_GESTE_APRES="600:relever-automation;800:menu:Éteindre la lecture de toutes les pistes (2);1000:relever-automation;1100:relever-etat;1300:enregistrer:$brouillon/trois-eteint;1500:touche:ctrl + Z;1800:relever-automation;2000:menu:Allumer la lecture de toutes les pistes (1);2200:relever-automation;2400:menu:Désarmer le W de toutes les pistes (2);2600:relever-automation;2800:lister-menus"
s0="$(stems "$brouillon/trois" "$brouillon/st-avant" "$RENDU")"; s1="$(stems "$brouillon/trois-eteint" "$brouillon/st-apres" "$RENDU")"
e0="$(etats "$brouillon/trois.txt" 1)"; e1="$(etats "$brouillon/trois.txt" 2)"; e2="$(etats "$brouillon/trois.txt" 3)"
e3="$(etats "$brouillon/trois.txt" 4)"; e4="$(etats "$brouillon/trois.txt" 5)"
etat1="$(etat "$brouillon/trois.txt" 1)"
echo "       (1) R/W : départ [$e0] ; éteindre [$e1] ; Ctrl+Z [$e2] ; allumer [$e3] ; désarmer [$e4]"
echo "           stems A B C, dBFS : avant [$s0] ; après [$s1] ; $etat1"
verdict "(1) éteindre partout : trois R éteints, les deux W désarmés, et dit" \
    "$([ "$e1" = "éteint/W éteint/W éteint/W" ] && echo "$etat1" | grep -q "Lecture éteinte sur 2 piste(s)" && echo 1 || echo 0)"
set -- $s0; a0="${1:-x}"; b0="${2:-x}"; c0="${3:-x}"; set -- $s1; a1="${1:-x}"; b1="${2:-x}"; c1="${3:-x}"
verdict "(1) piste par piste : A et B +12,0 dB (± 0,1), C 0,0 dB" \
    "$(proche "$(ecart "$a1" "$a0")" 12.04 0.1 && proche "$(ecart "$b1" "$b0")" 12.04 0.1 && proche "$(ecart "$c1" "$c0")" 0.0 0.05 && echo 1 || echo 0)"
echo "           écarts : A $(ecart "$a1" "$a0"), B $(ecart "$b1" "$b0"), C $(ecart "$c1" "$c0") dB"
verdict "(1) Ctrl+Z : R allumé, allumé, éteint, W touch et trim" "$([ "$e2" = "allumé/W_touch allumé/W_trim éteint/W" ] && echo 1 || echo 0)"
verdict "(1) allumer partout, puis désarmer partout" \
    "$([ "$e3" = "allumé/W_touch allumé/W_trim allumé/W" ] && [ "$e4" = "allumé/W allumé/W allumé/W" ] && echo 1 || echo 0)"
gris="$(grep -cE "^VSM_MENU_LISTE : .*(Allumer la lecture de toutes les pistes \(0\)|Désarmer le W de toutes les pistes \(0\)) \[grisée\]" "$brouillon/trois.txt")"
verdict "(1) à la fin, « Allumer… (0) » et « Désarmer le W… (0) » grisés" "$([ "$gris" = 2 ] && echo 1 || echo 0)"

# (2) la suspension
cp_projet susp susp-v; cp_projet susp susp-p; cp_projet susp susp-rien
course "$BIN" susp-v "$brouillon/susp-v" VSM_DELAI=2400 VSM_GESTE_APRES="600:menu:Suspendre le volume;800:relever-automation;900:relever-etat;1100:enregistrer:$brouillon/susp-v;1300:exporter-audio:$brouillon/app-susp.wav;1700:touche:ctrl + Z;2000:relever-automation"
course "$BIN" susp-0 "$brouillon/susp-rien" VSM_DELAI=1600 VSM_GESTE_APRES="600:enregistrer:$brouillon/susp-rien;800:exporter-audio:$brouillon/app-lu.wav"
course "$BIN" susp-p "$brouillon/susp-p" VSM_DELAI=1200 VSM_GESTE_APRES="600:menu:Suspendre le panoramique;900:enregistrer:$brouillon/susp-p"
"$RENDU" "$brouillon/susp" "$brouillon/r-lu.wav" --tail 0 > "$brouillon/r-lu.txt" 2>&1
"$RENDU" "$brouillon/susp-v" "$brouillon/r-v.wav" --tail 0 > "$brouillon/r-v.txt" 2>&1
"$RENDU" "$brouillon/susp-p" "$brouillon/r-p.wav" --tail 0 > "$brouillon/r-p.txt" 2>&1
F2="$(champ "$brouillon/susp-v.txt" 1 'fader (-?[\d.]+) dB')"
S2="$(grep "^VSM_AUTOMATION_SUSPENDUES" "$brouillon/susp-v.txt" | sed -n 1p | sed 's/^VSM_AUTOMATION_SUSPENDUES : //')"
S2z="$(grep "^VSM_AUTOMATION_SUSPENDUES" "$brouillon/susp-v.txt" | sed -n 2p | sed 's/^VSM_AUTOMATION_SUSPENDUES : //')"
F2z="$(champ "$brouillon/susp-v.txt" 2 'fader (-?[\d.]+) dB')"
etat2="$(etat "$brouillon/susp-v.txt" 1)"
set -- $(niveaux "$brouillon/r-lu.wav"); lg="${1:-x}"; ld="${2:-x}"
set -- $(niveaux "$brouillon/r-v.wav"); vg="${1:-x}"; vd="${2:-x}"
set -- $(niveaux "$brouillon/r-p.wav"); pg="${1:-x}"; pd="${2:-x}"
set -- $(niveaux "$brouillon/app-lu.wav"); alg="${1:-x}"; ald="${2:-x}"
set -- $(niveaux "$brouillon/app-susp.wav"); asg="${1:-x}"; asd="${2:-x}"
champ_fichier="$(python3 -c "
import json, sys
for d in sys.argv[1:]:
    try: print(json.load(open(d + '/project.json')).get('automationSuspended', 'absent'), end=' ; ')
    except Exception: print('ABSENT', end=' ; ')" "$brouillon/susp-v" "$brouillon/susp-rien" "$brouillon/susp-p")"
dit2="$(grep -c "lecture suspendue" "$brouillon/r-v.txt")"
echo "       (2) volume suspendu : fader $F2 dB, suspendues « $S2 » ; Ctrl+Z : « $S2z », fader $F2z dB ; $etat2"
echo "           vsm-render G/D dBFS : lu [$lg $ld] volume suspendu [$vg $vd] panoramique suspendu [$pg $pd] ; application : lu [$alg $ald] suspendu [$asg $asd]"
echo "           fichiers : $champ_fichier ; « lecture suspendue » dit $dit2 fois par vsm-render"
verdict "(2) volume suspendu : le fader à 0,0 dB, « volume » au relevé, et dit" \
    "$(proche "$F2" 0.0 0.05 && [ "$S2" = "volume" ] && echo "$etat2" | grep -qi "suspendue" && echo 1 || echo 0)"
verdict "(2) vsm-render : +12,0 dB (± 0,1) et toujours à gauche (le droit 40 dB sous le gauche), et il le dit" \
    "$(proche "$(ecart "$vg" "$lg")" 12.04 0.1 && python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) - float(sys.argv[2]) > 40 else 1)" "$vg" "$vd" && [ "$dit2" -ge 1 ] && echo 1 || echo 0)"
verdict "(2) l'export de l'application : +12,0 dB (± 0,1)" "$(proche "$(ecart "$asg" "$alg")" 12.04 0.1 && echo 1 || echo 0)"
verdict "(2) panoramique suspendu : les deux canaux à 1 dB l'un de l'autre" "$(proche "$pg" "$pd" 1.0 && echo 1 || echo 0)"
verdict "(2) le fichier : [\"volume\"], rien sans suspension, [\"pan\"] pour le panoramique" \
    "$([ "$champ_fichier" = "['volume'] ; absent ; ['pan'] ; " ] && echo 1 || echo 0)"
verdict "(2) Ctrl+Z rétablit la lecture (aucune famille, fader −12,0 dB)" "$([ "$S2z" = "aucune" ] && proche "$F2z" -12.04 0.1 && echo 1 || echo 0)"

# (3) le panneau
course "$BIN" panneau-lu "$brouillon/panneau-lu" VSM_VUE="sans-rapport,automation" VSM_AUTOMATION=1 VSM_DELAI=1500
course "$BIN" panneau-tu "$brouillon/panneau-tu" VSM_VUE="sans-rapport,automation" VSM_AUTOMATION=1 VSM_DELAI=1500
l3="$(grep -m1 "^VSM_AUTOMATION : piste" "$brouillon/panneau-lu.txt" | grep -o "point(s), [a-z]*," )"
t3="$(grep -m1 "^VSM_AUTOMATION : piste" "$brouillon/panneau-tu.txt" | grep -o "point(s), [a-z]*," )"
i_lu="$(encre "$brouillon/panneau-lu.png" "$brouillon/panneau-lu.png")"; i_tu="$(encre "$brouillon/panneau-tu.png" "$brouillon/panneau-lu.png")"
echo "       (3) relevé du panneau : lu « $l3 », R éteint « $t3 » ; encre de la lane : $i_lu contre $i_tu"
verdict "(3) le relevé dit « lue » puis « tue »" "$([ "$l3" = "point(s), lue," ] && [ "$t3" = "point(s), tue," ] && echo 1 || echo 0)"
verdict "(3) la courbe tue pâlie : son encre sous 45 % de celle de la courbe lue" \
    "$([ "$i_lu" -gt 1000 ] && [ "$i_tu" -ge 0 ] && [ $((i_tu * 100)) -lt $((i_lu * 45)) ] && echo 1 || echo 0)"

# (4) en anglais
course "$BIN" en "$brouillon/trois" VSM_LANGUE=en VSM_DELAI=1200 VSM_GESTE_APRES="600:lister-menus"
en1="$(grep -c "^VSM_MENU_LISTE : .*Turn off automation read on all tracks (2)" "$brouillon/en.txt")"
en2="$(grep -c "^VSM_MENU_LISTE : .*Suspend automation read > Suspend volume" "$brouillon/en.txt")"
verdict "(4) en anglais : « Turn off automation read on all tracks (2) » et « Suspend automation read > Suspend volume »" \
    "$([ "$en1" -ge 1 ] && [ "$en2" -ge 1 ] && echo 1 || echo 0)"

# LES TÉMOINS (le binaire de D600)
if [ -x "$TEMOIN" ]; then
    cp_projet trois t-trois-a
    course "$TEMOIN" t-trois "$brouillon/t-trois-a" VSM_DELAI=1500 VSM_GESTE_APRES="600:menu:Éteindre la lecture de toutes les pistes (2);1000:enregistrer:$brouillon/t-trois-a"
    st="$(stems "$brouillon/t-trois-a" "$brouillon/st-temoin" "$RENDU")"
    ab="$(grep -c "VSM_MENU : .*aucune entr" "$brouillon/t-trois.txt")"
    echo "       témoin (1) : « aucune entrée » $ab fois ; stems après le geste [$st] contre avant [$s0]"
    # TROIS NOMBRES, pas deux listes égales : le premier essai a passé ce contrôle sur deux listes de « ? ».
    verdict "(1) témoin : l'entrée absente, les stems inchangés" \
        "$([ "$ab" -ge 1 ] && trois_nombres "$st" && [ "$st" = "$s0" ] && echo 1 || echo 0)"
    if [ -x "$RENDU_TEMOIN" ]; then
        "$RENDU_TEMOIN" "$brouillon/susp-main" "$brouillon/t-r-v.wav" --tail 0 > /dev/null 2>&1
        set -- $(niveaux "$brouillon/t-r-v.wav"); tg="${1:-x}"
        echo "       témoin (2) : vsm-render de D600 sur un projet au volume suspendu : $(ecart "$tg" "$lg") dB"
        verdict "(2) témoin : 0,0 dB d'écart (le champ ignoré)" "$(proche "$(ecart "$tg" "$lg")" 0.0 0.05 && echo 1 || echo 0)"
    fi
    course "$TEMOIN" t-panneau-tu "$brouillon/panneau-tu" VSM_VUE="sans-rapport,automation" VSM_AUTOMATION=1 VSM_DELAI=1500
    t_tu="$(encre "$brouillon/t-panneau-tu.png" "$brouillon/panneau-lu.png")"
    echo "       témoin (3) : relevé « $(grep -m1 "^VSM_AUTOMATION : piste" "$brouillon/t-panneau-tu.txt" | grep -o "point(s), [a-z0-9]*")… », encre $t_tu"
    verdict "(3) témoin : la courbe d'une piste à R éteint garde son encre (plus de 80 % de la lue)" \
        "$([ "$t_tu" -ge 0 ] && [ $((t_tu * 100)) -gt $((i_lu * 80)) ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : les témoins ne sont PAS joués"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-GLOBALE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-GLOBALE : toute la lecture d'un geste, une famille suspendue sur le projet, et le panneau pâlit ce qui ne joue pas"
