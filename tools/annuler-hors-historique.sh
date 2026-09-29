#!/usr/bin/env bash
# LA GARDE DE D518 : CE QUI NE FAIT PAS DE PAS NE SE DÉFAIT PAS PAR LE PAS D'À CÔTÉ.
#
# RÈGLE GARDÉE (29/09/2026). L'historique est fait d'instantanés du projet ENTIER,
# et Ctrl+Z restaure l'instantané. Trois données du morceau ne font, par décision
# écrite, aucun pas : les notes du projet (D508), le clic (D503), la boucle
# marche/arrêt (D0, D503). Avant D518, un geste, puis des notes tapées, puis
# Ctrl+Z : l'instantané d'avant le geste n'avait pas les notes — Ctrl+Z annonçait
# « Annuler Forme des fondus croisés » et effaçait aussi les notes, le clic et la
# boucle. Et quand l'état restauré n'avait pas de région, le bouton s'éteignait
# pendant que le MOTEUR bouclait encore.
#
# COMMENT. Un projet d'une piste de quatre notes, sans boucle. Trois courses, un
# HOME neuf chacune ; ce qui compte est lu dans le `project.json` ENREGISTRÉ (ce
# que Ctrl+S écrit) et dans `relever-boucle` (projet, moteur, bouton au même
# instant). Le fichier d'AVANT le Ctrl+Z est le témoin que les données avaient
# changé (D145) ; la forme des fondus, revenue au défaut, celui que le pas a bien
# été annulé.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/annuler-hors-historique.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-hors-historique.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# DEUX PROJETS : « projet » sans région de boucle, « region » avec une région posée
# mais ÉTEINTE — la bascule n'y fait qu'allumer. Celle que la bascule POSE sur un
# projet qui n'en a pas est un autre chemin, sans pas, nommé dans D518.
for nom in projet region; do
mkdir -p "$brouillon/$nom/midi"
python3 - "$brouillon/$nom" "$nom" <<'PY'
import json, struct, sys
d, nom = sys.argv[1], sys.argv[2]
fin = 1920 if nom == "region" else 0
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une")]
for i in range(4):
    evs += [(0 if i == 0 else 1440, bytes([0x90, 60 + i, 100])), (480, bytes([0x80, 60 + i, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "hors", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": fin, "startTick": 0},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": "une"}]},
          open(d + "/project.json", "w"), indent=1)
PY
done

lancer() {   # $1 nom ; $2 gestes ; $3 projet (défaut : « projet », sans région)
    local h
    h="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/${3:-projet}" VSM_VUE="sans-rapport" VSM_DELAI=4000 \
        VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$1.png" timeout 45 "$BIN" > "$brouillon/$1.txt" 2>&1
    # D147 : ce que l'application avertit se relaie AVANT de conclure.
    grep -E "VSM_(MENU|GESTE_APRES|TOUCHE|GESTE) : .*(aucune|refusé|JAMAIS|grisée|AUCUNE|inconnu)" "$brouillon/$1.txt" \
        | sed 's/^/        journal : /' >&2
}
lire() {   # $1 dossier enregistré → « notes|boucle|clic|fondus »
    python3 - "$1/project.json" <<'PY'
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except OSError:
    print("ABSENT"); sys.exit()
t = p.get("transport", {})
print("|".join([p.get("notes", ""), "oui" if t.get("loop", {}).get("enabled") else "non",
                "oui" if t.get("metronome") else "non", p.get("crossfadeShape", "defaut")]))
PY
}

rates=0
ok()   { printf '  OK   %-46s %s\n' "$1" "$2"; }
rate() { printf '  RATÉ %-46s %s\n' "$1" "$2"; rates=$((rates + 1)); }

echo "=== D518 : ce qui ne fait pas de pas ne se défait pas par le pas d'à côté ==="
# 1. UN GESTE, PUIS LES TROIS DONNÉES SANS PAS, PUIS Ctrl+Z.
lancer principal "400:menu:Lente;800:notes:Refrain trop long;1200:menu:Boucle (marche / arrêt);1600:menu:Métronome (marche / arrêt);2000:enregistrer:$brouillon/avant;2300:relever-historique;2600:touche:ctrl + Z;2900:relever-historique;3200:relever-boucle;3500:enregistrer:$brouillon/apres" region
IFS='|' read -r n_av b_av c_av f_av <<< "$(lire "$brouillon/avant")"
IFS='|' read -r n_ap b_ap c_ap f_ap <<< "$(lire "$brouillon/apres")"
grep "VSM_HISTORIQUE_PAS" "$brouillon/principal.txt" | sed 's/^/        /'
if [ "$n_av" = "Refrain trop long" ] && [ "$b_av" = oui ] && [ "$c_av" = oui ] && [ "$f_av" != defaut ]; then
    ok "avant Ctrl+Z : le fichier porte les quatre" "notes [$n_av], boucle $b_av, clic $c_av, fondus $f_av"
else
    rate "avant Ctrl+Z : le fichier porte les quatre" "notes [$n_av], boucle $b_av, clic $c_av, fondus $f_av — le témoin manque, rien ne se juge"
fi
if [ "$f_ap" = defaut ]; then ok "Ctrl+Z : le pas est annulé" "fondus $f_av → $f_ap"
else rate "Ctrl+Z : le pas est annulé" "fondus « $f_ap » (attendu : défaut)"; fi
if [ "$n_ap" = "$n_av" ]; then ok "Ctrl+Z : les notes du projet gardées" "[$n_ap]"
else rate "Ctrl+Z : les notes du projet gardées" "[$n_av] → [$n_ap]"; fi
if [ "$b_ap" = oui ]; then ok "Ctrl+Z : la boucle gardée" "$b_av → $b_ap"
else rate "Ctrl+Z : la boucle gardée" "$b_av → $b_ap"; fi
if [ "$c_ap" = oui ]; then ok "Ctrl+Z : le clic gardé" "$c_av → $c_ap"
else rate "Ctrl+Z : le clic gardé" "$c_av → $c_ap"; fi
b_rel="$(grep "VSM_BOUCLE : " "$brouillon/principal.txt" | tail -1 | sed 's/VSM_BOUCLE : //')"
case "$b_rel" in
    "projet oui"*"moteur oui"*"bouton oui ; clic oui") ok "Ctrl+Z : projet, moteur, bouton d'accord" "$b_rel" ;;
    *) rate "Ctrl+Z : projet, moteur, bouton d'accord" "« $b_rel » (attendu : tout allumé)" ;;
esac

# 2. UNE RÉGION POSÉE PAR UN PAS, PUIS ANNULÉE : le moteur doit suivre le projet.
lancer moteur "400:relever-boucle;800:menu:Début de boucle à la tête;1200:relever-boucle;1600:touche:ctrl + Z;2000:relever-boucle"
mapfile -t boucles < <(grep "VSM_BOUCLE : " "$brouillon/moteur.txt" | sed 's/VSM_BOUCLE : //')
printf '        %s\n' "${boucles[@]}"
if [ "${#boucles[@]}" -ne 3 ]; then
    rate "Début de boucle à la tête, puis Ctrl+Z" "${#boucles[@]} relevé(s) sur 3"
else
    case "${boucles[1]}" in
        *"moteur oui"*) ok "Début de boucle à la tête : le moteur boucle" "(le témoin que la boucle était posée)" ;;
        *) rate "Début de boucle à la tête : le moteur boucle" "${boucles[1]}" ;;
    esac
    case "${boucles[2]}" in
        "projet non"*"moteur non"*"bouton non"*) ok "… puis Ctrl+Z : projet, moteur, bouton éteints" "" ;;
        *) rate "… puis Ctrl+Z : projet, moteur, bouton éteints" "${boucles[2]}" ;;
    esac
fi

# 3. CONTRÔLE : des notes tapées AVANT le geste sont dans son instantané.
lancer controle "400:notes:Avant le geste;800:menu:Lente;1200:touche:ctrl + Z;1600:enregistrer:$brouillon/ctl"
IFS='|' read -r n_ct _ _ f_ct <<< "$(lire "$brouillon/ctl")"
if [ "$n_ct" = "Avant le geste" ] && [ "$f_ct" = defaut ]; then ok "contrôle : notes tapées avant le geste" "[$n_ct], fondus $f_ct"
else rate "contrôle : notes tapées avant le geste" "[$n_ct], fondus $f_ct"; fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
