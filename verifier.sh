#!/usr/bin/env bash
#
# Passe TOUS les garde-fous du dépôt, et dit ce qu'il n'a pas pu passer.
#
#     ./verifier.sh            les suites déjà compilées, puis Python, lint, types
#     ./verifier.sh --compiler compile d'abord les cibles de test (deux travaux)
#     ./verifier.sh --gardes   seulement les gardes des sources de tools/ (D378)
#     ./verifier.sh --bancs    seulement les bancs qui LANCENT l'application (D504)
#
# POURQUOI CE FICHIER. Les garde-fous existaient tous — cinq suites C++, une
# suite Python, `ruff check .` déclaré par `ruff.toml`, `mypy` déclaré par
# `mypy.ini` — et RIEN ne les lançait ensemble. Il n'y a pas d'intégration
# continue dans ce dépôt : la vérification est un geste, et un geste qui demande
# huit commandes n'est pas fait. Celui-ci en demande une.
#
# CE QU'IL NE FAIT PAS, ET C'EST DÉLIBÉRÉ. Il ne compile rien sans qu'on le lui
# demande. Remplacer `build/tools/vsm-render` pendant qu'une reconstruction
# tourne tue la course (règle du dépôt) ; et à `-j` élevé une compilation JUCE
# pendant une séparation demucs se fait tuer par le manque de mémoire. Avec
# `--compiler`, il compile à DEUX travaux, ce qui est la limite écrite.
#
# LES GARDES DE tools/ (D378). La règle du dépôt (D150) range dans tools/ ce
# qui doit empêcher une régression -- et ce fichier n'en lançait AUCUNE : une
# garde que rien ne rejoue n'empêche rien. Ne passent ici que celles qui lisent
# les SOURCES seules (ni application lancée, ni image, ni audio) et rendent 1 à
# la faute ; les bancs qui lancent l'application se jouent par `--bancs` (D504). `--gardes`
# ne passe qu'elles : elles tiennent en quelques secondes et peuvent tourner
# pendant une campagne, ce que la suite Python entière ne peut pas.
#
# Le code de sortie vaut 0 si tout ce qui a pu être passé est vert.

set -u
cd "$(dirname "$0")"

VENV=analyse/.venv/bin/python
ECHECS=0
SAUTES=0

titre()  { printf '\n\033[1m== %s\033[0m\n' "$1"; }
vert()   { printf '   \033[32m✓\033[0m %s\n' "$1"; }
rouge()  { printf '   \033[31m✗\033[0m %s\n' "$1"; ECHECS=$((ECHECS + 1)); }
saute()  { printf '   \033[33m—\033[0m %s\n' "$1"; SAUTES=$((SAUTES + 1)); }

GARDES_SEULES=0
[ "${1:-}" = "--gardes" ] && GARDES_SEULES=1

# D504 : LES BANCS QUI LANCENT L'APPLICATION. Ils se jouaient « à part » (D378),
# c'est-à-dire à la main, quatre ou cinq à la fois : rien ne rejouait les trente et un (quarante-trois depuis D529 — la liste en comptait 40 et non 41 avant D525, compté le 02/10),
# et un script que rien ne rejoue n'est pas une garde (D150). Liste FERMÉE, comme
# celle des gardes : un banc neuf n'y entre qu'écrit ici.
# HORS LISTE, ET POURQUOI : `reconstruction-annuler.sh` lance une vraie séparation
# demucs (son en-tête le réserve aux changements de la chaîne) ; `apres-campagne`,
# `comparer-rendus` et `garder-batterie` ne sont pas des gardes.
if [ "${1:-}" = "--bancs" ]; then
    titre "Bancs qui lancent l'application (D504)"
    # PAS PENDANT UNE CAMPAGNE : certains bancs exportent, et un export pendant une
    # course la triple (13/09). Le motif est dans CE fichier, pas dans la ligne de
    # commande d'un shell : `pgrep -f` ne se trouve pas lui-même ici.
    if course=$(pgrep -af "reconstruire\.py|corpus\.py|demucs" | head -1) && [ -n "$course" ]; then
        rouge "une campagne tourne — bancs NON passés : $course"
        exit 1
    fi
    # D516 : UN ÉCRAN ÉTEINT OU VERROUILLÉ fausse les bancs de focus et de clic : la
    # fenêtre passe pour invisible (D94). Le 29/09, l'écran s'est éteint pendant la
    # série (batterie à 16 %) ; deux bancs sont tombés, et le binaire de la veille
    # tombait pareil. L'état de l'écran est relevé AVANT et APRÈS, et dit.
    ecran() {
        local e=""
        command -v kscreen-doctor > /dev/null && kscreen-doctor --dpms show 2>/dev/null | grep -q ": off" && e="éteint"
        # D520 : la session qui porte un SIÈGE — la première de la liste est la session
        # « manager » de systemd, qui dit toujours LockedHint=no : le verrou n'était
        # jamais vu (le 29/09, « éteint » seul, alors que la session était verrouillée).
        local n; n=$(loginctl 2>/dev/null | awk -v u="$USER" '$3 == u && $4 ~ /seat/ {print $1; exit}')
        [ -n "$n" ] && loginctl show-session "$n" -p LockedHint 2>/dev/null | grep -q "=yes" && e="${e:+$e, }verrouillé"
        echo "$e"
    }
    ecran_avant=$(ecran)
    [ -n "$ecran_avant" ] && rouge "écran $ecran_avant AU DÉPART : les bancs de focus et de clic ne se jugent pas (D94, D516)"
    BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
    [ -x "$BIN" ] || { rouge "binaire absent — ./verifier.sh --compiler, ou compiler la cible de l'application"; exit 1; }
    # D577 : LES JOURNAUX SUR LE DISQUE, PAS SUR `/tmp`. `/tmp` est un tmpfs : le 23/10, le poste éteint à 07 h 25 a
    # emporté ceux de la série de D573 (150 bancs lancés à 04 h 52), et son bilan n'a jamais été lu. `build/` survit à
    # une extinction et n'est pas suivi par git.
    journaux="${VSM_JOURNAUX_BANCS:-$PWD/build/verifier-bancs}"
    mkdir -p "$journaux"
    # LES PRÉFÉRENCES DE L'UTILISATEUR, copiées JUSTE AVANT la série (D77 : jamais
    # contre une copie ancienne — l'application sert pendant qu'on travaille).
    prefs="$HOME/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    [ -f "$prefs" ] && cp "$prefs" "$journaux/preferences-avant.settings"
    # D578 : LA MÉMOIRE DU COMPOSITEUR, RELEVÉE AUTOUR DE CHAQUE BANC, ET LA SÉRIE ARRÊTÉE AVANT QU'`oomd` NE TUE LA
    # SESSION. Le 23/10, deux fois : `systemd-oomd` a tué `plasma-kwin_wayland.service` (le compositeur, Xwayland, la
    # session graphique ENTIÈRE de l'utilisateur) à 07 h 24 pendant la série de D573, puis à 12 h 14 pendant celle de
    # D577 — l'unité à 11,9 Go de mémoire et 11 Go d'échange ; les 16 derniers bancs sont morts en segfault, sans
    # affichage. Ce que la série coûte au compositeur se lit donc banc par banc (`compositeur.tsv`, et le compte sur la
    # ligne du banc) ; et la série S'ARRÊTE, et le dit, quand l'unité dépasse `VSM_PLAFOND_COMPOSITEUR` Mo (3 072) ou que
    # le système n'a plus `VSM_PLANCHER_MEMOIRE` Mo (3 072) de mémoire et d'échange libres : un rouge vaut mieux qu'une
    # session tuée.
    unite_compositeur=$(systemctl --user show plasma-kwin_wayland.service -p ControlGroup --value 2>/dev/null)
    [ -n "$unite_compositeur" ] && [ -r "/sys/fs/cgroup$unite_compositeur/memory.current" ] \
        || { saute "unité du compositeur introuvable : la garde ne voit que la mémoire du système"; unite_compositeur=""; }
    compositeur_mo() {   # l'unité entière, mémoire + échange
        [ -n "$unite_compositeur" ] || { echo 0; return; }
        local d="/sys/fs/cgroup$unite_compositeur"
        echo $(( ($(cat "$d/memory.current" 2>/dev/null || echo 0) + $(cat "$d/memory.swap.current" 2>/dev/null || echo 0)) / 1048576 ))
    }
    compositeur_anon_mo() {   # kwin_wayland et Xwayland : la mémoire anonyme, échange compris — ce qui fuit
        local p t=0
        for p in $(pgrep -x kwin_wayland) $(pgrep -x Xwayland); do
            t=$(( t + $(awk '/^(RssAnon|VmSwap):/ {s += $2} END {print s + 0}' "/proc/$p/status" 2>/dev/null || echo 0) ))
        done
        echo $(( t / 1024 ))
    }
    libre_mo() { awk '/^(MemAvailable|SwapFree):/ {s += $2} END {print int(s / 1024)}' /proc/meminfo; }
    # D581 : LE SOMMET DE L'UNITÉ (`memory.peak`, depuis l'ouverture de la session) — un saut PASSAGER pendant un banc
    # ne se voit pas entre deux lectures : le 23/10 au soir, l'unité est montée à 10,4 Go pendant `quantifier-audio.sh`,
    # écran éteint, et elle était redescendue à 0,9 Go une minute après. Le sommet qui bouge pendant un banc le nomme.
    sommet_mo() {
        [ -n "$unite_compositeur" ] || { echo 0; return; }
        echo $(( $(cat "/sys/fs/cgroup$unite_compositeur/memory.peak" 2>/dev/null || echo 0) / 1048576 ))
    }
    plafond="${VSM_PLAFOND_COMPOSITEUR:-3072}"
    plancher="${VSM_PLANCHER_MEMOIRE:-3072}"
    attente="${VSM_ATTENTE_COMPOSITEUR:-120}"   # D581 : secondes accordées à l'unité pour redescendre sous le plafond
    printf 'banc\tunite_avant_mo\tunite_apres_mo\tanon_avant_mo\tanon_apres_mo\tlibre_apres_mo\tsommet_apres_mo\n' > "$journaux/compositeur.tsv"
    compositeur_depart=$(compositeur_mo)
    anon_depart=$(compositeur_anon_mo)
    debut_serie=$(date +%s)
    passes=0
    for banc in accords.sh annulation-des-menus.py annuler-hors-historique.sh editeur-logique.sh instantanes-console.sh aplatir-clips.sh copier-plage.sh temps-dans-un-clip.sh copie-liee-temps.sh fin-du-morceau.sh locateurs-section.sh decouper-aux-silences.sh accords-depuis-notes.sh fenetre-reperes.sh quantifier-audio.sh notes-depuis-attaques.sh groove-depuis-audio.sh transposition-globale.sh courbe-de-gain.sh tempo-detecte.sh normaliser-niveau.sh doublons.sh clip-verrouille.sh decompte-punch.sh verrou-suppression.sh montage-en-chaine.sh aimant-glisser.sh a-la-tete.sh fondu-a-la-tete.sh silence-plage.sh dispositions.sh choisir-par-plage.sh couper-aux-locateurs.sh pistes-vides.sh coller-pistes.sh rogner-aux-locateurs.sh caler-grille.sh chevauchements-mono.sh glisser-contenu.sh automation-retiree.sh lire-selection.sh fusionner-midi.sh boucler-selection.sh restreindre-polyphonie.sh pedales-en-durees.sh controleurs-retires.sh lire-autour.sh eclater-canal.sh meme-nom.sh repere-suivant.sh tete-evenement.sh couverture-laissee.sh quantifier-fins.sh eclaircir-controleurs.sh annuler-zoom.sh repere-a-la-tete.sh tete-fin-selection.sh tete-attaque.sh lever-solos-muets.sh console-a-zero.sh canaux-echanges.sh canaux-recomptes.sh fondus-retires.sh plier-toutes.sh reperes-attaques.sh analyse-fichier-long.sh clips-des-pistes.sh tempo-tape.sh decalage-continu.sh statistiques-clip.sh chevauchements-clips.sh reglages-tranche.sh nouvelle-version.sh revenir-version.sh copie-projet.sh tranches-choisies.sh canaux-modes.sh nettoyer-dossier.sh zoom-locateurs.sh tete-au-repere.sh notes-piste.sh annuler-quantification.sh point-accroche.sh lier-tranches.sh projet-vide-cadre.sh departs-contournes.sh fondus-locateurs.sh agrandir-pistes.sh dupliquer-plage.sh midi-locateurs.sh inserts-contournes.sh notes-par-plage.sh automation-extraite.sh tete-fixe.sh couleur-notes.sh couleur-repere.sh prereglages-quantification.sh loi-panoramique.sh grouper-clips.sh aimant-relatif.sh departs-panoramique.sh zoom-prereglages.sh largeur-stereo.sh formes-des-fondus.sh duree-effet-midi.sh aimant-cibles.sh plage-effet-midi.sh zone-quantification.sh aleatoire-effet-midi.sh inserts-apres-fader.sh legato-reglages.sh quintolet-grille.sh masquer-pistes-sans-donnees.sh temoins-ecretage.sh pre-post-roll.sh moniteur-midi.sh armement-suit.sh prise-quantifiee-punch.sh punch-a-l-arret.sh tete-posee.sh parabole-automation.sh voix-dessus-dessous.sh controleurs-suivent.sh position-affichee.sh solo-choisit.sh molette-gain.sh molette-fondus.sh rattrapage-prise.sh midi-entrant-prise.sh remises-apres-prise.sh zoom-a-la-regle.sh chasse-aux-notes.sh nom-des-notes.sh zoom-regle-pianoroll.sh suivi-suspendu.sh prise-hors-clips.sh zoom-molette-pointeur.sh reticule.sh historique-profondeur.sh piste-sous-la-choisie.sh largeur-tete.sh dossier-des-choisies.sh desarmer-tout.sh ecoute-attenuee.sh ligne-d-ensemble.sh disposition-accords.sh coupe-bas-haut.sh macros.sh tempo-fixe.sh expandeur-deesser.sh ligne-info-clips.sh automation-main.sh automation-lecture.sh automation-globale.sh automation-remplir.sh automation-controleur.sh reperes-cycle.sh arrangement-defile.sh arret-tete.sh commandes-apprises.sh automation-echelle.sh autosauvegarde-vue.sh balayer-facades.sh banc-fumee.sh \
                barre-transport.sh bascules-retenues.sh cadrage-ouverture.sh clavier-emprunte.sh \
                fader-console.sh gestes-vivants.py grille-gamme-projet.sh ligne-d-etat.py liste-ajouter.sh \
                liste-editer.sh marque-enregistre.sh \
                metronome-projet.sh miniature-clips.sh notes-du-projet.sh onglets-du-dock.sh ouvrir-midi.sh \
                pas-a-pas.sh piano-roll-piste-choisie.sh pianoroll-bord.sh pianoroll-zones.sh police-plancher.sh portes-de-l-arrangement.sh regions-historique.sh selection-au-bord.sh \
                portes-des-outils.sh portes-des-pistes.sh portes-du-transport.sh quantifier.sh \
                theme-sombre.sh tout-voir.sh transport-au-repos.sh transposer-hors-plage.sh vca-interface.sh versions-piste.sh volet-anglais.sh \
                vue-du-morceau.sh vumetre-console.sh zoom-reassigne.sh; do
        unite_avant=$(compositeur_mo); anon_avant=$(compositeur_anon_mo); libre=$(libre_mo)
        # D581 : AU-DELÀ DU PLAFOND, ON ATTEND qu'elle redescende — rien n'est lancé pendant ce temps —, et l'on dit
        # combien de temps ; si elle ne redescend pas, la série s'arrête comme avant (D578).
        if [ "$unite_avant" -gt "$plafond" ] || [ "$libre" -lt "$plancher" ]; then
            haut=$unite_avant; attendu=0
            while { [ "$unite_avant" -gt "$plafond" ] || [ "$libre" -lt "$plancher" ]; } && [ "$attendu" -lt "$attente" ]; do
                sleep 5; attendu=$((attendu + 5))
                unite_avant=$(compositeur_mo); libre=$(libre_mo)
            done
            if [ "$unite_avant" -gt "$plafond" ] || [ "$libre" -lt "$plancher" ]; then
                rouge "SÉRIE ARRÊTÉE avant $banc : compositeur à $unite_avant Mo (plafond $plafond) après $attendu s d'attente, $libre Mo libres (plancher $plancher) — ce banc et les suivants ne sont PAS passés (D578, D581)"
                break
            fi
            saute "avant $banc : le compositeur était à $haut Mo (plafond $plafond) ; redescendu à $unite_avant Mo après $attendu s d'attente (D581)"
            anon_avant=$(compositeur_anon_mo)
        fi
        t0=$(date +%s)
        passes=$((passes + 1))
        if [ "$banc" = "balayer-facades.sh" ]; then
            "tools/$banc" "$journaux/facades.tsv" > "$journaux/$banc.log" 2>&1
        else
            "tools/$banc" > "$journaux/$banc.log" 2>&1
        fi
        rc=$?
        duree=$(( $(date +%s) - t0 ))
        unite_apres=$(compositeur_mo); anon_apres=$(compositeur_anon_mo)
        printf '%s\t%d\t%d\t%d\t%d\t%d\t%d\n' "$banc" "$unite_avant" "$unite_apres" "$anon_avant" "$anon_apres" "$(libre_mo)" \
            "$(sommet_mo)" >> "$journaux/compositeur.tsv"
        cout=$(printf 'compositeur %+d Mo' $((anon_apres - anon_avant)))
        if [ "$rc" -eq 0 ]; then vert "$banc (${duree} s, $cout) : $(grep -v '^\s*$' "$journaux/$banc.log" | tail -1 | cut -c1-90)"
        else rouge "$banc (${duree} s, code $rc, $cout) : $(grep -v '^\s*$' "$journaux/$banc.log" | tail -1 | cut -c1-90)"
             grep -E 'RATÉ|REFUS|rouge' "$journaux/$banc.log" | head -4 | sed 's/^/       /'
        fi
    done
    if [ -f "$journaux/preferences-avant.settings" ]; then
        if cmp -s "$prefs" "$journaux/preferences-avant.settings"
        then vert "préférences de l'utilisateur identiques après la série"
        else rouge "préférences de l'utilisateur MODIFIÉES pendant la série — lire clé par clé avant de conclure (D77) : $journaux/preferences-avant.settings"
        fi
    fi
    ecran_apres=$(ecran)
    [ -n "$ecran_apres" ] && rouge "écran $ecran_apres À LA FIN de la série : un rouge de focus ou de clic peut venir de là — le rejouer écran allumé avant de conclure (D516)"
    titre "Bilan"
    printf '   %d banc(s) en %d min, journaux dans %s\n' "$passes" $(( ($(date +%s) - debut_serie) / 60 )) "$journaux"
    printf '   compositeur : unité %d → %d Mo (sommet de la session %d Mo), kwin_wayland + Xwayland %d → %d Mo (anonyme, échange compris) ; les plus coûteux :\n' \
        "$compositeur_depart" "$(compositeur_mo)" "$(sommet_mo)" "$anon_depart" "$(compositeur_anon_mo)"
    # D584 : TRIÉ SUR LE NOMBRE, SIGNÉ ENSUITE — `sort -n` ne lit pas le « + » de tête, et rangeait « +9 » avant « +81 »
    # (la série de D584 donnait accords.sh, +9, premier, et zoom-reassigne.sh, +81, quatrième).
    tail -n +2 "$journaux/compositeur.tsv" | awk -F'\t' '{printf "%d\t%s\n", $5 - $4, $1}' | sort -rn | head -5 \
        | awk -F'\t' '{printf "      %+d\t%s\n", $1, $2}'
    if [ "$ECHECS" -eq 0 ]; then printf '   \033[32mTous les bancs sont verts.\033[0m\n'; exit 0; fi
    printf '   \033[31m%d en échec.\033[0m\n' "$ECHECS"
    exit 1
fi

if [ "${1:-}" = "--compiler" ]; then
    titre "Compilation des cibles de test (deux travaux)"
    # D387 : ET LES BANCS. Deux d'entre eux ne se compilaient plus depuis D364,
    # vingt-deux phases, parce que rien ne les bâtissait. Toutes les cibles
    # `vsm-*` que CMake connaît, SAUF `vsm-render` : c'est celui qu'une course
    # emploie, et le remplacer la tue (règle du dépôt).
    bancs=$(cmake --build build --target help 2>/dev/null | grep -o 'vsm-[a-z0-9-]*' \
            | sort -u | grep -vx 'vsm-render' | tr '\n' ' ')
    if cmake --build build -j 2 --target vsm_core_tests vsm_audio_tests \
             vsm_interchange_tests vsm_clap_tests vsm_panels_tests $bancs > /tmp/vsm-verifier-build.log 2>&1
    then vert "cinq cibles de test et $(echo $bancs | wc -w) bancs compilés ($bancs)"
    else rouge "compilation en échec — voir /tmp/vsm-verifier-build.log"
         grep 'Error' /tmp/vsm-verifier-build.log | grep -o 'CMakeFiles/[a-z0-9_-]*\.dir' | sort -u | sed 's/^/       /'
         exit 1
    fi
fi

if [ "$GARDES_SEULES" -eq 0 ]; then
titre "Suites du moteur (C++)"
for suite in core/vsm_core_tests audio/vsm_audio_tests interchange/vsm_interchange_tests \
             clap/vsm_clap_tests panels/vsm_panels_tests; do
    nom=$(basename "$suite")
    if [ ! -x "build/$suite" ]; then
        saute "$nom — pas compilée (relancer avec --compiler)"
        continue
    fi
    if sortie=$("./build/$suite" 2>&1); then vert "$nom : $(echo "$sortie" | tail -1)"
    else rouge "$nom : $(echo "$sortie" | tail -1)"
    fi
done

titre "Suite de la chaîne d'analyse (Python)"
if [ ! -x "$VENV" ]; then
    saute "environnement absent — voir analyse/requirements.txt"
else
    if sortie=$("$VENV" -u analyse/tests/run.py 2>&1); then
        vert "$(echo "$sortie" | tail -1)"
    else
        rouge "$(echo "$sortie" | tail -1)"
        echo "$sortie" | grep -o '^\[ÉCHEC\] [a-z_]*' | sed 's/^/       /'
    fi
fi

titre "Lint (ruff) et types (mypy)"
if [ -x analyse/.venv/bin/ruff ]; then
    if sortie=$(analyse/.venv/bin/ruff check --output-format=concise . 2>&1)
    then vert "ruff : aucun signalement"
    else rouge "ruff : $(echo "$sortie" | tail -1)"; echo "$sortie" | head -8 | sed 's/^/       /'
    fi
else
    saute "ruff absent — analyse/.venv/bin/python -m pip install ruff"
fi

if [ -x "$VENV" ] && "$VENV" -c "import mypy" 2>/dev/null; then
    if sortie=$("$VENV" -m mypy analyse tools 2>&1)
    then vert "mypy : $(echo "$sortie" | tail -1)"
    else rouge "mypy : $(echo "$sortie" | grep -c 'error:') erreur(s)"; echo "$sortie" | head -8 | sed 's/^/       /'
    fi
else
    saute "mypy absent — analyse/.venv/bin/python -m pip install mypy"
fi
fi   # GARDES_SEULES

titre "Gardes des sources (tools/, D378)"
for garde in "accents-francais.py" "clips-numerotes.py" "index-a-jour.py" \
             "inventaire_langue.py --garde" "inventaire_langue.py --doublons" \
             "menus-des-regles.py" "menus-cites.py" "noms-des-gestes.py" "raccourcis-affiches.py" \
             "tables-markdown.py" "pas-hors-glisse.py" "touches-modifiees.py" "virgule-saisie.py" \
             "touches-du-menu-edition.py" "verbes-uniques.py" "echappements-hex.py" "mojibake.py"; do
    # Le nom et ses options se séparent ici : un seul mot passé à python
    # chercherait un fichier « inventaire_langue.py --garde ».
    read -r script options <<< "$garde"
    if [ ! -x "$VENV" ]; then saute "$garde — environnement absent"; continue; fi
    # shellcheck disable=SC2086
    if sortie=$("$VENV" "tools/$script" $options 2>&1); then vert "$garde : $(echo "$sortie" | tail -1)"
    else rouge "$garde : $(echo "$sortie" | tail -1)"; echo "$sortie" | grep -i 'raté\|écart\|faute' | head -5 | sed 's/^/       /'
    fi
done

titre "Bilan"
if [ "$SAUTES" -gt 0 ]; then
    printf '   %d garde-fou(s) SAUTÉ(S) — un garde-fou sauté ne garde rien.\n' "$SAUTES"
fi
if [ "$ECHECS" -eq 0 ]; then
    printf '   \033[32mTout ce qui a pu être passé est vert.\033[0m\n'
    exit 0
fi
printf '   \033[31m%d garde-fou(s) en échec.\033[0m\n' "$ECHECS"
exit 1
