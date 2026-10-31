#!/usr/bin/env bash
# D617 — CHERCHER LES FICHIERS MANQUANTS DANS UN DOSSIER (le *Find Missing Files ▸ Search Folder* de Cubase), mesuré dans
# l'application : le verbe `chercher-manquants:<dossier>` et le bouton « Chercher dans un dossier... » de la réserve
# (`VSM_FICHIER` saute le sélecteur de dossier, D102) ; ce que la réserve lit ensuite, ce que le dossier contient, si la
# piste reliée SONNE, et ce que Ctrl+Z défait.
#
# LE JEU : « Voix » joue `audio/voix.wav` (présent) ; cinq pistes désignent des fichiers absents — « Basse »
# (`absent.wav`), « Nappe » (`nappe.wav`), « Chœur » (`double.wav`), « Cordes » (`perdu.wav`), « Bruit » (`bruit.wav`).
# Hors du dossier du projet, `ailleurs/` : `sous/absent.wav` (2 s, stéréo, −10 dBFS de crête), `Nappe.wav`,
# `a/double.wav`, `b/double.wav`, et `bruit.wav` (du texte).
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D617, attendu (b), Ctrl+Z CORRIGÉ AVANT LA MESURE) :
#   « 2 relié(s), 1 introuvable(s), 1 ambigu(s), 1 illisible(s) » ; la réserve : 3 manquants, `absent.wav` et `Nappe.wav`
#   « joué », copiés dans `audio/` ; le stem exporté de « Basse » au-dessus de −40 dBFS, silencieux au témoin non
#   cherché (dont « Voix » sonne) — CORRIGÉ APRÈS LE PREMIER ESSAI : le vumètre en lecture, publié, n'est plus jugé (la
#   boîte d'ouverture, invisible écran éteint, bloquait Play) ; UN pas ;
#   Ctrl+Z → 4 manquants (`nappe.wav` revenu, `Nappe.wav` gardé pour l'historique, `absent.wav` joué — la copie est à
#   sa place) ; sans manquant → « aucun fichier manquant », aucun pas ; le bouton : le même compte ; le témoin (D616) :
#   le verbe inconnu ; l'anglais.
#
#   tools/chercher-manquants.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D616/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-chercher.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
racine = sys.argv[1]
a = f"{racine}/ailleurs"
for d in (f"{a}/sous", f"{a}/a", f"{a}/b"):
    os.makedirs(d, exist_ok=True)
def mtrk(corps):
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
tempo = mtrk(b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00")
vide = mtrk(b"\x00\xff\x2f\x00")
sr = 48000
def son(chemin, secondes, canaux=1, crete=3000):
    with wave.open(chemin, "wb") as w:
        w.setnchannels(canaux); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<" + "h" * canaux, *([int(crete * math.sin(2 * math.pi * 220 * n / sr))] * canaux))
                               for n in range(int(secondes * sr))))
son(f"{a}/sous/absent.wav", 2.0, canaux=2, crete=10362)   # −10 dBFS de crête
son(f"{a}/Nappe.wav", 1.0); son(f"{a}/a/double.wav", 0.5); son(f"{a}/b/double.wav", 0.25)
open(f"{a}/bruit.wav", "w").write("ceci n'est pas un fichier audio\n")
mix = {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}
def source(nom, frames, canaux=1):
    return {"file": f"audio/{nom}", "sampleRate": sr, "frames": frames, "channels": canaux}
def piste(i, nom, fichier, frames, canaux=1):
    return {"kind": "audio", "channel": i, "color": "#FF6B9BFF", "effects": [], "name": nom, "mix": mix,
            "audio": source(fichier, frames, canaux)}
def projet(dossier, pistes):
    os.makedirs(f"{dossier}/midi", exist_ok=True); os.makedirs(f"{dossier}/audio", exist_ok=True)
    open(f"{dossier}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1 + len(pistes), 480) + tempo + vide * len(pistes))
    son(f"{dossier}/audio/voix.wav", 1.0)
    json.dump({"format": "vsm-project", "version": 1, "title": "chercher", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": pistes}, open(f"{dossier}/project.json", "w"), indent=1)
projet(f"{racine}/projet", [piste(0, "Voix", "voix.wav", sr), piste(1, "Basse", "absent.wav", 2 * sr, 2),
                            piste(2, "Nappe", "nappe.wav", sr), piste(3, "Chœur", "double.wav", sr // 2),
                            piste(4, "Cordes", "perdu.wav", sr), piste(5, "Bruit", "bruit.wav", sr)])
projet(f"{racine}/complet", [piste(0, "Voix", "voix.wav", sr)])
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Une copie du jeu (`JEU`), un HOME neuf.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/${JEU:-projet}" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport" VSM_DELAI=3400 VSM_GESTE_APRES="$gestes" VSM_VUMETRES=1 VSM_CAPTURE_PANNEAUX=1 "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# LA FENÊTRE DÉGÉNÉRÉE (D607, D613) : une course dont la réserve est relevée sous 200 × 200 est REJOUÉE une fois, et c'est DIT.
rejouees=0
course_sure() {
    course "$@"
    if grep -q "fenêtre 1[0-9][0-9]x1[0-9][0-9]$" "$brouillon/$2.txt"; then
        echo "       course « $2 » REJOUÉE : $(grep -o "fenêtre [0-9]*x[0-9]*" "$brouillon/$2.txt" | head -1) au premier essai"
        rejouees=$((rejouees + 1))
        rm -rf "$brouillon/p-$2"
        course "$@"
    fi
}
ligne() { grep "^VSM_RESERVE : $2 ; " "$brouillon/$1.txt" | tail -1; }
etat_de() { echo "$1" | awk -F' ; ' '{print $NF}'; }
compte() { grep "^VSM_RESERVE : chercher : [0-9]* reli" "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_RESERVE : chercher : //; s/ ; .*//'; }
manquants() { ligne "$1" total | sed -n 's/.* · \([0-9]*\) manquant(s) · .*/\1/p'; }
# La crête d'un WAV en dBFS (PCM 16/24/32 bits ou flottant 32) ; « -inf » pour un silence, « ABSENT » s'il manque.
crete() {
    python3 - "$1" <<'PY2'
import math, os, struct, sys
p = sys.argv[1]
if not os.path.isfile(p) or os.path.getsize(p) == 0: print("ABSENT"); sys.exit(0)
d = open(p, "rb").read(); i = 12; fmt = None; data = None
while i + 8 <= len(d):
    nom, taille = d[i:i + 4], int.from_bytes(d[i + 4:i + 8], "little")
    if nom == b"fmt ": fmt = struct.unpack("<HHIIHH", d[i + 8:i + 24]); ext = d[i + 8 + 24:i + 8 + 26]
    if nom == b"data": data = d[i + 8:i + 8 + taille]
    i += 8 + taille + (taille & 1)
if fmt is None or data is None: print("ABSENT"); sys.exit(0)
tag, bits = fmt[0], fmt[5]
if tag == 0xFFFE: tag = int.from_bytes(ext, "little")   # WAVE_FORMAT_EXTENSIBLE : le sous-format dit PCM (1) ou flottant (3)
m = 0.0
if tag == 3:
    for (v,) in struct.iter_unpack("<f", data[: len(data) // 4 * 4]): m = max(m, abs(v))
else:
    n = bits // 8; plein = float(1 << (bits - 1))
    for k in range(0, len(data) - n + 1, n):
        m = max(m, abs(int.from_bytes(data[k:k + n], "little", signed=True)) / plein)
print("-inf" if m == 0.0 else f"{20 * math.log10(m):.2f}")
PY2
}
vumetre_basse() { grep "^VSM_VUMETRE : tranche 1 : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_VUMETRE : tranche 1 : \([-0-9.inf]*\) dBFS.*/\1/p'; }
au_dessus() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v not in ('', '-inf') and float(v) > float(sys.argv[2]) else 1)" "$@" 2>/dev/null; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

F="Réserve des fichiers"
AIL="$brouillon/ailleurs"
# LA BOÎTE D'OUVERTURE D'ABORD (D608) : les fichiers absents font ouvrir le projet sur « Audio non chargé », modale.
OK="900:cliquer:fenetre:Audio non chargé:OK"
OK_EN="900:cliquer:fenetre:Audio not loaded:OK"
ATTENDU="2 relié(s), 1 introuvable(s), 1 ambigu(s), 1 illisible(s)"
echo "=== D617 : chercher les fichiers manquants dans un dossier ==="
course_sure "$BIN" nonrelie "$OK;1200:relever-reserve;1300:exporter-stems:$brouillon/stems-nonrelie;1800:cliquer:Play;3000:relever-ordre"
course_sure "$BIN" cherche "$OK;1200:chercher-manquants:$AIL;1500:relever-reserve;1600:relever-historique;1700:exporter-stems:$brouillon/stems-cherche;1800:cliquer:Play;3000:relever-ordre"
course_sure "$BIN" bouton "$OK;1200:relever-reserve;1400:cliquer:fenetre:$F:Chercher dans un dossier...;1800:relever-reserve" VSM_FICHIER="$AIL"
course_sure "$BIN" annule "$OK;1200:chercher-manquants:$AIL;1500:touche:ctrl + Z;1900:relever-reserve"
JEU=complet course_sure "$BIN" aucun "1200:chercher-manquants:$AIL;1500:relever-reserve;1600:relever-historique"
course_sure "$BIN" en "$OK_EN;1200:relever-reserve;1400:cliquer:fenetre:Pool:Search a folder...;1800:relever-reserve" \
    VSM_LANGUE=en VSM_FICHIER="$AIL"

echo "       compte (verbe)  : $(compte cherche)"
grep "^VSM_RESERVE : chercher : audio" "$brouillon/cherche.txt" | sed 's/^/       /'
LA="$(ligne cherche audio/absent.wav)"; LN="$(ligne cherche audio/Nappe.wav)"
echo "       $LA"
echo "       $LN"
echo "       $(ligne cherche total)"
SB="$(crete "$brouillon/stems-cherche/02 - Basse.wav")"; TB="$(crete "$brouillon/stems-nonrelie/02 - Basse.wav")"
TV="$(crete "$brouillon/stems-nonrelie/01 - Voix.wav")"
echo "       stem « Basse » : cherché $SB dBFS ; non cherché $TB dBFS (et « Voix » au témoin : $TV dBFS)"
echo "       vumètre « Basse », publié, non jugé : cherché $(vumetre_basse cherche) dBFS ; non cherché $(vumetre_basse nonrelie) dBFS"
echo "       transport au relevé : cherché « $(grep -o "; [a-zé ]*lecture\|; arrêté" "$brouillon/cherche.txt" | head -1) » ; non cherché « $(grep -o "; [a-zé ]*lecture\|; arrêté" "$brouillon/nonrelie.txt" | head -1) »"
verdict "(b) le compte : « $ATTENDU »" "$([ "$(compte cherche)" = "$ATTENDU" ] && echo 1 || echo 0)"
verdict "(b) avant : 5 manquants, « Chercher » actif" \
    "$([ "$(manquants nonrelie)" = 5 ] && grep -q "^VSM_RESERVE : Chercher actif$" "$brouillon/nonrelie.txt" && echo 1 || echo 0)"
verdict "(b) après : 3 manquants ; absent.wav « joué » (2.00 s stéréo, « Basse »), Nappe.wav « joué »" \
    "$([ "$(manquants cherche)" = 3 ] && [ "$(etat_de "$LA")" = "joué" ] && echo "$LA" | grep -q " ; 2.00 s ; 48 kHz stéréo ; .* ; piste « Basse »" \
       && [ "$(etat_de "$LN")" = "joué" ] && echo 1 || echo 0)"
verdict "(b) les ambigus, introuvables et illisibles restent MANQUANT : double, perdu, bruit" \
    "$([ "$(etat_de "$(ligne cherche audio/double.wav)")" = MANQUANT ] && [ "$(etat_de "$(ligne cherche audio/perdu.wav)")" = MANQUANT ] \
       && [ "$(etat_de "$(ligne cherche audio/bruit.wav)")" = MANQUANT ] && echo 1 || echo 0)"
verdict "(b) l'ambigu dit ses deux candidats, et rien n'est relié pour lui" \
    "$(grep -q "^VSM_RESERVE : chercher : audio/double.wav ambigu, 2 candidats : .*/a/double.wav, .*/b/double.wav" "$brouillon/cherche.txt" && echo 1 || echo 0)"
verdict "(b) copiés dans audio/ : absent.wav et Nappe.wav ; ni double.wav, ni bruit.wav" \
    "$([ -s "$brouillon/p-cherche/audio/absent.wav" ] && [ -s "$brouillon/p-cherche/audio/Nappe.wav" ] \
       && [ ! -e "$brouillon/p-cherche/audio/double.wav" ] && [ ! -e "$brouillon/p-cherche/audio/bruit.wav" ] && echo 1 || echo 0)"
R="$(grep "^VSM_RESERVE : remarque ; " "$brouillon/cherche.txt" | tail -1)"
echo "       $R"
verdict "(b) la réserve le dit à l'écran : la remarque nomme l'ambigu, l'introuvable et l'illisible" \
    "$(echo "$R" | grep -q "audio/double.wav (2)" && echo "$R" | grep -q "Introuvables : audio/perdu.wav" \
       && echo "$R" | grep -q "Illisibles : audio/bruit.wav" && echo 1 || echo 0)"
H="$(grep "^VSM_HISTORIQUE_PAS" "$brouillon/cherche.txt" | tail -1)"
echo "       $H"
verdict "(b) un seul pas : « Chercher les fichiers manquants »" "$(echo "$H" | grep -q "^VSM_HISTORIQUE_PAS : 1 : Chercher les fichiers manquants$" && echo 1 || echo 0)"
verdict "(b) « Basse » sonne : son stem exporté au-dessus de −40 dBFS ($SB)" "$(au_dessus "$SB" -40 && echo 1 || echo 0)"
# SILENCIEUX = SOUS −130 dBFS — CORRIGÉ APRÈS LE DEUXIÈME ESSAI : l'export en 24 bits porte le tramage TPDF de ± 1 LSB
# (D14.4), −138,47 dBFS sur une piste muette ; « −inf » n'y sort jamais.
silencieux() { python3 -c "import sys; v = sys.argv[1]; sys.exit(0 if v == '-inf' or (v != 'ABSENT' and float(v) < -130) else 1)" "$1" 2>/dev/null; }
verdict "(b) témoin non cherché : le stem de « Basse » silencieux, sous −130 dBFS ($TB), celui de « Voix » sonne ($TV)" \
    "$(silencieux "$TB" && au_dessus "$TV" -60 && echo 1 || echo 0)"
UN="$(ligne annule audio/nappe.wav)"; UNN="$(ligne annule audio/Nappe.wav)"; UA="$(ligne annule audio/absent.wav)"
echo "       Ctrl+Z : $(manquants annule) manquant(s) ; nappe.wav $(etat_de "$UN") ; Nappe.wav $(etat_de "$UNN") ; absent.wav $(etat_de "$UA")"
verdict "(b) Ctrl+Z : 4 manquants ; nappe.wav MANQUANT, Nappe.wav gardé pour l'historique, absent.wav joué" \
    "$([ "$(manquants annule)" = 4 ] && [ "$(etat_de "$UN")" = MANQUANT ] && [ "$(etat_de "$UNN")" = "gardé pour l'historique" ] \
       && [ "$(etat_de "$UA")" = "joué" ] && echo 1 || echo 0)"
verdict "(b) sans manquant : « aucun fichier manquant », « Chercher » grisé, aucun pas" \
    "$(grep -q "^VSM_RESERVE : chercher : aucun fichier manquant$" "$brouillon/aucun.txt" && grep -q "^VSM_RESERVE : Chercher grisé$" "$brouillon/aucun.txt" \
       && grep -q "^VSM_HISTORIQUE_PAS : 0 : $" "$brouillon/aucun.txt" && echo 1 || echo 0)"
echo "       compte (bouton) : $(compte bouton)"
verdict "(b) le bouton pressé : le même compte, 3 manquants" "$([ "$(compte bouton)" = "$ATTENDU" ] && [ "$(manquants bouton)" = 3 ] && echo 1 || echo 0)"
E="$(grep "^VSM_RESERVE : remarque ; " "$brouillon/en.txt" | tail -1)"
echo "       anglais : $E"
verdict "(b) en anglais : « Search a folder... » pressé dans « Pool », « 2 relinked, 1 not found, 1 ambiguous, 1 unreadable »" \
    "$(echo "$E" | grep -q "2 relinked, 1 not found, 1 ambiguous, 1 unreadable" && [ "$(etat_de "$(ligne en audio/absent.wav)")" = playing ] && echo 1 || echo 0)"

if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-cherche "$OK;1200:chercher-manquants:$AIL;1500:relever-reserve"
    ti="$(grep -c "geste inconnu" "$brouillon/t-cherche.txt")"; tc="$(grep -c "^VSM_RESERVE : chercher" "$brouillon/t-cherche.txt")"
    echo "       témoin : $tc recherche(s) ; $ti « geste inconnu » ; $(manquants t-cherche) manquant(s)"
    verdict "(b) témoin (D616) : aucune recherche, le verbe inconnu, 5 manquants" \
        "$([ "$tc" = 0 ] && [ "$ti" -ge 1 ] && [ "$(manquants t-cherche)" = 5 ] && echo 1 || echo 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$rejouees" -gt 0 ] && echo "       $rejouees course(s) rejouée(s) pour une fenêtre dégénérée (voir plus haut)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "CHERCHER-MANQUANTS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CHERCHER-MANQUANTS : les manquants se retrouvent par leur nom, l'ambigu est dit, et la recherche se défait d'un pas"
