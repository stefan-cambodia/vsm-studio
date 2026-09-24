# Le recensement des sources — combien de parties, lesquelles, jouées par quoi

*Chantier ouvert le 24/09/2026. Ce premier état ne contient QUE l'hypothèse et
ses attendus (§ 0), écrits et commités avant toute mesure : le hash du commit
qui introduit ce paragraphe est la preuve d'antériorité. Le cahier des charges
de l'étage (§ 1 et suivants) vient dans un commit séparé, après celui-ci. Rien
n'est implémenté avant la validation de l'utilisateur.*

---

## 0. H37 — un étage isolé peut recenser les parties mieux que la parité d'aujourd'hui, et c'est la séparation qui porte l'essentiel de son erreur

**Numéro.** H37 est le premier numéro libre du dépôt : les numéros vont
jusqu'à H36 (`CDC-machines-manquantes.md`, l'orgue à tuyaux). La série n'est
pas unique — H26 à H31 existent chacun deux fois : dans
`CDC-machines-manquantes.md` d'un côté, dans `CDC-detection-multipiste.md`
(H26, H27, H30, H31) ou `ROADMAP-apprentissage.md` (H28, H29) de l'autre
(relevé par `grep` sur `docs/` le 24/09) — si bien que H37 se cite toujours
avec ce document.

### 0.1 La question

Peut-on estimer, pour un morceau, **le nombre de parties et leur nature** avec
une précision suffisante pour piloter la parité (`CDC-detection-multipiste.md`
§ 0) ? Et **quelle part de l'erreur vient de la séparation** ?

Trois niveaux, mesurés séparément :

- **N1 — rôles** : pièces de batterie (grosse caisse, caisse claire, charleston,
  toms, cymbales), basse, lead, nappe, accompagnement/arpège, piano, voix,
  autre ;
- **N2 — timbres distincts** : combien de sons différents porte chaque stem ;
- **N3 — machine** : la ou les machines du parc les plus proches de chaque
  timbre, en top 1 et top 5.

### 0.2 Ce qui est acquis, et que H37 ne rouvre pas

Chiffres relus dans le dépôt le 24/09, pas recopiés du cahier de mission :

- **S1** (`CDC-banc-synthetique.md` § 6) : séparation de la basse **0,21 dB** de
  SDR (corrélation 0,26), F1 de transcription **0,367**, et la borne de piste
  (vraie machine, vrai patch) plus éloignée du stem séparé que la chaîne dans
  **42 pistes sur 43** — des PISTES, pas des morceaux (S1 en compte vingt).
  « Régler une machine pour ressembler au stem séparé marche mieux que viser le
  stem vrai, parce que le stem séparé n'est pas le stem vrai. »
- **R1** (`CDC-separation-par-synthese.md` § 7) : la boucle résiduelle est
  inerte — zéro soustraction, corrélations batterie/stem de 0,001 à 0,049 pour
  une médiane attendue de 0,4 à 0,8, et inerte même forcée (lot `r1f-13sep`).
  **Aucune soustraction, sous aucune forme, n'entre dans ce chantier.**
- **La parité d'aujourd'hui, comme estimateur de compte** (relue dans
  `reconstruction/travail/s1-sec-banc/rapport.json` et `s1-prod-banc`, stems
  séparés, `--parite`) : le nombre de pistes obtenues rate le nombre de parties
  vraies de **3,8 en moyenne absolue** sur `s1-sec` (**0 morceau exact sur 10**,
  2 sur 10 à ±1), **3,5** sur `s1-prod` (4 sur 10 à ±1). Sur les seules parties
  mélodiques : **3,6** (3 sur 10 à ±1), et 8 erreurs sur 10 sont des
  SOUS-comptes. Sur les pièces de batterie : **1,2**. C'est le chiffre à battre.
- **B9 / H26** (`CDC-detection-multipiste.md` § 12.9-12.13) : le compte de
  **timbres installés** (`vsm_paliers.timbres_installes`, paliers de 5 s)
  tombe sur la composition écrite de *Children* — 4 pour `other` (4 parties),
  1 pour `piano`, **0 pour les deux stems de fuite** — mais aucun DÉCOUPAGE des
  notes (hauteur, temps, les deux) ne sépare les parties. Ce chantier-ci ne
  découpe rien : il COMPTE et NOMME.
- **H25** (`ROADMAP-fusion.md`) : le profil harmonique dépend du registre plus
  que de l'instrument ; un instrument dont les registres se séparent par un vide
  compte pour deux. Un embedding de timbre héritera de ce piège, et il est
  mesuré à part (niveau L1 ci-dessous).
- **A0.4 / A6** (`ROADMAP-apprentissage.md`) : le classifieur de machine (k plus
  proches voisins, 58 machines, 43 descripteurs) fait **83,6 %** de top 1 sur des
  notes isolées du moteur à patchs jamais vus, et **s'abstient 96 à 100 %** du
  temps sur un stem de synthés SÉPARÉ (« un stem séparé est un son qu'aucune
  machine ne produit ») ; entraîné au sec, il tombe à **25 %** de top 1 sur des
  exemples repassés par demucs. N3 est donc attendu BAS après séparation, et la
  mesure sert à localiser la chute, pas à la découvrir.

### 0.3 Les définitions qui rendent les attendus mesurables

**Le compte vrai.** Convention du banc (`CDC-banc-synthetique.md` § 2.4.3,
héritée du CDC multipiste § 6 bis) : **K = K_mél + K_bat**, K_mél le nombre de
parties mélodiques de `verite.json` (basse, accompagnement, mélodie, nappe,
piano-deux-mains, chant — une partie `piano-deux-mains` compte UNE), K_bat le
nombre de pièces frappées de la partie `batterie`. Les trois nombres sont
publiés ; les attendus portent sur K et K_mél.

**Les niveaux de l'ablation**, du plus propre au plus réel. L'erreur de chaque
partie vraie est attribuée au PREMIER niveau où elle apparaît — c'est ce qui
permet d'imputer chaque erreur de comptage à un étage (règle 5 de la mission) :

| niveau | ce que l'étage reçoit | ce qu'il ajoute comme source d'erreur |
|---|---|---|
| **L1** | chaque partie vraie SEULE (`stems-vrais/NN-*.wav`) | **segmentation + embedding** : une partie seule doit donner exactement un timbre ; deux = l'embedding coupe (H25), zéro = il rate |
| **L2** | les stems vrais SOMMÉS comme la séparation devrait les rendre : `bass` ← basse, `drums` ← batterie, `vocals` ← chant, `other` ← le reste (la convention de `--stems-vrais` du banc) | **clustering dans un mélange** |
| **L3** | les stems SÉPARÉS par la chaîne (`htdemucs_6s`, ceux que le banc a gardés) | **séparation** |
| **L4** | le MÉLANGE seul, sans séparation (approche B) | ce que la séparation coûte ou rapporte, vu de l'autre côté |

**Part de l'erreur imputable à la séparation** : (E_L3 − E_L2) / E_L3, E étant
l'erreur absolue moyenne de K sur le lot. Et, partie par partie, la part des
parties perdues ou dédoublées d'abord en L3.

**La vérité de chaque segment (N2).** Un segment porte l'étiquette de la partie
qui y pèse au moins **60 %** de l'énergie des stems vrais ; en dessous, il est
« mixte », exclu de l'ARI/NMI, et sa part est publiée. Le bruit d'HDBSCAN est
COMPTÉ comme une étiquette (sévère : l'exclure gonflerait l'ARI).

**L'appariement grappe ↔ partie (N1, N3)** : algorithme hongrois sur le nombre
de segments partagés. Une grappe sans partie est INVENTÉE, une partie sans
grappe est FONDUE — les mots du banc.

**Les rôles (N1), dictionnaire fixé ici.** Le banc ne connaît que ses rôles, et
la mission en demande d'autres ; la correspondance est une décision, écrite :

| rôle N1 | rôle du banc | remarque |
|---|---|---|
| basse | `basse` | |
| lead | `melodie` | |
| nappe | `nappe` | |
| accompagnement/arpège | `accompagnement` | le générateur arpège 60 % du temps et plaque sinon, SANS l'écrire dans la vérité : les deux sont UN rôle ici, faute de pouvoir les juger |
| piano | `piano-deux-mains` | un RÔLE de jeu (deux mains, deux registres), joué par n'importe quelle machine — pas le timbre du piano acoustique |
| voix | chant (`s2` seulement) | |
| grosse caisse, caisse claire, charleston, toms | `kick`, `snare` + `clap`, `hihat` + `openhat`, `tom` | |
| **cymbales** | **aucune** | le générateur n'en joue pas : **INDÉCIDABLE sur le banc**, dit et non compté zéro (leçon de D265) |

**La bibliothèque de référence (N3).** Le corpus A6 existe (273 061 exemples,
58 machines mélodiques, notes rendues à patchs tirés) et EST une bibliothèque
de notes rendues de chaque machine du parc : il sert tel quel, avec le modèle
adopté (`modeles/classifieur.joblib`). Les MOTIFS (phrases de deux mesures par
machine et par patch) ne sont rendus que si N3 échoue dès L1 — c'est-à-dire si
l'écart entre une note isolée et une phrase est lui-même le fossé.

### 0.4 Les approches comparées, et leurs réglages FIGÉS ICI

Aucune n'est choisie d'avance ; toutes tournent aux quatre niveaux où elles ont
un sens. **Chaque seuil ci-dessous est posé avant la mesure et ne bouge pas
après** ; un seuil mauvais fait tomber l'hypothèse, il ne se corrige pas.

| | approche | sur quoi | réglages figés |
|---|---|---|---|
| **P** | la parité de la chaîne (témoin) | pistes obtenues, relues aux rapports du banc | aucune course neuve pour S1 : les rapports existent |
| **A-pal** | timbres installés (B9), existant | chaque stem | `vsm_paliers` tel quel : 5 s, −50 dBFS, distance L1 entre profils < 0,30, 4 fenêtres |
| **A-grp** | segments + descripteurs + HDBSCAN | chaque stem | segments aux attaques (`librosa.onset.onset_detect`, retour à l'attaque), bornés à **[0,10 s ; 1,00 s]**, un silence de plus d'1 s découpé en fenêtres d'1 s (pour qu'une nappe tenue ait des segments), segments sous **−50 dBFS** écartés et comptés ; descripteur = les **40 grandeurs de TIMBRE** de `vsm_corpus.descriptors` (les 3 conditions de jeu — note, gate, durée — retirées : les garder ferait grouper par HAUTEUR, le piège d'H25), centrées-réduites par les statistiques du corpus A6 ; `sklearn.cluster.HDBSCAN(min_cluster_size = max(5, ⌈2 % des segments⌉), min_samples = 3)` ; une grappe COMPTE si ses segments cumulent **≥ 4 s**. Batterie : les pièces que `--batterie-par-piece` retient déjà, seuils de la chaîne inchangés |
| **A-voix** | voix par registre (notes) | chaque stem mélodique | notes de la chaîne (Basic Pitch 0.4.0, modèle ICASSP 2022) ; registres de `registres_par_vides` ; par registre, le **90ᵉ centile** du nombre de notes simultanées sur les trames de 50 ms où il sonne ; K_mél = somme arrondie |
| **B** | le mélange seul | L4 | segmentation, descripteur, HDBSCAN et seuil de 4 s identiques à A-grp ; une grappe est PERCUSSIVE si la part percussive (HPSS de librosa, marges par défaut) de ses segments dépasse **0,6** en médiane → K_bat ; les autres → K_mél |
| **C** | hybride A + B | — | **n'est écrite QUE si** A et B échouent sur des morceaux DIFFÉRENTS : Jaccard < 0,5 entre les deux ensembles de morceaux où l'erreur sur K dépasse 1. Sinon elle n'a pas d'objet et n'est pas construite |

**Les rôles N1 se déduisent de règles, pas d'un apprentissage** : le banc
n'entraîne rien (`CDC-banc-synthetique.md` § 3, anti-objectif 2). Grappe de
`drums` → la pièce que la chaîne lui donne ; de `vocals` → voix ; de `bass` →
basse. Ailleurs, sur les notes que la chaîne a transcrites dans les segments
de la grappe (durée médiane d, polyphonie moyenne p sur ses trames actives,
hauteur médiane h, ambitus a) : **nappe** si d ≥ 1,0 s et p ≥ 2 ; **piano** si
p ≥ 2, a ≥ 24 et au moins deux registres par les vides ; **basse** si h < 48 et
p < 1,5 ; **lead** si p < 1,5 et h ≥ 60 ; **accompagnement** sinon. Une grappe
sans note transcrite est « autre ».

**N3** : pour chaque grappe appariée à une partie mélodique, le descripteur
COMPLET (43 grandeurs, note = hauteur médiane de ses notes, 60 s'il n'y en a
pas) est classé par le modèle adopté ; on relève le rang de la vraie machine
parmi les 58. Hasard : 1,7 % en top 1, **8,6 %** en top 5. Le fossé de domaine
se lit à la **distance médiane au corpus** (celle du rayon de nouveauté, 3,91)
par niveau.

**Ce qui n'est PAS dans la première passe, et pourquoi** — la contradiction
entre la mission et le dépôt est tranchée ici, par le dépôt :
`CDC-apprentissage.md` § 9 interdit toute « nouvelle dépendance lourde […]
(torch…) » sans « une mesure prouvant que le petit modèle plafonne ». Les
embeddings pré-entraînés (CLAP, MERT, OpenL3, PANNs, AST) et la sous-séparation
de la batterie (DrumSep) sont de telles dépendances — nouveaux paquets et
points de contrôle à télécharger. **Ils entrent en seconde passe, et seulement
si** la première tient le critère de plafond écrit ci-dessous (attendu 3,
réfutation) ; chacun alors sous le même protocole, aux mêmes niveaux, avec son
point de contrôle épinglé par empreinte.

### 0.5 Les attendus — seuils de RÉUSSITE et seuils d'ÉCHEC, écrits avant la mesure

Lots : **`s1-sec`** (10 morceaux de 30 s, stems séparés gardés), **`s1-prod`**
(les mêmes, production ajoutée au mélange), **`s2`** (10 morceaux de 186 à
269 s, parties qui entrent et sortent, chant, échantillons ; ses stems séparés
n'existent que pour le morceau 1 — la course `banc-s2` est morte le 20/09 à
03:38 sans ligne de fin, **1 morceau mesuré sur 10** — et les neuf autres
seront séparés par la même fonction de la chaîne, `htdemucs_6s`, avant la
mesure). Les attendus portent sur `s1-sec` sauf mention ; `s1-prod` et `s2`
sont jugés sur les mêmes seuils, et je prédis qu'ils y perdent (dernière ligne).

| # | attendu | RÉUSSITE si | ÉCHEC (réfutation) si |
|---|---|---|---|
| 1 | **Le recensement bat la parité** : meilleure approche en L3 (la condition d'usage), erreur absolue moyenne sur K | **≤ 2,0**, et **≥ 4/10** morceaux à ±1 | **≥ 3,8** — pas mieux que la parité, l'étage n'apporte rien |
| 2 | **La séparation porte l'essentiel de l'erreur** : (E_L3 − E_L2) / E_L3 sur K, meilleure approche A | **≥ 0,50**, et ≥ 50 % des parties perdues ou dédoublées le sont d'abord en L3 | **< 0,25** — l'erreur vient de l'étage lui-même (segmentation, embedding, clustering), pas de la séparation |
| 3 | **L'embedding voit le timbre, pas le registre** (L1, A-grp) : part des parties seules qui donnent exactement 1 grappe | **≥ 80 %** ; le `piano-deux-mains` coupé en deux dans au moins une occurrence sur deux (H25 attendu, compté à part) | **< 60 %** — l'embedding est le goulot ; c'est le CRITÈRE DE PLAFOND qui autorise la seconde passe (CLAP, OpenL3, PANNs…) |
| 4 | **Le clustering dans un mélange** (L2, A-grp, `other`) | ARI **≥ 0,30**, NMI **≥ 0,45** ; K_mél à ±1 dans ≥ 5/10 | ARI **< 0,10** |
| 5 | **Ce que la séparation coûte à N2** | ARI L2 − ARI L3 **≥ 0,10** | ARI L3 ≥ ARI L2 (la séparation ne coûterait rien au regroupement — contraire à S1) |
| 6 | **N1, batterie** (L3, les pièces de `--batterie-par-piece`) | rappel grosse caisse **≥ 0,9**, charleston **≥ 0,8**, caisse claire **≥ 0,7**, toms **≥ 0,5** ; cymbales : INDÉCIDABLE | rappel de la grosse caisse **< 0,7** |
| 7 | **N1, mélodique** (L2, rôles par les règles du § 0.4, hors basse — triviale en L2) | F1 macro sur lead / nappe / accompagnement / piano **≥ 0,40** | **< 0,25** — le hasard à quatre classes |
| 8 | **N3 sur une partie seule** (L1) | top 1 **≥ 50 %**, top 5 **≥ 80 %** | top 5 **< 50 %** — la bibliothèque de notes ne transfère même pas au propre ; les MOTIFS deviennent nécessaires (§ 0.3) |
| 9 | **N3 dans le mélange, puis après séparation** | L2 : top 1 **≥ 20 %**, top 5 **≥ 40 %** ; L3 : top 1 au moins **5 points** sous L2 | L2 : top 5 **< 15 %** (à peine au-dessus du hasard, 8,6 %) |
| 10 | **Le fossé de domaine, étage par étage** : distance médiane au corpus A6 | croissante L1 < L2 < L3 ; L1 **≤ 3,91** (dans le rayon), L3 **≥ 5,0** (comme les stems séparés de *B4*, 5,21 à 6,47) | L3 ≤ L2 — la séparation n'éloignerait pas du corpus, contre A0.4 |
| 11 | **B contre A** (L4 contre L3, K_mél) | je prédis **B moins bon** : erreur B ≥ erreur A-L3 | B meilleur de plus de 0,5 : la séparation nuirait au recensement, et la recommandation change de sens |
| 12 | **Coût** (première passe, séparation exclue et publiée à part) | ≤ **2×** la durée du morceau par morceau, sur ce poste, en CPU | > **10×** |
| 13 | **Morceaux réels, comptes écrits AVANT** | *Clair de Lune* (piano seul) : K = **1** ; *Children* : K dans **[8 ; 10]** (§ 12.1 du CDC multipiste : 8 parties sûres ou probables, 10 au plus, dont 3 pièces de batterie) | *Clair de Lune* ≥ **3** (H25 : le piano coupé par registres) ; *Children* ≤ **5** ou ≥ **13** |
| 14 | **Production et longueur** | `s1-prod` : erreur sur K à **+0,5** au plus de `s1-sec` ; `s2` : K_mél à ±1 dans ≥ 3/10 | — publiés sans réfutation : ce sont des prédictions de dégradation, pas des conditions |

*B4 Wuz Then*, *Us and Them* et *Sky and Sand* n'ont pas de compte écrit
d'avance dans le dépôt : leurs résultats seront publiés **sans attendu**, et
aucun compte ne leur sera attribué après coup pour les juger.

### 0.6 La règle du verdict, écrite avant

- **CONFIRMÉE** si les attendus 1 et 2 sont tenus ;
- **RÉFUTÉE** si l'attendu 1 tombe dans sa zone d'échec (≥ 3,8) — quel que soit
  le reste ;
- **PARTIELLE** sinon, avec pour chaque attendu tenu, raté ou réfuté le chiffre
  exact, et l'étage (segmentation, embedding, clustering, séparation,
  identification) à qui chaque erreur de comptage est attribuée.

Les deux cas qui ne peuvent pas être tranchés par ces seuils sont nommés :
l'attendu 2 est un ratio, il n'a pas de sens si E_L3 = 0 (on publiera alors
« erreur nulle en L3, part non définie ») ; et N1 « cymbales » est indécidable
sur le banc.

### 0.7 Ce qui est promis sur la forme, avant la mesure

- **Un module isolé, derrière une option** : sans elle, la chaîne est inchangée
  À L'OCTET (le témoin se vérifie comme au § 7 bis.4 du banc). Le recensement
  PUBLIE ; il ne coupe, ne fusionne et ne crée aucune piste dans cette phase.
- **Reproductible** : versions (numpy, scikit-learn, librosa, basic-pitch,
  demucs, torch), empreinte SHA-256 des points de contrôle (`htdemucs_6s`, le
  modèle ICASSP 2022, `classifieur.joblib`) et commit dans la provenance du
  rapport ; HDBSCAN est déterministe, la seule graine (celle des lots) est
  celle des morceaux ; commandes exactes consignées au verdict.
- **Aucun seuil touché après la mesure** sans un diagnostic écrit ; un
  balayage, s'il y en a un, se publie ENTIER.
- **B3 et les autres éléments de l'INDEX ne bougent pas.**
