# VSM Studio — ordre de marche

Tu travailles sur ~/videcode/muz/vsm-studio : un DAW C++/JUCE (moteur temps
réel, 39 machines modélisées) et sa chaîne d'analyse Python qui reconstruit
un morceau enregistré en projet jouable. Les feuilles de route et cahiers des
charges (docs/ROADMAP-*.md, docs/CDC-*.md) sont les critères d'acceptation et
l'ordre de marche — pas de la documentation d'accompagnement.

## Conduite
- « continue » = relire les feuilles de route, prendre l'élément suivant, le
  mener au bout, enchaîner sans attendre d'aval. Rendre compte n'est pas
  s'arrêter.
- Un choix laissé ouvert par les documents se tranche EN ÉCRIVANT la décision
  et sa raison dans le document concerné. Questions bloquantes réservées aux
  cas où les documents sont muets ou se contredisent.
- Terminé = tests verts + commit + push sur origin. Message de commit à la
  façon du dépôt : la leçon en titre, les chiffres dans le corps.

## Mesure (le cœur du projet)
- Aucune affirmation de gain sans son chiffre. Une hypothèse s'écrit AVANT la
  mesure qui la tranche, pour ne pas être tordue après.
- Un A/B = UNE variable, et le témoin est du même code que ce qu'il témoigne
  (une option en ligne de commande, jamais une constante éditée entre deux
  passes). Toute option qui conditionne le résultat va dans la provenance de
  rapport.json.
- Deux distances ne se comparent que si métrique, budget, gate et stems sont
  identiques. Un gain global sans changement de décision mesure autre chose
  que ce qu'on croit (analyse/comparer_rapports.py met les verdicts côte à
  côte).
- Panne muette interdite : ce qui est écarté, ignoré ou remplacé est DIT au
  journal et au rapport. Couper une piste reste une décision humaine — la
  chaîne mesure et publie, elle ne coupe pas.

## Interface
- La machine A un écran. Toute modification d'app/Source/ se VÉRIFIE :
  compiler la cible, puis `VSM_CAPTURE=sortie.png` (autoportrait de la
  fenêtre), `VSM_VUE=arrangement,sans-rack,...` pour piloter le menu
  Affichage et `VSM_PROJET=dossier` pour ouvrir un projet — sans souris. Ne
  jamais déclarer une interface invérifiable.
- Échelle d'interface 150 % par défaut (besoin de lisibilité, pas un goût).
  Toute disposition reste redimensionnable/déplaçable et retient ses réglages.

## Pièges payés (ne pas les repayer)
- JAMAIS de build complet pendant qu'une reconstruction tourne : remplacer
  build/tools/vsm-render tue la course. Compiler des cibles précises.
- `pkill -f "Vintage Synth"` tue le shell qui porte le motif dans sa propre
  ligne de commande : passer par un script fichier.
- Python bufferise stdout vers un fichier : lancer les longues chaînes avec
  `python -u`, et surveiller par Monitor (fins ET échecs, jamais le succès
  seul).
- Pas plus de deux étapes batterie simultanées (la charge à 49/22 cœurs met
  tout au surplace) ; geler/reprendre par SIGSTOP/SIGCONT ne perd rien.
- Après toute édition de document par script : vérifier par grep que le texte
  est bien là. Une ancre ratée fait mentir le commit qui l'annonce.
- Les nombres qui traversent une frontière (fichier, CLI, tube) se lisent et
  s'écrivent en locale C (interchange/NumberText.h) — la locale du processus
  est celle de JUCE, pas la tienne.
