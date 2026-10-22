#!/usr/bin/env bash
# D573.1 — LA LARGEUR STÉRÉO DES TRANCHES CHOISIES (Mixage ▸ Largeur stéréo des tranches choisies ▸ Largeur N % ; le
# panoramique combiné de Cubase), MESURÉE PAR L'EXPORT (D332) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Un fichier de deux secondes : un la à 440 Hz à GAUCHE, le silence à droite ; importé sur une piste
# audio, au centre, sous la loi par défaut (−3 dB). Le projet vierge s'ouvre avec une piste MIDI vide (rang 0) : la piste
# importée est au rang 1, et c'est elle qu'on choisit (`piste:1`) — choisir le rang 0 réglait la piste vide, sans un mot
# de faux au journal (« 1 tranche(s) ») et un export inchangé : la première course de ce banc l'a payé.
#   (1) le TÉMOIN : l'export d'origine sonne à gauche et se tait à droite (sous −100 dBFS), et le projet n'écrit pas de
#       largeur — sans quoi « la droite remonte » ne prouverait rien ;
#   (2) chaque entrée écrit sa largeur au projet (`mix.width` 0,75 / 0,5 / 0,25 / 0) ;
#   (3) L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D573) : sous la crête de la gauche du témoin, la gauche à −1,16 /
#       −2,50 / −4,08 / −6,02 dB et la droite à −18,06 / −12,04 / −8,52 / −6,02 dB pour 75, 50, 25 et 0 %, à 0,05 dB
#       près ; à 0 %, les deux canaux égaux au dix-millième de dB ;
#   (4) le menu coche la largeur posée (« Largeur 50 % » cochée après le geste, « 100 % » avant) ; rouverte, la tranche
#       le dit (`VSM_MIXEUR` : « largeur 50 % ») ;
#   (5) un pas : Ctrl+Z rend l'image entière, après un geste qui a eu lieu.
#
#   tools/largeur-stereo.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-largeur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.5 * math.sin(2 * math.pi * 440 * n / sr) for n in range(2 * sr)]
with wave.open(f"{b}/gauche.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), 0) for v in la))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/vierge/midi", exist_ok=True)
open(f"{b}/vierge/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                    + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/vierge/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT|IMPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
largeur() {   # la largeur écrite de la première piste audio, « (aucune) », ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
pistes = [t for t in d['tracks'] if t.get('audio')]
if not pistes: print('ABSENT'); sys.exit(0)
w = pistes[0].get('mix', {}).get('width')
print('(aucune)' if w is None else f'{w:g}')" "$1"
}
cretes() {   # « gauche droite » en dBFS (« -inf » pour un canal muet), ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if x.size == 0 or x.shape[1] < 2:
    print("ABSENT"); sys.exit(0)
def db(c):
    c = float(np.max(np.abs(c)))
    return "-inf" if c <= 1e-12 else f"{20 * np.log10(c):.4f}"
print(db(x[:, 0]), db(x[:, 1]))
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="menu:Mixage > Largeur stéréo des tranches choisies"

echo "=== D573.1 : la largeur stéréo des tranches choisies ==="
course import "$brouillon/vierge" VSM_DELAI=2500 VSM_VUE="sans-rapport" VSM_IMPORT_AUDIO="$brouillon/gauche.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/vierge"
for k in 75 50 25 0 annule; do cp -r "$brouillon/vierge" "$brouillon/w$k"; done
declare -A LIBELLE=([75]="Largeur 75 %" [50]="Largeur 50 %" [25]="Largeur 25 %" [0]="Largeur 0 % (mono)")
for k in 75 50 25 0; do
    course "menu-$k" "$brouillon/w$k" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:1" \
        VSM_GESTE_APRES="900:lister-menus;1200:$MENU > ${LIBELLE[$k]};1500:lister-menus;1900:enregistrer:$brouillon/w$k"
done
course annuler "$brouillon/wannule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:1" \
    VSM_GESTE_APRES="1200:$MENU > ${LIBELLE[50]};1600:touche:ctrl + Z;2100:enregistrer:$brouillon/wannule"
course export-temoin "$brouillon/vierge" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
for k in 75 50 25 0; do
    course "export-$k" "$brouillon/w$k" VSM_DELAI=1500 VSM_MIXEUR=1 VSM_EXPORT="$brouillon/w$k.wav"
done

lt="$(largeur "$brouillon/vierge")"; la="$(largeur "$brouillon/wannule")"
declare -A L C
for k in 75 50 25 0; do L[$k]="$(largeur "$brouillon/w$k")"; C[$k]="$(cretes "$brouillon/w$k.wav")"; done
ct="$(cretes "$brouillon/temoin.wav")"
echo "       largeurs écrites : témoin [$lt] ; 75 [${L[75]}] ; 50 [${L[50]}] ; 25 [${L[25]}] ; 0 [${L[0]}] ; Ctrl+Z [$la]"
echo "       crêtes (g d, dBFS) : témoin [$ct] ; 75 [${C[75]}] ; 50 [${C[50]}] ; 25 [${C[25]}] ; 0 [${C[0]}]"
grep -h "^VSM_CONSOLE : largeur" "$brouillon"/menu-*.txt "$brouillon/annuler.txt" | sort | uniq -c | sed 's/^/       /'
mapfile -t coches < <(grep -h "^VSM_MENU_LISTE : Mixage > Largeur stéréo des tranches choisies > Largeur" "$brouillon/menu-50.txt" \
    | grep -E "\[(cochée|coché)\]" | sed 's/^VSM_MENU_LISTE : Mixage > Largeur stéréo des tranches choisies > //')
for x in "${coches[@]}"; do echo "       cochée : $x"; done
tranche="$(grep -h "^VSM_MIXEUR : piste 1 " "$brouillon/export-50.txt" | head -1)"
ecarts="$(python3 -c "
import sys
t = sys.argv[1].split()
try:
    ref = float(t[0])
except (ValueError, IndexError):
    print('ABSENT'); sys.exit(0)
out = []
for c in sys.argv[2:]:
    g, d = c.split() if c != 'ABSENT' else ('nan', 'nan')
    out.append(f'{float(g) - ref:.3f}/{float(d) - ref:.3f}')
print(' '.join(out))" "$ct" "${C[75]}" "${C[50]}" "${C[25]}" "${C[0]}")"
echo "       sous la gauche du témoin (g/d, dB) pour 75 50 25 0 % : [$ecarts]"

verdict "(1) le témoin : la gauche sonne, la droite se tait, aucune largeur écrite" \
    "$(awk -v c="$ct" -v l="$lt" 'BEGIN { if (c == "ABSENT") { print 0; exit } split(c, C, " ")
        print (l == "(aucune)" && C[1] != "-inf" && C[1] + 0 > -20 && (C[2] == "-inf" || C[2] + 0 < -100)) ? 1 : 0 }')"
verdict "(2) chaque entrée écrit sa largeur (0,75 / 0,5 / 0,25 / 0)" \
    "$([ "${L[75]}" = 0.75 ] && [ "${L[50]}" = 0.5 ] && [ "${L[25]}" = 0.25 ] && [ "${L[0]}" = 0 ] && echo 1 || echo 0)"
verdict "(3) l'attendu écrit avant : −1,16/−18,06, −2,50/−12,04, −4,08/−8,52, −6,02/−6,02 dB à 0,05 dB ; à 0 % les canaux égaux" \
    "$(python3 -c "
import sys
e = sys.argv[1].split()
att = [(-1.1598, -18.0618), (-2.4988, -12.0412), (-4.0824, -8.5194), (-6.0206, -6.0206)]
try:
    ok = len(e) == 4 and all(abs(float(x.split('/')[0]) - a) < 0.05 and abs(float(x.split('/')[1]) - b) < 0.05
                             for x, (a, b) in zip(e, att))
    g, d = e[3].split('/')
    ok = ok and abs(float(g) - float(d)) < 1e-4 + 1e-9
except ValueError:
    ok = False
print(1 if ok else 0)" "$ecarts")"
verdict "(4) le menu coche « Largeur 50 % » après le geste (et « 100 % » avant) ; rouverte, la tranche dit « largeur 50 % »" \
    "$([ "${#coches[@]}" = 2 ] && echo "${coches[0]}" | grep -q "^Largeur 100 % (image entière)" \
       && echo "${coches[1]}" | grep -q "^Largeur 50 %" && echo "$tranche" | grep -q ", largeur 50 %" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend l'image entière, après un geste qui a eu lieu" \
    "$([ "$la" = "(aucune)" ] && grep -q "^VSM_CONSOLE : largeur stéréo 50 % sur 1 tranche(s)" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LARGEUR-STEREO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LARGEUR-STEREO : l'image d'une piste se resserre vers son centre avant le panoramique, à l'export comme au projet"
