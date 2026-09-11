# VSM Studio — ordre de marche

Tu travailles sur ~/videcode/muz/vsm-studio : un DAW C++/JUCE (moteur temps
réel, 53 machines modélisées) et sa chaîne d'analyse Python qui reconstruit
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
- Une campagne lancée depuis le shell de l'outil MEURT avec la session, même
  sous nohup (S1, 04/09 : 1 h 44 de course perdues à la reprise). Lancer par
  `setsid nohup script.sh > x.log 2>&1 < /dev/null & disown`, et à chaque
  reprise vérifier `pgrep` avant de croire le journal.
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
- JAMAIS de build à plus de deux travaux (`-j 2`) pendant qu'une campagne
  tourne : une compilation JUCE à `-j 6` pendant une séparation demucs a été
  tuée par le manque de mémoire (05/09, 15 Go) — et c'est le build qui a
  été tué, pas la course, par chance. Tuer la course coûterait des heures.
- Le code de sortie d'un tube est celui de son DERNIER maillon : `cmake
  --build … | grep …` rend 0 même quand la compilation échoue, et l'on
  vérifie alors un ancien binaire en croyant vérifier le nouveau (payé deux
  fois le 05/09). Lire `${PIPESTATUS[0]}`, ou ne pas filtrer. Même piège
  SANS tube : `cmake --build … > log ; grep error log` rend le code du grep,
  et 0 veut dire qu'il A TROUVÉ des erreurs (11/09, D94 — vu au journal, pas
  au code). Garder `rc=$?` après le build et finir par `exit $rc`.
- En C++20, `u8"…"` est un `char8_t[]` : `juce::String + u8"…"` et `u8"…" +
  juce::String` sont AMBIGUS et ne compilent pas. Envelopper :
  `juce::String(u8"…")`. Payé quatre fois dans la même journée.
  Cinquième fois le 10/09 (D89), sous une autre forme : un tableau de
  `const char*` initialisé par des `u8"…"` — déclarer les champs en
  `const char8_t*`. Et une commande de mesure qui ENCHAÎNE une compilation
  doit s'arrêter sur son échec : sinon elle mesure l'ancien binaire.
- En zsh, `grep --include=*.cpp` est un glob que le shell mange (« no
  matches found ») : quoter `--include='*.cpp'`, ou passer par `find`.
- `VSM_MENU=libellé` prend le PREMIER libellé exact tous menus confondus
  (Fichier d'abord) : « Automatique » a piloté les threads de rendu au lieu
  du mode d'écoute (06/09). Un libellé de menu neuf doit être unique dans
  toute la barre, et la capture qui le vérifie regarde l'EFFET, pas l'absence
  d'erreur.
- Les MACHINES vivent dans audio/plugins/<machine>/, pas dans audio/src/ ni
  audio/include/ : un `grep` limité à ces deux dossiers a produit « aucune
  machine ne répond aux contrôleurs », écrit dans un commit, alors que 42
  répondent à la molette (06/09). Un « zéro » sorti d'un grep se revérifie en
  listant ce qu'on a cherché ET où, avant de l'écrire.
- Un port MIDI de sortie VIRTUEL (ALSA) se présente aussi comme une ENTRÉE :
  le moteur, qui écoute toutes les entrées au démarrage, a rebouclé sa
  propre sortie (3 440 notes reçues pour 8 jouées, D27). Filtrer son propre
  port ; et toute preuve par outil extérieur (aseqdump…) doit couvrir la
  fenêtre où le morceau JOUE — un morceau de 1,85 s est fini avant que
  l'outil ne soit branché (VSM_LECTURE=4000 retarde la lecture).
- Un banc qui LANCE l'application écrit dans les préférences de
  l'utilisateur (`~/VintageSynthMidiStudio/*.settings`) : chaque
  `VSM_PROJET` s'inscrit dans ses projets récents, et `VSM_VUE=flottant`
  a basculé sa disposition en panneaux flottants, conservée au lancement
  suivant (10/09, D77 — les captures d'après montraient une fenêtre vide, et
  deux images vides donnaient « 0 pixel de différence »). Lancer les bancs
  sous `HOME=<brouillon>` ; et vérifier par `cmp` que le fichier de
  l'utilisateur n'a pas bougé — contre une copie prise JUSTE AVANT la série,
  jamais contre une copie ancienne : l'utilisateur se sert de l'application
  pendant qu'on travaille, et le 10/09 à 21:13 son propre usage a fait
  échouer le contrôle. Un fichier changé se lit clé par clé avant de conclure,
  et ne se « rétablit » jamais par-dessus ce qu'il a fait.
- JAMAIS la suite Python complète (`verifier.sh`) pendant qu'une campagne
  tourne : le 11/09, à côté du corpus A6 (59 machines, 15 Go), elle a été
  arrêtée faute de mémoire — le matin même elle était passée à côté du même
  corpus, qui démarrait. Le corpus a survécu, par chance. Passer les tests
  ciblés (`run.py <filtre>`), ruff et mypy, ou attendre la fin de la course.
- JAMAIS d'édition de analyse/analyzer/*.py pendant qu'une course tourne : la
  chaîne importait des modules À LA DEMANDE, cinq heures après le départ, dans
  l'état du disque à cet instant (parite-v2, 5 h 24 perdues au réglage final).
  `charger_tous_les_modules()` importe tout au départ depuis le 03/09 ; la
  règle reste : ce qui n'a pas été importé au départ ne se touche pas.
- Un ÉCRAN VERROUILLÉ fait passer la fenêtre pour minimisée : `isShowing()`
  rend faux partout, et `VSM_TEXTES_LISTE` a listé 0 texte sur trois
  lancements (11/09, D94) pendant que `VSM_CAPTURE` dessinait la fenêtre
  entière. Un banc qui lit l'état « affiché » d'un composant lit `isVisible()`
  en descendant depuis la racine, et dit son compte (`VSM_TEXTES : N`). Vérifier
  `loginctl show-session <n> -p LockedHint` avant de croire un zéro.
- Une BOÎTE MODALE absente d'une photo (`VSM_CAPTURE_PANNEAUX`) ne prouve
  rien : c'est la course de D72, une photo sur sept au pire. Relancer avant de
  conclure. Trois ratés de suite ont failli faire écrire « le port MIDI ferme
  la boîte » (D91) ; relancée, elle est venue du premier coup, quatre fois.
  Sous un écran VERROUILLÉ, ce n'est plus une sur sept mais AUCUNE : 0 boîte
  sur 7 lancements, aucun composant modal deux secondes après le geste (D95).
  Une boîte se lit donc au moment où elle est demandée — `VSM_BOITE`, écrite
  par `montrerBoite()` — et la photo ne sert qu'à la regarder.
- `juce::Button` REDÉCLARE `mouseDown`/`mouseUp` en PROTÉGÉ, là où `juce::Slider`
  les laisse publics : un banc qui presse un bouton par son nom ne compile pas
  (« est protégé dans ce contexte », 12/09, D145). Passer par la base —
  `static_cast<juce::Component*>(bouton)->mouseDown(e)` — atteint le MÊME code
  virtuel que le système, sans rien simuler. Et `triggerClick()` POSTE un
  message : le relevé peut le précéder.
- Une valeur qui REVIENT à son point de départ ne prouve rien sans le témoin qui
  montre qu'elle en était partie : « rendu » et « jamais changé » donnent le même
  chiffre (12/09, D145 — le cas (c) lisait -0,9 dB, et il a fallu un cas sans
  annulation, à -6,0 dB, pour que « rendu » veuille dire quelque chose). Toute
  mesure d'annulation porte donc son témoin sans annulation.
