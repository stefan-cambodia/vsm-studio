#!/usr/bin/env bash
# LA GARDE DE D524 : CHOISIR UNE PISTE MONTRE SES NOTES, PAS UNE FENÊTRE VIDE D'ELLE.
#
# RÈGLE GARDÉE (30/09/2026). Au changement de piste, le piano roll cadrait la
# HAUTEUR (la médiane des notes) et jamais le TEMPS : une piste qui entre à la
# mesure 10 s'ouvrait sur les mesures 1 à 6, ses seuls fantômes à l'écran (vu sur
# `b4wuzthen`, la kick). Désormais, si AUCUNE note de la piste choisie n'est dans
# la fenêtre, le piano roll défile jusqu'à la plus proche — sans toucher au zoom ;
# si une seule l'est, rien ne bouge (l'endroit où l'on travaille est le choix de
# l'utilisateur).
#
# COMMENT. Un projet de trois pistes : « tout » (une note par mesure, de 1 à 60),
# « tard » (mesures 40 à 44), « tot » (mesures 1 et 2). Ouvert sur « tout », on
# choisit « tard », puis « tout », puis « tot », et `relever-pianoroll` dit après
# chaque choix la fenêtre, le zoom et `visibles=<a>/<n>` — les notes de la piste
# choisie à l'écran, lues à la source de la peinture (les fantômes n'en sont pas).
#
# LE CONTRÔLE EST DANS LA GARDE : revenir sur « tout », qui joue aussi à la
# mesure 40, ne doit PAS défiler ; le zoom ne doit bouger à aucun choix ; et un
# projet enregistré sur « tard » à la mesure 1 se rouvre à la mesure 1 — la vue
# reprise (D369) prime sur ce défilement.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/piano-roll-piste-choisie.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-piste-choisie.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/entrees/midi"
python3 - "$brouillon/entrees" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, canal, hauteur, mesures):
    evs, t = [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())], 0
    for m in mesures:
        debut = (m - 1) * 1920
        evs.append((debut - t, bytes([0x90 | canal, hauteur, 100])))
        evs.append((480, bytes([0x80 | canal, hauteur, 0])))
        t = debut + 480
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
pistes = [("tout", 0, 60, range(1, 61)), ("tard", 1, 48, range(40, 45)), ("tot", 2, 72, range(1, 3))]
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + b"".join(piste(*p) for p in pistes))
json.dump({"format": "vsm-project", "version": 1, "title": "entrees", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": c, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                       "name": n} for n, c, _, _ in pistes]},
          open(d + "/project.json", "w"), indent=1)
PY
# LE MÊME, ENREGISTRÉ SUR « tard » À LA MESURE 1 (D369) : une vue reprise d'un projet
# PRIME — elle est posée après le choix de la piste —, même si elle ne montre rien
# de la piste. C'est l'endroit où l'utilisateur a laissé son travail.
cp -r "$brouillon/entrees" "$brouillon/entrees-vue"
python3 - "$brouillon/entrees-vue/project.json" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
# Les QUATRE champs du piano roll : `reprendreLaVue` refuse une vue incomplète, et le
# cadrage de repli la remplacerait — la garde ne mesurerait alors pas la reprise.
p["view"] = {"selectedTrack": 1, "pianoRollPixelsPerTick": 0.08, "pianoRollScrollTick": 0,
             "pianoRollTopNote": 60, "pianoRollNoteHeight": 16}
json.dump(p, open(sys.argv[1], "w"), indent=1)
PY

h="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/entrees" VSM_VUE="sans-rapport" VSM_DELAI=3200 \
    VSM_GESTE_APRES="500:relever-pianoroll;800:choisir:1;1100:relever-pianoroll;1400:choisir:0;1700:relever-pianoroll;2000:choisir:2;2300:relever-pianoroll" \
    VSM_CAPTURE="$brouillon/choix.png" timeout 45 "$BIN" > "$brouillon/choix.txt" 2>&1
# D147 : ce que l'application avertit se relaie AVANT de conclure.
grep -E "VSM_(GESTE_APRES|GESTE|TAILLE) : .*(refusé|inconnu|aucun|JAMAIS|RÉDUITE|hors bornes)" "$brouillon/choix.txt" \
    | cut -c1-160 | sed 's/^/        journal : /' >&2
mapfile -t releves < <(grep "VSM_PIANOROLL_RANG" "$brouillon/choix.txt")
for r in "${releves[@]}"; do
    echo "        $(grep -o "zoom=[^ ]* defilement=[^ ]* fenetre=[^ ]* visibles=[^ ]*" <<< "$r")"
done

rates=0
ok()   { printf '  OK   %-58s %s\n' "$1" "$2"; }
rate() { printf '  RATÉ %-58s %s\n' "$1" "$2"; rates=$((rates + 1)); }
champ() { grep -o "$2=[^ ]*" <<< "${releves[$1]:-}" | cut -d= -f2; }
visible() {   # $1 index du relevé ; $2 libellé
    local v; v="$(champ "$1" visibles)"
    if [ -n "$v" ] && [ "${v%/*}" -gt 0 ] && [ "${v#*/}" -gt 0 ]; then ok "$2" "$v"
    else rate "$2" "« ${v:-aucun relevé} »"; fi
}

echo "=== D524 : choisir une piste montre ses notes ==="
if [ "${#releves[@]}" -ne 4 ]; then
    rate "quatre relevés (ouverture, « tard », « tout », « tot »)" "${#releves[@]} lu(s) — rien ne se juge"
    echo "--- $rates raté(s)"; exit 1
fi
visible 0 "ouverture sur « tout » : ses notes à l'écran"
visible 1 "« tard » (mesures 40-44) : ses notes à l'écran"
d1="$(champ 1 defilement)"; d2="$(champ 2 defilement)"
visible 2 "retour sur « tout » : ses notes à l'écran"
if [ "$d1" = "$d2" ]; then ok "retour sur « tout », qui joue là aussi : pas de défilement" "$d2"
else rate "retour sur « tout », qui joue là aussi : pas de défilement" "$d1 → $d2"; fi
visible 3 "« tot » (mesures 1-2, avant la fenêtre) : ses notes à l'écran"
zooms="$(for i in 0 1 2 3; do champ "$i" zoom; done | sort -u | wc -l)"
if [ "$zooms" -eq 1 ]; then ok "le zoom n'a bougé à aucun choix" "$(champ 0 zoom)"
else rate "le zoom n'a bougé à aucun choix" "$(for i in 0 1 2 3; do champ "$i" zoom; done | tr '\n' ' ')"; fi

# LE CONTRÔLE DE LA VUE REPRISE (D369).
h="$(mktemp -d "$brouillon/home.XXXX")"
env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/entrees-vue" VSM_VUE="sans-rapport" VSM_DELAI=1500 \
    VSM_GESTE_APRES="500:relever-pianoroll" VSM_CAPTURE="$brouillon/vue.png" timeout 45 "$BIN" > "$brouillon/vue.txt" 2>&1
r="$(grep "VSM_PIANOROLL_RANG" "$brouillon/vue.txt" | tail -1)"
p="$(grep -o "VSM_PISTE_CHOISIE : [0-9]*" "$brouillon/vue.txt" | tail -1 | cut -d' ' -f3)"
echo "        vue reprise : piste ${p:-?}, $(grep -o "defilement=[^ ]* fenetre=[^ ]* visibles=[^ ]*" <<< "$r")"
d="$(grep -o "defilement=[^ ]*" <<< "$r" | cut -d= -f2)"
if [ "${p:-}" = "1" ] && [ "${d:-}" = "0" ]; then ok "une vue reprise d'un projet prime (« tard », mesure 1)" "piste $p, défilement $d"
else rate "une vue reprise d'un projet prime (« tard », mesure 1)" "piste ${p:-?}, défilement ${d:-?}"; fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
