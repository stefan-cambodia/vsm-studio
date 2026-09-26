#!/usr/bin/env bash
# balayer-facades.sh — LA GARDE DU PLANCHER DE 18 PX SUR TOUTES LES FAÇADES, au
# rack de la disposition par défaut.
#
# RÈGLE (D62, rappelée par D292-D293, 14/09/2026) : aucun bouton de façade sous
# 18 px de diamètre. D64-D70 avaient publié « 0 / 63 façades sous 18 px » sans
# nommer la largeur de rack de la mesure ; au rack de 364 px (fenêtre par défaut
# à 150 %), le MODIFIERS du Minimoog en posait huit à 16 px. Cet outil pose
# CHAQUE machine du registre sur la piste 0 d'un projet neuf, laisse la façade
# écrire sa mesure (VSM_MESURE_FACADE, une ligne par commande) et publie, par
# machine, la taille de façade et le plus petit bouton — puis la liste de celles
# qui passent sous le plancher. Sous un HOME de brouillon : les préférences de
# l'utilisateur ne bougent pas (D77).
#
#   tools/balayer-facades.sh [sortie.tsv]      → tableau, puis verdict ; code 1 si une façade est sous 18 px
#   tools/balayer-facades.sh --juger fichier.tsv   → rejuge un balayage déjà écrit, sans relancer
#
# LA SÉRIGRAPHIE (D379) : 12 pt partout où le mot tient dans sa case, la plus
# grande taille qui tient ailleurs, jamais sous 8. Le verdict compte aussi les
# sérigraphies COUPÉES par « … » (plafond 3). Témoin : VSM_SERIGRAPHIE_PLANCHER=8
# VSM_SERIGRAPHIE_REPLI=0, le comportement d'avant.
#
# LA DERNIÈRE DISPOSITION, PAS LA PREMIÈRE (D302, 14/09/2026). Une façade écrit
# sa mesure à CHAQUE resized() : d'abord à la taille du rack (364 × 626), puis à
# sa taille naturelle (434, 818, 1 030 px…) une fois D63 posée -- c'est celle-là
# que l'utilisateur voit, l'autre est transitoire. Le verdict prenait le minimum
# sur TOUTES les lignes, et jugeait donc l'hybride PCM à 18 px (la passe de 626)
# quand ses boutons font 44 px sur la façade affichée. On ne garde, par machine,
# que les lignes de la DERNIÈRE taille écrite ; si une passe transitoire était
# plus petite, le tableau le dit.
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
if [ "${1:-}" = "--juger" ]; then
    sortie="${2:?fichier.tsv}"; [ -s "$sortie" ] || { echo "REFUS : $sortie absent ou vide" >&2; exit 2; }
    juger_seulement=1
else
    sortie="${1:-/tmp/facades-$(date +%Y%m%d-%H%M).tsv}"
    juger_seulement=0
fi
bin="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$bin" ] || { echo "binaire absent : $bin" >&2; exit 2; }
if [ "$juger_seulement" -eq 0 ]; then
brouillon="$(mktemp -d)"
rm -f "$sortie"
ids=$(grep -o 'pluginId = "[^"]*"' panels/src/MachinePanels.cpp | sed 's/.*"\(.*\)"/\1/' | sort -u)
n=0
for id in $ids; do
    n=$((n+1))
    # D390 : LA FENÊTRE EST FIXÉE, sinon la garde mesure l'ÉCRAN du jour. Le même
    # code a donné 356 × 642 (écran en 1 920 × 1 200, fenêtre de 1 280 logiques)
    # puis 364 × 797 (écran passé en 3 200 × 2 000) : un rack plus haut, une
    # façade qui ne défile plus, d'autres cases, d'autres verdicts. 1 280 × 800
    # logiques : la fenêtre par défaut d'un 1 920 × 1 200 à 150 %, la plus
    # serrée des deux, et celle des mesures de D294 à D382.
    HOME="$brouillon" DISPLAY="${DISPLAY:-:0}" VSM_TAILLE="${VSM_TAILLE:-1280x800}" VSM_VUE=sans-rapport,arrangement,piste:0 \
        VSM_GESTE_PISTE="machine:$id" VSM_MESURE_FACADE="$sortie" VSM_DELAI=1500 \
        VSM_CAPTURE="$brouillon/capture.png" timeout 60 "$bin" > "$brouillon/$id.log" 2>&1 \
        || echo "  $id : code $? au lancement" >&2
done
rm -rf "$brouillon"
echo "$n machine(s) posée(s), mesures dans $sortie"
# La DERNIÈRE taille rencontrée par machine est la bonne (la façade est posée plusieurs fois au montage).
# D380 : LA DERNIÈRE À LA PLUS GRANDE LARGEUR. Un balayage sur trois, vsm.chebyshev
# a écrit une passe transitoire à 197 px APRÈS la bonne (356 × 716) : la règle de
# D302 la prenait pour la finale et comptait un bouton de 13 px qui n'existe pas à
# l'écran. La largeur d'une façade est celle du rack ; une passe plus étroite
# qu'une autre de la même course est transitoire, où qu'elle tombe.
fi
# Deux lectures du fichier : la première retient, par machine, la DERNIÈRE
# taille de façade écrite ; la seconde ne juge que ses lignes (D302).
awk -F'\t' 'FNR==1 { f++ } f==1 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 > large[$1]) large[$1]=t[1]+0 }; next } f==2 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 >= large[$1]-16) derniere[$1]=$2 }; next }
  FNR>1 && $2!="0x0" && $10>0 && $2==derniere[$1] {
    taille[$1]=$2; if (!($1 in mini) || $10<mini[$1]) { mini[$1]=$10; ou[$1]=$3" / "$4 } }
  FNR>1 && $2!="0x0" && $10>0 && $2!=derniere[$1] {
    if (!($1 in transitoire) || $10<transitoire[$1]) transitoire[$1]=$10 }
  END { for (m in mini) printf "%-22s %-10s %3d px  %s%s\n", m, taille[m], mini[m], ou[m],
            (m in transitoire && transitoire[m]<mini[m]) ? sprintf("   (passe transitoire : %d px, ignoree)", transitoire[m]) : "" }' \
    "$sortie" "$sortie" "$sortie" | sort -k3 -n
sous=$(awk -F'\t' 'FNR==1 { f++ } f==1 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 > large[$1]) large[$1]=t[1]+0 }; next } f==2 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 >= large[$1]-16) derniere[$1]=$2 }; next }
  FNR>1 && $2!="0x0" && $10>0 && $2==derniere[$1] { if (!($1 in mini) || $10<mini[$1]) mini[$1]=$10 }
  END { c=0; for (m in mini) if (mini[m]<18) c++; print c }' "$sortie" "$sortie" "$sortie")
total=$(awk -F'\t' 'NR>1 && $2!="0x0" && $10>0 {v[$1]=1} END{print length(v)}' "$sortie")
echo "VERDICT : $sous façade(s) sur $total sous le plancher de 18 px"
# D379 : LA SÉRIGRAPHIE, sur la dernière disposition de chaque machine, lignes
# identiques comptées une fois. Colonnes 11 (police) et 12 (besoin : largeur du
# texte / largeur offerte ; au-delà de 1/0,55 JUCE coupe par « … »). Plafond de
# coupées : 3, la valeur mesurée à l'adoption (19 au plancher de 8).
read -r a12 serig coupees <<< "$(awk -F'\t' 'FNR==1 { f++ } f==1 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 > large[$1]) large[$1]=t[1]+0 }; next } f==2 { if (FNR>1 && $2!="0x0") { split($2, t, "x"); if (t[1]+0 >= large[$1]-16) derniere[$1]=$2 }; next }
  FNR>1 && $2!="0x0" && $2==derniere[$1] && NF>=12 && $5!="titre" && !vu[$0]++ { n++; if ($11>=12) d++; if ($12>1/0.55) c++ }
  END { print d+0, n+0, c+0 }' "$sortie" "$sortie" "$sortie")"
echo "SÉRIGRAPHIE : $a12 / $serig à 12 pt ou plus, $coupees coupée(s) par « … » (plafond 3)"
[ "$sous" -eq 0 ] && [ "$coupees" -le 3 ]
