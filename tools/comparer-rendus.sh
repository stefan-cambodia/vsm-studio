#!/usr/bin/env bash
# comparer-rendus.sh — LES DEUX CHEMINS DE RENDU, MIS FACE À FACE.
#
# LA RÈGLE QU'IL FAIT RESPECTER (invariant n° 3 du § 6 de docs/ROADMAP-daw.md) :
# ce que l'application EXPORTE et ce que `vsm-render` REND doivent être le même
# son. La chaîne d'analyse optimise contre `vsm-render` ; le musicien écoute
# l'application. Si les deux divergent, chaque distance publiée par ce dépôt
# mesure un son que personne n'entend.
#
# POURQUOI UN OUTIL PLUTÔT QU'UNE MESURE DE PHASE. Trois défauts de cette
# famille ont été trouvés le 13/09, et AUCUN ne se voyait à l'écran ni ne levait
# d'erreur :
#   D185/D186 : une piste GELÉE était muette au rendu hors ligne (−6,55 dB) —
#               le hors ligne ne branchait l'audio que pour `kind == Audio` ;
#   D187      : le gel coupait la queue de l'instrument (2,43 s, un clic) ;
#   D188/D189 : `capturePreset` ne capturait jamais le PROFIL d'une machine
#               multi-échantillons, si bien que l'export perdait la piste ET
#               qu'un Ctrl+S effaçait « profile » du fichier.
# Chacun s'est mesuré en comparant les deux chemins sur un vrai projet. Ce
# script est cette mesure, rendue rejouable : `tools/` plutôt qu'un script de
# phase, parce qu'un script de phase n'est ni relu, ni rejoué, ni corrigé
# (leçon de D150).
#
# USAGE
#   tools/comparer-rendus.sh <dossier-projet> [--frequence 44100] [--garder]
#
# CE QU'IL FAIT, ET POURQUOI AINSI
#   - les deux rendus sont en **int24**, le format que l'application exporte :
#     comparer un 24 bits à un flottant ajouterait un plancher de quantification
#     (−127 dB, D177) et, sur un morceau qui dépasse 0 dBFS, un écrêtage d'un
#     seul côté ;
#   - l'application tourne sous un HOME de BROUILLON (règle de D77 : un banc ne
#     doit pas écrire dans les préférences de l'utilisateur), avec un lien vers
#     sa bibliothèque de profils — sans quoi une machine à profil serait muette
#     pour une raison qui n'a rien à voir avec ce qu'on mesure ;
#   - le verdict est un CODE DE SORTIE : 0 si les deux chemins rendent le même
#     son, 1 sinon, 2 si quelque chose a empêché la mesure.
set -u
DEPOT="$(cd "$(dirname "$0")/.." && pwd)"
FREQ=44100
GARDER=0
PROJET=""
while [ $# -gt 0 ]; do
  case "$1" in
    --frequence) FREQ="$2"; shift 2 ;;
    --garder) GARDER=1; shift ;;
    -*) echo "option inconnue : $1" >&2; exit 2 ;;
    *) PROJET="$1"; shift ;;
  esac
done
[ -n "$PROJET" ] || { echo "usage : $0 <dossier-projet> [--frequence Hz] [--garder]" >&2; exit 2; }
[ -f "$PROJET/project.json" ] || { echo "pas un projet VSM : $PROJET/project.json manquant" >&2; exit 2; }

RENDER="$DEPOT/build/tools/vsm-render"
APP="$DEPOT/build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
for b in "$RENDER" "$APP"; do
  [ -x "$b" ] || { echo "binaire manquant : $b" >&2; exit 2; }
done
# LES DEUX BINAIRES SONT DATÉS DANS LE COMPTE RENDU. Un `vsm-render` plus vieux
# que le correctif qu'on vérifie ferait échouer la comparaison pour une raison
# qui n'est pas celle du projet : c'est arrivé le 13/09, une campagne interdisant
# de le relier.
echo "vsm-render  : $(date -r "$RENDER" '+%d/%m %H:%M')"
echo "application : $(date -r "$APP" '+%d/%m %H:%M')"

TRAVAIL="$(mktemp -d "${TMPDIR:-/tmp}/comparer-rendus-XXXXXX")"
nettoyer() { [ "$GARDER" -eq 1 ] || rm -rf "$TRAVAIL"; }
trap nettoyer EXIT
mkdir -p "$TRAVAIL/home/.local/share/vsm-studio"
[ -d "$HOME/.local/share/vsm-studio/profils" ] \
  && ln -s "$HOME/.local/share/vsm-studio/profils" "$TRAVAIL/home/.local/share/vsm-studio/profils"

"$RENDER" "$PROJET" "$TRAVAIL/render.wav" --sample-rate "$FREQ" --format int24 --quiet || exit 2
HOME="$TRAVAIL/home" VSM_PROJET="$PROJET" VSM_EXPORT="$TRAVAIL/app.wav" \
  VSM_CAPTURE="$TRAVAIL/photo.png" "$APP" > /dev/null 2>"$TRAVAIL/app.log"
[ -f "$TRAVAIL/app.wav" ] || { echo "l'application n'a rien exporté ; voir $TRAVAIL/app.log" >&2; GARDER=1; exit 2; }
grep -E "VSM_BOITE|VSM_OUVERTURE" "$TRAVAIL/app.log" | sed 's/^/  /' || true

"$DEPOT/analyse/.venv/bin/python" - "$TRAVAIL" <<'PY'
import sys, numpy as np, soundfile as sf
T = sys.argv[1]
a, sra = sf.read(f"{T}/app.wav",    dtype="float64", always_2d=True)
b, srb = sf.read(f"{T}/render.wav", dtype="float64", always_2d=True)
if sra != srb:
    print(f"ÉCHEC : fréquences différentes ({sra} contre {srb})"); sys.exit(2)
n = min(len(a), len(b))
if abs(len(a) - len(b)) > 1:
    print(f"ATTENTION : durées différentes ({len(a)} contre {len(b)} échantillons)")
a, b = a[:n], b[:n]
rms = lambda v: float(np.sqrt(np.mean(np.asarray(v) ** 2)))
d = (a - b).ravel()
corr = float(np.corrcoef(a.ravel(), b.ravel())[0, 1])
ecart = 20 * np.log10(max(rms(d), 1e-300) / max(rms(a.ravel()), 1e-300))
deb, seg = n // 2, int(2 * sra)
x = a[deb:deb+seg, 0] - a[deb:deb+seg, 0].mean()
y = b[deb:deb+seg, 0] - b[deb:deb+seg, 0].mean()
dec = int(np.argmax(np.abs(np.correlate(y, x, mode="full"))) - (len(x) - 1))
print(f"  échantillons : {n}   fréquence : {sra} Hz   durée : {n/sra:.3f} s")
print(f"  corrélation  : {corr:.9f}")
print(f"  écart        : {ecart:.2f} dB sous le signal   (max |d| {np.max(np.abs(d)):.3e})")
print(f"  décalage     : {dec} échantillon(s)")
bon = corr >= 0.999999 and ecart <= -100.0 and dec == 0
print("VERDICT : " + ("les deux chemins rendent le MÊME son" if bon
                      else "LES DEUX CHEMINS DIVERGENT — voir ROADMAP-daw.md D177, D185, D188"))
sys.exit(0 if bon else 1)
PY
