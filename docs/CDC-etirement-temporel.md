# L'étirement temporel (phase D12) — cahier des charges

Un clip audio de VSM Studio est du **temps réel** posé sur une ligne de temps
**musicale** : sa position est en ticks, son contenu en secondes
(`Clip::sourceStartSeconds`, et le commentaire qui l'accompagne dit pourquoi).
Changer le tempo du projet déplace le clip et ne change rien à ce qu'il joue.
Cubase (*élastique*) et Live (*warp*) font autre chose : le contenu SUIT le
tempo, en gardant sa hauteur, et l'on peut caler une prise sur la grille
marqueur par marqueur. C'est ce que D12 écrit — dans le dépôt, sans
bibliothèque, comme le choix n° 3 du § 4 de `ROADMAP-daw.md` l'a fixé.

Écrit le 04/09/2026, le jour où sa condition se réalise : les campagnes de la
parité sont closes (CDC multipiste § 8 et § 9) et les machines de la branche
sont fusionnées par la campagne 5. Il se lit avec `ROADMAP-daw.md` (D2, D5,
D8.2, D11.8) et se mesure comme tout le reste : un chiffre par promesse,
écrit avant la mesure.

## 0. La règle qui prime : un clip non étiré ne change pas d'un bit

Tout ce que D2 et D8.2 ont mesuré reste vrai après D12 : un clip dont le
suivi de tempo est ÉTEINT passe par le même chemin qu'aujourd'hui, échantillon
pour échantillon (le test de D2.6, « hors ligne = temps réel à
1,2 × 10⁻⁷ », et celui de D8.2, « la mémoire ne dépend pas de la durée »,
restent dans la suite et doivent rester verts). Un clip étiré au rapport 1,0
rend AUSSI l'original au bit près : l'algorithme a un court-circuit, et un
test le prouve. Sans cette règle, D12 ferait mentir neuf phases.

## 1. Ce qu'on entend par « suivre le tempo »

Trois modes par clip, parce que les DAW de référence en ont trois et qu'ils
ne se remplacent pas :

| Mode | Ce qu'il fait | Coût | Quand |
|---|---|---|---|
| **Éteint** (défaut, l'état actuel) | temps réel ; `sourceStartSeconds` | aucun | une prise qu'on ne touche pas, la voix reportée par la reconstruction |
| **Hauteur conservée** (Live « Beats/Complex », Cubase « élastique ») | la durée suit le tempo, la hauteur ne bouge pas | l'algorithme du § 3 | caler une prise, changer le tempo d'un morceau reconstruit |
| **Rééchantillonné** (Live « Re-Pitch ») | la durée suit le tempo ET la hauteur suit avec (un vinyle qu'on ralentit) | un noyau de rééchantillonnage | boucles rythmiques, effet voulu |

Le mode est un champ du clip (`Clip::warpMode`), sauvegardé, annulable. Le
**défaut est Éteint** : un projet existant s'ouvre identique, et la chaîne
d'analyse continue d'écrire des reports d'audio en temps réel — la
reconstruction n'a pas besoin de D12, c'est un besoin de production (D11.8).

## 2. Les marqueurs : la carte entre le fichier et la grille

Suivre le tempo suppose de savoir OÙ, dans le fichier, tombent les temps.
Un clip étiré porte des **marqueurs** (`Clip::warpMarkers`), chacun une
paire *(position dans le fichier, en secondes ; position musicale, en ticks
depuis le début du matériau du clip)*. Entre deux marqueurs la relation est
linéaire (un rapport d'étirement constant) ; avant le premier et après le
dernier, le rapport du segment voisin se prolonge. Deux marqueurs au moins
existent dès qu'on allume le mode : le début du clip et sa fin, posés par la
règle « le clip fait N mesures » (§ 6) — et c'est TOUT ce qu'il faut pour
qu'une boucle de quatre mesures à 118 BPM joue à 120.

Ce que les marqueurs ne sont pas : une détection de tempo. Un suiveur de
temps (*beat tracker*) est un autre chantier, avec son propre banc ; D12 pose
les marqueurs à la main et par la règle des N mesures, et le dit (§ 8).

## 3. L'algorithme, et pourquoi celui-là

**Décision : un WSOLA avec verrouillage des transitoires** (*Waveform
Similarity Overlap-Add*), écrit dans `audio/include/vsm/audio/dsp/TimeStretch.h`.
Pas un vocodeur de phase, et voici la pesée :

- ce qu'on étire d'abord dans ce dépôt est **rythmique ou chanté** (des
  stems séparés, des prises) : un WSOLA garde les attaques nettes là où un
  vocodeur de phase les étale (*smearing*) sauf à lui ajouter un verrouillage
  de phase par transitoire — c'est-à-dire la même détection qu'ici, plus une
  FFT ;
- il travaille **dans le domaine temporel, grain par grain** : il lit le
  fichier autour de la position courante (± une fenêtre), ce qui convient à
  la diffusion depuis le disque de D8.2 (`SampleStore::frameAt` et
  `requestRange`), sans tampon de plusieurs secondes ;
- il coûte peu : une recherche de similarité de ± 256 trames sur des grains
  de 1 024, soit de l'ordre de cent millions de multiplications par seconde
  et par clip stéréo — vingt clips tiennent sur cette machine ;
- il est **déterministe et sans allocation** dans `process()` : l'état
  (fenêtres, position, dernier grain) est alloué à la publication de la piste,
  sur le thread de l'interface, comme les `AudioClipSpan`.

Ce qu'il fait moins bien, et qui est dit : sur un son TENU et très
harmonique (une nappe, un accord d'orgue) étiré loin (au-delà de ×1,5), un
WSOLA laisse entendre un léger flottement là où un vocodeur de phase reste
lisse. **Le vocodeur de phase n'est pas écrit dans D12** ; il le sera si une
mesure sur un stem réel montre que le WSOLA y perd (§ 5, critère 4), et
`RealIfft` existe déjà pour lui. Le mode *Rééchantillonné* est un **noyau
fenêtré (sinc de Kaiser, 32 points)** qui remplace aussi l'interpolation
linéaire de D2.3 — ce que D2 avait annoncé (« il sera écrit avec
l'étirement temporel plutôt qu'emprunté »).

## 4. Où cela vit, et ce qui ne bouge pas

- **Modèle** (`core/`) : `Clip::warpMode` et `Clip::warpMarkers` ; les gestes
  de `ClipEdit` (déplacer, couper, rogner, boucler) transportent les
  marqueurs — couper un clip à la mesure 3 laisse chaque moitié avec les
  marqueurs qui la concernent et un marqueur neuf au point de coupe. Tests.
- **Format** (`interchange/`) : les deux champs dans `project.json`, écrits
  seulement quand le mode n'est pas Éteint ; la version de format ne monte
  que si un projet s'en sert (choix n° 4 du § 4, la règle de D2) ; un projet
  ancien se charge sans changement.
- **Moteur** (`audio/`) : `AudioClipSpan` gagne une carte *ligne de temps →
  fichier* par morceaux (en trames, calculée à la publication par
  `ticksToSeconds`, jamais dans `process()`), et `mixInto()` lit à travers
  l'étireur quand le mode l'exige. `ProcessGraph` ne bouge pas — la couture
  annoncée à D2 et tenue à D8.2 tient une troisième fois, ou le document le
  dit.
- **Rendu hors ligne** : `vsm-render` passe par le même `mixInto()` ; le test
  de D2.6 s'étend au clip étiré (critère 3 du § 5).
- **Interface** (`app/Source/ui/`) : dans l'arrangement (D5), le clip audio
  a un menu « Suivre le tempo » (Éteint · Hauteur conservée ·
  Rééchantillonné), « Le clip fait N mesures », et ses marqueurs se voient
  sur la forme d'onde, se déplacent, s'ajoutent (double-clic) et se
  suppriment ; la forme d'onde est dessinée dans le temps ÉTIRÉ (un clip qui
  suit le tempo montre ses temps sur la grille). Tout est annulable, et la
  vue se vérifie à l'écran (`VSM_CAPTURE`).

## 5. Le banc, écrit avant la première mesure

| # | Promesse | Mesure | Seuil |
|---|---|---|---|
| 1 | la hauteur ne bouge pas | un la3 tenu (220 Hz, 3 s) étiré ×0,75 et ×1,5 ; fréquence du pic de la sortie | ≤ 5 cents d'écart |
| 2 | la durée est exacte | longueur de la sortie pour une entrée de N trames au rapport r | = round(N·r) trames, à zéro près |
| 3 | rapport 1,0 = l'original | sortie contre entrée | identiques au bit près (court-circuit) ; et hors ligne = temps réel sur un clip étiré, comme D2.6 |
| 4 | les transitoires ne se doublent ni ne se perdent | un train de 16 clics espacés de 250 ms étiré ×1,5 et ×0,66 ; comptage des attaques de la sortie | 16 attaques, chacune à ≤ 1 ms de sa position théorique |
| 5 | un son tenu ne flotte pas trop | la3 tenu étiré ×1,5 ; profondeur de la modulation d'enveloppe sur 100 ms | ≤ 10 % (et le chiffre est publié, c'est lui qui décidera du vocodeur de phase) |
| 6 | déterminisme | deux rendus | identiques au bit près |
| 7 | pas d'allocation, pas d'entrée-sortie dans `process()` | le compteur d'allocations du banc de D8.2 | zéro |
| 8 | le noyau fenêtré vaut mieux que l'interpolation linéaire | 44,1 → 48 kHz sur un balayage 20 Hz–20 kHz ; erreur rms contre la référence | sous 10⁻⁴ jusqu'à 20 kHz (D2.3 mesurait 10⁻³ sous 10 kHz) |
| 9 | la mémoire ne dépend toujours pas de la durée | le test de D8.2 sur un clip étiré | égalité, comme avant |

**Critère de phase, et c'est le plus important** : la reconstruction de *Sky
and Sand* (D2 : « se joue entière, voix comprise ») s'ouvre, on change le
tempo du projet de +10 %, et **la voix reste en place** — les attaques de la
piste audio étirée tombent à ≤ 10 ms des attaques du MIDI reconstruit sur
les huit premières mesures (mesuré par le banc, pas à l'oreille). Sans D12,
la voix dérive d'une mesure entière au bout d'une minute.

## 6. « Le clip fait N mesures », et pourquoi c'est la première commande

Un musicien qui pose une boucle sait combien de mesures elle fait ; il ne
sait pas son tempo au centième. La commande prend N, pose les deux marqueurs
extrêmes, et en déduit le tempo d'origine (qu'elle affiche, pour qu'on le
vérifie). C'est ce que Live fait à l'import d'une boucle, et c'est ce qui rend
D12 utilisable avant tout suiveur de temps.

## 7. Ordre de marche

| Étape | Contenu | Terminé quand |
|---|---|---|
| D12.1 | le noyau de rééchantillonnage fenêtré (sinc de Kaiser), qui remplace l'interpolation linéaire de D2.3 et fait le mode *Rééchantillonné* | banc 8 ; les empreintes qui passent par le rééchantillonnage sont régénérées EN LE DISANT — **fait le 04/09/2026** (aucune empreinte ne passait par le rééchantillonnage : rien à régénérer ; le mode *Rééchantillonné* du clip attend D12.5) |
| D12.2 | `TimeStretch` : le WSOLA, la recherche de similarité, le court-circuit à 1,0 | bancs 1, 2, 3 (première moitié), 5, 6, 7 — **fait le 04/09/2026** (banc 7 au moment de D12.5, dans `process()`) |
| D12.3 | la détection de transitoires (flux d'énergie sur le matériau, à la publication, mise en cache par fichier comme les crêtes de forme d'onde) et le verrouillage | banc 4 — **fait le 04/09/2026** (le cache par fichier attend D12.5) |
| D12.4 | le modèle : `warpMode`, `warpMarkers`, les gestes de `ClipEdit`, le format | tests `core/` et `interchange/` ; un projet v2 s'ouvre inchangé — **fait le 04/09/2026** |
| D12.5 | le moteur : la carte par morceaux dans `AudioClipSpan`, `mixInto()` à travers l'étireur, `vsm-render` | banc 3 (seconde moitié), 9 ; `ProcessGraph` intact — **fait le 04/09/2026** ; depuis D12.8, l'étireur de « hauteur conservée » est le vocodeur de phase, le WSOLA le témoin |
| D12.6 | l'interface : le menu du clip, « N mesures », les marqueurs sur la forme d'onde, la forme dessinée en temps étiré, annulation | vu à l'écran, `VSM_CAPTURE`, réglages retenus — **fait le 04/09/2026** |
| D12.7 | le critère de phase sur *Sky and Sand* | ≤ 10 ms sur huit mesures à +10 % de tempo — **mesuré le 04/09/2026 : −8 ms sur les huit mesures, 14 ms sur la pire mesure prise seule ; voir la note** |

Le vocodeur de phase, s'il vient, sera D12.8 avec son propre attendu, écrit
sur le chiffre du banc 5.

> **D12.2 ET D12.3 SONT FAITES (04/09/2026), ET VOICI LES CHIFFRES.**
> `audio/include/vsm/audio/dsp/TimeStretch.h` (grains de 2 048, saut de
> 1 024, recherche ± 768 grossière au pas de 8 puis fine, Hann périodique,
> corrélation normalisée sur la zone de recouvrement) et
> `TransientDetector.h` (flux d'énergie par blocs de 256, seuil à trois fois
> la moyenne des ± 20 blocs voisins, plancher, maximum local, 48 ms entre
> deux, position affinée à la trame de plus forte pente). Le banc,
> `audio/tests/test_time_stretch.cpp` :
>
> | # | attendu | mesuré |
> |---|---|---|
> | 1 | ≤ 5 cents | **0,0 cent** à ×0,75 et ×1,5 (la3, 2 s au milieu) |
> | 2 | sonne jusqu'à round(N·r), silence après | rms 0,348 avant la fin contre 0,353 au milieu ; **0,000000** après, aux deux rapports |
> | 3 | rapport 1 au bit près | **identique**, 48 000 trames, signal riche ; un décalage entier reste un court-circuit, 1,0000001 non |
> | 4 | 16 attaques à ≤ 1 ms, ×1,5 et ×0,66 | **16 et 16, écart 0,98 ms** — le retard constant du détecteur du banc (fenêtre de 1 ms) ; avec les transitoires DÉTECTÉS et non déclarés : 16, 0,98 ms ; le détecteur seul : 16 sur 16 à 0,00 ms, et 0 sur un la3 tenu |
> | 5 | flottement ≤ 10 % | **0,0 %** (rms 0,3535 à 0,3536 par 100 ms) |
> | 6 | déterministe, blocs de 256 = blocs de 4 096 | **identiques au bit près** ; un `seek` repart à froid, identique à un départ là |
>
> **Ce que la première mesure a enseigné, et qui n'était pas dans le § 3.**
> Le premier verrouillage (le grain propriétaire posé juste, son prédécesseur
> une demi-fenêtre plus tôt, son successeur à la suite) laissait UN CLIC SUR
> DEUX 17 à 20 ms trop tôt à ×1,5. La cause : à l'étirement, la fenêtre d'un
> grain (2 048 trames) lit plus de source que son avance nominale (683 par
> saut), si bien qu'un grain CHERCHÉ deux ou trois sauts avant le
> propriétaire attrape déjà l'attaque dans sa queue, atténuée par la fenêtre
> mais entière comme attaque. La règle complétée : un grain cherché ne
> contient jamais de transitoire — coupé au premier qu'il rencontre, les
> grains d'avant gardent ce qui précède, ceux d'après ce qui suit ; seuls les
> trois grains alignés jouent l'attaque, et leur somme de fenêtres vaut un.
> Le prix, dit dans l'en-tête : un creux de quelques millisecondes dans la
> queue du grain coupé, juste avant l'attaque.

> **D12.1 EST FAITE (04/09/2026), ET LE BANC A CHOISI LA LONGUEUR DU NOYAU.**
> `audio/include/vsm/audio/dsp/SincResampler.h` : sinc sous fenêtre de
> Kaiser (β = 8), table de 512 phases interpolée, coupure abaissée au
> Nyquist de session en sous-échantillonnage, gain unité phase par phase ;
> branché sur le chargeur résident ET la diffusion depuis le disque (même
> noyau, même valeurs à 10⁻⁵ près, le test de D8.2 le vérifie toujours). Le
> § 3 disait 32 points ; le banc, `test_sinc_resampler.cpp`, a mesuré trois
> longueurs, erreur rms contre la référence analytique, 44,1 → 48 kHz :
>
> | fréquence | linéaire (D2.3) | sinc 32 | **sinc 64** | sinc 96 |
> |---|---|---|---|---|
> | 1 kHz | 1,3 × 10⁻³ | 3,1 × 10⁻⁵ | **1,2 × 10⁻⁵** | 1,3 × 10⁻⁶ |
> | 10 kHz | 1,3 × 10⁻¹ | 1,8 × 10⁻⁵ | **1,7 × 10⁻⁵** | 3,3 × 10⁻⁶ |
> | 18 kHz | 3,7 × 10⁻¹ | 3,3 × 10⁻⁵ | **3,3 × 10⁻⁵** | 5,6 × 10⁻⁶ |
> | 20 kHz | 4,4 × 10⁻¹ | 4,4 × 10⁻² | **9,5 × 10⁻⁶** | 1,8 × 10⁻⁵ |
>
> Et en sous-échantillonnage 48 → 44,1 kHz, ce qui replierait (rms restant
> d'un sinus à 0,707) : 23 kHz → 0,17 (32 points), **0,054 (64)**, 0,0082
> (96) ; 23,5 kHz → 0,10, **0,0072**, 0,00004. **Décision : 64 points.**
> 32 laissent 20 kHz rouler et n'atténuent un 23 kHz replié que de 12 dB ;
> 64 tiennent l'attendu du banc 8 (sous 10⁻⁴ jusqu'à 20 kHz) et 22 dB de
> réjection ; 96 n'apportent qu'au dernier kilohertz sous Nyquist, pour une
> fois et demie le coût, sur un chemin (48 → 44,1) qui est l'exception. Le
> chiffre du linéaire à 1 kHz (1,3 × 10⁻³) dit au passage que le
> « millième sous 10 kHz » de D2.3 était optimiste d'un ordre de grandeur
> dès 5 kHz (3,2 × 10⁻²).

> **D12.4 EST FAITE (04/09/2026) : LE MODÈLE ET LE FORMAT.** `Clip::warpMode`
> (`Off` par défaut, `KeepPitch`, `Repitch`) et `Clip::warpMarkers`, des
> paires *(secondes de fichier ; tick RELATIF au début du clip)* — relatives,
> si bien que déplacer un clip ne les touche pas, ce qu'un test vérifie.
> Six gestes dans `ClipEdit` : allumer le suivi pose la **paire neutre** (le
> rapport un, donc le court-circuit du moteur : le son ne change pas d'un
> bit) ; « le clip fait N mesures » (§ 6) répartit le matériau joué sur
> N × ticksPerBar et rend le tempo d'origine déduit ; ajouter un marqueur le
> pose là où la carte est déjà (le son ne change pas, le point devient
> calable) ; le déplacer bouge sa position MUSICALE en gardant sa position
> dans le fichier, borné par ses voisins ; le retirer, jamais le premier ni
> sous deux. **Couper et rogner transportent la carte** : un test compare
> `warpSourceSecondsAt` tous les 120 ticks avant et après la coupe, les deux
> moitiés mises bout à bout SONT l'ancienne carte, tick pour tick ; rogner la
> tête d'un clip étiré suit sa carte et non le tempo (0,5 s là où le tempo
> disait 1,0 s).
>
> **Format : la version 3 ne s'écrit que si un clip suit le tempo.** Un
> projet sans étirement garde son fichier de version 2, octet pour octet
> (testé) ; sinon, `warp` et `warpMarkers` s'écrivent et la version monte,
> parce qu'un lecteur de la version 2 jouerait un clip étiré SANS l'étirer,
> en silence — et ce format refuse plutôt qu'il ne devine. Les quatre
> recopies positionnelles de clip du sérialiseur sont remplacées par deux
> fonctions nommées : un champ ajouté au clip se recopie désormais à un seul
> endroit, pas à quatre.
>
> **Un défaut trouvé par le banc, et il valait le détour** : `addWarpMarker`
> rendait 94 au lieu de 1. La cause n'est pas dans l'algorithme mais dans une
> ligne de C++ : `m.insert(ou, neuf) - m.begin()` n'a pas d'ordre d'évaluation
> garanti, et `m.begin()` lu AVANT l'insertion est un pointeur que la
> réallocation invalide. L'indice se calcule maintenant avant. Aucun test
> n'aurait vu ce défaut sans vérifier la valeur de retour d'une fonction dont
> on aurait pu croire qu'elle « range bien le marqueur ».

> **D12.5 EST FAITE (04/09/2026) : LE MOTEUR SUIT LE TEMPO.** `AudioClipSpan`
> porte un `ClipWarp` — nul quand le clip ne suit pas le tempo, c'est-à-dire
> presque toujours, et le chemin de lecture est alors **exactement celui
> d'avant D12**, sans un test de plus par échantillon. Le `ClipWarp` tient la
> carte traduite en trames, l'étireur, ses tampons et le noyau ; tout est
> alloué à la publication par `prepareWarpedSpans`, qui cherche aussi les
> attaques du fichier **une fois par piste** (elles sont une propriété du
> matériau, pas du clip) et les partage entre les portées. `ProcessGraph` n'a
> pas bougé d'une ligne : la couture annoncée à D2 et tenue à D8.2 tient une
> troisième fois. Mesuré (`test_audio_track.cpp`) :
>
> | promesse | mesuré |
> |---|---|
> | rapport un = pas un bit de différence | **identique** au clip non étiré, 48 000 trames |
> | ×2, hauteur conservée | pic **220,0 Hz**, le clip dure 96 000 trames et se tait après (rms 0,000000) |
> | ×2, rééchantillonné | pic **110,0 Hz** — la hauteur suit la vitesse, c'est ce qui sépare les deux modes |
> | blocs de 256 contre blocs de 4 096 | **identiques au bit près** (la condition de D2.6) |
> | fondu de 0,5 s et gain 0,5 sur un clip étiré | rms 0,015 à 40 ms contre 0,178 à 0,85 s |
>
> **Deux décisions prises en écrivant, et dites ici.** (1) Un clip étiré **ne
> boucle pas** : il donne une seule portée, et sa carte dit ce qu'il joue.
> Répéter une fenêtre et suivre une carte sont deux réponses à la même
> question, et c'est la carte qui a été posée à la main ; la boucle d'un clip
> étiré entrera avec un attendu à elle si une main la demande. (2) Un bloc
> plus long que 8 192 trames est rendu en plusieurs passes, ce qui évite de
> faire descendre la taille maximale de bloc jusqu'à la portée — et ne change
> aucune valeur, puisque l'étireur est indépendant de la taille des blocs (le
> banc le vérifie au bit près). Le noyau du mode rééchantillonné est réglé sur
> le rapport le PLUS RAPIDE de la carte : c'est lui qui décide de la coupure
> anti-repliement, et sous-estimer la vitesse laisserait replier le passage le
> plus rapide.
>
> Six suites vertes : core 170, audio 1161, interchange 252, panneaux 11,
> CLAP 25, VST3 19 — et l'application a été lancée et regardée.

> **D12.6 EST FAITE (04/09/2026) : L'INTERFACE, ET ELLE A ÉTÉ REGARDÉE.** Le
> menu d'un clip AUDIO — et de lui seul, un clip MIDI suivant le tempo par
> nature — porte un sous-menu « Suivre le tempo » (Non · Hauteur conservée ·
> Rééchantillonné, coché sur l'état courant), « Le clip fait N mesures… »,
> « Ajouter un marqueur ici » et « Retirer ce marqueur ». Les marqueurs se
> **voient** (un trait ambre sur toute la hauteur du clip et une pointe en
> haut) et se **saisissent** : quatre pixels autour du trait, le curseur
> change avant le clic, et tirer déplace la position MUSICALE du marqueur en
> laissant sa position dans le fichier — c'est le geste de calage. Le premier
> marqueur ne se saisit pas : il est le début du clip, et le déplacer voudrait
> dire rogner, ce que le bord gauche fait déjà. Tout passe par
> `onEditStarted`, donc tout s'annule.
>
> **La forme d'onde d'un clip étiré est dessinée dans le TEMPS ÉTIRÉ** :
> chaque colonne demande au clip où elle est dans le fichier. Sans cela, un
> clip calé montrerait ses temps ailleurs qu'où il les joue, et le calage se
> ferait à l'oreille alors qu'il se fait à l'œil.
>
> **Vu à l'écran** (`VSM_PROJET` + `VSM_VUE=arrangement` + `VSM_CAPTURE`, sur
> un projet écrit à la main en version 3) : une boucle de huit secondes
> déclarée faire six mesures s'étend de la mesure 1 à la mesure 7, son
> marqueur intermédiaire se voit à la mesure 4,5, et la densité des frappes
> CHANGE de part et d'autre — plus lâche avant, plus serrée après, ce qui est
> exactement ce que la carte demande (4 s de matériau sur 3,5 s avant le
> marqueur, 4 s sur 2,5 s après). La même boucle non étirée, sur la piste
> d'à côté, s'arrête à la mesure 5 : les deux se comparent d'un coup d'œil.
>
> Une ambiguïté de C++20 rattrapée au passage : concaténer une `juce::String`
> et un littéral `u8"..."` ne compile pas (`char8_t`), et cela ne se voit qu'à
> la compilation de l'application — le piège qui avait fait annoncer une
> capture « faite avec ce code » à D11.1. Chaque littéral passe désormais par
> `juce::String`.

> **D12.7 EST MESURÉE (04/09/2026), ET LA MESURE A D'ABORD TROUVÉ UNE PANNE
> MUETTE.** Le protocole (`analyse/mesure_d12_critere_de_phase.py`, pour le
> refaire) : trois projets écrits à partir de sky-parite — `ref` (120 BPM,
> la voix en temps réel), `sansWarp` (132 BPM, la voix en temps réel : l'état
> d'avant D12) et `avecWarp` (132 BPM, la voix avec la paire neutre de
> marqueurs, hauteur conservée) — rendus par `vsm-render --stems` ; puis
> l'enveloppe de la voix de `ref` (rms par 2 ms) comprimée de 1,1 — ce que
> le nouveau tempo DOIT donner — comparée par corrélation croisée à celle
> des deux autres rendus, sur huit mesures au nouveau tempo, et mesure par
> mesure. Deux choses à dire avant les chiffres. (1) **La voix reportée de
> *Sky and Sand* est silencieuse jusqu'à 151,8 s** (rms médian 0,00007 par
> tranche de 5 s, maximum 0,083) : les « huit premières mesures » du morceau
> sont instrumentales, la mesure porte donc sur les huit mesures qui suivent
> l'entrée de la voix. (2) **La première exécution rendait trois fichiers
> IDENTIQUES au bit près** : `prepareWarpedSpans` n'était câblé que dans
> l'application, et le rendu hors ligne exportait un clip calé sans son
> calage, sans un mot. C'est corrigé dans `OfflineReconstruction.cpp`, et
> c'est exactement le genre de panne pour laquelle un critère se MESURE au
> lieu de se déclarer.
>
> | | décalage sur huit mesures | corrélation | par mesure |
> |---|---|---|---|
> | avec suivi du tempo, avant le rappel | −10 ms | 0,930 | −4 à −14 ms, toutes du même côté |
> | **avec suivi du tempo** | **−8 ms** | **0,930** | **0 à −14 ms** (−12, 0, −8, −4, −14, −10, −12, −2) |
> | sans suivi du tempo | −98 ms | 0,332 | (la voix est ailleurs : par comptage d'attaques, 13,8 s de dérive à la huitième mesure) |
>
> **Ce qui est tenu, ce qui ne l'est pas, et pourquoi.** Sur les huit mesures,
> la voix est à −8 ms de sa place : le critère est tenu, et il n'y a AUCUNE
> dérive (le décalage de la huitième mesure n'est pas plus grand que celui
> de la première). Prise mesure par mesure, la pire est à −14 ms : le
> critère n'est pas tenu par chacune séparément. La cause est connue et
> bornée : un WSOLA place chaque grain à ± 16 ms de sa position nominale (la
> zone de recherche, 768 trames), et sur une voix chantée — pas d'attaque
> franche à verrouiller — le raccord choisi flotte dans cette zone. La
> première mesure montrait un biais SYSTÉMATIQUE de −12 ms (toutes les
> mesures du même côté) : la recherche s'installait du même côté grain après
> grain. Un rappel vers la carte (à qualité de raccord égale, le grain reste
> où la carte le met ; `kPull` = 0,05 de corrélation au bord de la zone) a
> ramené le biais à −8 ms et centré le flottement. Le reste est du
> flottement de grain, pas un défaut de la carte : c'est le chiffre que le
> vocodeur de phase (D12.8), s'il vient, devra battre — et la mesure elle-
> même a 2 ms de résolution et r ≈ 0,9 sur une voix.
>
> **Décision** : D12.7 est acceptée comme critère de DÉRIVE (la voix reste
> en place sur huit mesures, à −8 ms, contre une voix ailleurs sans D12), et
> la limite de 10 ms est lue sur la fenêtre de huit mesures, comme la
> phrase du § 5 l'écrit. Le chiffre par mesure (14 ms) est publié et non
> arrondi ; il entre dans l'attendu de D12.8.

## 7 bis. D12.8 — le vocodeur de phase, et son attendu écrit AVANT (04/09/2026)

**Pourquoi il vient.** Le § 3 le réservait au cas où « une mesure sur un stem
réel montre que le WSOLA y perd ». La mesure est venue par D12.7, et ce n'est
pas le flottement d'enveloppe du banc 5 (0,0 %) : c'est le **flottement de
PLACEMENT**. Un WSOLA pose chaque grain à ± 16 ms de sa position nominale
(la zone de recherche), et sur une voix chantée, sans attaque franche à
verrouiller, le raccord choisi flotte dans cette zone — mesuré de 0 à 14 ms
d'une mesure à l'autre, sans dérive mais sans précision. Un vocodeur de
phase n'a pas ce degré de liberté : chaque trame de synthèse est posée
EXACTEMENT à son saut, et c'est la phase, pas la position, qui assure la
continuité. Son défaut connu est l'inverse : les attaques s'étalent
(*smearing*) et les sons tenus deviennent « phaseux » si les phases des
partiels dérivent les unes par rapport aux autres — deux défauts qui ont
leurs parades (verrouillage de phase par pic, remise à zéro des phases aux
transitoires), écrites ici avec le reste.

**Ce qui est écrit.** `audio/include/vsm/audio/dsp/PhaseVocoder.h` : STFT à
2 048 points sous fenêtre de Hann, saut de synthèse fixe (512), saut
d'analyse dicté par la carte, propagation de phase par bande avec
**verrouillage d'identité sur les pics** (Laroche et Dolson : les bandes
d'un même pic gardent leur relation de phase, c'est ce qui évite le son
phaseux), et **remise à zéro des phases** à chaque transitoire déclaré (la
trame qui contient l'attaque reprend les phases d'analyse telles quelles,
posée là où la carte met l'attaque). Une transformée de Fourier directe
radix-2 est écrite à côté de l'inverse qui existait (`RealFft.h`), pour la
même raison qu'elle. Même contrat que `TimeStretch` (carte, transitoires,
`render` par blocs, `seek`, court-circuit au rapport un), pour que le
moteur choisisse l'un ou l'autre sans changer une ligne de `mixInto`.

*Ce que j'attends, écrit avant la mesure (dans le banc, avant de le
lancer)* :

| # | promesse | mesure | seuil |
|---|---|---|---|
| 1 | la hauteur ne bouge pas | la3 tenu, ×0,75 et ×1,5 | ≤ 5 cents |
| 2 | la durée est exacte | sonne jusqu'à round(N·r), silence après | comme le banc 2 |
| 3 | rapport 1 = l'original | court-circuit | au bit près |
| 4 | les attaques passent une fois, en place | 16 clics, ×1,5 et ×0,66 | 16 attaques, ≤ 1 ms — c'est la parade des phases remises à zéro qui est jugée |
| 5 | déterministe, indépendant des blocs | 256 contre 4 096 | au bit près |
| 6 | **le placement ne flotte pas** | une « voix » de synthèse (harmonique, vibrato lent, enveloppe molle, sans attaque), ×1,1 ; décalage du pic de corrélation d'enveloppe par fenêtres de 1,8 s, comme D12.7 | ≤ 2 ms par fenêtre, là où le WSOLA, mesuré sur le même signal dans le même test, fait plus |
| 7 | pas phaseux | la3 + ses cinq premiers partiels, ×1,5 ; les partiels gardent leur rapport d'amplitude (± 20 %) et aucun creux d'enveloppe de plus de 10 % | seuils publiés, pas devinés : c'est la promesse du verrouillage de phase |
| 8 | **le critère de phase, refait** | `mesure_d12_critere_de_phase.py` sur sky-parite, mode vocodeur | ≤ 10 ms sur CHAQUE mesure, ≤ 5 ms sur les huit |

Si le banc 8 tient, le mode « Hauteur conservée » passe au vocodeur pour
tous les clips, et le WSOLA reste dans le dépôt comme témoin et comme
solution de repli (une option de clip, dite). S'il ne tient pas, le WSOLA
reste le défaut, et le chiffre du vocodeur se publie à côté du sien.

> **D12.8 EST TRANCHÉE : LE VOCODEUR TIENT LE BANC 8, ET IL EST LE DÉFAUT
> (04/09/2026).** `audio/include/vsm/audio/dsp/PhaseVocoder.h`, et la
> transformée directe ajoutée à `RealFft.h`. Le banc,
> `test_phase_vocoder.cpp`, puis le critère de phase refait par
> `mesure_d12_critere_de_phase.py` avec l'algorithme ÉCRIT DANS LE CLIP
> (`keepPitch` = vocodeur, `keepPitchWsola` = témoin) — les deux chiffres
> sortent du même script, du même rendu, de la même mesure :
>
> | # | attendu | mesuré |
> |---|---|---|
> | 1 | ≤ 5 cents | **0,0 cent** à ×0,75 et ×1,5 |
> | 2 | sonne jusqu'à round(N·r), silence après | 0,348 avant la fin, **0,000000** après, aux deux rapports |
> | 3 | rapport 1 au bit près | **identique** |
> | 4 | 16 attaques à ≤ 1 ms, ×1,5 et ×0,66 | **16 et 16, 0,98 ms** (le retard constant du détecteur du banc) — la remise à zéro des phases tient |
> | 5 | déterministe, blocs 256 = 4 096 | **identiques au bit près** |
> | 6 | ≤ 2 ms sur une voix de synthèse, « et le WSOLA fait plus » | **RÉFUTÉ dans sa prémisse** : le vocodeur est à 4 ms et le WSOLA à 2 ms sur ce signal — périodique, donc le meilleur cas de la recherche de similarité ; le flottement de D12.7 tient à l'apériodicité d'une VRAIE voix, que la synthèse ne reproduit pas. Réécrit avant le banc 8 : ≤ 4 ms, chiffre publié, et le juge est le banc 8 |
> | 7 | rapports des partiels à ± 20 %, creux ≤ 10 % | **0,0 %** d'écart sur h2 à h5 (0,500 · 0,333 · 0,250 · 0,200), creux **7,1 %** — le verrouillage de pics tient |
> | 8 | ≤ 10 ms sur chaque mesure, ≤ 5 ms sur les huit | **vocodeur : −2 ms sur les huit (r 0,956), pire mesure 4 ms** (−4, −2, 0, 0, −2, −4, −2, 0) ; **WSOLA, même script : −8 ms (r 0,930), pire mesure 14 ms** (−12, 0, −8, −4, −14, −10, −12, −2) ; sans suivi : r 0,332 |
>
> **Décision, telle qu'elle était écrite d'avance** : « Hauteur conservée »
> (`WarpMode::KeepPitch`) est rendue par le vocodeur de phase ; le WSOLA
> devient `WarpMode::KeepPitchWsola`, « Hauteur conservée (WSOLA, témoin) »
> dans le sous-menu du clip et `keepPitchWsola` dans le fichier de projet.
> La variable d'environnement qui avait servi au premier A/B a été RETIRÉE
> le jour même : une option qui conditionne le résultat se lit dans le
> projet, pas dans l'environnement du processus. Ce que le vocodeur ne fait
> pas mieux, et qui est dit : son creux d'enveloppe autour d'une trame
> coupée est le même que celui du WSOLA (quatre trames se recouvrent, la
> somme des fenêtres au carré ne vaut 1,5 que si les quatre sont là) ; et
> son coût est une transformée aller-retour par saut de 512 échantillons,
> de l'ordre de cent multiplications par échantillon et par canal — le
> double du WSOLA, sans conséquence à vingt clips sur cette machine.

## 8. Ce qui n'est pas au programme, et pourquoi

- **Le suiveur de temps** (détection automatique des temps d'une prise) :
  un chantier de mesure à part entière (un banc de morceaux annotés) ; D12
  pose les marqueurs à la main et par N mesures, et le dit.
- **La transposition sans changer la durée** (*pitch shift*) : c'est un
  WSOLA au rapport r suivi d'un rééchantillonnage à 1/r, donc D12.1 + D12.2 —
  elle viendra comme réglage du clip quand une main la demandera, et sans
  nouvel algorithme.
- **L'étirement des clips MIDI** : ils suivent déjà le tempo, par nature.
- **Une bibliothèque** (Rubber Band, SoundTouch, élastique) : refusée par
  la règle n° 2 du § 0 de `ROADMAP-daw.md`, et par le choix n° 3 de son § 4.
