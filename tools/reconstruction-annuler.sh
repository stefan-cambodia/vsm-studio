#!/usr/bin/env bash
# La garde de D339-D341 : « ANNULER » ARRÊTE LA CHAÎNE, ET PAS SEULEMENT PYTHON.
#
# RÈGLES GARDÉES (18/09/2026) :
#   D339  « Annuler » arrête le GROUPE de processus de la chaîne -- demucs et
#         les rendus `vsm-render`, qui sont des PETITS-ENFANTS et survivaient à
#         un `kill()` du seul enfant direct. Après la course : ZÉRO processus de
#         chaîne vivant, code de sortie 0, ni « killing thread by force » ni
#         « terminate called ».
#   D340  la fenêtre dit ensuite « reconstruction interrompue », et son bouton
#         passe de « Annuler » à « Fermer ».
#   D341  le bouton est ATTEIGNABLE PAR SON NOM DE FENÊTRE : `cliquer:Annuler`
#         prend le premier bouton de ce nom (celui du piano roll) et le DIT
#         (« AMBIGU ») ; `cliquer:fenetre:Reconstruction:Annuler` prend celui de
#         la fenêtre nommée. Les deux clics sont joués dans la MÊME course, à une
#         seconde d'écart : le témoin de l'ambiguïté est du même code que ce
#         qu'il témoigne.
#
# POURQUOI UNE GARDE À PART, ET NON UNE LIGNE DU BANC DE FUMÉE. Elle lance une
# VRAIE séparation demucs sur quatre secondes d'audio : une minute de course et
# plusieurs cœurs. Le banc de fumée doit rester court ; celle-ci se rejoue après
# tout changement de `ReconstructionRunner`, `ProcessusDeChaine` ou du clic de
# banc.
#
# CE QU'ELLE MESURE, ET CE QU'ELLE REFUSE DE SUPPOSER. « 0 orphelin » ne prouve
# rien si la chaîne n'a jamais démarré (la leçon de D215 : une comparaison dont
# un côté manque rend « différent », pas « raté »). Un échantillonneur relève
# donc, toutes les 300 ms, le nombre de processus de chaîne vivants ; le verdict
# exige qu'il y en ait eu AU MOMENT DU CLIC, puis zéro après la sortie.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire ou la chaîne manquent.
#
#   tools/reconstruction-annuler.sh
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "analyse/.venv/bin/python" ] || { echo "REFUS : analyse/.venv absent — la chaîne ne peut pas être lancée"; exit 2; }

# LE BROUILLON VA SUR /home ET NON DANS /tmp : le brouillon de session est un
# tmpfs de 7,7 Go partagé avec tout le reste, et les stems d'une séparation y
# ont déjà rempli le disque (13/09). Ici ils sont petits, mais la règle ne se
# plie pas pour un cas favorable.
travail="$(mktemp -d "$HOME/vsm-reconstruction-annuler.XXXXXX")"
maison="$(mktemp -d "$travail/home.XXXX")"   # D318 : un HOME NEUF par course
nettoyer() { rm -rf "$travail"; }
trap nettoyer EXIT

# QUATRE SECONDES D'AUDIO, ENGENDRÉES ICI : aucun fixture binaire commis.
python3 - "$travail/banc-4s.wav" <<'PY'
import math, struct, sys, wave
sr, secondes = 44100, 4
f = wave.open(sys.argv[1], "wb")
f.setnchannels(2); f.setsampwidth(2); f.setframerate(sr)
trames = bytearray()
for i in range(sr * secondes):
    t = i / sr
    v = 0.30 * math.sin(2 * math.pi * 220.0 * t) + 0.20 * math.sin(2 * math.pi * 440.0 * t)
    e = int(max(-1.0, min(1.0, v)) * 32000)
    trames += struct.pack("<hh", e, e)
f.writeframes(bytes(trames)); f.close()
PY

# L'ÉCHANTILLONNEUR DE PROCESSUS DE CHAÎNE. Il lit /proc lui-même : `pgrep -f`
# se trouve lui-même quand le motif est dans sa propre ligne de commande, et
# c'est un piège payé plusieurs fois dans ce dépôt. Les motifs vivent dans ce
# fichier Python, jamais sur une ligne de commande.
cat > "$travail/compter.py" <<'PY'
import os, sys, time
MOTIFS = ("reconstruire.py", "demucs", "vsm-render")
def compte():
    n = 0
    for e in os.listdir("/proc"):
        if not e.isdigit() or int(e) == os.getpid():
            continue
        try:
            with open("/proc/" + e + "/cmdline", "rb") as f:
                ligne = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
        except OSError:
            continue
        if any(m in ligne for m in MOTIFS):
            n += 1
    return n
if sys.argv[1] == "--une-fois":
    print(compte()); raise SystemExit(0)
duree = float(sys.argv[1])
depart = time.time()
with open(sys.argv[2], "w") as sortie:
    while time.time() - depart < duree:
        sortie.write("%d %d\n" % (int((time.time() - depart) * 1000), compte()))
        sortie.flush()
        time.sleep(0.3)
PY

# LES DEUX CLICS, ET LEUR HORAIRE. `msAmbigu` presse « Annuler » sans nommer sa
# fenêtre (le témoin), `msNomme` le presse dans « Reconstruction ». La photo --
# donc la fermeture -- vient après les deux, sans quoi le geste ne serait jamais
# joué et son absence se lirait comme un bouton sans effet.
msAmbigu=14000
msNomme=15000
msPhoto=22000

python3 "$travail/compter.py" 45 "$travail/suivi.txt" &
suivi_pid=$!

env HOME="$maison" \
    VSM_FICHIER="$travail/banc-4s.wav" \
    VSM_MENU="Reconstruire un morceau..." \
    VSM_GESTE_APRES="$msAmbigu:cliquer:Annuler;$msNomme:cliquer:fenetre:Reconstruction:Annuler" \
    VSM_DELAI="$msPhoto" \
    VSM_TEXTES_LISTE=1 \
    VSM_CAPTURE="$travail/capture.png" \
    timeout 60 "$BIN" > "$travail/journal.txt" 2>&1
rc=$?
# ATTENDRE PAR PID, JAMAIS PAR MOTIF (la règle du dépôt).
while kill -0 "$suivi_pid" 2>/dev/null; do sleep 0.5; done
orphelins="$(python3 "$travail/compter.py" --une-fois)"

j="$(cat "$travail/journal.txt")"
# LE COMPTE AU MOMENT DU CLIC : la plus grande valeur relevée dans la seconde
# qui entoure le clic nommé. Sans lui, « 0 orphelin » ne veut rien dire.
auClic="$(awk -v a=$((msNomme - 1500)) -v b=$((msNomme + 1500)) \
              '$1 >= a && $1 <= b && $2 > m { m = $2 } END { print m + 0 }' "$travail/suivi.txt")"

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D339-D341 : « Annuler » pendant une reconstruction ==="
verdict "la chaîne TOURNAIT au moment du clic ($auClic processus)" \
        "$([ "$auClic" -ge 1 ] && echo 1 || echo 0)"
verdict "clic sans fenêtre nommée : pris ailleurs, et DIT ambigu (témoin)" \
        "$(grep -c 'VSM_CLIC : Annuler — cliqué dans .*AMBIGU' <<<"$j")"
verdict "clic nommé : pris dans « Reconstruction »" \
        "$(grep -c 'VSM_CLIC : Annuler — cliqué dans « Reconstruction »' <<<"$j")"
verdict "les deux gestes différés sont joués" \
        "$([ "$(grep -c 'VSM_GESTE_APRES : .* — joué' <<<"$j")" -eq 2 ] && echo 1 || echo 0)"
verdict "la fenêtre dit « reconstruction interrompue »" \
        "$(grep -c 'VSM_FENETRE_TEXTE : Reconstruction : .* : reconstruction interrompue' <<<"$j")"
verdict "son bouton est passé à « Fermer »" \
        "$(grep -c 'VSM_FENETRE_TEXTE : Reconstruction : bouton : Fermer' <<<"$j")"
verdict "0 processus de chaîne après la sortie (relevé : $orphelins)" \
        "$([ "$orphelins" -eq 0 ] && echo 1 || echo 0)"
verdict "code de sortie 0 (relevé : $rc)" "$([ "$rc" -eq 0 ] && echo 1 || echo 0)"
verdict "0 « killing thread by force », 0 « terminate called »" \
        "$([ "$(grep -c 'killing thread by force' <<<"$j")" -eq 0 ] && [ "$(grep -c 'terminate called' <<<"$j")" -eq 0 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
