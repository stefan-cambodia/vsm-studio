#!/usr/bin/env bash
# D567.1 — LES FONDUS AUX LOCATEURS (clic droit sur un clip ▸ Fondus aux locateurs ; l'*Adjust Fades to Range* de
# Cubase), MESURÉ PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Une piste MIDI, trois clips : A [0, 1 920), B [1 920, 3 840), C [3 840, 5 760) ; 120 BPM (960
# ticks = 1 s) ; les trois choisis.
#   (1) les locateurs à [1 440, 2 400) : la plage couvre la FIN de A — son fondu de sortie, 0,5 s — et le DÉBUT de B —
#       son fondu d'entrée, 0,5 s — ; C, hors de la plage, ne bouge pas, et le journal le compte ;
#   (2) à [2 400, 3 360), DANS B : B sonne plein entre les deux — fondu d'entrée 0,5 s, de sortie 0,5 s — ; A et C rien ;
#   (3) à [1 920, 3 840), B couvert EN ENTIER : rien ne change, et la boîte le dit ;
#   (4) un pas : Ctrl+Z rend des clips sans fondu, après un geste qui a eu lieu.
#
#   tools/fondus-locateurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fondus.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for i in range(12):   # douze noires, de 0 à 5 760
    corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x83, 0x60]) + bytes([0x80, 60, 0])
corps += b"\x00\xff\x2f\x00"
for nom, (de, a) in {"bords": (1440, 2400), "milieu": (2400, 3360), "couvert": (1920, 3840)}.items():
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    clips = [{"start": s, "length": 1920, "sourceStart": s, "sourceLength": 1920, "name": n}
             for n, s in (("A", 0), ("B", 1920), ("C", 3840))]
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"loop": {"enabled": True, "startTick": de, "endTick": a},
                             "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                           "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0},
                           "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": clips}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fondus() {   # « nom:entrée/sortie » de chaque clip, en secondes, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{c.get('name', '?')}:{c.get('fadeIn', 0):g}/{c.get('fadeOut', 0):g}\" for c in d['tracks'][0].get('clips', [])))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="clip-midi-tous:Fondus aux locateurs"

echo "=== D567.1 : les fondus aux locateurs ==="
cp -r "$brouillon/bords" "$brouillon/annule"
for x in bords milieu couvert; do
    course "$x" "$brouillon/$x" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" VSM_MENU_CONTEXTE="$GESTE" \
        VSM_GESTE_APRES="1500:enregistrer:$brouillon/$x"
done
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" VSM_MENU_CONTEXTE="$GESTE" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/annule"

fb="$(fondus "$brouillon/bords")"; fm="$(fondus "$brouillon/milieu")"; fc="$(fondus "$brouillon/couvert")"; fz="$(fondus "$brouillon/annule")"
grep -h "^VSM_FONDUS" "$brouillon/bords.txt" "$brouillon/milieu.txt" "$brouillon/couvert.txt" | sed 's/^/       /'
echo "       bords [$fb] ; milieu [$fm] ; couvert [$fc] ; Ctrl+Z [$fz]"

verdict "(1) la plage au bord : la fin de A, le début de B ; C hors de la plage, compté" \
    "$([ "$fb" = "A:0/0.5 B:0.5/0 C:0/0" ] && grep -q "^VSM_FONDUS : 2 clip(s) aux locateurs, 1 hors de la plage, 0 couvert" "$brouillon/bords.txt" && echo 1 || echo 0)"
verdict "(2) la plage dans B : B sonne plein entre ses deux fondus ; A et C rien" \
    "$([ "$fm" = "A:0/0 B:0.5/0.5 C:0/0" ] && echo 1 || echo 0)"
verdict "(3) B couvert en entier : rien, et la boîte le dit" \
    "$([ "$fc" = "A:0/0 B:0/0 C:0/0" ] && grep -q "^VSM_BOITE.*Aucun fondu réglé : 2 clip(s) hors de la plage, 1 qu'elle couvre en entier" "$brouillon/couvert.txt" && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les clips sans fondu, après un geste qui a eu lieu" \
    "$([ "$fz" = "A:0/0 B:0/0 C:0/0" ] && grep -q "^VSM_FONDUS : 2 clip(s) aux locateurs" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FONDUS-LOCATEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FONDUS-LOCATEURS : au bord du clip la plage est le fondu, au milieu elle sonne plein"
