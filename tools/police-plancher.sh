#!/bin/bash
# RÈGLE (D323) : aucune police écrite en dur sous 12 pt dans les panneaux du DAW
# (app/Source/ui/*.cpp et app/Source/*.cpp). L'utilisateur a du mal à lire les
# petits textes ; l'échelle d'interface (150 % par défaut) agrandit tout du même
# facteur, et un 10 pt reste le plus petit texte de l'écran quelle que soit
# l'échelle. Les façades de machines (app/Source/ui/machines/) sont HORS de cette
# garde : leur sérigraphie vit dans des cellules dictées par la description de la
# machine, et D61 la fait rétrécir plutôt que la couper. Elles sont TRAITÉES
# depuis D379, ailleurs : 12 pt partout où le mot tient dans sa case, la plus
# grande taille qui tient sinon, jamais sous 8 — mesuré et gardé par
# `balayer-facades.sh` (colonne police, plafond de sérigraphies coupées).
# Sortie 0 = rien sous 12 pt ; 1 = la liste des sites fautifs, un par ligne.
cd "$(dirname "$0")/.." || exit 2
# Deux formes : `FontOptions(11.0f)` et le raccourci `g.setFont(11.0f)` — la
# première passe de la garde ne lisait que la première, et deux sites (la règle
# du piano roll, la lane de vélocité) lui ont échappé.
fautifs=$(grep -rnE "FontOptions\(([0-9]|1[01])(\.[0-9]+)?f?\)|setFont\(([0-9]|1[01])(\.[0-9]+)?f?\)" app/Source/*.cpp app/Source/ui/*.cpp 2>/dev/null)
if [ -z "$fautifs" ]; then
    echo "police-plancher : 0 site sous 12 pt (hors façades de machines)"
    exit 0
fi
echo "$fautifs"
echo "police-plancher : $(echo "$fautifs" | wc -l) site(s) sous 12 pt — la règle est dans l'en-tête de $0"
exit 1
