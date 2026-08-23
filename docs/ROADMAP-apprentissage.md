# Feuille de route — Apprentissage (phases A0 à A5)

Prolonge [`ROADMAP-fusion.md`](ROADMAP-fusion.md) (phases 8-11, terminées).
Le cahier des charges est dans [`CDC-apprentissage.md`](CDC-apprentissage.md) ;
cette feuille ne répète pas ses exigences, elle les ordonne et fixe les
critères de clôture.

**L'ordre n'est pas chronologique par confort : il va du gain le plus sûr au
plus risqué.** A2 (batterie) est indépendante et son juge existe déjà ; A1
(classifieur) est bon marché et comble un trou mesuré ; A3 (estimateur) porte
le risque des pistes 10.1/10.2, déjà rejetées deux fois sous d'autres formes —
elle passe en dernier et peut être rejetée à son tour, chiffres à l'appui.

---

## Phase A0 — Corpus et infrastructure

Le socle de tout le reste : sans corpus regénérable, aucune mesure d'A1-A3
n'est rejouable.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A0.1 | Générateur de corpus (`vsm_corpus.py`) : tirage dans `SearchProfile`, rendus par `VsmEngine`, caractéristiques v2, manifeste | une commande regénère le corpus des 15 machines de recherche ; coût mesuré et publié |
| A0.2 | Déterminisme des données | deux exécutions même graine → caractéristiques identiques au bit près (testé) |
| A0.3 | Péremption par empreinte | changer artificiellement une empreinte marque le corpus périmé et fait refuser le modèle dérivé (testé) |
| A0.4 | Augmentations seedées (EQ, réverbération courte, compression, bruit, fuite, désaccord) | chaque augmentation inscrite au manifeste ; A/B possible avec et sans |

**Critère de phase** : le corpus complet se regénère sans intervention en
moins d'une nuit, et sa taille/coût réels remplacent les ordres de grandeur
du CDC §3.

## Phase A1 — Le classifieur de machine

| Étape | Contenu | Terminé quand |
|---|---|---|
| A1.1 | Entraînement + banc tenu à l'écart | vraie machine top 3 ≥ 95 % (cas indistinguables comptés à part) ; deux entraînements → mêmes verdicts |
| A1.2 | Abstention | stems acoustiques (violon, piano) → « aucune machine », avec la mesure qui le motive ; jamais un score confiant hors du parc |
| A1.3 | Intégration en présélection dans `reconstruire.py` | sur le banc 10.3 (4 cibles) et les vérités terrain : même gagnant final que la recherche complète ; temps de présélection avant/après publié |
| A1.4 | Rejouer House Of God | verdicts finaux inchangés ou meilleurs ; le rapport porte version du modèle et décisions imprimées |

**Critère de phase** : la présélection deux passes (174 s) est remplacée ou
réduite par le classement quasi gratuit, **sans qu'aucun verdict du banc ne se
dégrade**. Si un verdict se dégrade, le classifieur nourrit la shortlist au
lieu de la remplacer, et c'est écrit.

## Phase A2 — Les gabarits de batterie appris

Indépendante d'A1/A3 ; peut commencer dès A0.1 (les frappes générées suffisent).

| Étape | Contenu | Terminé quand |
|---|---|---|
| A2.1 | Corpus de frappes : TR-808/909 + kits sampler, variations, superpositions construites | frappes étiquetées par construction, cas mêlés inclus (les tueurs des architectures 1-3) |
| A2.2 | Nommage des gabarits par modèle appris dans `vsm_drumkit.py` | motif-vérité « double-croche » : charleston > 24/32 (contre 8/32), kick et snare toujours 8/8, zéro frappe inventée |
| A2.3 | Contretemps et familles supplémentaires | motif « contretemps » > 14/16 ; toms/percussions/claps nommables (au-delà de 3 familles) |
| A2.4 | Rejouer House Of God | co-frappes toujours 0 ; ≥ 6 pièces ; l'écoute ne révèle pas de régression |

**Critère de phase** : la limite écrite au §9.5 (« gabarits appris, pas un
seuil mieux choisi ») est levée, motifs-vérité à l'appui. En cas d'échec, le
nommage actuel reste en place et l'échec est documenté avec ses chiffres.

## Phase A3 — L'estimateur de paramètres

La plus risquée ; n'entre en chaîne que si son A/B global est positif.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A3.1 | Estimateur avec incertitude par axe | couverture mesurée : vraie valeur dans [estimation ± incertitude] ≥ 98 % par axe sur données tenues à l'écart ; les axes sous le seuil gardent leur plage déclarée |
| A3.2 | Resserrement de l'espace cherché | A/B à budget égal sur le banc : distances ≤ chaîne actuelle ; chaque resserrement imprimé |
| A3.3 | Candidat bon marché (une évaluation) | sur les cibles « faciles » : recherche raccourcie, verdict imprimé avec les deux distances |
| A3.4 | A/B global | médiane des distances finales ≤ chaîne actuelle avec ≤ la moitié des évaluations ; aucun verdict de vérité terrain dégradé |

**Critère de phase** : A3.4, sans appel. S'il n'est pas atteint, le code est
conservé désactivé et documenté — le sort exact des étapes 10.1 et 10.2, et
c'est un résultat, pas une honte.

## Phase A4 — Intégration, rapport, repli

| Étape | Contenu | Terminé quand |
|---|---|---|
| A4.1 | Modèles versionnés, chargés et vérifiés (empreintes) au démarrage | modèle absent ou périmé → repli chaîne actuelle, dit et daté |
| A4.2 | `rapport.json` : versions de corpus/modèles, abstentions et resserrements avec motifs | deux exécutions complètes → rapports identiques à la 4e décimale (le déterminisme de House Of God, préservé) |
| A4.3 | `--sans-apprentissage` | reproduit exactement la chaîne d'aujourd'hui ; sert de témoin dans tous les A/B |

## Phase A5 — Validation : un morceau de musique classique

Le banc final, choisi parce qu'il est le plus hostile au parc (couverture,
séparation, polyphonie). Les attentes sont écrites **avant** la mesure au
CDC §10 — cette phase vérifie qu'elles se réalisent, pas qu'on les contourne.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A5.1 | Reconstruction de bout en bout (`reconstruire.py`) sur l'enregistrement choisi | la chaîne va au bout ; `rapport.json` et `comparaison.wav` produits |
| A5.2 | Vérification des abstentions | les sources acoustiques passent par abstention → sampler, chacune avec sa mesure ; aucune machine de caractère « confiante » sur un instrument d'orchestre |
| A5.3 | Écoute et correction dans le DAW | projet ouvert (11.1), A/B contre l'original (11.2), notes douteuses marquées (11.3) ; ce que l'oreille trouve que le rapport ne disait pas devient une entrée de ce tableau |
| A5.4 | Bilan | distances par stem publiées, passes documentées comme pour House Of God ; les leçons remontent dans les CDC |

**Critère de phase** : un rapport qui dit vrai et une écoute qui ne surprend
pas son lecteur. Une distance élevée honnêtement expliquée vaut mieux qu'une
petite distance obtenue en trichant sur ce qu'on mesure.

---

## Risques nommés, et leur parade

| Risque | Parade |
|---|---|
| Écart de domaine (corpus sec vs stems réels) | augmentations (A0.4) ; validation uniquement sur vérités terrain et morceaux réels |
| Modèles périmés quand une machine évolue | péremption par empreinte (A0.3), refus au chargement |
| Classifieur confiant hors du parc | abstention obligatoire, testée sur sources acoustiques (A1.2) |
| Timbres indistinguables | exclus du dénominateur, comptés à part (CDC §1.4) |
| L'estimateur répète le sort de 10.1/10.2 | ordre des phases ; A3.4 sans appel ; rejet documenté = résultat |
| Dérive du déterminisme (bibliothèques ML) | versions épinglées, graines fixées, identité des verdicts testée (A0.2, A1.1) |
| Séparation pop sur un orchestre (A5) | attendu et écrit d'avance ; le rapport le dit au lieu de le maquiller |

## Invariants — ceux de la fusion, plus quatre

```
[ ] Tous les invariants de ROADMAP-fusion §7, mot pour mot
[ ] Aucun modèle appris ne produit ni ne modifie de l'audio entendu
[ ] Sans modèle sur disque, la chaîne = la chaîne d'aujourd'hui, à l'identique
[ ] Toute décision d'un modèle est imprimée avec sa mesure, et versionnée
    dans rapport.json
[ ] Corpus, entraînement, inférence : seedés, regénérables, rejouables
```
