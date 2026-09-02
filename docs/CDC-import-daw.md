# Importer un projet d'un autre DAW — cahier des charges

VSM Studio doit pouvoir **ouvrir un projet fait ailleurs** : Ableton Live, FL
Studio, Cubase. Ce document dit ce que chaque format permet réellement, ce
qu'on importera, et ce qu'on n'importera pas — avec la raison.

## 0. La règle qui prime : ne jamais faire semblant d'avoir importé

Un import partiel est utile. Un import partiel **qu'on croit complet** est
nuisible : le musicien perd des heures à chercher pourquoi son morceau ne sonne
pas comme chez lui. La règle du dépôt s'applique ici plus qu'ailleurs — *ce qui
est écarté, ignoré ou remplacé est DIT* — et elle prend une forme précise :

**tout import produit un RAPPORT** qui liste, poste par poste, ce qui a été lu,
ce qui a été approché, et ce qui a été perdu. Ce rapport n'est pas un journal
de mise au point : c'est une partie du résultat, au même titre que les notes.

## 1. Ce que les trois formats sont réellement

| Format | Nature | Documentation | Ce qu'on peut en tirer |
|---|---|---|---|
| **Ableton Live** `.als` | **XML** compressé en gzip | Aucune spécification publique, mais le XML est LISIBLE et ses balises sont explicites (`MidiTrack`, `KeyTrack`, `MidiNoteEvent`…) | Tempo, signature, pistes, clips MIDI, notes avec durée et vélocité, noms, couleurs, muet/solo |
| **FL Studio** `.flp` | **Binaire**, suite d'événements typés (octet, mot, double-mot, texte) | Aucune spécification publique ; format déchiffré par rétro-ingénierie, stable depuis des années et documenté par plusieurs projets libres | Tempo, motifs, notes (blocs de 24 octets), noms de canaux, ordre de lecture |
| **Cubase** `.cpr` | **Binaire** propriétaire | **Aucune** documentation, publique ou reconstituée, exploitable | Voir § 4 — la réponse honnête n'est pas « on lira le .cpr » |

## 2. Ce qu'on importe, et ce qu'on n'importera jamais

**Ce qui se transporte** : la musique et la structure — tempo, signature,
pistes, notes (hauteur, position, durée, vélocité), noms, couleurs, muet et
solo, marqueurs quand il y en a.

**Ce qui ne se transporte pas, et ce n'est pas un manque d'effort** :

- **Les instruments et les effets.** Un projet Live utilise Operator, Wavetable
  ou un VST tiers ; ces machines n'existent pas ici et leurs réglages n'ont
  aucun équivalent. Une piste importée arrive donc **sans instrument
  assigné**, et le rapport le dit piste par piste. Prétendre convertir un
  patch d'Operator en `vsm.dx7` serait inventer un son que personne n'a écrit.
- **L'audio.** Les clips audio référencent des fichiers hors du projet, souvent
  absents. On importe la RÉFÉRENCE et son placement, et le rapport dit si le
  fichier a été trouvé.
- **L'automation des plugins tiers**, pour la même raison que les instruments.

## 3. Le décompresseur : écrit ici, comme la transformée de Fourier

Un `.als` est un XML gzippé, il faut donc savoir décompresser du DEFLATE
(RFC 1951). `zlib` est présent sur la machine, mais **`interchange` n'a aucune
dépendance externe** et c'est une propriété qu'on garde : le même jour, la
synthèse spectrale a reçu sa propre IFFT (`dsp/RealFft.h`) plutôt qu'une
bibliothèque, pour la même raison. Un inflate tient en deux cents lignes et se
teste contre des cas connus ; une dépendance se porte, se fige et s'explique
pour toujours.

## 4. Cubase : la réponse honnête

**Le format `.cpr` est fermé et non documenté.** Il n'existe aucune
spécification publique ni aucune rétro-ingénierie assez complète pour en
extraire des notes de façon fiable. Écrire un lecteur au jugé produirait un
import qui marche sur le fichier d'essai et casse sur le suivant — c'est-à-dire
exactement la « panne muette » que ce dépôt refuse partout ailleurs.

**Ce qui MARCHE pour venir de Cubase, et qu'il faut donc offrir et
documenter :**

1. **Track Archive XML** (`.xml`) — Cubase exporte ses pistes dans un format
   XML documenté par Steinberg (*Fichier ▸ Exporter ▸ Archive de pistes*). Il
   contient les pistes, les événements MIDI et le tempo. **C'est le chemin
   recommandé**, et il donne un import de qualité comparable à celui de Live.
2. **MIDI Type 1** (`.mid`) — tout Cubase l'exporte, VSM Studio le lit déjà.

Le lecteur `.cpr` n'est donc pas écrit, et **l'interface doit le dire quand on
lui présente un `.cpr`** : un message qui nomme les deux chemins ci-dessus vaut
mieux qu'un import qui échoue sans expliquer. Le jour où une spécification
apparaît, ce paragraphe sera la première chose à corriger.

## 5. Ordre de marche

1. `Inflate` — décompression gzip/DEFLATE, avec ses tests.
2. **Ableton `.als`** — le plus rentable : le XML est lisible et l'essentiel y
   est nommé.
3. **FL Studio `.flp`** — lecteur d'événements binaires.
4. **Cubase** — Track Archive XML, et le message qui oriente pour le `.cpr`.
5. Un rapport d'import commun aux quatre chemins, et l'interface qui l'affiche.

## 6. Critères d'acceptation

```
[ ] Chaque lecteur a ses tests sur des fichiers construits DANS le test
    (aucune dépendance à un fichier d'exemple qu'on n'a pas le droit de
    redistribuer)
[ ] Un fichier tronqué ou corrompu donne une ERREUR nommée, jamais un
    plantage ni un projet à moitié rempli
[ ] Tout ce qui n'est pas importé figure dans le rapport, poste par poste
[ ] Les pistes arrivent sans instrument, et le rapport le dit
[ ] Aucune dépendance externe ajoutée
[ ] Toutes les suites vertes, zéro warning
```
