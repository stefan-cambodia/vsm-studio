#!/usr/bin/env bash
# D600 — LES DÉPARTS ET LE TRIM QUI SUIVENT LEURS COURBES ET CÈDENT À LA MAIN, LA LECTURE DE L'AUTOMATION (le *R* d'une
# tranche de Cubase), LE TEMPS DE RETOUR (*Automation Return Time*), MESURÉS PAR CE QUE LA CONSOLE MONTRE, CE QUE LE
# MOTEUR JOUE, CE QUE L'EXPORT ÉCRIT ET LA COURBE ENREGISTRÉE.
#
# LE JEU (celui de D599) : 480 ticks à la noire, 4/4, 120 BPM (960 ticks par seconde) ; une piste MIDI « Partie »
# (vsm.testtone), UNE note tenue au do 6 (1 046 Hz) du tick 0 au tick 7 680 — le vumètre lit l'efficace d'un bloc, et
# une note plus grave le faisait bruiter de 2 dB (D599) ; volume statique 1,0 (0 dB). Selon la course, un bus de départ
# « Écho » (un égaliseur à plat, AVANT le fader), des courbes, un mode d'écriture, la lecture éteinte.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D600) :
#   (1) à l'arrêt, un départ de niveau statique 0 sous une courbe plate à 0,5, un trim statique de 0 dB sous une courbe
#       plate à −6 dB : le bouton du départ lit 0,50, le trim −6,0 dB ; le témoin 0,00 et 0,0 dB ;
#   (2) la main sur eux, W éteint, en lecture : le départ posé à 1,0 → joué 1,0 tant qu'on tient, puis 0,5 ; le trim
#       posé à 0 dB → joué 0 dB, puis −6 dB ;
#   (3) R éteint sur une courbe de volume plate à 0,25 : l'export de l'application et `vsm-render` sortent 12,0 dB plus
#       fort qu'avec R allumé (± 0,1 dB) ; le témoin : 0,0 dB d'écart ; le fader lit 0,0 dB ; W cliqué → refusé et dit ;
#       la courbe pâlie dans l'arrangement ; Ctrl+Z rallume ;
#   (4) le temps de retour : la passe Touch de D599 (3), lâchée au tick `fin` — à 250 ms (240 ticks), la courbe vaut 1,0
#       à `fin`, 0,625 à `fin + 120`, 0,25 à `fin + 241` ; le témoin du même code, le réglage à 0 : 0,25 dès `fin + 1` ;
#   (5) en anglais, R et le réglage nommés.
# LES TÉMOINS. Le binaire de D599 (`build/temoin-D599`) : son relevé ne connaît ni le R, ni le trim, ni les départs —
# ce qu'il MONTRE se lit par `VSM_TEXTES_LISTE` (la valeur affichée de chaque curseur nommé, à l'ouverture ; rien dans
# ce binaire ne fait bouger ces deux curseurs ensuite), ce qu'il fait ENTENDRE par le vumètre — celui de la tranche pour
# le trim (après lui), celui du master pour le départ (la piste muette au fader, le bus avant le fader : le master
# n'entend que le retour) —, ce qu'il EXPORTE par le fichier. Le témoin de (4) est le binaire neuf, le réglage à 0.
#
#   tools/automation-lecture.sh [binaire de l'application] [binaire témoin] [vsm-render] [vsm-render témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D599/Vintage Synth MIDI Studio}"
RENDU="${3:-./build/tools/vsm-render}"
RENDU_TEMOIN="${4:-./build/temoin-D599/vsm-render}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-lecture.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = [(0, 1, bytes([0x90, 84, 100])), (7680, 0, bytes([0x80, 84, 0]))]   # do 6, 1 046 Hz
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets; t = tick
corps += b"\x00\xff\x2f\x00"
def plat(parametre, valeur):
    return {"parameter": parametre, "points": [{"tick": 0, "value": valeur}, {"tick": 7680, "value": valeur}]}
ECHO = [{"name": "Écho", "effect": "eq", "returnGain": 1.0, "preFader": True, "parameters": {}}]
# nom : (volume statique, départs du projet, départs de la piste, courbes, mode, lecture)
jeux = {
    "repos":   (1.0, ECHO, [0.0], [plat("mix.trim", -6.0), plat("mix.send.1", 0.5)], "", True),
    "trim":    (1.0, [], [], [plat("mix.trim", -6.0)], "", True),
    "depart":  (0.0, ECHO, [0.0], [plat("mix.send.1", 0.5)], "", True),
    "allume":  (1.0, [], [], [plat("mix.volume", 0.25)], "", True),
    "eteint":  (1.0, [], [], [plat("mix.volume", 0.25)], "", False),
    "touch":   (1.0, [], [], [plat("mix.volume", 0.25)], "touch", True),
}
for nom, (volume, bus, departs, courbes, mode, lecture) in jeux.items():
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
             "mix": {"muted": False, "pan": 0.0, "sends": departs, "solo": False, "volume": volume},
             "instrument": {"preferredPlugin": "vsm.testtone"}, "automation": courbes}
    if mode: piste["automationMode"] = mode
    if not lecture: piste["automationRead"] = False
    projet = {"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
              "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                            "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
              "tracks": [piste]}
    if bus: projet["sends"] = bus
    json.dump(projet, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = dossier du projet ; le reste : variables (MAISON=… : un HOME préparé)
    local bin="$1" nom="$2" projet="$3" n maison
    shift 3
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    if [ -n "${RETOUR_MS:-}" ]; then   # D600.3 : le réglage, posé dans le fichier que JUCE lira (jamais celui de l'utilisateur)
        mkdir -p "$maison/VintageSynthMidiStudio"
        printf '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="automationRetourMs" val="%s"/>\n</PROPERTIES>\n' \
            "$RETOUR_MS" > "$maison/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    fi
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure (le témoin ne connaît pas les relevés
    # neufs : ses avertissements sont attendus, et comptés à part).
    local motif="VSM_(GESTE_APRES|GESTE|APPUI|RELACHE|VALEUR_POSEE|TOUCHE|BOITE|CLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }

# Un champ du k-ième relevé de la tranche 0 : $1 fichier, $2 rang, $3 motif (un groupe).
champ() {
    python3 - "$1" "$2" "$3" <<'PY'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_AUTOMATION_TRANCHE : piste 0 ")]
k = int(sys.argv[2])
if len(lignes) < k: print("ABSENT"); sys.exit(0)
m = re.search(sys.argv[3], lignes[k - 1])
print(m.group(1) if m else "?")
PY
}
valeur_affichee() { grep -m1 "^VSM_VALEUR : $2 : " "$1" | sed "s/^VSM_VALEUR : $2 : //"; }
efficace() { grep -m1 "^VSM_VUMETRE : tranche 0 " "$1" | sed -n 's/.*efficace \(-\?[0-9.inf-]*\) dBFS.*/\1/p'; }
crete_master() { grep -m1 "^VSM_VUMETRE : master : " "$1" | sed -n 's/^VSM_VUMETRE : master : \(-\?[0-9.inf-]*\) dBFS.*/\1/p'; }
ecart_wav() {   # l'écart d'efficace en dB entre deux WAV : $1 − $2
    python3 - "$1" "$2" <<'PY'
import math, struct, sys
def efficace(chemin):
    # LE RIFF LU À LA MAIN : `wave` refuse le flottant (format 3), que `vsm-render` écrit — le premier essai a rendu
    # « ? » pour lui et un vert pour l'application, qui écrit des entiers.
    try:
        d = open(chemin, "rb").read()
    except Exception:
        return None
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE": return None
    i, fmt, donnees = 12, None, None
    while i + 8 <= len(d):
        nom, taille = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24])
        elif nom == b"data": donnees = d[i + 8:i + 8 + taille]
        i += 8 + taille + (taille & 1)
    if fmt is None or donnees is None: return None
    code, bits = fmt[0], fmt[5]
    if code == 0xFFFE: code = 3 if bits == 32 and b"\x03\x00\x00\x00\x00\x00\x10\x00" in d[:200] else 1
    if code == 3 and bits == 32: v = struct.unpack("<%df" % (len(donnees) // 4), donnees); echelle = 1.0
    elif code == 1 and bits == 16: v = struct.unpack("<%dh" % (len(donnees) // 2), donnees); echelle = 32768.0
    elif code == 1 and bits == 32: v = struct.unpack("<%di" % (len(donnees) // 4), donnees); echelle = 2147483648.0
    elif code == 1 and bits == 24:
        v = [int.from_bytes(donnees[k:k + 3], "little", signed=True) for k in range(0, len(donnees) - 2, 3)]
        echelle = 8388608.0
    else: return None
    s = sum((x / echelle) ** 2 for x in v)
    return math.sqrt(s / max(1, len(v)))
a, b = efficace(sys.argv[1]), efficace(sys.argv[2])
print("?" if not a or not b else "%.2f" % (20 * math.log10(a / b)))
PY
}
courbe_volume() {   # la courbe de volume enregistrée : « tick:valeur » séparés par des espaces
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
for c in p['tracks'][0].get('automation', []):
    if c['parameter'] == 'mix.volume':
        print(' '.join('%d:%.4f%s' % (q['tick'], q['value'], ':p' if q.get('step') else '') for q in c['points']))" "$1"
}
retour() {   # « fin v(fin) v(fin+1) v(fin+120) v(fin+241) » — `fin`, le dernier point à 1,0 de la courbe enregistrée
    python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
pts = next(c['points'] for c in p['tracks'][0]['automation'] if c['parameter'] == 'mix.volume')
def v(t):
    if t <= pts[0]['tick']: return pts[0]['value']
    if t >= pts[-1]['tick']: return pts[-1]['value']
    for a, b in zip(pts, pts[1:]):
        if a['tick'] <= t <= b['tick']:
            if a.get('step') or b['tick'] == a['tick']: return a['value']
            return a['value'] + (t - a['tick']) / (b['tick'] - a['tick']) * (b['value'] - a['value'])
uns = [q['tick'] for q in pts if abs(q['value'] - 1.0) < 1e-4]
if not uns: print('ABSENT'); sys.exit(0)
fin = max(uns)
print('%d %.4f %.4f %.4f %.4f' % (fin, v(fin), v(fin + 1), v(fin + 120), v(fin + 241)))" "$1" 2>/dev/null || echo "ABSENT"
}
ambre() {   # LA CHALEUR de l'arrangement : Σ max(0, rouge − bleu) — la courbe est ambre, le clip dessous est bleu
    # CORRIGÉ APRÈS LE PREMIER ESSAI, ET DIT : l'instrument comptait les pixels d'un ambre « franc » sur toute la
    # moitié haute, et lisait 1 945 contre 1 862 — la tête de lecture et les cadres de sélection, ambre eux aussi ; le
    # trait de la courbe (1,5 px, anticrénelé sur le bleu du clip) n'atteint jamais l'ambre pur. La zone est désormais
    # l'arrangement seul (la tête, posée au tick 0, est à sa gauche), et la mesure une somme de chaleur.
    python3 - "$1" <<'PY'
import sys
from PIL import Image
try:
    im = Image.open(sys.argv[1]).convert("RGB")
except Exception:
    print(-1); sys.exit(0)
w, h = im.size
px = im.load()
print(sum(max(0, px[x, y][0] - px[x, y][2]) for x in range(int(w * 0.30), int(w * 0.74)) for y in range(int(h * 0.12), int(h * 0.62))))
PY
}
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D600 : les départs et le trim qui suivent leurs courbes, le R, le temps de retour ==="
# (1) à l'arrêt
cp_projet repos repos-enr
course "$BIN" repos "$brouillon/repos-enr" VSM_TEXTES_LISTE=1 VSM_DELAI=1500 \
    VSM_GESTE_APRES="900:relever-automation;1100:enregistrer:$brouillon/repos-enr"
# (2) la main, W éteint, en lecture : la photo pendant la prise (a), puis la prise entière (b)
cp_projet trim trim-a; cp_projet trim trim-b; cp_projet depart depart-a; cp_projet depart depart-b
TENIR_TRIM="700:appuyer:mixeur.trim;800:valeur:mixeur.trim=0"
TENIR_DEPART="700:appuyer:mixeur.depart;800:valeur:mixeur.depart=1"
course "$BIN" trim-a "$brouillon/trim-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR_TRIM;1500:relever-automation"
course "$BIN" trim-b "$brouillon/trim-b" VSM_LECTURE=1 VSM_DELAI=2600 \
    VSM_GESTE_APRES="$TENIR_TRIM;1300:relever-automation;1400:relever-etat;1600:relacher:mixeur.trim;1900:relever-automation"
course "$BIN" depart-a "$brouillon/depart-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR_DEPART;1500:relever-automation"
course "$BIN" depart-b "$brouillon/depart-b" VSM_LECTURE=1 VSM_DELAI=2600 \
    VSM_GESTE_APRES="$TENIR_DEPART;1300:relever-automation;1600:relacher:mixeur.depart;1900:relever-automation"
# (3) le R : les exports, l'application ouverte R éteint (le W refusé, la courbe pâlie), et R cliqué puis Ctrl+Z
course "$BIN" export-allume "$brouillon/allume" VSM_EXPORT="$brouillon/app-allume.wav" VSM_DELAI=800
course "$BIN" export-eteint "$brouillon/eteint" VSM_EXPORT="$brouillon/app-eteint.wav" VSM_DELAI=800
"$RENDU" "$brouillon/allume" "$brouillon/rendu-allume.wav" --tail 0 > "$brouillon/rendu-allume.txt" 2>&1
"$RENDU" "$brouillon/eteint" "$brouillon/rendu-eteint.wav" --tail 0 > "$brouillon/rendu-eteint.txt" 2>&1
course "$BIN" eteint "$brouillon/eteint" VSM_VUE="arrangement,sans-rapport,courbes" VSM_DELAI=2000 \
    VSM_GESTE_APRES="700:relever-automation;900:cliquer:mixeur.W;1100:relever-automation;1300:relever-etat"
course "$BIN" allume "$brouillon/allume" VSM_VUE="arrangement,sans-rapport,courbes" VSM_DELAI=1200 VSM_GESTE_APRES="700:relever-automation"
course "$BIN" annuler "$brouillon/allume" VSM_DELAI=2200 \
    VSM_GESTE_APRES="700:cliquer:mixeur.R;900:relever-automation;1100:relever-etat;1300:touche:ctrl + Z;1700:relever-automation"
# (4) le temps de retour : 250 ms (l'origine), et le témoin du même code, le réglage à 0
TENIR="700:appuyer:mixeur.volume;800:valeur:mixeur.volume=0"
PRISE="$TENIR;1300:relever-automation;1600:relacher:mixeur.volume;1900:relever-automation;2100:touche:spacebar"
cp_projet touch retour-250; cp_projet touch retour-0
course "$BIN" retour-250 "$brouillon/retour-250" VSM_LECTURE=1 VSM_DELAI=3000 \
    VSM_GESTE_APRES="$PRISE;2400:enregistrer:$brouillon/retour-250"
RETOUR_MS=0 course "$BIN" retour-0 "$brouillon/retour-0" VSM_LECTURE=1 VSM_DELAI=3000 \
    VSM_GESTE_APRES="$PRISE;2400:enregistrer:$brouillon/retour-0"
# (5) en anglais : l'infobulle du R, et les préférences
course "$BIN" en "$brouillon/repos" VSM_LANGUE=en VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-automation"
course "$BIN" preferences "$brouillon/repos" VSM_LANGUE=en VSM_VUE="sans-rapport,preferences" VSM_TEXTES_LISTE=1 \
    VSM_CAPTURE_PANNEAUX=1 VSM_DELAI=1500

T1="$(champ "$brouillon/repos.txt" 1 'trim (-?[\d.]+) dB \(')"; D1="$(champ "$brouillon/repos.txt" 1 'd\S+part 1 (-?[\d.]+) \(')"
echo "       (1) à l'arrêt : trim $T1 dB, départ $D1 ; affichés à l'ouverture : « $(valeur_affichee "$brouillon/repos.txt" mixeur.trim) », « $(valeur_affichee "$brouillon/repos.txt" mixeur.depart) »"
verdict "(1) à l'arrêt : le trim lit −6,0 dB et le départ 0,50 (leurs courbes)" \
    "$(proche "$T1" -6.0 0.05 && proche "$D1" 0.5 0.005 && echo 1 || echo 0)"
# LE DÉFAUT TROUVÉ PAR LE PREMIER ESSAI (D30.4) : la capture n'avait pas de nom pour le trim, et l'effaçait du projet.
trims() { python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
print(' '.join(c['parameter'] for c in p['tracks'][0].get('automation', [])))" "$1" 2>/dev/null || echo "ABSENT"; }
C1="$(trims "$brouillon/repos-enr")"
echo "       (1) courbes du projet enregistré : [$C1]"
verdict "(1) la courbe de trim survit à l'enregistrement (mix.trim et mix.send.1)" \
    "$(echo " $C1 " | grep -q " mix.trim " && echo " $C1 " | grep -q " mix.send.1 " && echo 1 || echo 0)"
TJ2p="$(champ "$brouillon/trim-b.txt" 1 'entendu (-?[\d.]+) dB\)')"; TJ2l="$(champ "$brouillon/trim-b.txt" 2 'entendu (-?[\d.]+) dB\)')"
TM2p="$(champ "$brouillon/trim-b.txt" 1 'main (\S+)')"
DJ2p="$(champ "$brouillon/depart-b.txt" 1 'd\S+part 1 [\d.-]+ \(courbe [\d.-]+, entendu (-?[\d.]+)\)')"
DJ2l="$(champ "$brouillon/depart-b.txt" 2 'd\S+part 1 [\d.-]+ \(courbe [\d.-]+, entendu (-?[\d.]+)\)')"
DM2p="$(champ "$brouillon/depart-b.txt" 1 'main (\S+)')"
etat2="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/trim-b.txt")"
echo "       (2) trim : entendu $TJ2p dB pendant (main $TM2p), $TJ2l dB après ; départ : entendu $DJ2p pendant (main $DM2p), $DJ2l après ; $etat2"
verdict "(2) la main sur le trim : joué 0 dB tant qu'on tient, −6 dB après" \
    "$(proche "$TJ2p" 0.0 0.01 && proche "$TJ2l" -6.0 0.01 && echo 1 || echo 0)"
verdict "(2) la main sur le départ : joué 1,0 tant qu'on tient, 0,5 après" \
    "$(proche "$DJ2p" 1.0 0.005 && proche "$DJ2l" 0.5 0.005 && echo 1 || echo 0)"
verdict "(2) W éteint, la ligne d'état dit que le trim suit sa courbe" "$(echo "$etat2" | grep -q "Trim d'entr.e : suivi de sa courbe" && echo 1 || echo 0)"

E_app="$(ecart_wav "$brouillon/app-eteint.wav" "$brouillon/app-allume.wav")"
E_rendu="$(ecart_wav "$brouillon/rendu-eteint.wav" "$brouillon/rendu-allume.wav")"
F3="$(champ "$brouillon/eteint.txt" 1 'fader (-?[\d.]+) dB')"; R3="$(champ "$brouillon/eteint.txt" 1 '; R (\S+) ;')"
W3="$(champ "$brouillon/eteint.txt" 2 '(W(?: \w+)?) ; t')"
etat3="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/eteint.txt")"
dit_rendu="$(grep -c "lecture éteinte" "$brouillon/rendu-eteint.txt")"
echo "       (3) export R éteint − R allumé : application $E_app dB, vsm-render $E_rendu dB ($dit_rendu ligne(s) « lecture éteinte ») ; fader $F3 dB, R $R3, W après le clic « $W3 » ; $etat3"
verdict "(3) l'export de l'application, R éteint : +12,0 dB (± 0,1)" "$(proche "$E_app" 12.04 0.1 && echo 1 || echo 0)"
verdict "(3) vsm-render, R éteint : +12,0 dB (± 0,1), et il le dit" "$(proche "$E_rendu" 12.04 0.1 && [ "$dit_rendu" -ge 1 ] && echo 1 || echo 0)"
verdict "(3) R éteint : le fader lit 0,0 dB, le R « éteint »" "$(proche "$F3" 0.0 0.05 && [ "$R3" = "éteint" ] && echo 1 || echo 0)"
verdict "(3) R éteint : W cliqué → refusé (le mode reste « W ») et dit" \
    "$([ "$W3" = "W" ] && echo "$etat3" | grep -q "la rallumer pour écrire" && echo 1 || echo 0)"
A_on="$(ambre "$brouillon/allume.png")"; A_off="$(ambre "$brouillon/eteint.png")"
echo "       (3) chaleur de l'arrangement (Σ rouge − bleu) : R allumé $A_on, R éteint $A_off"
verdict "(3) la courbe pâlie : moins d'un tiers de la chaleur de R allumé" \
    "$([ "$A_on" -gt 10000 ] && [ "$A_off" -ge 0 ] && [ $((A_off * 3)) -lt "$A_on" ] && echo 1 || echo 0)"
Ra="$(champ "$brouillon/annuler.txt" 1 '; R (\S+) ;')"; Fa="$(champ "$brouillon/annuler.txt" 1 'fader (-?[\d.]+) dB')"
Rb="$(champ "$brouillon/annuler.txt" 2 '; R (\S+) ;')"; Fb="$(champ "$brouillon/annuler.txt" 2 'fader (-?[\d.]+) dB')"
echo "       (3) R cliqué : R $Ra, fader $Fa dB ; Ctrl+Z : R $Rb, fader $Fb dB"
verdict "(3) R cliqué l'éteint (fader 0,0 dB) ; Ctrl+Z le rallume (fader −12,0 dB)" \
    "$([ "$Ra" = "éteint" ] && proche "$Fa" 0.0 0.05 && [ "$Rb" = "allumé" ] && proche "$Fb" -12.04 0.1 && echo 1 || echo 0)"

r250="$(retour "$brouillon/retour-250")"; r0="$(retour "$brouillon/retour-0")"
echo "       (4) fin v(fin) v(fin+1) v(fin+120) v(fin+241) : 250 ms [$r250] ; 0 ms [$r0]"
echo "           courbes : 250 ms [$(courbe_volume "$brouillon/retour-250")] ; 0 ms [$(courbe_volume "$brouillon/retour-0")]"
set -- $r250
verdict "(4) 250 ms : 1,0 à fin, 0,625 à fin + 120, 0,25 à fin + 241" \
    "$([ "$#" = 5 ] && proche "$2" 1.0 0.001 && proche "$4" 0.625 0.002 && proche "$5" 0.25 0.001 && echo 1 || echo 0)"
set -- $r0
verdict "(4) le témoin, 0 ms : 1,0 à fin, 0,25 dès fin + 1" \
    "$([ "$#" = 5 ] && proche "$2" 1.0 0.001 && proche "$3" 0.25 0.001 && echo 1 || echo 0)"

enR="$(grep -c "^VSM_AUTOMATION_R : .*Automation Read" "$brouillon/en.txt")"
enP="$(grep -cE "^VSM_(FENETRE_)?TEXTE : .*Automation return time" "$brouillon/preferences.txt")"
verdict "(5) en anglais : l'infobulle du R nomme « Automation Read », la préférence « Automation return time »" \
    "$([ "$enR" -ge 1 ] && [ "$enP" -ge 1 ] && echo 1 || echo 0)"

# LES TÉMOINS (le binaire de D599) : ce qu'il montre, ce qu'il fait entendre, ce qu'il exporte.
if [ -x "$TEMOIN" ]; then
    cp_projet trim t-trim-a; cp_projet depart t-depart-a
    cp_projet repos t-repos-enr
    course "$TEMOIN" t-repos "$brouillon/t-repos-enr" VSM_TEXTES_LISTE=1 VSM_DELAI=1500 \
        VSM_GESTE_APRES="1100:enregistrer:$brouillon/t-repos-enr"
    echo "       témoin : courbes du projet enregistré [$(trims "$brouillon/t-repos-enr")]"
    course "$TEMOIN" t-trim-a "$brouillon/t-trim-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR_TRIM"
    course "$TEMOIN" t-depart-a "$brouillon/t-depart-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR_DEPART"
    course "$TEMOIN" t-export-allume "$brouillon/allume" VSM_EXPORT="$brouillon/t-app-allume.wav" VSM_DELAI=800
    course "$TEMOIN" t-export-eteint "$brouillon/eteint" VSM_EXPORT="$brouillon/t-app-eteint.wav" VSM_DELAI=800
    vt="$(valeur_affichee "$brouillon/t-repos.txt" mixeur.trim)"; vd="$(valeur_affichee "$brouillon/t-repos.txt" mixeur.depart)"
    echo "       témoin (1) : affichés « $vt », « $vd »"
    verdict "(1) témoin : le trim montre 0,0 dB et le départ -inf (la valeur statique)" \
        "$(echo "$vt" | grep -q "0\.0 dB" && ! echo "$vt" | grep -q "6\.0" && echo "$vd" | grep -q "inf" && echo 1 || echo 0)"
    neuf="$(efficace "$brouillon/trim-a.txt")"; vieux="$(efficace "$brouillon/t-trim-a.txt")"
    ecart="$(python3 -c "import sys; print('%.2f' % (float(sys.argv[1]) - float(sys.argv[2])))" "$neuf" "$vieux" 2>/dev/null || echo "?")"
    echo "       vumètre de la tranche, la main sur le trim : $neuf dBFS (neuf) contre $vieux (témoin) → $ecart dB"
    verdict "(entendu) trim : +6 dB au-dessus du témoin pendant que la main tient (± 1,5 dB)" "$(proche "$ecart" 6.02 1.5 && echo 1 || echo 0)"
    neuf="$(crete_master "$brouillon/depart-a.txt")"; vieux="$(crete_master "$brouillon/t-depart-a.txt")"
    ecart="$(python3 -c "import sys; print('%.2f' % (float(sys.argv[1]) - float(sys.argv[2])))" "$neuf" "$vieux" 2>/dev/null || echo "?")"
    echo "       crête du master, la main sur le départ : $neuf dBFS (neuf) contre $vieux (témoin) → $ecart dB"
    verdict "(entendu) départ : +6 dB au-dessus du témoin pendant que la main tient (± 1,5 dB)" "$(proche "$ecart" 6.02 1.5 && echo 1 || echo 0)"
    E_t="$(ecart_wav "$brouillon/t-app-eteint.wav" "$brouillon/t-app-allume.wav")"
    if [ -x "$RENDU_TEMOIN" ]; then
        "$RENDU_TEMOIN" "$brouillon/allume" "$brouillon/t-rendu-allume.wav" --tail 0 > /dev/null 2>&1
        "$RENDU_TEMOIN" "$brouillon/eteint" "$brouillon/t-rendu-eteint.wav" --tail 0 > /dev/null 2>&1
        E_tr="$(ecart_wav "$brouillon/t-rendu-eteint.wav" "$brouillon/t-rendu-allume.wav")"
    else E_tr="(vsm-render témoin absent)"; fi
    echo "       témoin (3) : export de l'application $E_t dB, vsm-render $E_tr dB"
    verdict "(3) témoin : 0,0 dB d'écart (le champ ignoré)" "$(proche "$E_t" 0.0 0.05 && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : les témoins de (1), (2) et (3) ne sont PAS joués"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-LECTURE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-LECTURE : les départs et le trim suivent leurs courbes et cèdent à la main, le R fait taire les courbes, et la passe revient en rampe"
