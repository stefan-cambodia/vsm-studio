#!/usr/bin/env bash
# D581.2 — LA PARABOLE DE L'OUTIL LIGNE (Édition ▸ Dessiner l'automation sur la sélection ▸ Parabole montante / descendante
# (lente au départ) ; le *Parabola* du *Line Tool* de Cubase), MESURÉE PAR LA COURBE DU PROJET ENREGISTRÉ, RELUE AUX QUARTS
# ET RAMENÉE ENTRE SES DEUX BOUTS — la rampe droite pour témoin.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4 ; la boucle [0, 1920) posée, une courbe « filter.1.drive » sur la piste).
# La forme se lit en proportion de l'étendue de la courbe — (v(t) − bas) / (haut − bas), bas et haut ses valeurs à 0 et à
# 1920 — : elle ne dépend pas de l'étendue du paramètre, et la descendante s'y lit de haut en bas.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D581) :
#   (1) la parabole montante : 0,0625, 0,25, 0,5625 aux quarts (à 0,01 près) ;
#   (2) le TÉMOIN, la rampe montante : 0,25, 0,5, 0,75 ;
#   (3) la parabole descendante : 0,9375, 0,75, 0,4375 (de la valeur haute vers la basse) ;
#   (4) Ctrl+Z après la parabole : la courbe d'avant, ses deux points, en UN pas ;
#   (5) en anglais : « Rising parabola (slow start) », « Falling parabola (slow start) ».
#
#   tools/parabole-automation.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-parabole.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x8f\x00\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "parabole", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": True, "startTick": 0, "endTick": 1920},
                         "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "automation": [{"parameter": "filter.1.drive", "points": [{"tick": 0, "value": 1.0},
                                                                                {"tick": 3840, "value": 1.0}]}]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GESTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
forme() {   # $1 = dossier enregistré : les trois proportions aux quarts, ou ABSENT
    python3 - "$1/project.json" <<'PY2'
import json, sys
try:
    p = json.load(open(sys.argv[1]))
    pts = sorted((q["tick"], q["value"]) for q in p["tracks"][0]["automation"][0]["points"])
except Exception:
    print("ABSENT"); sys.exit(0)
def v(t):
    if t <= pts[0][0]: return pts[0][1]
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if t0 <= t <= t1: return v0 if t1 == t0 else v0 + (v1 - v0) * (t - t0) / (t1 - t0)
    return pts[-1][1]
bas, haut = sorted((v(0), v(1920)))
print("PLAT" if haut - bas < 1e-9 else " ".join(f"{(v(t) - bas) / (haut - bas):.4f}" for t in (480, 960, 1440)))
PY2
}
points() { python3 -c "import json,sys; print(len(json.load(open(sys.argv[1]))['tracks'][0]['automation'][0]['points']))" "$1/project.json" 2>/dev/null || echo ABSENT; }
proche() {   # $1 = mesure « a b c » ; $2 $3 $4 = attendus ; à 0,01 près
    python3 -c "import sys; m=sys.argv[1].split(); a=[float(x) for x in sys.argv[2:]]; sys.exit(0 if len(m)==3 and all(abs(float(x)-y)<=0.01 for x,y in zip(m,a)) else 1)" "$@"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
DESSIN="menu:Édition > Dessiner l'automation sur la sélection"

echo "=== D581.2 : la parabole de l'outil ligne ==="
course monte VSM_DELAI=2200 VSM_GESTE_APRES="600:choisir:0;900:$DESSIN > Parabole montante (lente au départ);1400:enregistrer:$brouillon/monte"
course droite VSM_DELAI=2200 VSM_GESTE_APRES="600:choisir:0;900:$DESSIN > Rampe montante;1400:enregistrer:$brouillon/droite"
course descend VSM_DELAI=2200 VSM_GESTE_APRES="600:choisir:0;900:$DESSIN > Parabole descendante (lente au départ);1400:enregistrer:$brouillon/descend"
course defait VSM_DELAI=2400 VSM_GESTE_APRES="600:choisir:0;900:$DESSIN > Parabole montante (lente au départ);1300:touche:ctrl + Z;1700:enregistrer:$brouillon/defait"
course anglais VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

fm="$(forme "$brouillon/monte")"; fd="$(forme "$brouillon/droite")"; fs="$(forme "$brouillon/descend")"
nm="$(points "$brouillon/monte")"; nz="$(points "$brouillon/defait")"
grep -h "^Automation tracée" "$brouillon"/monte.txt "$brouillon"/droite.txt "$brouillon"/descend.txt | sed 's/^/       /'
echo "       aux quarts : parabole montante [$fm] ($nm points) ; rampe [$fd] ; parabole descendante [$fs] ; après Ctrl+Z : $nz point(s)"
en="$(grep -ch "^VSM_MENU_LISTE : Edit > .* > \(Rising parabola (slow start)\|Falling parabola (slow start)\)" "$brouillon/anglais.txt")"
echo "       en anglais : $en entrée(s)"

verdict "(1) la parabole montante : 0,0625 0,25 0,5625" "$(proche "$fm" 0.0625 0.25 0.5625 && echo 1 || echo 0)"
verdict "(2) le témoin, la rampe : 0,25 0,5 0,75" "$(proche "$fd" 0.25 0.5 0.75 && echo 1 || echo 0)"
verdict "(3) la parabole descendante : 0,9375 0,75 0,4375" "$(proche "$fs" 0.9375 0.75 0.4375 && echo 1 || echo 0)"
verdict "(4) Ctrl+Z : les deux points d'avant, en un pas" "$([ "$nz" = 2 ] && [ "$nm" != 2 ] && echo 1 || echo 0)"
verdict "(5) en anglais (2 entrées)" "$([ "$en" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PARABOLE-AUTOMATION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PARABOLE-AUTOMATION : la parabole suit le carré de l'avancement, lente au départ dans les deux sens"
