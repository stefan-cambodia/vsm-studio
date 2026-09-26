# VSM Studio — ordre de marche

Tu travailles sur ~/videcode/muz/vsm-studio : un DAW C++/JUCE (moteur temps
réel, 64 machines au registre — 65 dossiers dans `audio/plugins/`, dont un
résultat négatif gardé hors build, `vsm.flute` ; `vsm.cone` y est depuis le 01/09) et sa chaîne d'analyse Python qui
reconstruit un morceau enregistré en projet jouable. Les feuilles de route et
cahiers des charges (docs/ROADMAP-*.md, docs/CDC-*.md) sont les critères
d'acceptation et l'ordre de marche — pas de la documentation d'accompagnement.

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
  ligne de commande : passer par un script fichier. Même piège avec `pgrep -f`
  dans une ATTENTE : `until ! pgrep -f "corpus.py --sortie"` ne se termine
  JAMAIS, la boucle se trouvant elle-même (12/09 — la course était finie depuis
  vingt minutes et deux surveillances la croyaient en cours). Attendre par PID
  (`while kill -0 $PID`), jamais par motif.
- Une SURVEILLANCE qui cherche un motif déjà présent se déclenche tout de suite :
  le 13/09 au soir, un `until grep -q "LOT FORCÉ TERMINÉ"` posé après avoir
  RELANCÉ la course a trouvé la ligne de la course PRÉCÉDENTE, encore dans les
  cinq dernières du journal, et a annoncé la fin dans la seconde. Attendre par
  PID (`while kill -0 $PID`), toujours — c'est la même règle que pour `pgrep -f`,
  et elle vaut aussi pour les motifs d'un journal qu'on vient de rouvrir.
  **Et le PID se prend sur le BON processus** : cinq secondes après le lancement,
  `pgrep … | head -1` a rendu un PID transitoire (un maillon du tube, déjà mort),
  et la surveillance a de nouveau annoncé la fin dans la seconde. Lister d'abord
  (`pgrep -af`), reconnaître la ligne de commande attendue, prendre CE PID.
  **Et `tail -f` REJOUE les dernières lignes du fichier**, ce qui est la même
  faute sous un autre outil : une surveillance posée sur un journal déjà écrit
  reçoit aussitôt son ancien contenu. Payé DEUX FOIS le 20/09 sur la campagne s2
  — deux surveillances ont annoncé « DÉBUT morceau » une heure après le vrai
  départ, la ligne de 02:31 étant encore dans les dix dernières du journal. Le
  remède tient en trois caractères : `tail -n 0 -f`, qui ne rend QUE les lignes
  neuves. Et l'état réel se vérifie par PID (`kill -0 $PID`), jamais par ce
  qu'une surveillance vient d'annoncer.
- Une campagne lancée depuis le shell de l'outil MEURT avec la session, même
  sous nohup (S1, 04/09 : 1 h 44 de course perdues à la reprise). Lancer par
  `setsid nohup script.sh > x.log 2>&1 < /dev/null & disown`, et à chaque
  reprise vérifier `pgrep` avant de croire le journal.
- Python bufferise stdout vers un fichier : lancer les longues chaînes avec
  `python -u`, et surveiller par Monitor (fins ET échecs, jamais le succès
  seul).
- Un EXPORT hors-ligne de l'application pendant une campagne ne la tue pas : il
  la TRIPLE. Le 13/09, les mesures de D215 (sept rendus d'un projet de 309 s, puis
  onze stems d'un projet de 453 s) ont porté le morceau en cours de 56 min
  (morceau 4) à 2 h 50 (morceau 5) — la course a survécu, mais cinq morceaux
  restants à ce régime ne tenaient plus dans la batterie. Les mesures qui RENDENT
  de l'audio se groupent donc pour après la course, ou se font sur un projet
  court ; les bancs d'interface (une capture, un relevé, un menu) ne coûtent rien.
- Pas plus de deux étapes batterie simultanées (la charge à 49/22 cœurs met
  tout au surplace) ; geler/reprendre par SIGSTOP/SIGCONT ne perd rien.
- Après toute édition de document par script : vérifier par grep que le texte
  est bien là. Une ancre ratée fait mentir le commit qui l'annonce.
- Une TABLE Markdown se relit en COMPTANT ses barres, pas à l'œil : une valeur
  qui en contient une (« 00:33,000 | mes. 17 · 3 ») coupe la ligne en deux
  cellules de plus et déplace toute la fin du tableau. Payé le 13/09 sur deux
  lignes ; la garde tient en cinq lignes de Python (compter `|` par ligne d'une
  même table, les `\|` échappées exclues) et se rejoue après chaque édition. Et un grep
  qui TROUVE ne prouve pas que la FORME est bonne : le 13/09, une ligne insérée
  au lieu d'être remplacée a mis deux rangées de tableau bout à bout sur une
  seule (onze champs au lieu de six), et le texte cherché s'y trouvait bien.
  Pour un tableau, compter les champs (`awk -F'|' '{print NF}'`) ; pour une
  liste, compter les lignes.
- Les nombres qui traversent une frontière (fichier, CLI, tube) se lisent et
  s'écrivent en locale C (interchange/NumberText.h) — la locale du processus
  est celle de JUCE, pas la tienne.
- JAMAIS de build à plus de deux travaux (`-j 2`) pendant qu'une campagne
  tourne : une compilation JUCE à `-j 6` pendant une séparation demucs a été
  tuée par le manque de mémoire (05/09, 15 Go) — et c'est le build qui a
  été tué, pas la course, par chance. Tuer la course coûterait des heures.
- Même à `-j 2`, puis à `-j 1`, une compilation de l'application PENDANT une
  campagne a été tuée deux fois de suite (14/09, D285) — par la garde mémoire de
  l'outil, sur `MainComponent.cpp` (12 000 lignes, plusieurs Go à compiler), avec
  six rendus en parallèle à côté. Le remède qui a marché : GELER la campagne le
  temps du build (`kill -STOP -- -<pgid>` sur le groupe de la course lancée par
  `setsid`), lancer le build DÉTACHÉ (`setsid nohup bash -c 'cmake … ; kill -CONT
  -- -<pgid>'`) pour que la reprise soit automatique même si l'on oublie, et
  attendre par le journal du build — et attendre par un `Monitor`, pas par un
  Bash en arrière-plan : la même garde mémoire a tué l'ATTENTE (D286), et l'on
  a cru un instant le build mort alors qu'il compilait. Un gel ne perd rien
  (SIGSTOP/SIGCONT) ; un build tué à répétition perd une heure.
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
- Un banc qui ne RELAIE pas ce que l'application avertit jette la preuve qu'elle
  lui tend. Le 12/09 (D147), quatre cas ont semblé réfuter l'attendu — le muet
  « ne s'annulait pas » — parce que le verbe était envoyé par `VSM_GESTE_PISTE`
  quand il appartient à `VSM_VUE` ; l'application l'avait dit quatre fois
  (« VSM_GESTE_PISTE : geste inconnu », `Main.cpp:248`), et c'est la ligne de
  résumé du banc, qui ne lisait que ses deux relevés, qui l'a effacé. Tout
  banc compte et affiche les avertissements du journal AVANT de conclure, et un
  verbe se vérifie dans la fonction qui le dispatche, pas de mémoire.
- `u8?"` n'est pas `(?:u8)?"` : le premier veut « un `u`, puis un `8`
  facultatif » et saute TOUT littéral écrit sans le préfixe. Payé le 12/09
  (D149) : un inventaire de libellés bâti sur ce motif a rendu « 88 chaînes,
  une seule sans traduction », là où l'outil du dépôt en trouvait deux et
  dix-huit invisibles. Toute regex qui trie du code se valide sur un cas de
  CHAQUE forme avant de servir de mesure.
- Une COMPARAISON dont un côté MANQUE rend « différent », pas « raté » : le
  13/09 (D215), `cmp -s a b` a conclu « DIFFÉRENT » parce que la course qui
  devait écrire `b` n'avait jamais démarré — `/usr/bin/time` n'existe pas sur
  cette machine, et l'erreur est passée dans un tube filtré par `grep`. Tout
  verdict par comparaison vérifie d'abord que ses DEUX fichiers existent et ne
  sont pas vides, et une mesure n'emprunte pas un outil sans l'avoir vu répondre.
- Comparer deux listes TRIÉES de la même façon suppose que le geste préserve
  l'ordre — et trois gestes sur quatre ne le préservent pas. Payé TROIS FOIS le
  13/09 : la comparaison d'événements MIDI du matin (qui a failli publier une
  « transposition » inexistante), la quantification (l'ordre des notes déplacées
  se croise, et la colonne « Δ hauteur » affichait −40/+40 sur un geste qui ne
  touche pas aux hauteurs), et le miroir des hauteurs (qui INVERSE l'ordre d'un
  accord : « 61 sommes distinctes » là où il n'y en a qu'une, 124). Une mesure
  d'édition musicale se fait sur des MULTIENSEMBLES (`collections.Counter`), ou
  sur un appariement que le geste justifie — jamais sur deux listes triées à
  l'aveugle.
- Un script d'analyse écrit pour une phase n'est pas une garde : il n'est ni
  relu, ni rejoué, ni corrigé. Ce qui doit empêcher une régression va dans
  `tools/`, avec sa règle écrite dans son en-tête (12/09, D150).
- Une vérification par `grep` d'un texte qu'on vient d'écrire doit tenir sur UNE
  ligne : une phrase repliée par le retour à la ligne rend « 0 occurrence » et
  laisse croire que l'écriture a échoué (12/09, D150). Chercher un fragment
  court, ou recoller les lignes (`tr '\n' ' '`).
- LE BROUILLON DE SESSION EST UN `tmpfs` — c'est-à-dire de la RAM, 7,7 Go
  partagés avec tout le reste. Le 13/09, neuf copies d'un projet de 308 Mo et
  deux jeux de stems flottants y ont rempli `/tmp` : la campagne du lot forcé est
  morte sur `OSError: [Errno 28] No space left on device` au bout de 985 s, et
  l'outil lui-même ne pouvait plus écrire la sortie de ses commandes. `/home`
  avait 168 Go libres. Donc : le brouillon pour les petits fichiers (captures,
  journaux, scripts) ; **tout ce qui pèse plus de quelques dizaines de mégaoctets
  — copies de projets, rendus, stems — va sur `/home`**, dans un dossier qu'on
  nettoie derrière soi. Et `df -h /tmp` avant de copier un projet, pas après.
- Un panneau qui PEINT ses lignes (`g.drawText`) est INVISIBLE au relevé de
  textes, qui descend les composants : la fenêtre d'historique (12/09, D149)
  puis le volet « Projet ouvert, avec des réserves » (D152) ont tous deux failli
  faire écrire « la phrase ne s'affiche pas » alors qu'elle s'affichait en grand,
  au centre de l'écran. Quand `VSM_TEXTES_LISTE` ne trouve rien, REGARDER LA
  PHOTO avant de conclure — ou ajouter un relevé qui lise la même source que la
  peinture.
- Un TAUX ne se publie pas sans savoir DE QUOI son dénominateur est fait. Le
  13/09 (D265), « la chaîne rate 96,7 % des notes de moins de 150 ms » a tenu
  trois phases — D259 mesure, D260 précise, D261 explique — avant qu'on demande
  ce que ces notes ÉTAIENT : **1 865 sur 1 865 des frappes de batterie**, et
  `stems[].noteConfidence` ne porte AUCUNE percussion. L'instrument ne pouvait pas
  les voir et les comptait absentes, en silence. Un histogramme par catégorie —
  trois secondes de calcul — l'aurait dit au premier jour. Avant d'expliquer un
  chiffre, décomposer sa population ; et une mesure qui ne peut pas voir une
  chose doit le DIRE, jamais compter zéro.
- Un correcteur se juge sur ce qu'il CASSE autant que sur ce qu'il répare. Le
  13/09 (D270), une relecture d'octave faisait passer les notes fautives de 104 à
  74 : publiable, et faux — elle en réparait 50 et en cassait 42, à tous les
  seuils, un rapport qui ne quittait jamais 1,2×. Tout attendu de correction porte
  donc son contrôle (« pas plus de N % des notes déjà justes cassées »), et un
  balayage de seuil se publie ENTIER, jamais à son meilleur point.
- Une CORRÉLATION dont un seul point est non nul est une coïncidence écrite en
  décimales. Le 13/09 (D269), « le sous-oscillateur explique l'octave de la
  basse » sortait à Pearson **+1,000** — sur neuf parties dont **une** portait un
  sous-oscillateur. Compter les points DISTINCTS avant de lire un coefficient.
- Un banc qui désigne des coupables DIFFÉRENTS à chaque version est le coupable.
  Le 13/09 (D266), six bancs successifs ont déclaré fausses des machines justes —
  dont le piano et le Mellotron — et j'allais régler le septième jusqu'à ce qu'il
  donne la réponse voulue. Deux pièges précis en sont sortis : **une OCTAVE est le
  pire intervalle pour comparer deux spectres** (les partielles de l'aiguë sont
  les partielles paires de la grave, un glissement nul en aligne déjà la moitié —
  prendre un triton), et **un souffle tiré d'un générateur déterministe de même
  graine est IDENTIQUE d'une note à l'autre** et corrèle à zéro (décaler les
  fenêtres d'analyse). Quand un banc accuse, vérifier le banc avant la cible.
- Un test qui n'emploie pas l'OBJET DU CHEMIN RÉEL garde autre chose. Le
  13/09, deux tests neufs bâtis sur `SearchDimension` passaient au vert pendant
  que six tests de génération du corpus tombaient : le code testé reçoit des
  `SearchParameter`, un type voisin qui ne portait PAS le champ `unit` dont il
  dépendait. Les deux classes ont les mêmes champs à un près, et c'est
  exactement celui qui comptait. Avant d'écrire un objet d'essai à la main,
  vérifier de quel TYPE est ce que la fonction reçoit vraiment — et lancer la
  suite ENTIÈRE avant de commiter, pas seulement le filtre du test qu'on vient
  d'écrire (le commit du jour annonçait « tests verts » sur un `run.py corpus`
  qui n'en voyait que vingt).
- Une garde se vérifie EN LA FAISANT ÉCHOUER, et l'essai en rouge sert d'abord à
  vérifier la GARDE. Le 13/09, `tools/gestes-vivants.py` — écrite pour attraper un
  geste de menu qui ne fait rien — est restée VERTE quand on lui a remis le défaut
  qu'elle devait attraper : elle comptait toute ligne `VSM_BOITE` comme « le geste
  a dit pourquoi », alors qu'une fenêtre restée OUVERTE en imprime une aussi. Les
  deux formes se distinguent d'un mot au journal (« sans réponse de banc »). Trois
  gardes du même jour (D263, D266, D275) n'ont valu que parce qu'on les a vues
  rouges ; celle-ci a d'abord échoué à échouer.
- `$?` se lit AVANT toute substitution `$(…)` de la même ligne : dans
  `echo "[$(date +%T)] FIN $n rc=$?"`, le `$(date)` s'exécute d'abord et
  remet `$?` à zéro. Payé le 24/09 (H37) : la mesure de *B4 Wuz Then* était
  morte sur un `.mp4` que `soundfile` ne lit pas, et le script a écrit
  « rc=0 ». Garder `rc=$?` sur la ligne qui suit la commande, puis l'écrire.
- En zsh, `${PIPESTATUS[0]}` est VIDE : le tableau s'appelle `$pipestatus` et
  s'indexe à partir de 1 (`$pipestatus[1]`). Un `rc=${PIPESTATUS[0]}` rend une
  chaîne vide, et le `echo "rc=$rc"` qui suit affiche `rc=` — ce qui ressemble à
  un succès. Mieux : ne pas filtrer par un tube, écrire le journal dans un fichier
  et garder `rc=$?`.
- Un lancement de banc SANS `VSM_CAPTURE` (ni autre verbe qui quitte) NE QUITTE
  PAS : `VSM_DELAI` ne fait rien à lui seul, le `timeout` tue l'application, et
  chaque mort laisse une « session interrompue » dans le HOME du banc — 22 dans
  le même HOME de brouillon le 14/09 au soir, et une mesure qui comptait « 1
  session restante » comptait la sienne (D318). Un lancement de banc porte
  toujours une capture, et chaque course prend un HOME NEUF (`mktemp -d`), comme
  `tools/ouvrir-midi.sh` ; un HOME réutilisé rouvre ses autosauvegardes AVANT le
  geste demandé.
- Une commande qui ÉDITE puis LANCE dans le même appel exécute l'ANCIEN outil
  quand l'édition échoue : le 14/09 au soir, un `assert` Python raté a laissé
  `tools/balayer-facades.sh` intact, et le `--juger fichier.tsv` qui suivait dans
  la même ligne a été pris par l'ancienne version pour un NOM DE SORTIE — 63
  lancements de l'application pendant un build, et un fichier `--juger` dans le
  dépôt. Éditer, VÉRIFIER (grep du texte neuf), lancer : trois appels, jamais un.
- Un encadré qui annonce du travail RESTANT se lit jusqu'au bout de son bloc
  avant d'être cru : sa clôture est souvent trente lignes plus bas, dans le même
  encadré. Payé DEUX FOIS le 12/09 — la table « Nommé, chiffré, non fait » des
  boutons sous 18 px (le récapitulatif disait « parc entier : 98 → 0 »), puis
  « D18.7b reste à faire » (« D18.7b EST FAITE » suivait, avec ses quatre
  attendus tenus et 1 808 tests verts). Avant d'ouvrir un chantier nommé par un
  document, `grep` « EST FAITE », « CLOS » et le nom de l'élément dans le même
  fichier. **Et cela vaut d'abord pour `docs/INDEX.md`**, qu'on lit en premier :
  ses lignes dérivent derrière les cahiers des charges qu'elles citent. Le 12/09,
  TROIS de ses éléments étaient déjà faits — les boutons sous 18 px, D18.7b, et
  A4 dont le § 4.4 publiait trois mesures. Avant de travailler une ligne de
  l'INDEX, ouvrir le § qu'elle nomme.
- Une règle posée dans l'APPLICATION seule ne vaut pas pour l'EXPORT : le rendu
  hors-ligne construit SON graphe dans `interchange` (`OfflineReconstruction.cpp`),
  et l'export de l'application passe par lui. Le 15/09 (D332), la déclaration des
  contrôleurs GM (CC 74 → coupure) vivait dans `MainComponent` : le test du graphe
  était vert, et l'export ne bougeait pas d'un hertz (397 / 507 avant comme
  après). Tout ce qui conditionne le SON d'une piste se pose dans `interchange`
  et s'appelle des deux chemins — c'est le contrat de D21 (« les deux chemins
  rendent le même son ») — et la mesure qui le prouve est un EXPORT, pas un test
  du graphe.
- `grep -n motif $F` avec `$F` VIDE lit l'ENTRÉE STANDARD et ne rend jamais la
  main : le 15/09, un `F=$(grep -rln … | head -1)` sans résultat a laissé un
  `grep` suspendu jusqu'à ce que la garde mémoire le tue, une heure plus tard.
  Toute variable de fichier issue d'une recherche se teste (`[ -n "$F" ]`) avant
  de servir d'argument.
- `VSM_TEXTES_LISTE` (et les autres relevés du démarrage) s'exécutent AVANT
  les gestes de `VSM_GESTE_APRES`, alors que la photo est prise APRÈS : le
  26/09 (D414 bis), un relevé a lu « Volume : -0.9 dB » pendant que la photo
  de la même course montrait le fader à -6.0 dB, et j'ai écrit « l'infobulle
  reste figée 1,7 s plus tard » — le geste n'avait pas encore eu lieu. Un relevé
  et une photo d'une même course ne datent pas du même instant : lire le
  journal DANS L'ORDRE (le geste « joué » doit précéder le relevé), et pour un
  relevé après geste, prendre le geste immédiat (`VSM_GESTE_PISTE`).
- Une photo de `VSM_CAPTURE_PANNEAUX` a un fond TRANSPARENT : elle ne peint que
  le contenu, pas le fond de la fenêtre (`Palette::panel`). Le 26/09 (D417),
  les libellés de « Réglages audio » y semblaient gris foncé sur fond sombre, et
  une phase de contraste s'écrivait déjà ; leurs pixels étaient clairs, sur de
  l'alpha 0 que la visionneuse rendait en gris. Avant de juger un contraste sur
  une photo de panneau, la recomposer sur `0x1f1f24` (ou lire l'alpha).
- Un REMPLACEMENT GLOBAL fait APRÈS avoir inséré une fonction qui contient le
  motif remplacé réécrit la fonction elle-même : le 15/09 (D335), `sortieChassee`
  appelait `lastOutBefore`, puis un `replace("lastOutBefore(passages, ",
  "sortieChassee(passages, ")` sur tout le fichier l'a fait s'appeler elle-même —
  récursion infinie, les tests core morts en segfault au 82e test, et l'export
  qui ne chasse pas restait vert. Remplacer AVANT d'insérer, ou exclure l'insert ;
  et lancer les tests core après tout changement du séquenceur, pas seulement
  ceux dont on croit avoir touché le chemin.
