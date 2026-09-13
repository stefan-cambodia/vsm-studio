#!/usr/bin/env bash
# Ce qu'il faut faire QUAND une campagne se termine, et rien avant.
#
# POURQUOI CE FICHIER (13/09/2026). Trois choses attendent toujours la fin d'une
# course, et se sont oubliées deux fois : relier `build/tools/vsm-render` (on ne
# le remplace JAMAIS pendant une campagne, sous peine de la tuer), vérifier que
# l'export de l'application et `vsm-render` rendent encore le même son
# (tools/comparer-rendus.sh), et relire le tableau du lot. Elles sont ici, dans
# cet ordre, avec leurs garde-fous.
#
# RÈGLE : ce script REFUSE de travailler tant qu'une campagne tourne. Il attend
# par PID, jamais par motif (`pgrep -f` se trouve lui-même, payé le 12/09).
#
#   tools/apres-campagne.sh reconstruction/travail/r1f-13sep [projet-gelé]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
lot="${1:-reconstruction/travail/r1f-13sep}"
projet="${2:-reconstruction/travail/children-c3-plafond}"

# 1. PERSONNE NE DOIT COURIR. On cherche le processus de la chaîne, sans se
#    compter soi-même : `pgrep -f` rendrait le PID de ce script.
en_cours=$(pgrep -f "banc_synthetique.py|reconstruire.py" | grep -v "^$$\$" | head -3)
if [ -n "$en_cours" ]; then
    echo "REFUS : une chaîne tourne encore (PID $(echo $en_cours | tr '\n' ' '))."
    echo "        Attendre sa fin : while kill -0 <PID> 2>/dev/null; do sleep 60; done"
    exit 1
fi

# 2. LE BINAIRE DE RENDU, RELIÉ. Cible précise, deux travaux : jamais de build
#    complet ici (la machine sort d'une campagne, et rien ne presse).
echo "=== vsm-render : date avant ==="
ls -l --time-style=+%F\ %H:%M build/tools/vsm-render 2>/dev/null || echo "  (absent)"
cmake --build build -j 2 --target vsm-render > /tmp/vsm-render-build.log 2>&1
rc=$?
echo "=== build rc=$rc, date après ==="
ls -l --time-style=+%F\ %H:%M build/tools/vsm-render 2>/dev/null
[ $rc -ne 0 ] && { tail -5 /tmp/vsm-render-build.log; exit $rc; }

# 3. LES DEUX CHEMINS RENDENT-ILS LE MÊME SON ? (la garde de D186)
echo "=== comparer-rendus.sh sur $projet ==="
tools/comparer-rendus.sh "$projet"
verdict=$?
echo "=== code de comparer-rendus : $verdict ==="

# 4. LE TABLEAU DU LOT, tel que la campagne l'a écrit.
echo "=== $lot/tableau.txt (entêtes et agrégats) ==="
head -2 "$lot/tableau.txt" 2>/dev/null
grep -E "^morceau-[0-9]" "$lot/tableau.txt" 2>/dev/null | wc -l | sed 's/^/morceaux au tableau : /'
[ -f "$lot/rapport.json" ] && python3 - "$lot/rapport.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print("mesurés :", len(r.get("morceaux", [])), "| non mesurés :",
      [m.get("morceau") for m in r.get("nonMesures", [])])
PY
exit $verdict
