#!/usr/bin/env bash
# D599 — LE FADER QUI SUIT SA COURBE, LA MAIN QUI L'EMPORTE, LE MODE TRIM (le *Read*, le *Touch* qui s'entend et le *Trim*
# d'une tranche de Cubase), MESURÉS PAR CE QUE LA CONSOLE MONTRE, CE QUE LE MOTEUR JOUE ET LA COURBE ENREGISTRÉE.
#
# LE JEU : 480 ticks à la noire, 4/4, 120 BPM (960 ticks par seconde) ; une piste MIDI « Partie » (vsm.minimoog), une
# note de 450 ticks sur chaque temps jusqu'au tick 7 680 ; volume statique 1,0 (0 dB), panoramique 0. Selon la course, une
# courbe de volume (et de panoramique) et un mode d'écriture (`automationMode`) écrits dans project.json.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D599) :
#   (1) à l'arrêt, volume plat 0,25 et panoramique plat −0,5 : la tranche lit −12,0 dB et −0,50, la ligne 0,250 et −0,50 ;
#   (2) en lecture, une rampe de 0,5 (tick 0) à 0,0625 (tick 3 840) : deux relevés, chacun à moins de 0,5 dB de la courbe à
#       son tick, et d'au moins 1 dB l'un de l'autre (la rampe d'abord écrite jusqu'au tick 7 680 donnait moins d'1 dB
#       entre les deux instants relevés : raidie AVANT la mesure, en écrivant ce banc) ;
#   (3) Touch sur le plat à 0,25 : la main posée à 0 dB → joué 1,000 ; lâchée → joué 0,250, le fader revenu à −12,0 dB ;
#       la courbe enregistrée à 1,0 dans la plage tenue, 0,25 hors d'elle ;
#   (4) sans W : joué 1,000 tant qu'on tient, 0,250 après ; la courbe enregistrée IDENTIQUE ; la ligne d'état le dit ;
#   (5) Trim sur un palier (0,5 jusqu'au tick 1 440, puis 0,25) : la main prise vers le tick 670, posée à 0 dB (+6,02 dB
#       sur la référence de −6,02 dB) → joué 0,500 après le tick 1 440 ; la courbe enregistrée : 1,0 avant 1 440, 0,5 de
#       1 440 au lâcher (le palier GARDÉ), 0,25 après ;
#   (6) quatre clics sur le W : « W touch », « W latch », « W trim », « W » ; en anglais, l'infobulle nomme Trim.
# ET CE QUI S'ENTEND, AU VUMÈTRE, contre le témoin (le binaire de D598, `build/temoin-D598`, s'il existe) : la photo est
# prise PENDANT que la main tient, et le vumètre de la piste (après le fader) lit +12 dB sur le témoin en Touch et sans
# W, +6 dB en Trim. Le témoin ne connaît ni `relever-automation` ni `relacher:` : ses relevés sont « inconnus », c'est
# son vumètre et sa courbe enregistrée qui témoignent.
#
#   tools/automation-main.sh [binaire de l'application] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D598/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-main.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = []
for debut in range(0, 7680, 480):
    evs.append((debut, 1, bytes([0x90, 48, 100])))
    evs.append((debut + 450, 0, bytes([0x80, 48, 0])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets; t = tick
corps += b"\x00\xff\x2f\x00"
def point(tick, valeur, palier=False):
    p = {"tick": tick, "value": valeur}
    if palier: p["step"] = True
    return p
plat = [{"parameter": "mix.volume", "points": [point(0, 0.25), point(7680, 0.25)]}]
jeux = {
    "repos": ("", plat + [{"parameter": "mix.pan", "points": [point(0, -0.5), point(7680, -0.5)]}]),
    "rampe": ("", [{"parameter": "mix.volume", "points": [point(0, 0.5), point(3840, 0.0625)]}]),
    "touch": ("touch", plat),
    "sansw": ("", plat),
    "trim": ("trim", [{"parameter": "mix.volume", "points": [point(0, 0.5, True), point(1440, 0.25), point(7680, 0.25)]}]),
    "w": ("", []),
}
for nom, (mode, courbes) in jeux.items():
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
             "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
             "instrument": {"preferredPlugin": "vsm.minimoog"}}
    if mode: piste["automationMode"] = mode
    if courbes: piste["automation"] = courbes
    projet = {"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
              "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                            "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
              "tracks": [piste]}
    json.dump(projet, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = dossier du projet ; le reste : variables
    local bin="$1" nom="$2" projet="$3" n
    shift 3
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$bin" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure (le témoin ne connaît pas les relevés
    # neufs : ses avertissements sont attendus, et comptés à part).
    local motif="VSM_(GESTE_APRES|GESTE|APPUI|RELACHE|VALEUR|TOUCHE|BOITE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
cp_projet() { rm -rf "$brouillon/$2"; cp -r "$brouillon/$1" "$brouillon/$2"; }

# Les relevés, rendus en champs : fader, courbe du fader, pan, courbe du pan, ligne volume, ligne pan, main, joué, tête.
champs() {   # $1 = fichier ; $2 = rang du relevé (1 = le premier)
    python3 - "$1" "$2" <<'PY'
import re, sys
lignes = [l for l in open(sys.argv[1], encoding="utf-8", errors="replace") if l.startswith("VSM_AUTOMATION_TRANCHE : piste 0 ")]
k = int(sys.argv[2])
if len(lignes) < k: print("ABSENT"); sys.exit(0)
l = lignes[k - 1]
def f(motif):
    m = re.search(motif, l)
    return m.group(1) if m else "?"
print(" ".join([f(r"t\S+te (-?\d+)"), f(r"fader (-?[\d.]+) dB"), f(r"fader [^(]*\(courbe (-?[\d.]+|-)"),
                f(r"pan (-?[\d.]+) \("), f(r"pan [^(]*\(courbe (-?[\d.]+|-)"), f(r"ligne (-?[\d.]+) /"),
                f(r"ligne [\d.-]+ / (-?[\d.]+)"), f(r"main (\S+)"), f(r"jou\S+ (-?[\d.]+) /"),
                f(r"(W(?: \w+)?) ; t")]))
PY
}
efficace() { grep -m1 "^VSM_VUMETRE : tranche 0 " "$1" | sed -n 's/.*efficace \(-\?[0-9.inf-]*\) dBFS.*/\1/p'; }
courbe() {   # la courbe de volume enregistrée : « tick:valeur[:p] » séparés par des espaces
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
valeur_a() {   # la valeur de la courbe enregistrée au tick $2 (interpolée, palier compris)
    python3 -c "
import json, sys
p = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
t = int(sys.argv[2])
pts = next(c['points'] for c in p['tracks'][0]['automation'] if c['parameter'] == 'mix.volume')
if t <= pts[0]['tick']: print('%.4f' % pts[0]['value']); sys.exit(0)
if t >= pts[-1]['tick']: print('%.4f' % pts[-1]['value']); sys.exit(0)
for a, b in zip(pts, pts[1:]):
    if a['tick'] <= t <= b['tick']:
        if a.get('step') or b['tick'] == a['tick']: print('%.4f' % a['value'])
        else: print('%.4f' % (a['value'] + (t - a['tick']) / (b['tick'] - a['tick']) * (b['value'] - a['value'])))
        break" "$1" "$2" 2>/dev/null || echo "?"
}
proche() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D599 : le fader qui suit sa courbe, la main qui l'emporte, le mode Trim ==="
TENIR="700:appuyer:mixeur.volume;800:valeur:mixeur.volume=0"
# (1) à l'arrêt
course "$BIN" repos "$brouillon/repos" VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-automation"
# (2) en lecture
course "$BIN" rampe "$brouillon/rampe" VSM_LECTURE=1 VSM_DELAI=3000 VSM_GESTE_APRES="1000:relever-automation;2500:relever-automation"
# (3) Touch : la photo pendant la prise (a), puis la prise entière (b)
cp_projet touch touch-a; cp_projet touch touch-b
course "$BIN" touch-a "$brouillon/touch-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR;1500:relever-automation"
course "$BIN" touch-b "$brouillon/touch-b" VSM_LECTURE=1 VSM_DELAI=3000 \
    VSM_GESTE_APRES="$TENIR;1300:relever-automation;1600:relacher:mixeur.volume;1900:relever-automation;2100:touche:spacebar;2400:enregistrer:$brouillon/touch-b"
# (4) sans W
cp_projet sansw sansw-a; cp_projet sansw sansw-b
course "$BIN" sansw-a "$brouillon/sansw-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR;1500:relever-automation"
course "$BIN" sansw-b "$brouillon/sansw-b" VSM_LECTURE=1 VSM_DELAI=3000 \
    VSM_GESTE_APRES="$TENIR;1300:relever-etat;1600:relacher:mixeur.volume;1900:relever-automation;2100:touche:spacebar;2400:enregistrer:$brouillon/sansw-b"
# (5) Trim
cp_projet trim trim-a; cp_projet trim trim-b
course "$BIN" trim-a "$brouillon/trim-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2300 VSM_GESTE_APRES="$TENIR;2000:relever-automation"
course "$BIN" trim-b "$brouillon/trim-b" VSM_LECTURE=1 VSM_DELAI=3200 \
    VSM_GESTE_APRES="$TENIR;2000:relever-automation;2300:relacher:mixeur.volume;2500:touche:spacebar;2800:enregistrer:$brouillon/trim-b"
# (6) le W, quatre clics ; en anglais, l'infobulle
course "$BIN" w "$brouillon/w" VSM_DELAI=2200 \
    VSM_GESTE_APRES="700:cliquer:mixeur.W;800:relever-automation;1000:cliquer:mixeur.W;1100:relever-automation;1300:cliquer:mixeur.W;1400:relever-automation;1600:cliquer:mixeur.W;1700:relever-automation"
course "$BIN" w-en "$brouillon/w" VSM_LANGUE=en VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-automation"

r1="$(champs "$brouillon/repos.txt" 1)"; echo "       (1) repos : [$r1]"
r2a="$(champs "$brouillon/rampe.txt" 1)"; r2b="$(champs "$brouillon/rampe.txt" 2)"; echo "       (2) rampe : [$r2a] puis [$r2b]"
r3p="$(champs "$brouillon/touch-b.txt" 1)"; r3l="$(champs "$brouillon/touch-b.txt" 2)"
echo "       (3) Touch : pendant [$r3p] ; lâché [$r3l] ; courbe [$(courbe "$brouillon/touch-b")]"
r4p="$(champs "$brouillon/sansw-a.txt" 1)"; r4l="$(champs "$brouillon/sansw-b.txt" 1)"
etat4="$(grep -m1 "^VSM_ETAT_PIANOROLL" "$brouillon/sansw-b.txt")"
echo "       (4) sans W : pendant [$r4p] ; lâché [$r4l] ; courbe [$(courbe "$brouillon/sansw-b")] ; $etat4"
r5p="$(champs "$brouillon/trim-b.txt" 1)"; echo "       (5) Trim : pendant [$r5p] ; courbe [$(courbe "$brouillon/trim-b")]"
modes="$(grep "^VSM_AUTOMATION_TRANCHE : piste 0 " "$brouillon/w.txt" | sed -n 's/.* : \(W[a-z ]*\) ; t.*/\1/p' | tr '\n' '|')"
echo "       (6) le W : $modes"
en="$(grep -c "^VSM_AUTOMATION_W : .*Three: Trim" "$brouillon/w-en.txt")"

set -- $r1
verdict "(1) à l'arrêt : le fader à −12,0 dB, le panoramique à −0,50 (la courbe), la ligne 0,250 / −0,50" \
    "$(proche "${2:-x}" -12.04 0.05 && proche "${4:-x}" -0.50 0.005 && proche "${6:-x}" 0.25 0.002 && proche "${7:-x}" -0.50 0.005 && echo 1 || echo 0)"
set -- $r2a; fa="${2:-x}"; ca="${3:-x}"; set -- $r2b; fb="${2:-x}"; cb="${3:-x}"
verdict "(2) en lecture : le fader à moins de 0,5 dB de la courbe, deux fois, et d'au moins 1 dB entre les deux" \
    "$(proche "$fa" "$ca" 0.5 && proche "$fb" "$cb" 0.5 && ! proche "$fa" "$fb" 1.0 && echo 1 || echo 0)"
set -- $r3p; j3p="${9:-x}"; set -- $r3l; j3l="${9:-x}"; f3l="${2:-x}"
verdict "(3) Touch : joué 1,000 pendant la prise, 0,250 après, le fader revenu à −12,0 dB" \
    "$(proche "$j3p" 1.0 0.001 && proche "$j3l" 0.25 0.001 && proche "$f3l" -12.04 0.1 && echo 1 || echo 0)"
t3="$(python3 -c "
import re, sys
t = open(sys.argv[1], encoding='utf-8', errors='replace').read()
a = re.findall(r'VSM_AUTOMATION_TRANCHE : piste 0 [^\n]*t\S+te (\d+)', t)
print(a[0] if a else -1)" "$brouillon/touch-b.txt")"
verdict "(3) Touch : la courbe enregistrée à 1,0 dans la prise (au tick du relevé pendant), 0,25 avant et après" \
    "$([ "$t3" -gt 0 ] && proche "$(valeur_a "$brouillon/touch-b" "$t3")" 1.0 0.001 \
        && proche "$(valeur_a "$brouillon/touch-b" 100)" 0.25 0.001 && proche "$(valeur_a "$brouillon/touch-b" 7000)" 0.25 0.001 \
        && echo 1 || echo 0)"
set -- $r4p; j4p="${9:-x}"; set -- $r4l; j4l="${9:-x}"
verdict "(4) sans W : joué 1,000 tant qu'on tient, 0,250 après" "$(proche "$j4p" 1.0 0.001 && proche "$j4l" 0.25 0.001 && echo 1 || echo 0)"
verdict "(4) sans W : la courbe enregistrée identique (deux points à 0,25), et la ligne d'état le dit" \
    "$([ "$(courbe "$brouillon/sansw-b")" = "0:0.2500 7680:0.2500" ] && echo "$etat4" | grep -q "suivi de sa courbe" && echo 1 || echo 0)"
set -- $r5p; j5p="${9:-x}"; t5p="${1:-0}"
verdict "(5) Trim : joué 0,500 après le tick 1 440 (la courbe × 2)" "$([ "$t5p" -gt 1440 ] 2>/dev/null && proche "$j5p" 0.5 0.002 && echo 1 || echo 0)"
verdict "(5) Trim : la courbe enregistrée — 1,0 avant 1 440, 0,5 après (le palier gardé), 0,25 au-delà, 0,5 avant l'appui" \
    "$(proche "$(valeur_a "$brouillon/trim-b" 1000)" 1.0 0.002 && proche "$(valeur_a "$brouillon/trim-b" 1439)" 1.0 0.002 \
        && proche "$(valeur_a "$brouillon/trim-b" 1500)" 0.5 0.002 && proche "$(valeur_a "$brouillon/trim-b" 7000)" 0.25 0.001 \
        && proche "$(valeur_a "$brouillon/trim-b" 100)" 0.5 0.001 && echo 1 || echo 0)"
verdict "(6) le W : W touch, W latch, W trim, W" "$([ "$modes" = "W touch|W latch|W trim|W|" ] && echo 1 || echo 0)"
verdict "(6) en anglais, l'infobulle nomme Trim" "$([ "$en" = 1 ] && echo 1 || echo 0)"

# CE QUI S'ENTEND : le vumètre de la piste, pendant que la main tient, contre le témoin.
if [ -x "$TEMOIN" ]; then
    for x in touch sansw trim; do cp_projet "$x" "t-$x-a"; done
    course "$TEMOIN" t-touch-a "$brouillon/t-touch-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR"
    course "$TEMOIN" t-sansw-a "$brouillon/t-sansw-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2000 VSM_GESTE_APRES="$TENIR"
    course "$TEMOIN" t-trim-a "$brouillon/t-trim-a" VSM_LECTURE=1 VSM_VUMETRES=1 VSM_DELAI=2300 VSM_GESTE_APRES="$TENIR"
    for x in touch:12 sansw:12 trim:6; do
        n="${x%%:*}"; attendu="${x##*:}"
        neuf="$(efficace "$brouillon/$n-a.txt")"; vieux="$(efficace "$brouillon/t-$n-a.txt")"
        ecart="$(python3 -c "import sys; print('%.2f' % (float(sys.argv[1]) - float(sys.argv[2])))" "$neuf" "$vieux" 2>/dev/null || echo "?")"
        echo "       vumètre efficace, $n : $neuf dBFS (neuf) contre $vieux (témoin) → $ecart dB"
        verdict "(entendu) $n : le vumètre $attendu dB au-dessus du témoin pendant que la main tient (à 1,5 dB près)" \
            "$(proche "$ecart" "$attendu" 1.5 && echo 1 || echo 0)"
    done
else
    echo "       témoin absent ($TEMOIN) : la comparaison des vumètres n'est PAS jouée"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-MAIN : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-MAIN : le fader suit sa courbe, la main s'entend sur elle, et Trim la décale en gardant sa forme"
