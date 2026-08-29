# Cahier des charges — ajouter une machine

**Nature du document.** Ceci décrit ce qu'une nouvelle machine **doit
satisfaire** pour être considérée comme terminée. Le « comment » pas à pas,
avec squelette copiable, reste dans [`GUIDE-ajout-machine.md`](GUIDE-ajout-machine.md) ;
ce document-ci sert de critère d'acceptation, à relire avant de déclarer une
machine finie.

**Pourquoi il existe.** Le guide date de la Phase 3, quand une machine n'était
qu'un DSP et une batterie de tests. Depuis, une machine doit aussi : porter des
identités sémantiques, apparaître dans les presets et l'interop CLAP, avoir une
façade, et figer son rendu par une empreinte. Une machine « finie » au sens du
guide serait aujourd'hui incomplète — et incomplète en silence, ce qui est le
pire cas.

---

## 0. Règle qui prime sur tout

**Ajouter une machine ne modifie ni le moteur, ni l'interface.** Concrètement :
`ProcessGraph`, `AudioEngine`, `TrackListComponent`, `SynthRackComponent`, le
piano roll et le mixer ne sont **jamais** touchés. Une machine se déclare, elle
ne s'immisce pas.

Les seuls points de couplage autorisés sont énumérés en §7. S'il en faut un
autre, c'est le signe qu'il manque une abstraction — la créer d'abord, ajouter
la machine ensuite.

---

## 1. Contrat DSP (obligatoire)

- Implémente `vsm::audio::plugin::ISynthPlugin` **en entier**, y compris
  `activeVoiceCount()` (renvoyer une valeur honnête ; 0 si la notion n'existe
  pas pour cette machine).
- `process()` respecte les règles temps réel **sans exception** : aucune
  allocation, aucune libération, aucun verrou, aucune I/O, aucun log. Tout ce
  qui alloue se fait dans `initialize()`.
- Les paramètres transitent par `std::atomic` : `setParameter()` et
  `getParameter()` sont appelables depuis l'UI, le thread MIDI et le thread
  audio.
- Tout aléatoire passe par `vsm::util::DeterministicRng`, **seedé**. Jamais
  `rand()`, jamais `std::random_device`. Deux rendus d'une même session doivent
  être identiques au bit près — c'est ce qui rend possibles les empreintes de
  non-régression et la reconstruction pilotée par le projet d'analyse.
- Les valeurs de paramètres sont exprimées en **unités physiques** (Hz,
  secondes, demi-tons, dB), jamais en normalisé 0..1. C'est ce qui permet à un
  outil extérieur d'écrire « 1200 Hz » sans connaître la machine.

## 2. Nommage des paramètres

- Un nom d'affichage **stable** par paramètre : il sert de clé à la table
  sémantique (§4) et de sérigraphie sur la façade (§5). Le renommer est un
  changement cassant, à faire sciemment et en même temps que les deux.
- Vocabulaire cohérent avec l'existant : `Filter Cutoff`, `Env Attack`,
  `Osc2 Detune`, `LFO Rate`… Un nouveau mot pour une notion déjà nommée
  ailleurs est un défaut, pas un style.
- Un paramètre = une notion. Deux réglages qui bougent ensemble restent deux
  paramètres si la machine d'origine en a deux.

## 3. Tests DSP (obligatoires)

Batterie minimale, dans `audio/tests/test_<machine>_synth.cpp` :

1. `..._registered` — présente dans le registre.
2. `..._silent_with_no_events` — sortie strictement nulle, zéro voix active.
3. `..._note_produces_sound` — signal fini (`std::isfinite`), amplitude non nulle.
4. **Comportement spécifique à la machine** — le cœur du travail : ce qui la
   distingue (accent, slide, poly-mod, choke, duophonie, sync, ring mod,
   non-sensibilité à la vélocité…). Un test générique ne prouve rien ici.
5. `..._is_deterministic` — deux rendus identiques au bit près, avec le
   caractère analogique **activé** (sinon on ne teste pas le RNG).
6. `..._save_load_roundtrip` — état sauvegardé puis rechargé à l'identique.
7. `..._parameter_list_size` — verrouille le nombre de paramètres, pour qu'un
   ajout ou une suppression soit un geste conscient.

**Stabilité sous conditions extrêmes** : résonance maximale, notes graves et
aiguës, blocs très courts, vélocité 1 et 127. Aucun NaN, aucune divergence.

## 4. Identité sémantique (obligatoire depuis la Phase 7)

- **Chaque** paramètre reçoit un identifiant sémantique dans
  `interchange/src/ParameterDescriptor.cpp`. Le test de complétude échoue
  sinon — c'est voulu : un paramètre sans identité est inatteignable depuis un
  preset, un script Python ou un hôte CLAP.
- Réutiliser le vocabulaire existant quand la fonction existe déjà
  (`filter.1.cutoff`, `envelope.1.attack`…) : c'est ce qui permet à un preset
  de voyager d'une machine à l'autre.
- Créer un identifiant propre à la machine quand la fonction lui est propre
  (`accent.amount`, `polyMod.toFilterCutoff`, `fm.operator.3.ratio`). Mieux
  vaut un nom spécifique honnête qu'un rapprochement approximatif.
- Deux paramètres ne peuvent pas partager une identité (test d'unicité).
- Les `clap_id` se déduisent automatiquement des identifiants sémantiques :
  rien à écrire, mais **rien à renommer non plus** sans casser les projets des
  utilisateurs qui automatisent ces paramètres dans un hôte.

## 5. Façade (obligatoire)

- Une description dans `panels/src/MachinePanels.cpp`, jamais un composant
  graphique dédié.
- **La disposition suit la machine d'origine**, pas une logique de rangement :
  trajet du signal si l'original le porte, une colonne par pièce pour une boîte
  à rythmes, curseurs si l'original en a. Regrouper « tous les niveaux » puis
  « tous les decays » est plus compact et rend l'instrument inutilisable.
- Chaque paramètre est soit posé sur la façade, soit déclaré dans
  `omittedParameters` **avec sa raison**. Les tests refusent l'oubli silencieux.
- Séquenceur intégré (`SequencerSpec`) **si et seulement si** la machine
  d'origine en a un : sur une boîte à rythmes, il n'est pas un accessoire, il
  est l'instrument.
- Regarder le résultat avant de conclure :
  `cmake --build build --target vsm-panel-preview` puis rendre les PNG. Les
  tests garantissent la cohérence, jamais la lisibilité.

**Ce qui est reproduit** : agencement, familles de commandes, code couleur,
matière du châssis. **Ce qui ne l'est pas** : logos, marques, sérigraphie
littérale. Le suffixe « -style » vaut aussi pour l'image.

## 6. Empreinte de non-régression audio (obligatoire)

- Ajouter `VSM_TEST(regression_<machine>) { checkMachine("vsm.<machine>"); }`
  dans `audio/tests/test_audio_regression.cpp`.
- Générer l'empreinte :
  `VSM_REGEN_AUDIO_FINGERPRINTS=1 ./build/audio/vsm_audio_tests`, puis recopier
  la ligne dans `audio/tests/audio_fingerprints.inc`.
- Un test garde-fou refuse toute machine enregistrée sans empreinte.

C'est cette empreinte qui protégera la machine des **modifications faites pour
une autre** : une brique DSP partagée (filtre, enveloppe, oversampler) touchée
ailleurs fera échouer ici, à l'endroit exact du dommage.

## 7. Points de couplage autorisés (et seulement ceux-là)

1. `audio/plugins/<machine>/` — les sources de la machine.
2. `VSM_REGISTER_SYNTH_PLUGIN(...)` en dernière ligne du `.cpp`, dans le
   namespace de la machine.
3. Force-link dans `audio/src/plugin/BuiltInPlugins.cpp` (+ include).
4. Deux lignes dans `audio/CMakeLists.txt` (source + test).
5. La table sémantique (§4), la façade (§5), l'empreinte (§6).

Rien d'autre. Si un cinquième fichier moteur doit changer, la conception est à
revoir.

## 8. Authenticité et honnêteté (§27 d'ARCHITECTURE.md)

- **Ne jamais prétendre une identité au matériel sans mesure.** Aucune machine
  du projet n'a été comparée à du hardware réel : le statut honnête est
  `derived`, jamais `measured`.
- **Documenter dans le code** chaque approximation : topologie de filtre
  substituée, sync sans correction BLEP, cymbales synthétisées au lieu
  d'échantillonnées, facteurs choisis « au caractère » et non mesurés.
- Une simplification tue si elle est cachée, pas si elle est écrite.

## 9. Qualité de build

- Zéro warning, y compris sous les flags stricts :
  `-Wall -Wextra -Wpedantic -Wshadow -Wfloat-equal -Wsign-conversion`.
- Les tests existants restent verts — sans exception et sans « c'était déjà
  cassé ».

## 10. Critères d'acceptation — la machine est finie quand

```
[ ] ISynthPlugin implémenté, règles temps réel respectées dans process()
[ ] Aléatoire seedé, rendu déterministe au bit près
[ ] Valeurs en unités physiques
[ ] Batterie de tests §3 complète, dont AU MOINS un test du trait distinctif
[ ] Chaque paramètre a une identité sémantique unique
[ ] `handleControlEvent` traité : la machine honore la molette de hauteur et
    les contrôleurs qui ont un sens pour elle, ou renvoie false en connaissance
    de cause
[ ] Façade décrite, chaque paramètre posé ou omis avec raison
[ ] Séquenceur intégré si l'original en a un
[ ] Aperçu PNG regardé, disposition fidèle à la machine d'origine
[ ] Empreinte de non-régression générée et committée
[ ] Approximations documentées dans le code
[ ] Zéro warning sous les flags stricts, toutes les suites vertes
[ ] ARCHITECTURE.md mis à jour (compte de tests, section machine)
```

**Sur la molette de hauteur et les contrôleurs.** Le moteur livre désormais aux
machines tout le MIDI qui n'est pas une note (`MidiControlEvent`), et le défaut
de `ISynthPlugin::handleControlEvent` est de répondre `false`. Répondre `false`
est un choix parfaitement légitime -- une boîte à rythmes n'a que faire d'un
pitch bend -- mais c'est un choix, pas un oubli : le moteur COMPTE ce qui a été
refusé (`ProcessGraph::ignoredControlEvents()`), pour que l'interface puisse
dire pourquoi une modulation ne s'entend pas. Les cinq machines à voix unique ou
double du parc (Minimoog, TB-303, SH-101, MS-20, ARP Odyssey) l'honorent ; les
polyphoniques restent à faire, machine par machine, et cette case est l'endroit
où on s'en souvient.

Une case non cochée n'est pas un détail à finir plus tard : c'est une machine
qui se comportera correctement aujourd'hui et se dégradera en silence dans six
mois, quand quelqu'un touchera une brique partagée.
