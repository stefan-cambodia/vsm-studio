# Index des documents — ce que chacun commande, et où en est le projet

*Écrit le 10/09/2026. Ce document ne décide rien : il **oriente**. La règle du
dépôt ne change pas — les feuilles de route et les cahiers des charges sont les
critères d'acceptation et l'ordre de marche, et c'est dans EUX que se tranchent
les choix, jamais ici. Quand cet index et un document se contredisent, c'est le
document qui a raison et cet index qui est périmé.*

**Une page consultable en donne le même contenu** :
[`ordre-de-marche.html`](ordre-de-marche.html), publiée à
<https://claude.ai/code/artifact/549ce2bb-a1e1-4a98-b0ac-3b4921bb42f1>. Elle est
un RENDU de ce fichier, pas une seconde source : deux copies du même contenu
divergent toujours, et c'est ce fichier-ci qui a raison. Qui modifie l'un
refait l'autre.

---

## 1. Comment lire le dépôt

Trois niveaux, et ils ne se confondent pas :

| Niveau | Ce que c'est | Qui commande |
|---|---|---|
| **Feuilles de route** (`ROADMAP-*.md`) | l'ordre de marche, phase par phase, avec les mesures qui les ont tranchées | elles décident **quoi faire ensuite** |
| **Cahiers des charges** (`CDC-*.md`) | ce qu'un chantier doit tenir, écrit **avant** de l'écrire | ils décident **quand c'est fini** |
| **Références** (`ARCHITECTURE.md`, `MODE-EMPLOI.md`, `GUIDE-*.md`) | l'état des lieux et les modes d'emploi | ils **décrivent**, ils ne prescrivent pas |

Deux axes de travail coexistent, et l'un passe devant l'autre quand il faut
choisir : **le DAW d'abord**, la chaîne d'analyse ensuite.

---

## 2. Index

### Feuilles de route

| Document | Ce qu'il commande | État |
|---|---|---|
| [`ROADMAP-daw.md`](ROADMAP-daw.md) (12 780 l.) | faire du démonstrateur un logiciel de studio « digne de Cubase, Live, FL Studio » | **90 titres de phase** écrits (D0 → D93), tous clos — relevé du 11/09 ; l'index en comptait 62 depuis D62 ; le document est en régime d'**audits successifs** (D11, D13-D16, D22-D25, D29-D40, D55, D60-D62) |
| [`ROADMAP-fusion.md`](ROADMAP-fusion.md) (3 747 l.) | la chaîne d'analyse : d'un enregistrement à un projet jouable | phases 8 à 11 **toutes closes** ; le travail vit désormais dans les § 5 *bis* → *quaterdecies* (les pannes muettes, les hypothèses H13 à H25) |
| [`ROADMAP-interop.md`](ROADMAP-interop.md) (276 l.) | Python ↔ CLAP ↔ formats d'échange | **P0 à P10 tous faits** — c'est la seule feuille de route entièrement terminée |
| [`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md) (1 162 l.) | les modèles appris (classifieur de machine, gabarits de batterie, estimateur) | A0, A1, A2, A4 faites ; **A3 refusée par la mesure** ; **A5 close sauf l'écoute** ; **A6 en cours** : le corpus des 59 candidates mélodiques, repris le 11/09 |

### Cahiers des charges

| Document | Le chantier | État |
|---|---|---|
| [`CDC-machines-manquantes.md`](CDC-machines-manquantes.md) (2 681 l.) | élargir le parc de machines, et le prix de chacune | § 1-8 livrés ; **21 hypothèses H tranchées**, 20 succès, 1 échec assumé (la guitare électrique, non écrite) ; § 33 donne le **coût CPU mesuré des 64 machines** |
| [`CDC-detection-multipiste.md`](CDC-detection-multipiste.md) (889 l.) | la parité des pistes : un original à N parties doit donner N pistes | § 4 ordre de marche : **1, 2, 4, 5, 6, 7 faits** ; le § 4.3 se croit ouvert alors que l'écran du rapport porte désormais densités, partage et avertissements |
| [`CDC-separation-par-synthese.md`](CDC-separation-par-synthese.md) (735 l.) | la boucle résiduelle : soustraire ce qu'on sait rendre, reséparer le reste | câblée et testée de bout en bout ; § 5 porte les décisions tranchées en écrivant, § 6-7 la campagne R1 |
| [`CDC-banc-synthetique.md`](CDC-banc-synthetique.md) (715 l.) | mesurer la chaîne sur des morceaux à vérité connue | **campagne S1 mesurée** (20 morceaux) — et elle a réfuté la chaîne à tous les étages ; § 7 liste ce que le banc ne sait pas encore faire |
| [`CDC-multisample.md`](CDC-multisample.md) (610 l.) | `vsm.multisample` : l'acoustique par échantillons | M1 (profil piano) et M2 (import SoundFont) livrés |
| [`CDC-etirement-temporel.md`](CDC-etirement-temporel.md) (516 l.) | qu'un clip audio suive le tempo | D12 livrée (WSOLA + vocodeur de phase) ; § 9 porte D54, la transposition |
| [`CDC-apprentissage.md`](CDC-apprentissage.md) (369 l.) | ce que les modèles appris doivent tenir | rempli par `ROADMAP-apprentissage.md` |
| [`CDC-nouvelle-machine.md`](CDC-nouvelle-machine.md) (259 l.) | la liste de contrôle d'une machine neuve | **permanent** — ses cases se cochent à chaque machine, jamais dans le document |
| [`CDC-import-daw.md`](CDC-import-daw.md) (170 l.) | importer un projet FL Studio / Ableton / Cubase | livré (D7) ; Cubase reste hors de portée, et le document dit pourquoi |

### Références

| Document | Ce que c'est |
|---|---|
| [`../ARCHITECTURE.md`](../ARCHITECTURE.md) (4 722 l.) | la conception complète, machine par machine, avec les mesures |
| [`../README.md`](../README.md) (702 l.) | la porte d'entrée : quoi, comment compiler, comment s'en servir |
| [`MODE-EMPLOI.md`](MODE-EMPLOI.md) (1 130 l.) | le manuel de l'application |
| [`GUIDE-ajout-machine.md`](GUIDE-ajout-machine.md) (508 l.) | comment on écrit une machine, pas à pas |
| [`../analyse/PONT-VSM.md`](../analyse/PONT-VSM.md) (102 l.) | le protocole du pont Python ↔ moteur |

---

## 3. Où en est le projet, en chiffres

| | |
|---|---|
| **Machines** | **63**, plus la tonalité d'essai — 64 identifiants au registre ; **16 effets** d'insert ; **63 façades** dessinées |
| **Tests** | 1 954 côté moteur — 1 291 audio, 330 core, 297 interchange, 25 clap, 11 panels — et **172 côté Python** (relevé du 10/09, D76) |
| **Lignes** | `analyse/` 132 k · `audio/` 69 k · `app/` **33 k** · `interchange/` 20 k · `core/` 16 k |
| **Vérification** | `ruff check .` (lint) et `mypy analyse tools` (types, 104 fichiers) |

**Le déséquilibre qui ouvrait `ROADMAP-daw.md` s'est refermé.** Le § 1.1 mesurait
`app/` à **7 303 lignes** contre 95 837 pour `analyse/` — « le parc a été nourri
sans relâche ; l'atelier est resté un démonstrateur ». `app/` en fait
aujourd'hui **32 873**, soit **4,5 fois plus**, et les cinq critères du § 2
(sauvegarder, faire entrer du son, arranger, mixer, exporter) sont tenus —
posés par D0 à D10, puis **vérifiés sur un projet réel** par D45 (ouvrir et
réenregistrer une reconstruction ne perd rien) et D46 (l'export du DAW et
`vsm-render` rendent le même son).

**Ce que la mesure a refusé, et qu'il ne faut pas rouvrir sans raison neuve :**

- **l'estimateur de paramètres appris** (A3) — le fossé de domaine ; code
  conservé et documenté ;
- **la guitare électrique modélisée** (H10) — mesurée, écart insuffisant, la
  machine ne sera pas écrite ;
- **la reconstruction de la voix** — reportée telle quelle, et c'est le plus
  honnête ;
- **l'étirement temporel comme besoin de mesure** (D11.8) — c'est un besoin de
  production, tranché et livré séparément en D12.

---

## 4. Liste de travaux

Ordonnée : ce qui ment d'abord, ce qui manque ensuite, la documentation en
dernier. Chaque élément porte sa source ; **aucun n'est inventé ici**.

### A. Ce qui est nommé, mesuré, et pas encore corrigé

| # | Travail | Source | Chiffre en main |
|---|---|---|---|
| ~~A1~~ | ~~Deux cellules de façade n'ont aucune hauteur~~ — **CLOS par D63** : la façade réclame la hauteur de sa grille et défile en dessous | `ROADMAP-daw.md` D63 | 2 boutons à 0 px → **0** |
| ~~A2~~ | ~~Quatre boutons du Minimoog sous 18 px~~ — **CLOS par D64** : son mélangeur est une bande verticale sur la machine d'origine, le dessiner en carré coûtait 11 px de large | `ROADMAP-daw.md` D64 | 23 → 4 → **0 / 49** sous 18 px |
| ~~A5~~ | ~~Quatre façades sur 63 gardent des boutons sous 18 px~~ — **CLOS par D70**, et le chiffre de D67 était faux : le sampler comptait **64** boutons sous le plancher et non 4, soit **98** pour le parc et non 37. Aucune voix n'a été empilée : c'est la grille INTERNE du bloc qui change, une colonne ne portant jamais deux pièces | `ROADMAP-daw.md` D64 → D70 | 98 boutons sous 18 px → **0** ; 4 façades → **0 / 63** |
| A3 | **La molette de hauteur manque à six machines** où un musicien plie la note (e-piano, clavinet, guimbarde, vielle, mandoline, kalimba) — un travail par machine, sur son modèle physique. *La flûte, septième de la liste d'origine, n'est pas dans le parc : hors build, résultat négatif assumé* | `ROADMAP-daw.md` D26 | 42 machines la tiennent, **6** la devraient |
| A4 | **`--batterie-par-piece` n'a pas de mesure de distance** — l'option est câblée et testée, son effet sur la fidélité n'est pas chiffré | `CDC-detection-multipiste.md` § 4.4 | — |

| ~~A7~~ | ~~L'écran de rapport d'ouverture ne se photographie qu'une fois sur sept~~ — **CLOS par D72** : il passe de la boîte modale au volet de rapport, qui est dans l'autoportrait. Il y gagne le défilement, le bouton Copier et la réouverture par le menu | `ROADMAP-daw.md` D71 → D72 | 1/7 → **10/10** captures, pixel du volet lu sur l'image |
| ~~A8~~ | ~~Les deux lecteurs d'un projet ne numérotent pas les pistes pareil~~ — **CLOS par D75** : les deux fabriquent leurs phrases par les mêmes fonctions de `interchange/`, comptent depuis 1 comme la liste de pistes, et ne disent plus « silencieuse » d'un dossier ni d'une piste désactivée. L'application comptait déjà des deux façons à elle seule, en recopiant les phrases du chargement | `ROADMAP-daw.md` D71 → D75 | 9 et 4 lignes, 8 numéros faux → **5 et 5, diff vide** |
| ~~A10~~ | ~~Enregistrer détruirait ce qu'on n'a pas pu ouvrir~~ — **CLOS par D76, et c'était pire que le soupçon** : huit cas mesurés, dont trois ordinaires — ajouter ou supprimer une piste recréait TOUTES les machines, réglages remis à l'usine (et l'annulation passe par le même chemin : lu, pas mesuré, voir A11) ; le premier Ctrl+S effaçait les échantillons d'un sampler ; l'export du DAW rendait muette la voix de `sky-v4`, ce qui contredit D46. Les machines dont la piste n'a pas bougé ne sont plus recréées, un réglage sans machine vivante est gardé, et la capture prend les échantillons | `ROADMAP-daw.md` D75 → D76 | 4/9 réglages perdus → **0/9** dans 6 cas ; voix exportée silencieuse → **−6,44 dBFS** |
| ~~A11~~ | ~~Ctrl+Z n'agit que si le piano roll a le focus~~ — **CLOS par D79** : Ctrl+Z et Ctrl+Maj+Z passent par le même `pianoRoll_.undo()`/`redo()` que le menu, d'où que vienne la touche ; et le cinquième chemin de perte de D76 est joué — annuler, avant D76, perdait 4 réglages sur 9. `MainComponent::keyPressed` ne connaissait pas `EditUndo`. *Rectifié par D79 : cette ligne disait aussi que le menu Édition n'a ni « Annuler » ni « Rétablir » ; c'est faux, il les porte — c'est le menu contextuel du piano roll, et la recherche ne portait que sur `MainComponent.cpp`.* Trouvé parce que le banc de D76 n'a pas su annuler : son cas « annulation » a enregistré 3 pistes, c'est-à-dire un second ajout | `ROADMAP-daw.md` D76 → D79 | Ctrl+Z hors du piano roll : `VSM_TOUCHE : touche inconnue ou sans commande` |
| A9 | **L'interface n'est traduite qu'en partie** — la barre de menus l'est entièrement (227/227), mais les panneaux du dock, les fenêtres flottantes et les phrases fabriquées dans `interchange/` restent en français. Une chaîne sans traduction ressort en français, jamais vide | `ROADMAP-daw.md` D73 → D74 → D77 → D78 → D80 → D81 → D82 → D83 → D84 → D85 → D86 → D87 → D88 → D89 → D90 → D91 → D92 → D93 → D94 → D95 → D96 → D97 → D98 → D99 → D100 → D102 → D103 → D104 → D105 → D107 → D108 → D109 → D110 → D111 → D112 → D113 → D114 → D115 → D116 → D117 → D118 → D119 → D120 → D121 → D122 → D123 → D124 → D125 → D126 → D128 → D129 | **Traduit, et vérifié sur ce qui s'affiche** : la barre de menus (35 entrées identiques FR / EN, toutes neutres — D80, D81 ; le « 227 / 227 » de D73 était faux), les noms des gestes (D82), les menus de l'arrangement (D83), le volet du rapport d'ouverture et les phrases du moteur qu'il montre (D84, D89), les Préférences (D85), l'assemblage des prises et l'ordre de jeu (D86), les raccourcis, les associations MIDI et le navigateur (D87, D88), **l'export audio et l'export par stems**, fenêtres d'options et comptes rendus (D90 : lignes identiques FR / EN 29 → 10 sur deux projets, toutes chemins ou lignes vides ; fichiers rendus identiques octet pour octet). Le bouton d'annulation des boîtes dit « Cancel » : sept disaient « Undo », le sens du menu Édition (D90, `trSelon`). **Les boîtes de dialogue** — aller à la mesure, programme MIDI, presets de piste et d'effet, repères, clips, renommage en série, report en audio, dépôt d'un fichier — sont traduites et photographiées dans les deux langues (D91 : français à 0 pixel du témoin pour 10 sur 11, l'onzième ne différant que par un chemin ; `VSM_MENU_CONTEXTE` et `VSM_DEPOSER` ouvrent ce que la barre de menus n'atteint pas). Table à **1 366** paires, **110** modèles de phrases (D115). La bascule en direct rend la même image que le démarrage (D78). **Le rapport de reconstruction** est traduit à l'affichage, phrases de la chaîne comprises (D92 : lignes identiques FR / EN 97 → 30 sur quatre rapports, toutes dossier, lignes vides ou « stem → machine · distance » ; les nombres identiques ligne pour ligne). **Le rapport d'import d'un autre DAW**, ses échecs compris (le `.cpr`, les messages du lecteur et de la décompression), est traduit à l'affichage, et **les deux rapports suivent la bascule en direct** : chaque client du volet refait le sien (D93 : lignes identiques FR / EN 43 → 13 sur les trois projets d'épreuve et un `.cpr`, toutes noms de format ou lignes vides ; rapport de reconstruction basculé identique à D92 lancé en anglais, au caractère près). **Les panneaux toujours visibles** — barre de transport, liste des pistes, piano roll et sa barre, mixeur, voies de CC, de tempo et d'automation, arrangement, liste d'événements, rack, spectre — sont traduits, infobulles comprises, et suivent la bascule (D94 : chaînes françaises atteignant l'écran sans `tr()` 396 → 273, compte de `tools/inventaire_langue.py` ; textes identiques FR / EN 146 → 103, tous neutres ou données sauf quatre noms de machines ; français identique au témoin, liste et image — hors la charge CPU). **Les boîtes du montage** — piste verrouillée, jonction ou changement de piste refusés, transposition hors plage, tempo d'une boucle, et les actions d'un clip audio (découper, rogner, transcrire, créer) — sont traduites (D95 : les 47 chaînes du lot ; une boîte se lit au moment où elle est demandée, `VSM_BOITE`, parce qu'une boîte ouverte au démarrage d'un banc n'est plus là à la photo sous un écran verrouillé ; français identique au témoin). **Les opérations de piste** — geler, reporter la piste ou la sélection, éclater par hauteur, publier les sorties — répondent dans la langue de l'interface, et sous le nom de leur menu (D96 : « Split by pitch » partout où « Explode by pitch » était ; français identique au témoin). **Les boîtes du projet** — statistiques, modèle, ouverture et enregistrement, import et export MIDI, audio non chargé, locateurs, titres des sélecteurs de fichier — sont traduites (D97 : −42 à l'inventaire, vérifiés sans faux TABLE ; les statistiques traduites à la source, parce que personne ne relit leur ligne au terminal). **Le groove et les presets de piste** répondent dans la langue de l'interface (D98 : −17, vérifiés sans faux TABLE). **Le navigateur** aussi (D99 : −18, huit affichages de plus que l'inventaire ne voyait pas, et les erreurs JSON et WAV qu'il montre ; commande de banc `VSM_NAVIGATEUR`). **Les quinze fenêtres flottantes** ont leur titre dans la langue, reposé au changement de langue, sans perdre la position retenue sous leur titre français (D100). **Les plugins** aussi (D102 : −21 à la règle stricte, −30 à la large ; commande de banc `VSM_PLUGIN`, et les fenêtres de choix lues quand elles sont demandées, `VSM_CHOIX`). **Les noms des machines** aussi, dans la liste des pistes, le rack et le navigateur, et ils suivent la bascule (D103 : 47 clés ; `--machines` compte les noms sans clé, 48 → 0 ; « Sampler (8 emplacements) » était faux, il en a 16). **La reprise après une session interrompue** aussi — la boîte qu'on lit quand le logiciel vient de tomber (D104 ; `demanderOuiNon()` lit une question à deux boutons quand elle est demandée). **Les dernières boîtes des opérations de piste** — prise retirée, signature, report en audio, import audio, ce qui vient d'être joué — aussi (D105 ; 16 messages de la même série ne vont qu'au terminal : l'inventaire suit désormais la phrase jusqu'à son `fputs`, D106). **Les boîtes du démarrage** (raccourcis et associations MIDI illisibles), l'import audio dans un projet jamais enregistré, les titres des sélecteurs de fichier et le menu des cibles d'une association MIDI aussi ; **une piste ajoutée prend son nom dans la langue de l'interface** (« Track 2 »), qu'elle garde ensuite (D107). **L'original de l'écoute A/B** aussi — la boîte d'un enregistrement illisible, les erreurs du décodeur (qui retrouvent leurs accents), la ligne du menu Fichier (D108 ; commande de banc `VSM_FICHIER`). **Les noms des pièces de batterie** aussi — sur le clavier du piano roll, et dans le nom des pistes qu'« Éclater par hauteur » crée (D109). **Les boîtes du départ d'une prise** aussi — deux pistes audio armées, projet jamais enregistré — et les erreurs du moteur audio et du rédacteur de prise qu'elles montrent, qui retrouvent leurs accents (D110 : 1 boîte sur 1 dans chaque langue, français identique au témoin ; geste de banc `VSM_GESTE_PISTE=armer`). **L'erreur d'« Aller à la mesure » et les deux boîtes d'« Assembler les prises »** aussi, et les réserves d'effet d'un bus de départ — déjà traduites à l'affichage depuis D89, mais composées par `+` dans le source, si bien que l'inventaire les comptait encore (D111 : donnée et affiché identiques au témoin octet pour octet, dans les deux langues). **Les boîtes de l'enregistrement** aussi — file de capture débordée, mesure de latence (pas d'entrée, rien de retrouvé, résultat), disque trop lent —, qu'aucun banc n'atteignait sans jouer de son : une fonction chacune, montrée par le geste de banc `VSM_BOITE_ESSAI` avec des chiffres fixes (D112 : 5 boîtes sur 5 par langue, mêmes nombres en anglais et en français). **Les raisons du balayage des plugins** aussi, et celles des hôtes CLAP (D113, voir A12). **La reconstruction lancée depuis l'application** aussi — « Reconstruction indisponible » et ses raisons (écrites par `ReconstructionChain`, traduites à l'affichage), le volet, le lanceur, le transcripteur, le texte d'attente des notes (D114 : la boîte par `VSM_BOITE_ESSAI=indisponible`, deux cas, raison vraie ; le volet et le lanceur vérifiés par le code) ; et dans la boîte du dépôt d'un fichier, où elles restaient françaises (D124, lu sur `VSM_BOITE`). **À l'inventaire strict, il ne reste que 7 chaînes, et aucune ne s'affiche.** **Ce que le compte strict ne voyait pas** non plus : « RÉGLAGES », la seule des 163 sérigraphies de section de façade qui ne soit pas anglaise (elle vit dans `panels/`, hors du champ de l'inventaire), et deux phrases d'état du piano roll de l'angle mort (D115 : « SETTINGS », « 12 note(s) softer than 64 » ; geste de banc `VSM_MENU_CONTEXTE=pianoroll:…`). **L'angle mort de l'inventaire** est mesuré (D101) : la règle stricte laisse passer au moins 33 chaînes françaises, nommées — ÉCRAN réel ≥ 159 à D101, ≥ 55 après D108 (ÉCRAN 45 à l'inventaire, 10 chaînes de l'angle mort) ; `--regle=large` les montre, avec 20 faux (unités, mots anglais). **Reste** : `MainComponent` hors du montage, des pistes, du projet, du groove, du navigateur et des plugins (5 chaînes à l'inventaire depuis D114 — 34 avant D110 —, aucune ne s'affiche : un jeton de banc, le mot que le volet cherche pour colorer, trois chemins de fichiers), ~~10 des 33 chaînes de l'angle mort~~ (*toutes traitées au 11/09 — D102 à D115 —, ou ne s'affichant pas : les deux « restaurations de preset » de D101 vont au terminal seul*), ~~un titre « Projet jamais enregistré » écrit sans `tr()`~~ (traduit par D110, le dernier des trois faux TABLE de D96), ~~des infobulles françaises écrites sans leurs accents~~ (*D118 : 60 infobulles distinctes relevées à l'écran en anglais, aucune française ; panneaux fermés, façades et boîtes non couverts* — *D128 : les panneaux fermés et six fenêtres flottantes relevés, 1 infobulle de plus et 53 textes de plus, aucun français ; restent non lues les façades, la fenêtre des tronçons (qui ne s'ouvre pas sans prise) et la fenêtre flottante de l'arrangement* — *D129 : les façades des 64 machines compilées relevées à l'écran, 357 infobulles distinctes, aucune française ; restent la fenêtre des tronçons et celle de l'arrangement*) ; ~~les couches audio, plugins et reconstruction (15)~~ (*au 11/09, l'inventaire strict n'y trouve plus que le nom du fil de balayage des plugins, qui ne s'affiche pas — D110, D113, D114*) ; ~~les réserves d'un preset appliqué à la main~~ (traduites depuis D89-D90 — VÉRIFIÉ au banc par D116, un preset abîmé appliqué en anglais) ; ~~le message d'import audio~~ (D105) ; ~~les raisons pour lesquelles la reconstruction est indisponible~~ (traduites à l'affichage par D114) ; ~~« RÉGLAGES », titre de section des façades~~ (D115) ; deux textes aux retours à la ligne forcés (D91) — *D117 : la boîte des mesures d'un clip n'a plus de mot orphelin ; le renommage garde le motif entier mais coupe encore l'exemple de résultat : `AlertWindow` choisit sa largeur d'après le texte, et trois gestes mesurés n'y suffisent pas* — *clos par D121 : les deux boîtes passent par `BoiteLisible`* ; ~~la boîte « Chaîne d'analyse »~~ (un dossier de chaîne invalide ; titre, raison et remède restaient français — un faux TABLE de l'inventaire, trouvé par D125 —, *traduits par D126, avec les deux lignes « ↳ » du menu Fichier qui montrent la même raison*) |
| ~~A12~~ | ~~Un plugin illisible disparaissait du balayage sans un mot~~ — **CLOS par D113**, et c'était trois défauts : l'erreur de l'hôte jetée par `ignoreUnused`, un enfant tué par un signal que `getExitCode()` de JUCE lit « 0 », et un délai de 20 s que la lecture bloquante du tube rendait lettre morte. L'enfant écrit dans un fichier et finit par une sentinelle | `ROADMAP-daw.md` D113 | 4 faux `.clap` : **0 → 4** fautifs nommés ; balayage **61 s → 22 s** |
| ~~A13~~ | ~~Trois boîtes sur dix coupent mal à 150 %~~ — **CLOS par D121 et D124** : `BoiteLisible` met le texte d'une boîte que l'application construit dans un bloc que JUCE met en page APRÈS avoir fixé la largeur, et une cale impose la largeur de la ligne la plus longue (D121, deux boîtes) ; « Que faire de ce fichier ? », une boîte STATIQUE de JUCE où « minutes. » restait seul, y passe aussi, avec sa variante à deux boutons (D124). Les autres boîtes statiques de l'application n'ont pas été relevées par D119 : pas réputées bien coupées, elles le seront quand un audit les montrera | `ROADMAP-daw.md` D117 → D121 → D124 | 4 photos sur 20 → **1** (D121) → **0** (D124) |
| ~~A14~~ | ~~Chaque fenêtre en plein écran, et retour, d'un simple clic~~ (demande de l'utilisateur, 11/09) — **FAIT par D122** : les quinze fenêtres flottantes annoncent agrandir au gestionnaire de fenêtres (`0x2c` → `0x3c`), une fenêtre agrandie ne retient pas ses limites ; les quatre zones de la fenêtre unique ont un bouton dessiné, agrandir et rendre ; la fenêtre principale avait déjà F11 | `ROADMAP-daw.md` D122 | retour **0 pixel** ; limites retenues inchangées |
| ~~A15~~ | ~~Les boîtes statiques de JUCE — 113 sites — coupent rarement mal, et dessinent leur texte par-dessus l'icône~~ — **CLOS par D126 et D127** — D125 : douze boîtes de la famille photographiées en français et en anglais, une seule mauvaise coupure (« it. » seul dans « The disk did not keep up ») ; JUCE garde, faute d'équilibre à 10 % près, la largeur où les deux dernières lignes sont les PLUS déséquilibrées. **« it. » corrigé par D126** (la boîte par `BoiteLisible`, dont la cale ne franchit pas le plafond de JUCE — 896 px ici : la phrase française y repasse proprement à la ligne). Le texte par-dessus l'icône est un FILIGRANE voulu par JUCE (40 % d'opacité), et il ne nuit pas à la lecture : contraste du texte **6,84:1** sur le « i », **9,19:1** sur le « ! », au-dessus du seuil AA (4,5:1) — le style reste (D127) | `ROADMAP-daw.md` D125 → D126 → D127 | français **0 / 12**, anglais **1 / 12 → 0** ; contraste au pire **6,84:1** |
| A6 | **La barre de transport prend deux rangées sur cet écran** (100 px au lieu de 56) parce qu'il lui faut ~1 400 px pour une seule, et que le plafond est 1 280. Le compromis est assumé — six éléments invisibles auparavant, dont un bouton jamais montré — mais sur un écran plus large la question ne se pose pas | `ROADMAP-daw.md` D68 → D94 | 44 px pris à l'arrangement. *Rectifié le 11/09 : une piste de D94 — deux témoins vides « placés » dans la barre, 216 px — était lue dans le code ; les captures la démentent (« CPU » à 8 px de « 4/4 »). Retirée.* |

### B. Ce que la mesure réclame

| # | Travail | Source | Pourquoi |
|---|---|---|---|
| B1 | **Réengendrer le corpus et réentraîner le classifieur de machine**, puis reprendre **A5.2** — EN COURS (phase A6, 10/09 au soir) | `ROADMAP-apprentissage.md` A6, `ROADMAP-fusion.md` § 5 bis.7 | le modèle du 28/08 connaît **20 des 59 candidates mélodiques**. **Le chiffre de « 43 machines sans empreinte », écrit le 10/09 au matin, était faux** : il comptait comme manquantes `vsm.drums`, `vsm.tr808` et `vsm.tr909`, qui ont leur empreinte dans l'AUTRE modèle (`modeles/frappes.joblib`, phase A2). Le compte juste est **39 mélodiques à faire**, plus `vsm.sampler` qui relève du corpus de frappes |
| B2 | **Écouter** — A5.3 : l'A/B dans le DAW contre l'original, les notes douteuses une par une | `ROADMAP-apprentissage.md` A5.3, `CDC-apprentissage.md` § 10 | « il reste exactement une chose : écouter » ; aucune mesure ne la remplace |
| B3 | **Contre-épreuve de `htdemucs_6s` sur un troisième original** | `CDC-detection-multipiste.md` § 4.2 | −10,4 % sur un morceau qui a guitare et piano, **+3,9 %** sur un morceau qui ne les a pas ; le défaut tient, la réserve est écrite |
| B4 | **Écouter les deux wav de `--voix-tete-choeurs`** — les fuites de réverbération de la tête dans les chœurs ne se jugent qu'à l'oreille | `CDC-detection-multipiste.md` § 4.5 | distance mesurée neutre (+0,05 %) ; la qualité, non |
| B5 | **Un lot de morceaux LONGS (3-5 min) avec des parties qui entrent et sortent** | `CDC-banc-synthetique.md` § 7 | le banc mesure une texture stable de 30 s, la chaîne travaille sur des disques de 4 min |
| B6 | **Le banc ne couvre ni les parties à échantillons ni la voix** | `CDC-banc-synthetique.md` § 7 | deux branches de la chaîne ne sont mesurées par rien |
| B7 | **Terminer le lot forcé de R1** (`r1f-sec`), écrire le § 7.4 et **signer la décision du § 7.5** | `CDC-separation-par-synthese.md` § 7.4-7.5 | la course s'est arrêtée au **6ᵉ morceau sur 10** (dernière ligne du journal : « DÉBUT morceau-0006-g6 », jamais close) ; elle est reprenable — un morceau dont `rapport.json` existe n'est pas rejoué |

### C. Ce que S1 a réfuté, et qui appelle un chantier

La campagne S1 a mesuré la chaîne à vérité connue sur vingt morceaux et **elle
est plus faible que prévu à tous les étages**. Ces chiffres sont le vrai plafond
du projet ; toute autre optimisation est en aval d'eux.

| # | Travail | Chiffre |
|---|---|---|
| C1 | **La séparation de la basse est le premier plafond** | SDR **0,21 dB**, corrélation **0,26** — attendu ≥ 6 dB / ≥ 0,85 |
| C2 | **La transcription est le second** | F1 **0,367** — attendu 0,50 à 0,70 ; et le rappel (0,345) est SOUS la précision (0,426), l'inverse de ce qui était prédit |
| C3 | **La séparation hallucine des sources**, et la chaîne les reconstruit consciencieusement | 19,8 % d'énergie hallucinée — la seule attente tenue, et c'est celle qui fait mal |

**LA BOUCLE RÉSIDUELLE A ÉTÉ ÉCRITE POUR ATTAQUER C1 ET C3, ET R1 L'A MESURÉE :
ELLE EST INERTE.** Zéro soustraction sur les vingt morceaux ; les dix-neuf
batteries candidates corrèlent à leur stem entre 0,001 et 0,049 quand le seuil
publié est 0,5 — réfuté d'un facteur quarante. Au niveau de l'échantillon, un
rendu ne ressemble pas assez à son stem pour en être soustrait, ni sur le banc
ni sur les deux disques. `--residuel` reste une option publiée, mesurée, et
**dite inerte dans son aide**.

L'hypothèse de repli est déjà écrite (§ 7.5 du CDC), pour ne pas être inventée
après : soustraire sur le **module du spectre** plutôt que sur l'échantillon,
la phase du mélange gardée. Elle ne s'écrira qu'une fois le lot forcé terminé.

### D. Documentation qui avait pris du retard — REFERMÉ le 10/09/2026

| # | Ce que le document annonçait | Ce qui est écrit maintenant |
|---|---|---|
| ~~D1~~ | `README` : « 53 machines », « 1 486 tests moteur » | **63 machines**, **1 946 tests** moteur et 168 Python ; « cinquante-deux façades » → soixante-trois |
| ~~D2~~ | `ROADMAP-daw` § 1.1 : `app/` à 7 303 lignes | le tableau reste **daté du 31/08** — c'est sa raison d'être, il justifie une phrase — avec un renvoi à l'état courant (32 873 lignes) |
| ~~D3~~ | `CDC-multipiste` § 4.3 : « le DAW ne montre pas ce que la chaîne sait » | l'écran du rapport porte partage, densités, bloc batterie, distance par stem, métrique et `gate` (D53) |
| ~~D4~~ | `ROADMAP-fusion` § 1 : « 33 machines, 13 effets » | 63 machines, 16 effets d'insert |
| ~~D5~~ | `ROADMAP-daw` D26 : la molette « en commençant par la flûte » | la flûte **n'est pas au registre** — hors build, résultat négatif assumé (`0edd7cc`) ; la phase porte sur six machines |
| ~~D6~~ | `MODE-EMPLOI` : « 53 machines · 13 effets · 1 081 paramètres · 1 486 tests » | **63 · 16 · 1 146 · 1 946**, relus au moteur |

Les cinq occurrences restantes de « 53 machines » et « 1 486 tests » dans les
cahiers des charges sont des **relevés datés** — un jalon, un moteur nommé dans
un A/B, un procès-verbal de recette. Elles restent telles quelles : un chiffre
historique qu'on rafraîchit cesse de dire ce qu'il disait.

### E. Dette d'outillage — REFERMÉ le 10/09/2026

| # | Ce que c'était | Ce qui est fait |
|---|---|---|
| ~~E1~~ | 54 signalements `ruff` dans `analyse/` et `tools/` | **zéro**. 18 corrections automatiques relues avant application ; les 13 `zip()` tranchés **un par un** (11 en `strict=True`, où les deux suites sont construites ensemble et le contrôle ne peut pas se déclencher ; 2 en `strict=False` avec leur raison écrite) ; le reste à la main |
| ~~E2~~ | rien ne lançait `ruff` ni `mypy` | `./verifier.sh` passe les cinq suites du moteur, la suite Python, `ruff` et `mypy` en une commande, et **nomme ce qu'il a sauté** |

**Une correction de lint a cassé le code, et le lint l'a rattrapée.** En
renommant `l` en `ligne` dans `epreuve_parite.py`, le corps de la boucle est
resté sur l'ancien nom : `ruff` a rendu quatre `F821 Undefined name 'l'` avant
qu'aucun test ne tourne. C'est l'argument pour le garde-fou, écrit par le
garde-fou lui-même.

---

## 5. Ce qui n'est pas au programme, et pourquoi

Rassemblé ici pour qu'on ne le repropose pas : chaque refus est mesuré ou
argumenté dans son document.

- **Une API réseau** — le tube JSON suffit (`ROADMAP-interop.md` § 0).
- **Des machines de caractère ajoutées pour la couverture** — elles élargissent
  le catalogue, pas la couverture ; à faire pour le plaisir de jouer, jamais en
  s'en réclamant pour la reconstruction (`ROADMAP-fusion.md` § 6). *L'élargissement
  du vivier reste une demande permanente, à ce titre-là et pas à l'autre.*
- **Un modèle appris de bout en bout (audio → paramètres)** — mené jusqu'au
  bout, mesuré, refusé (`ROADMAP-fusion.md` § 7).
- **Reconstruire la voix par synthèse** (`ROADMAP-fusion.md` § 6).
