# Le recensement des sources — combien de parties, lesquelles, jouées par quoi

*Chantier ouvert le 24/09/2026. L'hypothèse et ses attendus (§ 0) ont été
écrits et commités AVANT toute mesure et avant le reste de ce document : commit
`a110fd8` (24/09/2026 19:24:21 +0700), poussé sur origin — c'est la preuve
d'antériorité, et le § 0 ne se modifie plus que par ajout daté. Le cahier des
charges de l'étage (§ 1 à § 8) est venu dans un second commit. **Rien n'est
implémenté avant la validation de l'utilisateur.***

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

---

## 1. L'objet : un étage qui recense, et qui ne décide rien

L'étage reçoit un morceau et ses stems, et publie **un recensement** : combien
de parties, de quel rôle, de quel timbre, jouées par quelle machine du parc —
avec, pour chaque nombre, ce qui l'a produit et ce qui l'a empêché d'être sûr.

**Il ne coupe, ne fusionne, ne crée et ne supprime aucune piste.** La
parité reste la décision de `--parite`, et « couper une piste reste une décision
humaine » (`CLAUDE.md`). Le recensement devient une ENTRÉE de l'arbitrage
seulement dans une phase ultérieure, sous son propre attendu écrit d'avance
(§ 4.3), et seulement si H37 est confirmée ou partielle sur l'attendu 1.

## 2. Entrées

| entrée | forme | d'où elle vient |
|---|---|---|
| le mélange | WAV stéréo, lu en mono (moyenne des canaux) à sa fréquence d'origine | `morceau.wav` (banc) ou l'original (chaîne) |
| les stems | un WAV par stem nommé (`bass`, `drums`, `other`, `guitar`, `piano`, `vocals`) | séparés par la chaîne (`--garder-stems`), ou sommés depuis la vérité (niveau L2), ou une partie seule (L1) |
| les notes | les `StemNote` que la chaîne transcrit déjà (Basic Pitch) | réutilisées quand la chaîne tourne ; recalculées par la même fonction sur le banc |
| les pièces de batterie | la décision de `--batterie-par-piece` | idem |
| la bibliothèque | `modeles/classifieur.joblib` (58 machines, corpus A6) et les statistiques de centrage de son corpus | existante ; son empreinte est vérifiée comme `classifieur.py --eprouver` le fait (`verifie_fraicheur`), un modèle périmé est REFUSÉ et dit |
| la vérité | `verite.json` et `stems-vrais/` | banc seulement ; l'étage lui-même ne la lit jamais |

Un stem sous le seuil de la chaîne (0,5 % de l'énergie) est recensé quand même
— c'est l'endroit où l'on voit une fuite — mais marqué `sousSeuil: true`.

## 3. Sorties, et leur format

### 3.1 Le bloc `recensement` de `rapport.json`

Ajouté au rapport de la chaîne sous l'option, absent sans elle :

```json
"recensement": {
  "format": "vsm-recensement", "version": 1,
  "approche": "A-grp",
  "compte": {"K": 11, "K_mel": 7, "K_bat": 4},
  "comptesParApproche": {"A-pal": {"K_mel": 5}, "A-grp": {"K_mel": 7}, "A-voix": {"K_mel": 9}, "B": {"K_mel": 6, "K_bat": 3}},
  "stems": [
    {"stem": "other", "sousSeuil": false,
     "segments": {"retenus": 412, "silencieux": 38, "bruit": 51},
     "grappes": [
       {"id": 0, "role": "nappe", "segments": 140, "dureeS": 61.3,
        "activite": [[0.0, 28.4], [57.1, 212.0]],
        "notes": {"n": 188, "dureeMediane": 1.42, "polyphonie": 2.7, "hauteurMediane": 62, "ambitus": 19},
        "machines": [["vsm.juno106", 0.40], ["vsm.prophet", 0.20], ["vsm.obx", 0.10], ["vsm.supersaw", 0.10], ["vsm.jupiter8", 0.10]],
        "distanceAuCorpus": 5.8, "abstention": "au-delà du rayon (3,91)"}
     ]}
  ],
  "batterie": [{"piece": "kick", "frappes": 212}, {"piece": "hihat", "frappes": 530}],
  "indecidable": ["cymbales : aucun détecteur de cymbale dans la chaîne"],
  "secondes": {"segmentation": 1.2, "descripteurs": 6.8, "grappes": 0.3, "notes": 0.0, "identification": 0.9},
  "provenance": {"reglages": {"segmentMin": 0.10, "segmentMax": 1.00, "silenceDbfs": -50,
                              "hdbscanMinClusterPart": 0.02, "hdbscanMinClusterMin": 5,
                              "hdbscanMinSamples": 3, "dureeMinGrappeS": 4.0, "partPercussive": 0.6},
                 "versions": {"numpy": "…", "scikit-learn": "…", "librosa": "…", "basic-pitch": "…"},
                 "empreintes": {"classifieur.joblib": "sha256:…"}}
}
```

Règles du format :
- **`machines` est un classement, pas une décision** : cinq noms et leurs
  scores, et l'`abstention` du classifieur écrite quand il s'abstient — jamais
  un nom seul présenté comme le bon (la « pire faute » de `CDC-apprentissage.md`
  § 4).
- **Rien d'écarté en silence** : segments silencieux, segments de bruit,
  grappes sous 4 s (`grappesCourtes`, avec leur durée), stems sous le seuil —
  tout est compté.
- **`indecidable`** liste ce que l'étage ne PEUT pas voir ; une case absente ne
  vaut pas zéro.
- Les nombres traversent le JSON en locale C (règle de `interchange/NumberText.h`,
  appliquée ici par `json` de Python, qui l'est par construction).

### 3.2 Le journal

Une ligne par stem et une ligne de synthèse, au format de la chaîne :
`recensement : other → 4 grappes (nappe, lead, accompagnement, autre), 51
segments de bruit, 2 grappes sous 4 s écartées` ; puis `recensement : K = 11
(7 mélodiques + 4 pièces) — la parité a produit 9 pistes`. **L'écart entre le
recensement et la parité est toujours imprimé**, c'est le chiffre que la
mesure cherche.

## 4. Interfaces

### 4.1 Avec la chaîne

- **Module** : `analyse/analyzer/vsm_recensement.py`, fonctions pures
  (`segmenter`, `descripteurs_de_timbre`, `grouper`, `roles`, `identifier`,
  `recenser`), sans état global, sans import au chargement d'autre chose que
  numpy et scikit-learn — librosa et le classifieur sont importés dans les
  fonctions, comme ailleurs dans la chaîne.
- **Option** : `reconstruire.py --recensement` (défaut : éteinte). Elle est
  inscrite dans la provenance du rapport. Sans elle, le module n'est pas
  importé : **la chaîne est inchangée à l'octet**, vérifié par un témoin (§ 6).
- **Où elle s'insère** : après la séparation et la transcription, avant
  l'arbitrage — elle lit ce qu'ils ont produit et n'écrit que son bloc. Elle ne
  modifie aucune structure de la chaîne.
- `charger_tous_les_modules()` l'importe au départ comme les autres (règle du
  03/09 : rien d'importé à la demande pendant une course).

### 4.2 Avec le banc

`analyse/banc_recensement.py` — un script à part, qui n'appelle pas
`reconstruire.py` : il relit les stems que le banc a déjà gardés
(`<lot>-banc/<morceau>/stems-separes/stems/`) pour L3, somme les stems vrais
pour L2, prend les parties seules pour L1 et le mélange pour L4, et fait tourner
les quatre approches sur chacun. Il ne fait aucun rendu et ne touche pas au
moteur.

```
analyse/.venv/bin/python -u analyse/banc_recensement.py \
    --lot reconstruction/travail/s1-sec --banc reconstruction/travail/s1-sec-banc \
    --sortie reconstruction/travail/recensement-s1-sec
```

Pour `s2`, les stems séparés manquants (9 morceaux sur 10) sont d'abord
produits par `--separer`, qui appelle la fonction de séparation de la chaîne
(`htdemucs_6s`, sous-processus, `shifts=0` comme au défaut) et l'écrit dans
`recensement-s2/<morceau>/stems-separes/` — jamais dans le dossier du banc.

Sorties : `rapport.json` (format `vsm-banc-recensement`, provenance complète,
la ligne de commande mot pour mot) et `tableau.txt`, le tableau croisé
**approche × niveau × métrique** que la mission demande, par lot, puis par
morceau. Reprenable : un morceau mesuré n'est pas refait.

### 4.3 Avec l'arbitrage — ce qui est prévu, et NON fait dans cette phase

Le recensement pourrait un jour régler ce que la parité fixe aujourd'hui par
défaut : le nombre de voix par stem (`--voix-par-stem`, un MAXIMUM que les
k-moyennes remplissent par construction), l'ouverture de la porte du
fourre-tout (que B9 a déjà armée par les paliers), le nombre de pièces de
batterie. **Aucun de ces branchements n'est fait ici**, pour une raison
écrite : H37 mesure si le COMPTE est juste ; qu'un compte juste améliore la
RECONSTRUCTION est une autre hypothèse, qui demande une course de bout en bout
avec témoin — c'est la leçon de D274 et D282 (« un stem meilleur à l'oreille
du transcripteur n'est pas une reconstruction meilleure »). Elle s'écrira,
chiffres d'avance, après le verdict de H37.

## 5. Anti-objectifs

1. **Pas de soustraction** sous aucune forme (R1, § 0.2).
2. **Pas d'apprentissage sur le banc** : les rôles sont des règles, les
   seuils sont figés au § 0.4 (`CDC-banc-synthetique.md` § 3, anti-objectif 2).
   La bibliothèque N3 est le corpus A6 existant, qui n'est pas réentraîné.
3. **Pas de nouvelle dépendance lourde** avant l'échec écrit de l'attendu 3
   (`CDC-apprentissage.md` § 9) ; alors, une à la fois, point de contrôle
   épinglé par empreinte, et sa taille et son temps publiés.
4. **Pas une ligne hors de `analyse/` et `docs/`** : ni `core/`, ni `audio/`,
   ni `interchange/`, ni `app/`. Le DAW n'affiche pas le recensement dans cette
   phase ; le volet du rapport le montrera le jour où le bloc sera stable.
5. **Pas de décision** : l'étage publie, il ne touche pas au projet (§ 1).
6. **B3 et les autres éléments de l'INDEX ne bougent pas.**

## 6. Critères d'acceptation

```
[ ] Module vsm_recensement.py : fonctions pures, tests unitaires sur des signaux
    synthétiques (deux timbres alternés → 2 grappes ; un seul timbre sur deux
    registres → mesuré et publié, c'est H25 ; silence → 0 grappe et segments
    silencieux COMPTÉS ; stem vide → recensement vide dit, pas d'exception)
[ ] Les réglages du § 0.4 sont des constantes nommées du module, et le test
    vérifie qu'elles valent ce que le § 0.4 écrit (un seuil ne bouge pas sans
    que le test et le document bougent ensemble)
[ ] Déterminisme : deux recensements du même morceau → JSON identique
    (hors `secondes`)
[ ] Témoin de l'option : sans --recensement, rapport.json et projet d'une
    course sur le morceau minuscule du dépôt identiques À L'OCTET à ceux d'avant
    le chantier
[ ] Avec --recensement : le bloc est présent, conforme au § 3.1, et la
    provenance porte l'option, les réglages, les versions et les empreintes
[ ] Modèle périmé → recensement sans N3, dit (« classifieur refusé : … »),
    le reste publié
[ ] banc_recensement.py : sur banc-minuscule (analyse/tests/donnees/), les
    quatre niveaux se calculent, les métriques (erreur de compte, précision et
    rappel par rôle, ARI, NMI, top 1, top 5, temps) sont présentes, et la part
    imputable à la séparation se calcule ou se dit « non définie »
[ ] La garde des étiquettes : les métriques N2 sont vérifiées sur un cas
    construit à la main (étiquettes connues → ARI 1,0 ; permutées → 1,0 ;
    aléatoires → proche de 0) AVANT de mesurer quoi que ce soit — un banc
    se vérifie avant sa cible (leçon de D266)
[ ] ruff, mypy, suite Python ENTIÈRE verte — hors campagne (CLAUDE.md)
[ ] Mesures : s1-sec, s1-prod, s2 (L1-L4), puis Clair de Lune et Children ;
    le tableau croisé publié au § 7 ; le verdict de H37 écrit selon la règle
    du § 0.6, avec l'attribution par étage
[ ] INDEX et ROADMAP-fusion mis à jour avec le verdict
```

## 7. Plan de mesure, et son coût estimé

Estimations, pas mesures — le coût réel est l'attendu 12 et se publie :

| étape | quoi | coût estimé |
|---|---|---|
| 1 | module, tests, garde des étiquettes, témoin de l'option | écriture ; tests en secondes |
| 2 | `s1-sec` et `s1-prod`, L1 à L4, quatre approches | 20 morceaux de 30 s, stems existants : quelques minutes |
| 3 | `s2` : séparer 9 morceaux (`--separer`), puis L1 à L4 | séparation ~5 à 10 min par morceau de 4 min en CPU, soit ~1 h à 1 h 30 ; puis minutes |
| 4 | *Clair de Lune*, *Children* (comptes écrits), *B4 Wuz Then*, *Us and Them*, *Sky and Sand* (sans attendu) | stems séparés existants quand les courses les ont gardés (`children-c1-stems`, `b4wuzthen-stems`) ; sinon une séparation chacun |
| 5 | seconde passe (CLAP, OpenL3, PANNs, DrumSep), SEULEMENT si l'attendu 3 échoue | à chiffrer alors |

Précautions d'exploitation (`CLAUDE.md`) : une seule course lourde à la fois ;
la séparation de l'étape 3 est lancée détachée (`setsid nohup … & disown`),
surveillée par PID ; les stems et sorties vont sur `/home`, jamais dans le
brouillon `tmpfs` ; `python -u`.

## 8. Ce qui est incertain, et qui se dit maintenant

- **Les segments d'un mélange ne sont pas des notes isolées.** Le descripteur
  A6 a été conçu pour une note rendue seule ; appliqué à un segment où trois
  parties sonnent, il décrit leur somme. C'est précisément ce que L1 → L2
  mesure, et c'est la raison pour laquelle L1 existe.
- **Les rôles par règles sont grossiers**, et le choix d'accompagnement comme
  classe par défaut penche la précision de ce rôle vers le bas. Le F1 macro de
  l'attendu 7 le prend en compte ; une matrice de confusion complète est
  publiée à côté.
- **`s1` est dense** : 5 à 15 parties en 30 s, dont 6 à 11 mélodiques pour 8
  morceaux sur 10. Un recensement juste sur ce lot serait remarquable ; `s2`,
  plus long et plus aéré par ses sections, est le cas le plus proche d'un
  disque.
- **Sur les disques, la vérité est faible** : les comptes de *Children* sont
  une lecture de l'énergie et d'un MIDI d'amateur (§ 12.1 du CDC multipiste),
  pas une session. L'attendu 13 est donc une fourchette, et les trois autres
  disques n'ont pas d'attendu.
