# Piloter le moteur du DAW depuis Python

Ce dossier fait maintenant partie du dépôt VSM Studio. Les notes candidates
peuvent donc être rendues par le **moteur réel** -- celui de l'application et
des plugins CLAP -- au lieu du synthé Python approximatif.

**Pourquoi ça change tout** : un patch optimisé contre l'approximation Python
sonne différemment une fois chargé dans la vraie machine, et l'écart
n'apparaît qu'à la fin. En rendant les candidats avec le moteur, ce qu'on
optimise est exactement ce qu'on entendra.

## Mise en route

```bash
cmake --build build --target vsm-render      # depuis la racine du dépôt
```

```python
from analyzer.vsm_engine import VsmEngine

with VsmEngine(sample_rate=44100) as engine:
    audio = engine.render_note(
        "vsm.tb303",
        {"filter.1.cutoff": 700.0, "filter.1.resonance": 0.8},
        midi_note=45,
        duration=1.0,
    )   # numpy float32 mono
```

Les paramètres portent des noms **sémantiques** (`filter.1.cutoff`,
`envelope.1.attack`, `fm.operator.3.ratio`...), identiques d'une machine à
l'autre quand la fonction existe. Aucun identifiant interne à connaître.

## Chercher un patch

```python
from analyzer.vsm_patch_optimizer import optimize_patch_for_machine, choose_machine

with VsmEngine(sample_rate=44100) as engine:
    best, tous = choose_machine(
        target_audio, midi_note=45, engine=engine,
        machines=["vsm.minimoog", "vsm.tb303", "vsm.juno106"],
    )
    print(best.machine, best.parameters)
```

`choose_machine` cherche sur plusieurs machines et classe les résultats : on ne
sait pas d'avance laquelle a servi, et présenter le résultat d'une seule
machine comme « le » patch reviendrait à faire passer un choix arbitraire pour
une conclusion.

## Ce que ça coûte, mesuré

| Opération | Coût |
|---|---|
| Rendu d'une note d'une seconde | **~10 ms** |
| Distance audio (`audio_distance`) | ~16 ms |
| Distance à cible pré-calculée (`vsm_distance_cache`) | **~8 ms** |
| Évaluation complète dans l'optimiseur | ~21 ms |
| Recherche complète (1722 évaluations) | ~36 s |

Deux constats qui ont guidé la conception :

- **Un processus par rendu coûtait 24 ms**, dont l'essentiel en démarrage.
  D'où `vsm-render --serve` : le processus reste vivant et répond à des
  requêtes JSON, une par ligne.
- **La métrique coûtait plus cher que le moteur.** `audio_distance` recalcule
  les caractéristiques de la CIBLE à chaque appel, alors qu'elles ne changent
  jamais. `CachedTargetDistance` reprend la formule à l'identique (vérifié :
  écart de 1e-8) en mémorisant cette partie -- deux fois plus rapide.

## Déterminisme

Deux requêtes identiques donnent le **même** audio, octet pour octet. C'est la
condition de toute optimisation : sans elle, la recherche comparerait le
résidu de la note précédente autant que le patch.

Cela a demandé une correction : le service gardait d'abord les machines
instanciées en mémoire, en supposant que les créer coûtait cher. Mesure faite,
créer et initialiser un Minimoog, un DX7 ou un Jupiter-8 coûte **moins de
0,01 ms** contre ~10 ms de rendu. Le cache ne gagnait rien et cassait le
déterminisme : chaque requête part maintenant d'une instance neuve.

## Ce que le pont ne fait pas

- Il ne traduit pas les paramètres sans équivalent (`distortion`, mixages
  d'effets) : ils sont **omis**, jamais rapprochés d'un réglage « à peu près
  semblable » qui produirait un son faux sans le dire.
- Il ne choisit pas la machine à votre place : `choose_machine` classe, il ne
  tranche pas.

## Le reste de la chaîne

Pour produire un morceau complet plutôt qu'une note, le format de projet est
déjà en place (`project.json` + MIDI + presets), rendu par `vsm-render` sans
interface :

```bash
./build/tools/vsm-render mon-projet/ sortie.wav --tail 2
```

Voir `docs/examples/demo-project/` pour un dossier complet et jouable.
