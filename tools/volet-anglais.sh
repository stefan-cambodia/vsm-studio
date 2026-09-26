#!/usr/bin/env bash
# La garde de D411 : LE VOLET DES RÉSERVES NE GARDE AUCUNE PHRASE FRANÇAISE EN ANGLAIS.
#
# RÈGLE GARDÉE (26/09/2026) : une phrase composée par le moteur (« Piste 1
# (bass) : … ») ne se traduit que si un modèle de `kModeles` (ui/Langue.cpp) la
# RECONNAÎT. La garde de langue lit des CLÉS et ne voit pas un modèle qui a
# perdu sa phrase -- D409 : la phrase des notes douteuses avait grandi, son
# modèle non, et l'interface anglaise l'affichait en français.
#
# COMMENT. Un morceau reconstruit par dossier de campagne
# (`reconstruction/travail/*/`, le premier `morceau-*/course`), ouvert sous un
# HOME neuf (D77), relevé par `VSM_VOLET_LIGNES` (D408 -- le volet PEINT, il
# est invisible à `VSM_TEXTES_LISTE`). Les DONNÉES sont retirées avant le
# jugement : chemins, noms entre guillemets, nom de piste entre parenthèses.
# Est française une ligne qui garde un mot de la liste ci-dessous ou une
# lettre accentuée française.
#
# SE VOIT ROUGE : `tools/volet-anglais.sh fr` juge le volet FRANÇAIS, et doit
# déclarer françaises toutes ses lignes de réserve.
#
# N'EST PAS dans `verifier.sh --gardes` : elle lance l'application une fois
# par projet. Rend 0 si aucune ligne française, 1 sinon, 2 si le binaire ou
# les projets manquent.
#
#   tools/volet-anglais.sh [en|fr]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
langue="${1:-en}"
BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-volet-anglais-XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

projets=()
for campagne in reconstruction/travail/*/; do
    premier="$(find "$campagne" -maxdepth 3 -path '*/morceau-*/course/project.json' -print 2>/dev/null | sort | head -1)"
    [ -n "$premier" ] && projets+=("$(dirname "$premier")")
done
[ "${#projets[@]}" -gt 0 ] || { echo "REFUS : aucun morceau reconstruit sous reconstruction/travail"; exit 2; }

n=0
for projet in "${projets[@]}"; do
    n=$((n + 1))
    h="$brouillon/home-$n"; mkdir -p "$h"
    HOME="$h" VSM_LANGUE="$langue" VSM_VOLET_LIGNES=1 VSM_PROJET="$racine/$projet" \
        VSM_TAILLE=2117x1317 VSM_CAPTURE="$brouillon/capture-$n.png" \
        timeout 90 "$BIN" > "$brouillon/journal-$n.log" 2>&1
    rc=$?
    echo "$rc $projet" >> "$brouillon/projets.txt"
done

python3 - "$brouillon" "$langue" <<'EOF'
import pathlib, re, sys
brouillon, langue = pathlib.Path(sys.argv[1]), sys.argv[2]
MOTS = {"introuvable", "cherché", "piste", "pistes", "échantillon", "échantillon(s)",
        "aucun", "aucune", "réserve", "réserves", "ignoré", "ignorée", "inconnu",
        "inconnue", "signalée(s)", "douteuses", "chargé(s)", "échec", "indisponible",
        "elle", "elles", "avec", "sans", "sur", "dans", "une", "les", "des", "est",
        "pas", "appliqué", "restera", "silencieuse", "réglage", "effet", "profil"}
ACCENTS = re.compile(r"[éèêàùçôîâœ]")
DONNEES = [re.compile(p) for p in (r"\S*/\S*", r"«[^»]*»", r"“[^”]*”", r'"[^"]*"',
                                   r"^(Track|Piste) \d+ \([^)]*\)")]
def francaise(ligne):
    t = ligne
    for d in DONNEES: t = d.sub(" ", t)
    mots = {m.lower() for m in re.findall(r"[\w()']+", t)}
    return bool(mots & MOTS) or bool(ACCENTS.search(t))
fautes = total = 0
projets = (brouillon / "projets.txt").read_text().splitlines()
for k, entree in enumerate(projets, 1):
    rc, projet = entree.split(" ", 1)
    journal = (brouillon / f"journal-{k}.log").read_text(errors="replace")
    lignes = [l.split("VSM_VOLET_LIGNE : ", 1)[1] for l in journal.splitlines() if l.startswith("VSM_VOLET_LIGNE : ")]
    # Une ligne repliée (retrait de quatre espaces) continue la précédente : on juge la ligne entière.
    entieres = []
    for l in lignes:
        if l.startswith("    ") and entieres: entieres[-1] += " " + l.strip()
        else: entieres.append(l)
    avert = [l for l in journal.splitlines() if "inconnu" in l and l.startswith("VSM_")]
    fr = [l for l in entieres[1:] if francaise(l)]   # la première est le compte « N réserves »
    total += len(entieres); fautes += len(fr)
    print(f"  {'OK ' if not fr else 'FR '} rc={rc} {len(entieres):2d} ligne(s), {len(fr)} française(s)  {projet}")
    for l in avert: print(f"       AVERTISSEMENT : {l}")
    for l in fr: print(f"       « {l[:160]} »")
print(f"--- {langue} : {fautes} ligne(s) française(s) sur {total} relevée(s), {len(projets)} projet(s)")
sys.exit(1 if fautes else 0)
EOF
