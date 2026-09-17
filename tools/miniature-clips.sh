#!/usr/bin/env bash
# La garde de D343 : QUELQUES NOTES FAUSSES N'ÉCRASENT PAS LA MINIATURE D'UN CLIP.
#
# RÈGLE GARDÉE (18/09/2026). Un clip MIDI porte depuis D283 une miniature de ses
# notes : c'est ce qui permet de reconnaître un motif sans l'ouvrir. Sa hauteur se
# plie à l'ambitus du clip — et quand cet ambitus est celui des EXTRÊMES, quatre
# notes fausses sur mille six cents suffisent à écraser toutes les autres dans le
# bas du rectangle (c'est le cas mesuré sur `children-dream-v12` : brut 29-77,
# 49 rangs, pour 1 634 notes entre 29 et 48).
#
# La fenêtre est donc celle qui tient **98 % des notes**, les autres posées sur
# la rangée du bord — jamais cachées. La garde le vérifie sur un fichier ENGENDRÉ
# ici, dont on connaît la réponse :
#
#   piste « Motif » : 100 notes entre 36 et 48, plus UNE à 90.
#   attendu : brut 36-90 (55 rangs), retenu 36-48 (13 rangs), 1 note au bord.
#
# Le relevé `VSM_CLIPS_MINI=1` donne le brut ET le retenu sur la même ligne : le
# témoin est dans la mesure, pas dans un second binaire.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/miniature-clips.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-miniature-clips.XXXXXX")"
maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
trap 'rm -rf "$brouillon"' EXIT

python3 - "$brouillon/motif.mid" <<'PY'
import struct, sys
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(evs):
    corps = b"".join(vlq(dt) + oct for dt, oct in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
nom = lambda s: b"\xff\x03" + vlq(len(s)) + s.encode()
tempo = b"\xff\x51\x03\x07\xa1\x20"
sig = b"\xff\x58\x04\x04\x02\x18\x08"
evs = [(0, nom("Motif")), (0, tempo), (0, sig)]
# cent notes entre 36 et 48, puis UNE note aberrante à 90 : la note fausse qu'une
# transcription produit, et dont l'ambitus brut faisait la loi.
for i in range(100):
    h = 36 + (i % 13)
    evs += [(0, bytes([0x90, h, 100])), (120, bytes([0x80, h, 0]))]
evs += [(0, bytes([0x90, 90, 100])), (120, bytes([0x80, 90, 0]))]
open(sys.argv[1], "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + piste(evs))
PY

env HOME="$maison" VSM_TAILLE="1280x742" VSM_CLIPS_MINI=1 VSM_DELAI=2500 \
    VSM_VUE="ouvrir-midi:$brouillon/motif.mid,sans-rapport,arrangement" \
    VSM_CAPTURE="$brouillon/arrangement.png" \
    timeout 40 "$BIN" > "$brouillon/journal.txt" 2>&1
rc=$?

j="$(cat "$brouillon/journal.txt")"
rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D343 : la fenêtre de hauteurs d'une miniature de clip ==="
ligne="$(grep -m1 '^VSM_CLIP_MINI : ' <<<"$j")"
verdict "le relevé VSM_CLIP_MINI existe" "$([ -n "$ligne" ] && echo 1 || echo 0)"
if [ -n "$ligne" ]; then
    echo "       $ligne"
    brut="$(sed -n 's/.*brut \([0-9]*\)-\([0-9]*\) (\([0-9]*\) rangs).*/\3/p' <<<"$ligne")"
    retenu="$(sed -n 's/.*retenu \([0-9]*\)-\([0-9]*\) (\([0-9]*\) rangs).*/\3/p' <<<"$ligne")"
    bornes="$(sed -n 's/.*retenu \([0-9]*-[0-9]*\) (.*/\1/p' <<<"$ligne")"
    bord="$(sed -n 's/.*rangs), \([0-9]*\) note(s) au bord.*/\1/p' <<<"$ligne")"
    verdict "ambitus brut de 55 rangs (36-90 : la note fausse est bien là, relevé : $brut)" \
            "$([ "$brut" = "55" ] && echo 1 || echo 0)"
    verdict "fenêtre retenue 36-48, 13 rangs (relevé : $bornes, $retenu rangs)" \
            "$([ "$bornes" = "36-48" ] && [ "$retenu" = "13" ] && echo 1 || echo 0)"
    verdict "la note écartée est COMPTÉE, pas cachée (relevé : $bord au bord)" \
            "$([ "$bord" = "1" ] && echo 1 || echo 0)"
fi
verdict "code de sortie 0 (relevé : $rc)" "$([ "$rc" -eq 0 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
