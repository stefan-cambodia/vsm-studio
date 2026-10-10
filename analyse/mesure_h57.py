"""H57a — LE PAD RENDU PAR LE MOTEUR : des sinus sous le flanger et le chorus du rack, sur trois extraits jamais
mesurés par la lignée (docs/CDC-reload-indifferenciable.md § 19).

Les mesures sont celles de H47 (`mesure_h47.py`, importé, sans en changer une) : l'oracle des notes tenues, les
bornes `B_égal` et `B_mesuré`, le log-mel des cases qui portent, l'équilibre par bande, la tenue. S'y ajoute la
LARGEUR (side/mid), lue sur l'extrait stéréo et sur le rendu stéréo — `render_track_offline` rend du mono : le
`rendu.wav` du moteur est relu ici avant d'être effacé.

Candidates, chacune sur les notes de l'oracle, au diapason de H42 :
  - `sinus nus` : `vsm.additive`, UN partiel, sans effet ;
  - `sinus + rack` : les mêmes, sous le flanger (`f1`, profondeur 0,46 aux moindres carrés, réinjection 0,
    dosage 0,47) puis le chorus (`f2`, 1,10 ms, dosage 0,41) — § 19 ;
  - les machines du parc à leur patch d'usine (les candidates de H47), pour l'attendu 3 : la meilleure au
    classement `D` (le sens de H47), et, publiée à côté, la meilleure au log-mel.

    mesure_h57.py mesurer <stem other stéréo> --moteur <vsm-render> --sortie <dossier>
    mesure_h57.py verdict <dossier>/mesure.json
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))
import mesure_h47 as h47  # noqa: E402

EXTRAITS: Tuple[Tuple[float, float], ...] = ((84.0, 92.0), (154.0, 162.0), (211.0, 218.0))
F1_HZ, F2_HZ = 1.750, 2.37   # H50
SINUS = {"additive.partialCount": 1.0, "additive.spectralTilt": 0.0, "voice.analogCharacter": 0.0,
         "envelope.1.attack": 0.01, "envelope.1.decay": 0.5, "envelope.1.sustain": 1.0, "envelope.1.release": 0.3}
FLANGER = {"effect.flanger.rate": F1_HZ, "effect.flanger.depth": 0.46, "effect.flanger.feedback": 0.0,
           "effect.flanger.mix": 0.47}
CHORUS = {"effect.chorus.rate": F2_HZ, "effect.chorus.depth": 1.10, "effect.chorus.mix": 0.41}
BASSE_HZ = 120.0
LARGEUR_TOLERANCE = 0.30
LARGEUR_PLANCHER = 0.01


def largeur(stereo: np.ndarray) -> Optional[float]:
    """Énergie side/mid d'un signal stéréo (n, 2) ; `None` s'il est mono ou muet."""
    if stereo.ndim != 2 or stereo.shape[1] < 2:
        return None
    m = (stereo[:, 0] + stereo[:, 1]) / 2.0
    s = (stereo[:, 0] - stereo[:, 1]) / 2.0
    em = float(np.mean(m ** 2))
    return None if em <= 0.0 else float(np.mean(s ** 2) / em)


def part_sous(mono: np.ndarray, sr: int, hz: float) -> float:
    """La part de l'énergie sous `hz`, en dB (le contrôle « la basse absente » du § 19)."""
    spectre = np.abs(np.fft.rfft(np.asarray(mono, dtype=np.float64))) ** 2
    f = np.fft.rfftfreq(len(mono), 1.0 / sr)
    total = float(spectre.sum())
    return -120.0 if total <= 0.0 else 10.0 * math.log10(max(float(spectre[f < hz].sum()), 1e-30) / total)


def lire_stereo(stem: Path, debut_s: float, fin_s: float) -> Tuple[np.ndarray, int]:
    import soundfile as sf

    info = sf.info(str(stem))
    sr = int(info.samplerate)
    a, b = int(round(debut_s * sr)), int(round(fin_s * sr))
    if b > info.frames:
        raise ValueError(f"{stem} : {info.frames / sr:.1f} s, l'extrait demande {fin_s:.1f} s")
    x, _ = sf.read(str(stem), start=a, stop=b, dtype="float64", always_2d=True)
    return x, sr


def ecrire_ecoute(chemin: Path, stereo: np.ndarray, sr: int, niveau: float) -> None:
    """Un `.wav` pour l'oreille, calé au niveau efficace de l'extrait."""
    import soundfile as sf

    r = h47.rms(stereo.mean(axis=1)) if stereo.ndim == 2 else h47.rms(stereo)
    y = stereo if r <= 0.0 else stereo * (niveau / r)
    sf.write(str(chemin), np.clip(y, -1.0, 1.0).astype(np.float32), sr, subtype="FLOAT")


def mesurer_un_extrait(a: argparse.Namespace, debut: float, fin: float, outil: Any) -> Dict[str, Any]:
    from analyzer import diapason
    from analyzer.vsm_distance_cache import cached_distance_for
    from analyzer.vsm_offline_render import render_track_offline
    from analyzer.vsm_project_export import ExportNote, ExportTrack
    from analyzer.vsm_reconstruct import melodic_machines
    from analyzer.vsm_track_arbitration import build_candidates
    from reconstruire import profils_pour_arbitrage
    import soundfile as sf

    stereo, sr = lire_stereo(a.stem, debut, fin)
    extrait = stereo.mean(axis=1)
    n, duree = len(extrait), len(extrait) / sr
    tranche_s = 4 * 4 * 60.0 / a.tempo
    distance = cached_distance_for("v2")(np.asarray(extrait, dtype=np.float32), sr)
    niveau = h47.rms(extrait)
    sous_basse = part_sous(extrait, sr, BASSE_HZ)
    pics = h47.pics_tenus(extrait, sr)
    notes = h47.notes_de_l_oracle(pics, a.la4) if pics else []
    print(f"\n=== EXTRAIT {debut:.0f}-{fin:.0f} s ({duree:.2f} s) : niveau {niveau:.4f}, largeur {largeur(stereo):.4f}, "
          f"énergie sous {BASSE_HZ:.0f} Hz {sous_basse:+.1f} dB")
    print(f"ORACLE : {len(pics)} pic(s), {len(notes)} note(s) — "
          + ", ".join(f"{x} ({h47.NOMS_DE_CLASSE[x % 12]})" for x in notes))
    fiche: Dict[str, Any] = {"debut": debut, "fin": fin, "niveau": niveau, "largeur": largeur(stereo),
                             "sous_basse_db": round(sous_basse, 2), "notes": notes,
                             "pics": [{k: round(v, 4) for k, v in p.items()} for p in pics]}
    if not notes:
        fiche["refus"] = "aucun pic tenu : l'oracle n'a rien à écrire"
        return fiche

    fiche["bornes"] = {
        "soi": h47.mesures_de(extrait, extrait, sr, outil, distance, tranche_s),
        "egale": h47.mesures_de(extrait, h47.borne_egale(notes, a.la4, n, sr), sr, outil, distance, tranche_s),
        "mesuree": h47.mesures_de(extrait, h47.borne_mesuree(pics, n, sr), sr, outil, distance, tranche_s),
    }
    for cle, nom in (("soi", "l'extrait contre lui-même"), ("egale", "B_égal"), ("mesuree", "B_mesuré")):
        print(h47._ligne(nom, fiche["bornes"][cle]))

    diapason.poser(a.la4)
    travail = a.sortie / "travail" / f"{debut:.0f}-{fin:.0f}"
    travail.mkdir(parents=True, exist_ok=True)
    export_notes = [ExportNote(note=x, velocity=h47.VELOCITE, start=0.0, duration=duree) for x in notes]
    ecoute = a.sortie / "ecoute"
    ecoute.mkdir(parents=True, exist_ok=True)
    ecrire_ecoute(ecoute / f"{debut:.0f}-{fin:.0f}-original.wav", stereo, sr, niveau)

    def rendre(machine: str, parametres: Dict[str, float], effets: List[Dict[str, Any]], profil: Optional[str],
               dossier: Path) -> Tuple[Optional[np.ndarray], Optional[np.ndarray]]:
        piste = ExportTrack(name="pad", machine=machine, parameters=dict(parametres),
                            notes=list(export_notes), effects=effets, profile=profil or "")
        dossier.mkdir(parents=True, exist_ok=True)
        mono = render_track_offline(piste, dossier, sr, duration=duree, tempo=a.tempo,
                                    binary=str(a.moteur), title="h57-pad")
        wav = dossier / "rendu.wav"
        st: Optional[np.ndarray] = None
        if wav.is_file() and wav.stat().st_size > 0:
            st, _ = sf.read(str(wav), dtype="float64", always_2d=True)
        wav.unlink(missing_ok=True)
        return mono, st

    def mesurer_rendu(nom: str, mono: Optional[np.ndarray], st: Optional[np.ndarray]) -> Dict[str, Any]:
        if mono is None or mono.size == 0:
            print(f"  {nom:44s} NON MESURÉ — rendu vide")
            return {"nom": nom, "ecartee": "rendu vide"}
        m = h47.mesures_de(extrait, mono, sr, outil, distance, tranche_s)
        m["nom"] = nom
        m["largeur"] = None if st is None else largeur(st[:n])
        print(h47._ligne(nom, m) + f"  largeur {'—' if m['largeur'] is None else format(m['largeur'], '.4f')}")
        return m

    mono_nus, st_nus = rendre("vsm.additive", SINUS, [], None, travail / "sinus-nus")
    nus = mesurer_rendu("sinus nus", mono_nus, st_nus)
    effets = [{"type": "flanger", "parameters": dict(FLANGER)}, {"type": "chorus", "parameters": dict(CHORUS)}]
    mono_rack, st_rack = rendre("vsm.additive", SINUS, effets, None, travail / "sinus-rack")
    rack = mesurer_rendu("sinus + rack", mono_rack, st_rack)
    # LE CONTRÔLE : les effets ont-ils ÉTÉ appliqués ? Une clé que le moteur ne connaît pas se perd sans un mot, et
    # « sinus + rack » serait alors « sinus nus » sous un autre nom.
    if mono_nus is not None and mono_rack is not None and mono_nus.size and mono_rack.size:
        k = min(len(mono_nus), len(mono_rack))
        ecart = h47.rms(np.asarray(mono_rack[:k]) - np.asarray(mono_nus[:k])) / max(h47.rms(np.asarray(mono_nus[:k])), 1e-12)
        rack["ecart_aux_nus"] = round(float(ecart), 4)
        print(f"  CONTRÔLE : écart efficace rack/nus {ecart:.3f} du niveau des nus"
              + ("  — LES EFFETS N'ONT RIEN CHANGÉ" if ecart < 0.01 else ""))
    if st_nus is not None:
        ecrire_ecoute(ecoute / f"{debut:.0f}-{fin:.0f}-sinus-nus.wav", st_nus[:n], sr, niveau)
    if st_rack is not None:
        ecrire_ecoute(ecoute / f"{debut:.0f}-{fin:.0f}-sinus-rack.wav", st_rack[:n], sr, niveau)
    fiche["sinus_nus"], fiche["sinus_rack"] = nus, rack

    parc: List[Dict[str, Any]] = []
    with h47_moteur(a) as moteur:
        machines = melodic_machines(moteur)
        candidates = build_candidates([], machines, profils_pour_arbitrage(moteur, machines))
        for i, c in enumerate(candidates):
            mono, st = rendre(c.machine, {}, [], c.profile, travail / f"parc-{i}")
            m = mesurer_rendu(h47.nom_de({"machine": c.machine, "profil": c.profile}), mono, st)
            m["machine"], m["profil"] = c.machine, c.profile
            if "rms" in m:
                m["ecartee"] = h47.ecartee_par_le_niveau(m["rms"], niveau)
            parc.append(m)
    classes = [m for m in parc if m.get("ecartee") is None and "D" in m]
    fiche["parc_premiere_D"] = min(classes, key=lambda m: m["D"]) if classes else None
    fiche["parc_meilleur_logmel"] = min(classes, key=lambda m: m["logmel"]) if classes else None
    fiche["parc_mesurees"] = len(parc)
    return fiche


def h47_moteur(a: argparse.Namespace) -> Any:
    from analyzer.vsm_engine import VsmEngine

    return VsmEngine(binary=str(a.moteur), sample_rate=44100)


def verdict(mesure: Dict[str, Any]) -> List[Tuple[str, int, str, str]]:
    """(extrait, attendu, tenu/échec/—, ce qui est lu) — les attendus du § 19."""
    lignes: List[Tuple[str, int, str, str]] = []
    for f in mesure["extraits"]:
        e = f"{f['debut']:.0f}-{f['fin']:.0f} s"
        if "refus" in f:
            lignes.append((e, 0, "refus", f["refus"]))
            continue
        b, nus, rack = f["bornes"], f["sinus_nus"], f["sinus_rack"]
        if "logmel" not in rack or "logmel" not in nus:
            lignes.append((e, 0, "refus", "un rendu de sinus est vide"))
            continue
        ok1 = b["mesuree"]["logmel"] <= b["egale"]["logmel"] and b["soi"]["logmel"] == 0.0
        lignes.append((e, 1, "tenu" if ok1 else "échec",
                       f"B_mesuré {b['mesuree']['logmel']:.2f}, B_égal {b['egale']['logmel']:.2f}, soi {b['soi']['logmel']:.2f}"))
        gain = nus["logmel"] - rack["logmel"]
        lignes.append((e, 2, "tenu" if gain >= 1.0 else "échec",
                       f"sinus + rack {rack['logmel']:.2f} dB contre sinus nus {nus['logmel']:.2f} ({gain:+.2f})"))
        p = f.get("parc_premiere_D")
        if p is None:
            lignes.append((e, 3, "refus", "aucune machine du parc classée"))
        else:
            d = rack["logmel"] - p["logmel"]
            lignes.append((e, 3, "tenu" if d < 0 else ("échec" if d > 1.0 else "entre les deux"),
                           f"contre {p['nom']} (1re au classement D) {p['logmel']:.2f} dB ({d:+.2f}) ; meilleure au log-mel : "
                           f"{f['parc_meilleur_logmel']['nom']} {f['parc_meilleur_logmel']['logmel']:.2f}"))
        ecart = rack["logmel"] - b["mesuree"]["logmel"]
        lignes.append((e, 4, "tenu" if ecart <= 2.0 else ("échec" if ecart > 3.0 else "entre les deux"),
                       f"à {ecart:+.2f} dB de B_mesuré"))
        w, wo = rack.get("largeur"), f["largeur"]
        if w is None or wo is None:
            lignes.append((e, 5, "refus", "largeur illisible"))
        else:
            ok5 = abs(w - wo) <= LARGEUR_TOLERANCE * wo and w >= LARGEUR_PLANCHER
            lignes.append((e, 5, "tenu" if ok5 else "échec", f"rendu {w:.4f} contre extrait {wo:.4f}"))
        t = rack.get("tenue_db")
        lignes.append((e, 6, "tenu" if t is not None and t >= -1.0 else "échec", f"tenue {t} dB"))
    return lignes


def imprimer_verdict(mesure: Dict[str, Any]) -> None:
    print("\nVERDICT (§ 19) :")
    for e, n, etat, texte in verdict(mesure):
        print(f"  {e:12s} attendu {n} : {etat:15s} {texte}")


def mesurer(a: argparse.Namespace) -> int:
    for p in (a.stem, a.moteur):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    outil = h47.outil_ecart()
    depart = time.time()
    a.sortie.mkdir(parents=True, exist_ok=True)
    extraits = []
    for debut, fin in EXTRAITS:
        try:
            extraits.append(mesurer_un_extrait(a, debut, fin, outil))
        except ValueError as e:
            print(f"REFUS : {debut:.0f}-{fin:.0f} s : {e}")
            extraits.append({"debut": debut, "fin": fin, "refus": str(e)})
    mesure: Dict[str, Any] = {"extraits": extraits,
              "provenance": {"stem": str(a.stem), "moteur": str(a.moteur), "la4": a.la4, "tempo": a.tempo,
                             "sinus": SINUS, "flanger": FLANGER, "chorus": CHORUS,
                             "duree_s": round(time.time() - depart, 1)}}
    imprimer_verdict(mesure)
    sortie = a.sortie / "mesure.json"
    sortie.write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True, default=float) + "\n",
                      encoding="utf-8")
    print(f"ÉCRIT : {sortie} ({mesure['provenance']['duree_s']} s) ; à écouter : {a.sortie / 'ecoute'}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("stem", type=Path)
    m.add_argument("--moteur", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--la4", type=float, default=h47.LA4_HZ)
    m.add_argument("--tempo", type=float, default=h47.TEMPO_BPM)
    v = sous.add_parser("verdict")
    v.add_argument("mesure", type=Path)
    a = ap.parse_args()
    if a.mode == "verdict":
        if not a.mesure.is_file() or a.mesure.stat().st_size == 0:
            print(f"REFUS : {a.mesure} absent ou vide")
            return 2
        imprimer_verdict(json.loads(a.mesure.read_text(encoding="utf-8")))
        return 0
    return mesurer(a)


if __name__ == "__main__":
    sys.exit(main())
