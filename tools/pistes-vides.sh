#!/usr/bin/env bash
# D550.3 — RETIRER LES PISTES VIDES (Piste ▸ Retirer les pistes vides, le *Remove Empty Tracks* de Cubase), MESURÉ PAR
# LE PROJET RELU, LA BOÎTE ET L'EXPORT.
#
# LES RÈGLES GARDÉES. Projet R, six pistes : « Pleine » (des notes, un clip, routée vers le groupe « Bus »), « Vide »
# (rien), « Bus » (un groupe), « Dossier » (un dossier) dont le seul membre est « Membre vide », et « CC seuls » (un
# contrôleur, aucune note).
#   (1) le projet relu : « Pleine », « Bus », « CC seuls » — le groupe reste, le dossier part avec son membre vide,
#       une piste qui n'a QUE des contrôleurs n'est pas vide ; et « Pleine » route toujours vers « Bus », devenu 1 ;
#   (2) la boîte nomme ce qui est parti : « 3 piste(s) vide(s) retirée(s) : Vide, Dossier, Membre vide » ;
#   (3) un pas : Ctrl+Z rend les six pistes ;
#   (4) l'export ne bouge pas d'un échantillon : rien de ce qui sonnait n'est parti ;
#   (5) sans piste vide (R-plein : « Pleine » et « Bus ») : « Aucune piste vide » dit, aucun pas.
#
#   tools/pistes-vides.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pistes-vides.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def piste(nom, notes=(), cc=False, tempo=False):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if tempo else b"") + b"\x00\xff\x03" + bytes([len(nom.encode())]) + nom.encode()
    if cc:
        corps += b"\x00\xb1\x07\x64"           # un contrôleur (volume, 100) au tick 0, canal 2
    t = 0
    for debut in notes:
        corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(1440) + bytes([0x80, 60, 0])
        t = debut + 1440
    corps += b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
def entree(nom, **x):
    return dict({"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": nom, "mix": dict(mix)}, **x)
def ecrire(nom, pistes_mid, pistes_json):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes_mid), 480) + b"".join(pistes_mid))
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": pistes_json}, open(f"{b}/{nom}/project.json", "w"), indent=1)
pleine = entree("Pleine", instrument={"preferredPlugin": "vsm.minimoog"}, output=2,
                clips=[{"start": 0, "length": 3840, "sourceStart": 0, "sourceLength": 3840, "name": "A"}])
ecrire("R", [piste("Pleine", [0, 1920], tempo=True), piste("Vide"), piste("Bus"), piste("Dossier"), piste("Membre vide"),
             piste("CC seuls", cc=True)],
       [pleine, entree("Vide", instrument={"preferredPlugin": "vsm.minimoog"}), entree("Bus", kind="group"),
        entree("Dossier", kind="folder", folderDepth=0), entree("Membre vide", folderDepth=1),
        entree("CC seuls", channel=1, instrument={"preferredPlugin": "vsm.minimoog"})])
# Dans R-plein, le groupe est au rang 1 : la piste y route (la première version gardait le 2 de R, un routage
# vers rien, et le banc attendait « →1 » sur un projet qui disait « →2 »).
ecrire("R-plein", [piste("Pleine", [0, 1920], tempo=True), piste("Bus")], [dict(pleine, output=1), entree("Bus", kind="group")])
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|MENU_CONTEXTE|TOUCHE|POSITION|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
pistes() {   # « nom[→sortie] » des pistes, dans l'ordre, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' | '.join(t['name'] + (f\"→{t['output']}\" if t.get('output', -1) >= 0 else '') for t in p['tracks']) or 'AUCUNE')
" "$1"
}
ecart_max() {
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(b) == 0 or float(np.max(np.abs(a))) == 0.0:
    print("ABSENT"); sys.exit(0)
n = min(len(a), len(b))
print(f"{float(np.max(np.abs(a[:n] - b[:n]))):.3g} (crête {float(np.max(np.abs(a))):.3f}, {len(a)} et {len(b)} trames)")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
VUE="sans-rapport,arrangement"
RETIRER="menu:Retirer les pistes vides"

echo "=== D550.3 : retirer les pistes vides ==="
for x in Rr Rz; do cp -r "$brouillon/R" "$brouillon/$x"; done
course retirer "$brouillon/Rr" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$RETIRER;1700:enregistrer:$brouillon/Rr"
course annuler "$brouillon/Rz" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$RETIRER;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/Rz"
course plein "$brouillon/R-plein" VSM_DELAI=2500 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$RETIRER;1500:relever-historique;1700:enregistrer:$brouillon/R-plein"
course export-avant "$brouillon/R" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course export-apres "$brouillon/Rr" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"

r="$(pistes "$brouillon/Rr/project.json")"; z="$(pistes "$brouillon/Rz/project.json")"
p="$(pistes "$brouillon/R-plein/project.json")"; hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/plein.txt" | tail -1)"
ex="$(ecart_max "$brouillon/avant.wav" "$brouillon/apres.wav")"
echo "       retirées : [$r] ; puis Ctrl+Z [$z] ; sans piste vide [$p], ${hist:-historique non relevé}"
grep -h "^VSM_PISTES_VIDES\|^VSM_BOITE : Retirer" "$brouillon/retirer.txt" "$brouillon/plein.txt" | sed 's/^/       /'
echo "       export, écart maximal après − avant : $ex"

verdict "(1) le groupe reste, le dossier part avec son membre vide, les contrôleurs seuls restent, le routage suit" \
    "$([ "$r" = "Pleine→1 | Bus | CC seuls" ] && echo 1 || echo 0)"
verdict "(2) la boîte nomme ce qui est parti" \
    "$(grep -q "^VSM_BOITE : Retirer les pistes vides : 3 piste(s) vide(s) retirée(s) : Vide, Dossier, Membre vide" "$brouillon/retirer.txt" && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend les six pistes" \
    "$([ "$z" = "Pleine→2 | Vide | Bus | Dossier | Membre vide | CC seuls" ] && echo 1 || echo 0)"
verdict "(4) l'export ne bouge pas d'un échantillon" "$([ "${ex%% *}" = 0 ] && echo 1 || echo 0)"
verdict "(5) sans piste vide : dit, aucun pas, rien de retiré" \
    "$([ "$p" = "Pleine→1 | Bus" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Retirer les pistes vides : Aucune piste vide" "$brouillon/plein.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PISTES-VIDES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PISTES-VIDES : les pistes vides se retirent, et rien de ce qui sonne ne part"
