#!/usr/bin/env bash
# D544.1 — DES NOTES MIDI DEPUIS LES ATTAQUES D'UN CLIP AUDIO, MESURÉES PAR LE .MID EXPORTÉ.
#
# LA RÈGLE GARDÉE. Un fichier de quatre clics à 0,26 · 0,49 · 0,77 · 1,01 s, de crêtes 0,8 · 0,4 · 0,2 ·
# 0,8 (0, −6, −12, 0 dB), importé sur une piste audio (un clip). 120 BPM, 480 ticks par noire.
#   (1) « Créer des notes depuis les attaques… » (hauteur 36, durée 1/16, vélocité selon le niveau) : le
#       journal dit 4 notes de hauteur 36, 0 écartée ; le projet relu a UNE piste de plus, MIDI, juste APRÈS
#       la piste audio, nommée « … (attaques) » (le projet importé a déjà une piste MIDI vide, devant) ;
#   (2) dans le .mid EXPORTÉ, quatre notes de hauteur 36 aux ticks 250 · 470 · 739 · 970 (±1), de vélocités
#       127 · 109 · 91 · 127 (±2 : la crête est relue après le chargement), de durée 120 ;
#   (3) le même projet CALÉ d'abord par « Quantifier l'audio » (1/8) : les notes sur les lignes, 240 · 480 ·
#       720 · 960 — la carte d'étirement est lue, pas seulement le tempo ;
#   (4) un pas : Ctrl+Z après le geste → plus de piste MIDI au projet relu ;
#   (5) la fenêtre photographiée en français et en anglais (la photo regardée à la main).
# Chaque fichier se lit AVANT de juger : un .mid absent rend « ABSENT », jamais « faux ».
#
#   tools/notes-depuis-attaques.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-notes-attaques.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.mid "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
echantillons = [0.0] * (2 * sr)
for instant, crete in ((0.260, 0.8), (0.490, 0.4), (0.770, 0.2), (1.010, 0.8)):
    debut = round(instant * sr)
    for n in range(int(0.010 * sr)):
        echantillons[debut + n] += crete * math.sin(2 * math.pi * 3000 * n / sr) * math.exp(-n / (0.0012 * sr))
trames = b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in echantillons)
with wave.open(f"{b}/clics.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|EXPORT|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
pistes() {   # « genre:nom » de chaque piste d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    pistes = json.load(open(sys.argv[1]))['tracks']
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(f\"{p.get('kind', 'midi')}:{p.get('name', '')}\" for p in pistes) or 'AUCUNE')
" "$1"
}
notes() {   # « tick:hauteur:vélocité:durée » de chaque note d'un .mid, ticks à 480 par noire — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
if data[:4] != b"MThd":
    print("ABSENT"); sys.exit(0)
division = int.from_bytes(data[12:14], "big")
i, ouvertes, finies = 14, {}, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st, t = 0, 0, 0
    while j < len(corps):
        dt = 0
        while corps[j] & 0x80: dt = (dt << 7) | (corps[j] & 0x7F); j += 1
        dt = (dt << 7) | corps[j]; j += 1; t += dt
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        kind = st & 0xF0
        if kind == 0x90 and corps[j + 1] > 0:
            ouvertes[(st & 0x0F, corps[j])] = (t, corps[j + 1])
        elif kind in (0x80, 0x90):
            debut = ouvertes.pop((st & 0x0F, corps[j]), None)
            if debut: finies.append((debut[0], corps[j], debut[1], t - debut[0]))
        j += 1 if kind in (0xC0, 0xD0) else 2
k = 480 / division
print(" ".join(f"{round(t * k)}:{h}:{v}:{round(d * k)}" for t, h, v, d in sorted(finies)) or "AUCUNE")
PY
}
juge() {   # $1 = notes relevées ; $2 = ticks attendus ; $3 = vélocités attendues (« - » : ne pas juger)
    python3 -c "
import sys
try:
    lues = [tuple(int(x) for x in n.split(':')) for n in sys.argv[1].split()]
except ValueError:
    sys.exit(1)
ticks = [int(x) for x in sys.argv[2].split()]
vel = None if sys.argv[3] == '-' else [int(x) for x in sys.argv[3].split()]
ok = len(lues) == len(ticks) and all(h == 36 and d == 120 for _, h, _, d in lues) \
     and all(abs(t - u) <= 1 for (t, _, _, _), u in zip(lues, ticks)) \
     and (vel is None or all(abs(v - w) <= 2 for (_, _, v, _), w in zip(lues, vel)))
sys.exit(0 if ok else 1)" "$1" "$2" "$3"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D544.1 : des notes depuis les attaques d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/clics.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in notes annule cale; do cp -r "$brouillon/base" "$brouillon/$x"; done
MENU="clip-audio-tous:Créer des notes depuis les attaques…"
OPTIONS="hauteur=36;duree=2;velocite=1"
course notes "$brouillon/notes" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="$OPTIONS" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/notes.mid;2000:enregistrer:$brouillon/notes"
course caler "$brouillon/cale" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Quantifier l'audio…" \
    VSM_OPTIONS="grille=2" VSM_GESTE_APRES="1500:enregistrer:$brouillon/cale"
course notes-calees "$brouillon/cale" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="$OPTIONS" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/calees.mid"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="$OPTIONS" \
    VSM_GESTE_APRES="1500:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"

grep -h "^VSM_NOTES_ATTAQUES\|^VSM_QUANTIFIER_AUDIO" "$brouillon/notes.txt" "$brouillon/caler.txt" "$brouillon/notes-calees.txt" | sed 's/^/       /'
pb="$(pistes "$brouillon/base/project.json")"; pn="$(pistes "$brouillon/notes/project.json")"; pa="$(pistes "$brouillon/annule/project.json")"
echo "       pistes : importé [$pb] ; après le geste [$pn] ; Ctrl+Z [$pa]"
verdict "(1) quatre notes de hauteur 36, aucune écartée ; la piste neuve juste après la piste audio" \
    "$(grep -q "^VSM_NOTES_ATTAQUES : 4 note(s) de hauteur 36, 0 écartée(s)" "$brouillon/notes.txt" \
        && python3 -c "
import sys
avant, apres = sys.argv[1].split(' | '), sys.argv[2].split(' | ')
a = next((i for i, x in enumerate(apres) if x.startswith('audio:')), -1)
sys.exit(0 if len(apres) == len(avant) + 1 and 0 <= a < len(apres) - 1
         and apres[a + 1].startswith('midi:') and apres[a + 1].endswith('(attaques)') else 1)" "$pb" "$pn" \
        && echo 1 || echo 0)"
nn="$(notes "$brouillon/notes.mid")"; nc="$(notes "$brouillon/calees.mid")"
echo "       .mid (tick:hauteur:vélocité:durée) : clip tel quel [$nn] ; clip calé [$nc]"
verdict "(2) dans le .mid, les notes aux ticks 250 · 470 · 739 · 970, vélocités 127 · 109 · 91 · 127" \
    "$(juge "$nn" "250 470 739 970" "127 109 91 127" && echo 1 || echo 0)"
verdict "(3) le clip calé : les notes sur les lignes 240 · 480 · 720 · 960 (la carte est lue)" \
    "$(grep -q "^VSM_QUANTIFIER_AUDIO : grille 1/8, 4 attaque(s) à .* ; 4 calée(s)" "$brouillon/caler.txt" \
        && juge "$nc" "240 480 720 960" "-" && echo 1 || echo 0)"
verdict "(4) Ctrl+Z : plus de piste neuve" \
    "$([ "$pb" != ABSENT ] && [ "$pa" = "$pb" ] && echo 1 || echo 0)"

photos=0
for langue in fr en; do
    entree="Créer des notes depuis les attaques…"; titre="Créer des notes depuis les attaques"
    [ "$langue" = en ] && entree="Create notes from attacks…" && titre="Create notes from attacks"
    for essai in 1 2 3; do   # D72 : une boîte absente d'une photo ne prouve rien
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
            VSM_DELAI=3500 VSM_MENU_CONTEXTE="clip-audio-tous:$entree" VSM_CAPTURE_PANNEAUX=1 \
            VSM_CAPTURE="$brouillon/fenetre-$langue.png" timeout 60 "$BIN" > "$brouillon/fenetre-$langue.txt" 2>&1
        grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt" && break
        sleep 20
    done
    if grep -q "^VSM_BOITE : $titre — fenêtre modale ouverte" "$brouillon/fenetre-$langue.txt" \
        && grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt"; then
        photos=$((photos + 1))
        sed -n 's/^VSM_CAPTURE_PANNEAUX : /       photo ('"$langue"') : /p' "$brouillon/fenetre-$langue.txt"
    fi
done
verdict "(5) la fenêtre ouverte et photographiée en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NOTES-DEPUIS-ATTAQUES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NOTES-DEPUIS-ATTAQUES : une note par attaque, à sa place et à sa force, en un pas"
