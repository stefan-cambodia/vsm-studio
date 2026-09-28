#!/usr/bin/env bash
# La garde de D344 : LE VUMÈTRE D'UNE TRANCHE SE VOIT, SE GRADUE, ET GARDE SON ÉCRÊTAGE.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. AU REPOS, la fente du mètre se distingue du fond de la tranche. Avant
#      D344, elle était peinte en `pianoKeyBlack` (0x1a1a1f) sur un fond `panel`
#      (0x1f1f24) : **contraste 1,06**, c'est-à-dire rien — on ne savait ni où
#      était le mètre ni jusqu'où il pouvait monter. Règle : **>= 1,40** ;
#   2. la barre MONTE au bon endroit : une consigne en dBFS se relit à la même
#      valeur sur le relevé (`VSM_VUMETRES=1`) ;
#   3. le TÉMOIN D'ÉCRÊTAGE s'arme au-delà de 0 dBFS, **reste allumé quand la
#      crête est retombée** (c'est toute sa raison d'être : une crête dure un
#      buffer et passe entre deux rafraîchissements), et **s'efface d'un clic**.
#
#   4. D468 : LA CRÊTE SE LIT SUR L'ÉCHELLE DU FADER. Les seuls chiffres de la
#      tranche sont ceux du fader (D342) ; le mètre suivait une autre échelle
#      (-60..0 dBFS, linéaire) et une crête à -12 dBFS montait au « 0 » du fader.
#      Règle : pour -6, -12 et -24 dBFS, le haut de la barre est à **2 px au
#      plus** de la graduation du même nombre, lue sur la PHOTO (2 240 x 1 400,
#      où les six graduations se posent). Vue ROUGE sur le binaire du 27/09
#      avant d'être verte.
#   5. D469 : LE MÈTRE DU MASTER EST GRADUÉ, sur la même échelle. Il n'avait
#      aucun chiffre. Règle : à gauche de sa fente, le trait ambre du 0 dB et au
#      moins trois gris dessous ; une crête posée par `master:dBFS` à -6, -12 et
#      -24 monte à 2 px au plus de sa graduation ; les sept libellés de
#      potentiomètre tiennent dans leur case (le relevé les mesure à leur police).
#   6. D470 : LE RMS SE VOIT DANS LA BARRE. Il était un trait blanc à 35 % posé
#      dans une barre pleine qui était la crête. Règle : au bord du RMS (sa
#      hauteur se déduit des graduations 0 et -24 par la courbe de la console),
#      le pixel 3 px au-dessus et celui 3 px en dessous ont un contraste d'au
#      moins 1,8 — la bande pâle crête/RMS contre la barre pleine.
#
# COMMENT. `VSM_MIXEUR_NIVEAU=piste:dBFS[!]` pose une crête par le MÊME chemin
# que le minuteur de l'application (`setMeasurement`) ; le « ! » la pose UNE
# SEULE FOIS, sans quoi la consigne reposée à chaque tour réarmerait le témoin
# aussitôt effacé et l'effacement serait invérifiable. Le clic passe par
# `cliquer:mixeur.vumetre` (D344 : le clic de banc atteint aussi un composant qui
# n'est pas un bouton). Projet de démarrage, aucune donnée extérieure.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/vumetre-console.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-vumetre.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

lancer() {   # $1 nom de la course ; $2... variables supplémentaires
    local nom="$1"; shift
    local maison; maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
    env HOME="$maison" VSM_TAILLE="1280x742" VSM_VUMETRES=1 VSM_DELAI=2500 \
        VSM_CAPTURE="$brouillon/$nom.png" "$@" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D344 : le vumètre de la console ==="

# (a) au repos : la fente se voit
lancer repos VSM_MIXEUR=1
# LE RELEVÉ DIT OÙ, LA PHOTO DIT DE QUELLE COULEUR. Sans la position, la garde
# cherchait « le mètre » au jugé sur une rangée du fader : elle rendait 5,66 de
# contraste AVANT comme APRÈS, c'est-à-dire qu'elle mesurait le capuchon ambre du
# fader et validait le défaut qu'elle devait attraper.
zone="$(sed -n 's/.*mètre \([0-9]*\)x\([0-9]*\) @\([0-9]*\),\([0-9]*\).*/\1 \2 \3 \4/p' "$brouillon/repos.txt" | head -1)"
if [ -z "$zone" ]; then
    verdict "le relevé VSM_MIXEUR donne la position du mètre" 0
    zone="0 0 0 0"
fi
contraste="$(python3 - "$brouillon/repos.png" $zone <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print("0.00"); raise SystemExit(0)


def lum(c):
    f = [(v / 255) / 12.92 if v / 255 <= 0.03928 else ((v / 255 + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2]


im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
lw, lh, lx, ly = (int(v) for v in sys.argv[2:6])
if lw <= 0 or lh <= 0:
    print("0.00")
    raise SystemExit(0)
# Le fond de la tranche se lit À GAUCHE du mètre, sur la même rangée ; le mètre
# se lit dans ses propres colonnes. Une rangée au milieu de sa hauteur, loin du
# capuchon du fader et de la bande de corrélation du bas.
y = ly + lh // 2
fond = px[max(0, lx - 6), y]
colonnes = [px[x, y] for x in range(lx, lx + lw)]
pire = max(colonnes, key=lambda c: abs(lum(c) - lum(fond)))
a, b = sorted((lum(fond), lum(pire)), reverse=True)
print("%.2f" % ((a + 0.05) / (b + 0.05)))
PY
)"
verdict "au repos, la fente du mètre tranche sur le fond (contraste $contraste, règle : >= 1,40)" \
        "$(awk -v c="$contraste" 'BEGIN { print (c >= 1.40) ? 1 : 0 }')"

# (b) la barre monte au bon endroit
lancer barre VSM_MIXEUR_NIVEAU="0:-12.0"
lu="$(sed -n 's/^VSM_VUMETRE : tranche 0 : \(-\?[0-9.]*\) dBFS.*/\1/p' "$brouillon/barre.txt" | head -1)"
verdict "une consigne de -12,00 dBFS se relit -12,00 (relevé : $lu)" \
        "$([ "$lu" = "-12.00" ] && echo 1 || echo 0)"

# (c) le témoin d'écrêtage : armé, RETENU, puis effacé d'un clic
lancer ecrete VSM_MIXEUR_NIVEAU="0:0.5!"
arme="$(sed -n 's/^VSM_VUMETRE : tranche 0 : .*écrête \([01]\).*/\1/p' "$brouillon/ecrete.txt" | head -1)"
niveau="$(sed -n 's/^VSM_VUMETRE : tranche 0 : \(-\?[a-z0-9.]*\) dBFS.*/\1/p' "$brouillon/ecrete.txt" | head -1)"
verdict "une crête d'un seul buffer à +0,5 dBFS arme le témoin et le RETIENT (crête retombée à $niveau, témoin $arme)" \
        "$([ "$arme" = "1" ] && [ "$niveau" = "-inf" ] && echo 1 || echo 0)"
lancer efface VSM_MIXEUR_NIVEAU="0:0.5!" VSM_GESTE_APRES="1200:cliquer:mixeur.vumetre"
efface="$(sed -n 's/^VSM_VUMETRE : tranche 0 : .*écrête \([01]\).*/\1/p' "$brouillon/efface.txt" | head -1)"
clic="$(grep -c 'VSM_CLIC : mixeur.vumetre — cliqué' "$brouillon/efface.txt")"
verdict "le clic sur le mètre atteint le composant et efface le témoin (clic $clic, témoin $efface)" \
        "$([ "$clic" -ge 1 ] && [ "$efface" = "0" ] && echo 1 || echo 0)"

# (d) D468 : la crête se lit sur l'échelle du fader
# LA GRADUATION SE REPÈRE PAR SES PIXELS : trois pixels à gauche du fader, là où
# `peindreEchelle` trace ses traits (le chiffre s'arrête neuf pixels avant) ; le
# trait ambre est le 0 dB, les gris qui le suivent vers le bas -6, -12, -24, -40.
# LA CRÊTE, par le haut de la barre dans la colonne du mètre : la première rangée
# d'où partent six pixels hors du fond d'affilée (le témoin d'écrêtage n'en a que
# trois, les traits de 0 et -6 dBFS un seul).
for db in -6 -12 -24; do
    lancer "aligne$db" VSM_MIXEUR=1 VSM_TAILLE="2240x1400" VSM_MIXEUR_NIVEAU="0:$db.0"
    mesure="$(python3 - "$brouillon/aligne$db.png" "$brouillon/aligne$db.txt" "$db" <<'PY'
import re, sys
try:
    from PIL import Image
except ImportError:
    print("SANS-PIL"); raise SystemExit(0)
png, releve, db = sys.argv[1], sys.argv[2], int(sys.argv[3])
texte = open(releve, encoding="utf-8", errors="replace").read()
m = re.search(r"VSM_MIXEUR : piste 0 .*?tranche (\d+)x\d+.*?mètre (\d+)x(\d+) @(\d+),(\d+), échelle (\d+) px", texte)
if not m:
    print("SANS-RELEVE"); raise SystemExit(0)
sw, mw, mh, mx, my, ech = (int(v) for v in m.groups())
if ech <= 0:
    print("SANS-ECHELLE"); raise SystemExit(0)
im = Image.open(png).convert("RGB")
px = im.load()
# La tranche : r = bornes.reduced(4) ; le mètre prend les 10 px de droite, l'échelle
# les `ech` px de gauche ; le fader commence donc à mx + mw + 8 + ech - sw.
xt = mx + mw + 8 + ech - sw - 3
def proche(c, ref): return all(abs(a - b) <= 14 for a, b in zip(c, ref))
gris, ambre = (138, 136, 146), (227, 162, 77)
groupes, courant = [], None
for y in range(max(0, my - 12), min(im.size[1], my + mh + 12)):
    c = px[xt, y]
    genre = "a" if proche(c, ambre) else ("g" if proche(c, gris) else None)
    if genre and courant and courant[1] == genre and y == courant[2] + 1:
        courant[2] = y
    elif genre:
        courant = [y, genre, y]; groupes.append(courant)
    else:
        courant = None
rangs = [g for g in groupes]
try:
    ia = next(i for i, g in enumerate(rangs) if g[1] == "a")
except StopIteration:
    print("SANS-UNITE"); raise SystemExit(0)
dessous = [g[0] for g in rangs[ia + 1:] if g[1] == "g"]
rang = {-6: 0, -12: 1, -24: 2}[db]
if len(dessous) <= rang:
    print("GRADUATION-ABSENTE"); raise SystemExit(0)
yt = dessous[rang]
xm = mx + mw // 2
# D470 : la barre se repère par ce qui n'est plus le FOND DE LA FENTE (0x1a1a1f,
# somme 83) — la bande pâle crête/RMS n'est pas « claire » au seuil de 300.
clair = lambda y: sum(px[xm, y]) >= 150
haut = next((y for y in range(my, my + mh - 6) if all(clair(y + k) for k in range(6))), None)
if haut is None:
    print("SANS-BARRE"); raise SystemExit(0)
print(yt, haut, abs(haut - yt))
PY
)"
    set -- $mesure
    if [ $# -eq 3 ]; then
        verdict "une crête à $db dBFS monte à la graduation « $db » du fader (graduation y $1, barre y $2 : écart $3 px, règle : <= 2)" \
                "$([ "$3" -le 2 ] && echo 1 || echo 0)"
    else
        verdict "une crête à $db dBFS se mesure sur la photo ($mesure)" 0
    fi
done
# (e) D469 : le mètre du master, gradué sur la même échelle
# Les traits s'arrêtent DEUX pixels avant la fente (`peindreEchelleDb`, xDroite =
# fente - 2) : la colonne lue est donc fente - 4. Même lecture de la barre qu'en (d).
for db in -6 -12 -24; do
    lancer "master$db" VSM_MIXEUR=1 VSM_TAILLE="2240x1400" VSM_MIXEUR_NIVEAU="master:$db.0"
    mesure="$(python3 - "$brouillon/master$db.png" "$brouillon/master$db.txt" "$db" <<'PY'
import re, sys
try:
    from PIL import Image
except ImportError:
    print("SANS-PIL"); raise SystemExit(0)
png, releve, db = sys.argv[1], sys.argv[2], int(sys.argv[3])
texte = open(releve, encoding="utf-8", errors="replace").read()
m = re.search(r"VSM_MIXEUR : master — mètre (\d+)x(\d+) @(\d+),(\d+), échelle (\d+) px", texte)
if not m:
    print("SANS-RELEVE"); raise SystemExit(0)
mw, mh, mx, my, ech = (int(v) for v in m.groups())
im = Image.open(png).convert("RGB")
px = im.load()
xt = mx - 4
def proche(c, ref): return all(abs(a - b) <= 14 for a, b in zip(c, ref))
gris, ambre = (138, 136, 146), (227, 162, 77)
groupes, courant = [], None
for y in range(max(0, my - 12), min(im.size[1], my + mh + 12)):
    c = px[xt, y]
    genre = "a" if proche(c, ambre) else ("g" if proche(c, gris) else None)
    if genre and courant and courant[1] == genre and y == courant[2] + 1:
        courant[2] = y
    elif genre:
        courant = [y, genre, y]; groupes.append(courant)
    else:
        courant = None
try:
    ia = next(i for i, g in enumerate(groupes) if g[1] == "a")
except StopIteration:
    print("SANS-UNITE"); raise SystemExit(0)
dessous = [g[0] for g in groupes[ia + 1:] if g[1] == "g"]
rang = {-6: 0, -12: 1, -24: 2}[db]
if len(dessous) < 3:
    print("GRADUATIONS-" + str(len(dessous))); raise SystemExit(0)
yt = dessous[rang]
xm = mx + mw // 2
# D470 : la barre se repère par ce qui n'est plus le FOND DE LA FENTE (0x1a1a1f,
# somme 83) — la bande pâle crête/RMS n'est pas « claire » au seuil de 300.
clair = lambda y: sum(px[xm, y]) >= 150
haut = next((y for y in range(my, my + mh - 6) if all(clair(y + k) for k in range(6))), None)
if haut is None:
    print("SANS-BARRE"); raise SystemExit(0)
print(yt, haut, abs(haut - yt))
PY
)"
    set -- $mesure
    if [ $# -eq 3 ]; then
        verdict "master : une crête à $db dBFS monte à la graduation « $db » (graduation y $1, barre y $2 : écart $3 px, règle : <= 2)" \
                "$([ "$3" -le 2 ] && echo 1 || echo 0)"
    else
        verdict "master : une crête à $db dBFS se mesure sur la photo ($mesure)" 0
    fi
done
libelles="$(sed -n 's/^VSM_MIXEUR : master .*libellés \([0-9]*\) sur \([0-9]*\) tiennent.*/\1 \2/p' "$brouillon/master-12.txt" | head -1)"
set -- $libelles
verdict "master : les libellés de potentiomètre tiennent dans leur case (${1:-?} sur ${2:-?})" \
        "$([ $# -eq 2 ] && [ "$1" = "$2" ] && [ "$2" -gt 0 ] && echo 1 || echo 0)"
# (f) D470 : le RMS se voit dans la barre
# La consigne de banc pose le RMS à 0,7 fois la crête : -12 dBFS de crête, -15,10
# de RMS. Sa hauteur ne se lit pas sur une graduation ; elle se CALCULE sur la
# courbe de la console (-60..+6, milieu à -12), recalée sur deux graduations lues
# sur la photo — le 0 dB ambre et le -24.
lancer "rms" VSM_MIXEUR=1 VSM_VUMETRES=1 VSM_TAILLE="2240x1400" VSM_MIXEUR_NIVEAU="0:-12.0"
mesure="$(python3 - "$brouillon/rms.png" "$brouillon/rms.txt" <<'PY'
import math, re, sys
try:
    from PIL import Image
except ImportError:
    print("SANS-PIL"); raise SystemExit(0)
png, releve = sys.argv[1], sys.argv[2]
texte = open(releve, encoding="utf-8", errors="replace").read()
m = re.search(r"VSM_MIXEUR : piste 0 .*?tranche (\d+)x\d+.*?mètre (\d+)x(\d+) @(\d+),(\d+), échelle (\d+) px", texte)
if not m:
    print("SANS-RELEVE"); raise SystemExit(0)
sw, mw, mh, mx, my, ech = (int(v) for v in m.groups())
im = Image.open(png).convert("RGB")
px = im.load()
xt = mx + mw + 8 + ech - sw - 3
def proche(c, ref): return all(abs(a - b) <= 14 for a, b in zip(c, ref))
gris, ambre = (138, 136, 146), (227, 162, 77)
groupes, courant = [], None
for y in range(max(0, my - 12), min(im.size[1], my + mh + 12)):
    c = px[xt, y]
    genre = "a" if proche(c, ambre) else ("g" if proche(c, gris) else None)
    if genre and courant and courant[1] == genre and y == courant[2] + 1:
        courant[2] = y
    elif genre:
        courant = [y, genre, y]; groupes.append(courant)
    else:
        courant = None
try:
    ia = next(i for i, g in enumerate(groupes) if g[1] == "a")
except StopIteration:
    print("SANS-UNITE"); raise SystemExit(0)
dessous = [g[0] for g in groupes[ia + 1:] if g[1] == "g"]
if len(dessous) < 3:
    print("GRADUATIONS-" + str(len(dessous))); raise SystemExit(0)
skew = math.log(0.5) / math.log(48.0 / 66.0)
p = lambda db: ((db + 60.0) / 66.0) ** skew
y0, y24 = groupes[ia][0], dessous[2]
longueur = (y24 - y0) / (p(0.0) - p(-24.0))
yrms = round(y0 + longueur * (p(0.0) - p(-12.0 + 20.0 * math.log10(0.7))))
xm = mx + mw // 2
def lum(c):
    f = [(v / 255) / 12.92 if v / 255 <= 0.03928 else ((v / 255 + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2]
a, b = sorted((lum(px[xm, yrms - 3]), lum(px[xm, yrms + 3])), reverse=True)
print(yrms, "%.2f" % ((a + 0.05) / (b + 0.05)))
PY
)"
set -- $mesure
if [ $# -eq 2 ]; then
    verdict "le RMS (-15,10 dBFS) se voit dans la barre (bord calculé y $1, contraste de part et d'autre $2, règle : >= 1,80)" \
            "$(awk -v c="$2" 'BEGIN { print (c >= 1.80) ? 1 : 0 }')"
else
    verdict "le RMS se mesure sur la photo ($mesure)" 0
fi
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
