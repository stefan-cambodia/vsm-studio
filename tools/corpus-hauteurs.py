#!/usr/bin/env python3
"""Le corpus synthétique sonne-t-il à la hauteur que sa propre vérité annonce ?

    analyse/.venv/bin/python tools/corpus-hauteurs.py [--morceaux 10]

POURQUOI (D266, 13/09/2026). En mesurant le rappel des notes brèves, deux parties
de `morceau-0001-g1` se sont transcrites systématiquement à côté : **cinq
demi-tons trop haut sur les 84 notes de l'une, huit trop bas sur 52 des 67 notes
de l'autre**. Or `verite.json` est l'étalon de TOUTE mesure de transcription du
projet : si le rendu d'une partie ne sonne pas à la hauteur que sa liste de notes
annonce, chaque chiffre publié sur cette partie est faux, et personne ne le voit.

CE QUI EST MESURÉ. Chaque stem VRAI (`stems-vrais/NN-role.wav`) est transcrit, et
l'on compte, pour chaque note transcrite, l'écart en demi-tons avec les notes
vraies qui commencent au même instant (±60 ms). L'écart MODAL de la partie — le
plus fréquent — dit si la partie est en place.

COMMENT SE LIT LE RÉSULTAT, et la règle est écrite avant de compter :
  * écart modal **0**            : la partie sonne où elle doit.
  * écart modal **±12 ou ±24**   : l'ambiguïté d'octave du transcripteur (B14),
                                   connue et mesurée ailleurs — pas un défaut du corpus.
  * **tout autre écart constant** : il faut alors DÉPARTAGER, et depuis D367 on
                                   le peut, parce que la vérité porte la hauteur
                                   LUE SUR LE RENDU (`desaccord_mesure_demi_tons`) :

        - le patch l'explique (un désaccord déclaré)      → « expliqué par … »
        - le rendu sonne dans la borne (±2 demi-tons)     → **TRANSCRIPTION**
        - le rendu sonne hors de la borne                 → **CONTREDIT** : le
          corpus est en cause, et c'est lui qu'il faut regarder
        - la hauteur du rendu est illisible, ou la vérité
          est antérieure à D367                           → **INDÉCIDABLE**, et
          surtout pas « juste » par défaut

POURQUOI CE DÉPARTAGE CHANGE LE VERDICT. Sur `s2`, cinq parties portaient un
écart constant et **aucune des cinq n'était un défaut du corpus** : quatre
sonnent à moins d'un demi-ton de ce qu'elles écrivent (jusqu'à **+0,06**), la
cinquième n'a pas de hauteur lisible. Sans la mesure du rendu, elles étaient
comptées contre le corpus — et l'on aurait réglé le corpus pour un défaut du
transcripteur.

La batterie est ignorée : ses numéros de note désignent des pièces de kit, pas
des hauteurs, et Basic Pitch ne transcrit pas de percussion.

CE QUE LA MESURE A TROUVÉ, ET QUI N'EST PAS UN DÉFAUT (D267). Cinq parties sur
74 contredisent leur vérité, et les cinq s'expliquent EXACTEMENT par le patch :
`vsm.pcmhybrid` porte un « Attack Tune » de ±24 demi-tons et un « Tone Detune »
de ±12, `vsm.obx` un « Osc2 Detune » de ±12 — et le corpus tire ses patchs au
hasard. Une partie dont l'oscillateur est désaccordé de +4,75 demi-tons SONNE
cinq demi-tons au-dessus de ce que sa liste de notes annonce, et le transcripteur
a raison de l'entendre là.

**`verite.json` est donc une vérité sur les notes ÉCRITES, pas sur la hauteur
SONNANTE.** Toute mesure de transcription compare de la hauteur entendue à de la
hauteur écrite : pour 12,7 % des notes mélodiques du corpus — celles des parties
désaccordées de deux demi-tons ou plus — les deux ne coïncident pas. La colonne
« patch » ci-dessous donne l'écart que le patch PRÉDIT ; quand il égale l'écart
mesuré, il n'y a rien à corriger dans le corpus, et tout à corriger dans la
lecture qu'on fait de ses chiffres.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
# La borne du § 7 bis : au-delà, le rendu contredit vraiment sa vérité.
BORNE_MESUREE = 2.0
CORPUS_DEFAUT = RACINE / "reconstruction/travail/s1-sec"
# Le corpus se DÉSIGNE (--corpus) : il y en a désormais plus d'un, et un outil
# qui n'en connaîtrait qu'un mesurerait toujours l'ancien en croyant juger le neuf.
CORPUS = CORPUS_DEFAUT
TOLERANCE = 0.06     # secondes : la fenêtre où deux notes sont « au même instant »


def ecart_modal(vraies: list[tuple[float, int]], transcrites: list[tuple[float, int]]) -> tuple[int, int, int]:
    """(écart le plus fréquent, son compte, le nombre de notes transcrites appariées)."""
    comptes: Counter[int] = Counter()
    appariees = 0
    for t, h in transcrites:
        proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
        if proches:
            appariees += 1
        for hv in proches:
            comptes[hv - h] += 1
    if not comptes:
        return 0, 0, appariees
    ecart, compte = comptes.most_common(1)[0]
    return ecart, compte, appariees


sys.path.insert(0, str(Path(__file__).resolve().parent))
from hauteur_sonnante import desaccords as _desaccords  # noqa: E402


def desaccords_du_patch(partie: dict) -> list[tuple[str, float]]:
    """(paramètre, désaccord en demi-tons) — règle partagée, jamais recopiée.

    L'implémentation vit dans `tools/hauteur_sonnante.py`. Elle y a été portée le
    13/09/2026 parce que ce fichier-ci et `confiance-contre-verite.py` en avaient
    chacun une version, et qu'elles ne disaient PAS la même chose : l'une lisait
    l'unité déclarée par la machine, l'autre supposait des demi-tons partout.
    Deux outils du dépôt donnaient donc deux vérités. Une règle, un endroit.
    """
    return _desaccords(partie)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--morceaux", type=int, default=10)
    p.add_argument("--corpus", type=Path, default=CORPUS_DEFAUT,
                   help="dossier du lot du banc synthétique (défaut : s1-sec)")
    a = p.parse_args()

    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    suspectes: list[str] = []
    transcription: list[str] = []
    indecidables: list[str] = []
    parties = 0
    print(f"{'morceau':18s} {'rôle':16s} {'machine':20s} {'vraies':>7} {'transcr.':>9} "
          f"{'écart modal':>12} {'sur':>6} {'patch':>8}")
    for dossier in sorted(a.corpus.glob("morceau-*"))[: a.morceaux]:
        verite = json.loads((dossier / "verite.json").read_text(encoding="utf-8"))
        for partie in verite.get("parties", []):
            if partie.get("role") == "batterie":
                continue
            fichier = dossier / partie["fichier"]
            if not fichier.is_file():
                print(f"  {dossier.name} {partie['role']} : {fichier.name} absent")
                continue
            parties += 1
            _, _, evts = predict(str(fichier), model_or_model_path=ICASSP_2022_MODEL_PATH)
            vraies = sorted((float(n[2]), int(n[0])) for n in partie["notes"])
            trans = sorted((float(e[0]), int(e[2])) for e in evts)
            ecart, compte, appariees = ecart_modal(vraies, trans)
            desaccords = desaccords_du_patch(partie)
            pire = -desaccords[0][1] if desaccords else 0.0
            marque = ""
            if ecart != 0 and ecart % 12 != 0 and appariees >= 10 and compte >= 0.5 * appariees:
                # LE PATCH EXPLIQUE-T-IL L'ÉCART ? Un désaccord d'oscillateur
                # déplace la hauteur sonnante sans toucher aux notes écrites :
                # ce n'est pas le corpus qui se contredit, c'est la mesure qui
                # compare deux choses différentes.
                coupable = next(((k, v) for k, v in desaccords if abs(-v - ecart) <= 1.0), None)
                # LE RENDU A-T-IL ÉTÉ MESURÉ ? Depuis D367, la vérité porte la
                # dérive LUE SUR LA SONDE (`desaccord_mesure_demi_tons`) et non
                # plus seulement celle qu'on déduit du patch. Quand elle est là et
                # qu'elle tient dans la borne, la partie SONNE où elle est écrite :
                # l'écart est celui du TRANSCRIPTEUR, pas du corpus. Sans cette
                # distinction, cinq parties de `s2` étaient accusées à tort, dont
                # une qui sonne à six centièmes de demi-ton de sa note.
                mesure = partie.get("desaccord_mesure_demi_tons", "absent")
                if coupable is not None:
                    marque = f"   (expliqué par {coupable[0]} = {coupable[1]:+.2f} st)"
                elif mesure != "absent" and mesure is not None and abs(float(mesure)) <= BORNE_MESUREE:
                    marque = f"   (le rendu sonne à {float(mesure):+.2f} st : c'est le TRANSCRIPTEUR)"
                    transcription.append(
                        f"{dossier.name} {partie['role']} ({partie['machine']}) : "
                        f"{ecart:+d} transcrit, {float(mesure):+.2f} st mesuré sur le rendu")
                elif mesure == "absent" or mesure is None:
                    marque = "   (INDÉCIDABLE : la hauteur du rendu n'a pas été mesurée)"
                    indecidables.append(
                        f"{dossier.name} {partie['role']} ({partie['machine']}) : {ecart:+d} transcrit, "
                        + ("vérité antérieure à D367" if mesure == "absent"
                           else "hauteur du rendu illisible"))
                else:
                    marque = "   <<< LE RENDU CONTREDIT LA VÉRITÉ"
                    suspectes.append(
                        f"{dossier.name} {partie['role']} ({partie['machine']}) : "
                        f"{ecart:+d} transcrit, {float(mesure):+.2f} st MESURÉ sur le rendu")
            print(f"{dossier.name:18s} {partie['role']:16s} {partie['machine']:20s} "
                  f"{len(vraies):7d} {len(trans):9d} {ecart:+11d} {compte:5d} {pire:+7.2f}{marque}")
    print(f"\nPARTIES {parties}  CONTREDITES {len(suspectes)}  "
          f"TRANSCRIPTION {len(transcription)}  INDECIDABLES {len(indecidables)}")
    for s in suspectes:
        print(f"  CONTREDIT     {s}")
    for s in transcription:
        print(f"  transcripteur {s}")
    for s in indecidables:
        print(f"  indécidable   {s}")

    # LE SECOND VERDICT, et c'est le plus important (13/09/2026). Qu'un écart
    # soit EXPLIQUÉ par le patch ne le rend pas inoffensif : une partie dont la
    # hauteur sonnante s'écarte de sa hauteur écrite fausse toute statistique de
    # hauteur qui la compte. Et un morceau dont TOUTES les parties mélodiques
    # sont dans ce cas n'a pas un mauvais score : il a un score qui ne veut rien
    # dire. `morceau-0001-g1` en est l'exemple — ses deux parties mélodiques sont
    # désaccordées de +4,75 et −8,25 demi-tons, et son F1 passe de 0,027 à 0,567
    # selon la hauteur qu'on compare : de dernier des dix à premier.
    print()
    print("MORCEAUX dont des parties mélodiques ne sonnent PAS à leur hauteur écrite :")
    inutilisables = 0
    for dossier in sorted(a.corpus.glob("morceau-*"))[: a.morceaux]:
        fichier = dossier / "verite.json"
        if not fichier.is_file():
            continue
        verite = json.loads(fichier.read_text(encoding="utf-8"))
        melodiques = [x for x in verite.get("parties", []) if x.get("role") != "batterie"]
        if not melodiques:
            continue
        decalees = [x for x in melodiques
                    if any(abs(v) >= 1.0 for _, v in desaccords_du_patch(x))]
        if not decalees:
            continue
        notes_decalees = sum(len(x.get("notes", [])) for x in decalees)
        notes_totales = sum(len(x.get("notes", [])) for x in melodiques)
        toutes = len(decalees) == len(melodiques)
        if toutes:
            inutilisables += 1
        print(f"  {dossier.name:18s} {len(decalees)}/{len(melodiques)} partie(s), "
              f"{notes_decalees}/{notes_totales} notes"
              + ("   <<< TOUTES : statistiques de hauteur INUTILISABLES" if toutes else ""))
    print(f"\nMORCEAUX_INUTILISABLES {inutilisables}")
    return 1 if suspectes else 0


if __name__ == "__main__":
    raise SystemExit(main())
