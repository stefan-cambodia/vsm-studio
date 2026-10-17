#!/usr/bin/env bash
# H23 / D561.4 — LES GESTES D'ANALYSE D'UN CLIP AUDIO ENTENDENT TOUT LE FICHIER, MÊME AU-DELÀ DE VINGT SECONDES.
#
# CE QUI ÉTAIT EN CAUSE. Les gestes qui lisent la fenêtre d'un clip relisaient le fichier par la politique `Automatic`,
# qui DIFFUSE tout fichier de plus de 20 s (`kStreamAboveSeconds`) en mode `Realtime` — et ce magasin-là n'a aucun fil
# qui le remplisse : ce qui n'est pas en cache se lit comme du silence.
#
# LES RÈGLES GARDÉES. Deux fichiers de clics, un par seconde à partir de 0,5 s : 30 s (diffusé) et 10 s (résident).
#   (1) « Poser des repères aux attaques » (par `chargerFenetreDuClip`, le chemin des attaques, du tempo, de la
#       quantification de l'audio) : 30 repères sur le fichier de 30 s ;
#   (2) le témoin résident : 10 repères sur celui de 10 s ;
#   (3) « Découper aux transitoires » (son propre chargement) : le clip de 30 s coupé à chaque clic — 30 coupes, donc
#       31 morceaux, le premier avant le premier clic (la première écriture de ce banc attendait 30 morceaux) ;
#   (4) « Rogner au son » (son propre chargement) : le clip de 30 s commence au premier clic (0,5 s) et finit après le
#       dernier (29,5 s) — et non « aucun son ».
#
#   tools/analyse-fichier-long.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

# LES FICHIERS PÈSENT 5 Mo ET 2 Mo : le brouillon de session est un tmpfs, mais c'est peu (la règle des « quelques
# dizaines de mégaoctets »).
brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fichier-long.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
for nom, secondes in (("long", 30), ("court", 10)):
    e = [0.0] * (secondes * sr)
    for k in range(secondes):
        debut = round((0.5 + k) * sr)
        for n in range(int(0.010 * sr)):
            e[debut + n] += 0.8 * math.sin(2 * math.pi * 3000 * n / sr) * math.exp(-n / (0.0012 * sr))
    with wave.open(f"{b}/{nom}.wav", "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in e))
    vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                       + b"MTrk" + struct.pack(">I", len(vide)) + vide)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": []}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
lire() {   # $1 = dossier ; « repères clips début_s fin_s » de la piste audio, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
clips = sorted((c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])), key=lambda c: c['start'])
if not clips:
    print('ABSENT'); sys.exit(0)
s = lambda t: t / 960.0   # 120 BPM, 480 ticks par noire : 960 ticks par seconde
# « sourceStart » s'écrit en TICKS (la première écriture de ce banc le lisait en secondes, et voyait 475 s).
print(len(d.get('markers', [])), len(clips), f\"{s(clips[0].get('sourceStart', 0)):.2f}\", f\"{s(clips[-1]['start'] + clips[-1]['length']):.2f}\")" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== H23 / D561.4 : les gestes d'analyse entendent tout le fichier ==="
for nom in long court; do
    course "import-$nom" "$brouillon/$nom" VSM_DELAI=3500 VSM_IMPORT_AUDIO="$brouillon/$nom.wav" \
        VSM_GESTE_APRES="2500:enregistrer:$brouillon/$nom"
done
for x in long-r long-t long-g; do cp -r "$brouillon/long" "$brouillon/$x"; done
cp -r "$brouillon/court" "$brouillon/court-r"
course reperes-long "$brouillon/long-r" VSM_DELAI=4000 VSM_MENU_CONTEXTE="clip-audio:Poser des repères aux attaques" \
    VSM_GESTE_APRES="3000:enregistrer:$brouillon/long-r"
course reperes-court "$brouillon/court-r" VSM_DELAI=3000 VSM_MENU_CONTEXTE="clip-audio:Poser des repères aux attaques" \
    VSM_GESTE_APRES="2000:enregistrer:$brouillon/court-r"
course transitoires "$brouillon/long-t" VSM_DELAI=4000 \
    VSM_MENU_CONTEXTE="clip-audio-tous:Découper aux transitoires (clips audio choisis)" VSM_GESTE_APRES="3000:enregistrer:$brouillon/long-t"
course rogner "$brouillon/long-g" VSM_DELAI=4000 VSM_MENU_CONTEXTE="clip-audio:Rogner au son (détecter le silence)" \
    VSM_GESTE_APRES="3000:enregistrer:$brouillon/long-g"

avant="$(lire "$brouillon/long")"; rl="$(lire "$brouillon/long-r")"; rc="$(lire "$brouillon/court-r")"
tl="$(lire "$brouillon/long-t")"; gl="$(lire "$brouillon/long-g")"
for x in reperes-long reperes-court transitoires rogner; do
    grep -h "^VSM_REPERES_ATTAQUES\|^VSM_BOITE" "$brouillon/$x.txt" | head -2 | sed "s/^/       ($x) /"
done
echo "       (repères clips début_s fin_s) importé 30 s : [$avant]"
echo "       repères, 30 s : [$rl] ; 10 s : [$rc] ; transitoires, 30 s : [$tl] ; rogné, 30 s : [$gl]"

champ() { echo "$1" | cut -d' ' -f"$2"; }
verdict "(1) 30 repères sur le fichier de 30 s" "$([ "$(champ "$rl" 1)" = 30 ] && echo 1 || echo 0)"
verdict "(2) le témoin résident : 10 repères sur 10 s" "$([ "$(champ "$rc" 1)" = 10 ] && echo 1 || echo 0)"
verdict "(3) découpé aux transitoires : 30 coupes, 31 morceaux" \
    "$([ "$(champ "$tl" 2)" = 31 ] && grep -q "^VSM_BOITE : Découper aux transitoires : 30 coupe(s)" "$brouillon/transitoires.txt" && echo 1 || echo 0)"
verdict "(4) rogné au son : de 0,5 s environ à plus de 29,5 s" \
    "$(python3 -c "
import sys
try:
    _, _, d, f = sys.argv[1].split(); sys.exit(0 if 0.3 <= float(d) <= 0.55 and float(f) >= 29.5 else 1)
except ValueError:
    sys.exit(1)" "$gl" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ANALYSE-FICHIER-LONG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ANALYSE-FICHIER-LONG : les gestes d'analyse entendent un fichier long d'un bout à l'autre"
