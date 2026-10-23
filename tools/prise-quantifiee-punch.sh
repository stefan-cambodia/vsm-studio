#!/usr/bin/env bash
# D579.1, D579.3 — QUANTIFIER CHAQUE PRISE ET ARRÊTER APRÈS LA SORTIE DE PUNCH (Enregistrement ▸ Quantifier chaque prise,
# Enregistrement ▸ Arrêter après la sortie de punch ; l'*Auto Quantize* et le *Stop after Automatic Punch Out* de Cubase),
# MESURÉS PAR LE MIDI EXPORTÉ APRÈS UNE PRISE ET PAR LA TÊTE RELEVÉE À L'ARRÊT, LES TÉMOINS SANS LES OPTIONS À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM ; la grille du piano roll à la double-croche : 120 ticks). Une
# piste MIDI armée par son bouton R (`armer:0`) ; les notes entrent par `note-entrante:`, le point d'entrée du matériel.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D579) :
#   (1) sans décompte, F9, quatre notes entrantes à 300 ms d'écart, F9 : avec l'option, 4 notes, toutes à un multiple
#       de 120 ticks ;
#   (2) le TÉMOIN sans l'option : 4 notes dont au moins une hors de la grille ;
#   (3) « Annuler la quantification » après la prise quantifiée : au moins une note hors grille — les instants joués ;
#   (4) Ctrl+Z après la prise quantifiée : 0 note, en UN pas ;
#   (5) la région de punch [2 s, 4 s) active, un décompte d'une mesure, F9 : le TÉMOIN, relevé 5,0 s après F9, est en
#       lecture au-delà de 4,5 s ;
#   (6) avec « Arrêter après la sortie de punch » : arrêté à 4,000 s, la phase « arrêt » ;
#   (7) avec 1 mesure de post-roll en plus : arrêté à 6,000 s ;
#   (8) sans punch actif, l'option cochée : en lecture comme le témoin ;
#   (9) relancé dans le même HOME : les deux entrées cochées ; en anglais, « Quantise each take », « Stop after
#       punch-out » ;
#  (10) l'option cochée, sans décompte, la lecture lancée de 0 et F9 vers 4,6 s — APRÈS la sortie, F9 ne déplaçant pas
#       une tête qui joue : relevé 1 s plus tard, en lecture au-delà de 5 s ; l'arrêt part au FRANCHISSEMENT du point,
#       et le binaire d'avant la correction s'arrêtait aussitôt.
#
#   tools/prise-quantifiee-punch.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (mido) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-prise-quantifiee.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
# Une piste MIDI VIDE (rien à 0 : la fin du morceau ne doit pas compter), 120 BPM ; le même projet avec et sans punch.
# `long` : le punch, et UNE note à 10 s — un morceau vide est fini à 0, et une LECTURE (pas une prise) s'y arrête
# aussitôt ; le cas (10) veut un transport qui joue au-delà de la sortie AVANT F9.
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie\x00\xff\x2f\x00"
loin = (b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + bytes([0xcb, 0x00, 0x90, 48, 80, 0x83, 0x60, 0x80, 48, 0])
        + b"\x00\xff\x2f\x00")   # 9 600 ticks (10 s), puis 480
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
PUNCH = {"enabled": True, "startTick": 1920, "endTick": 3840}
for nom, punch, corps in (("base", None, vide), ("punch", PUNCH, vide), ("long", PUNCH, loin)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                 "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
    if punch: transport["punch"] = punch
    json.dump({"format": "vsm-project", "version": 1, "title": "prise", "midi": {"file": "midi/arrangement.mid"},
               "transport": transport,
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; $3 = projet ; le reste : variables
    local nom="$1" maison="$2" projet="$3"
    shift 3
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
debuts() {   # les débuts des notes d'un .mid, triés, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t = 0
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0: out.append(t)
print(" ".join(str(d) for d in sorted(out)) or "RIEN")
PY2
}
nombre() { case "$1" in ABSENT|RIEN) echo 0 ;; *) echo "$1" | wc -w ;; esac; }
sur_grille() {   # toutes à un multiple de 120 : 1 ; sinon 0
    case "$1" in ABSENT|RIEN) echo 0; return ;; esac
    for t in $1; do [ $((t % 120)) -eq 0 ] || { echo 0; return; }; done
    echo 1
}
releve() { grep -h "^VSM_DECOMPTE : " "$brouillon/$1.txt" | sed -n "${2:-1}p"; }
tete() { echo "$1" | sed -n 's/.*tête \([-0-9.]*\) s.*/\1/p'; }
etat() { echo "$1" | sed -n 's/.*transport \(.*\)$/\1/p'; }
phase() { echo "$1" | sed -n 's/^VSM_DECOMPTE : phase \([^,]*\),.*/\1/p'; }
au_dela() { python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) > float(sys.argv[2]) else 1)" "$1" "$2" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
AQ="menu:Enregistrement > Quantifier chaque prise"
ARRET="menu:Enregistrement > Arrêter après la sortie de punch"
SANS="menu:Enregistrement > Sans décompte"
POST1="menu:Transport > Post-roll > Post-roll : 1 mesure"
JOUER="1100:note-entrante:60:100:120;1400:note-entrante:62:100:120;1700:note-entrante:64:100:120;2000:note-entrante:65:100:120"

echo "=== D579.1 : quantifier chaque prise ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course aq "$maison" base VSM_DELAI=4000 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$AQ;800:touche:F9;$JOUER;2400:touche:F9;2800:exporter-midi:$brouillon/aq.mid;3000:menu:Édition > Sélection > Tout sélectionner;3200:menu:Édition > Temps et durée > Annuler la quantification (4);3500:exporter-midi:$brouillon/aq-annulee.mid"
course temoin "" base VSM_DELAI=3200 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:touche:F9;$JOUER;2400:touche:F9;2800:exporter-midi:$brouillon/temoin.mid"
course defait "" base VSM_DELAI=3400 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;500:$AQ;800:touche:F9;$JOUER;2400:touche:F9;2700:touche:ctrl + Z;3000:exporter-midi:$brouillon/defait.mid"

echo "=== D579.3 : arrêter après la sortie de punch ==="
course p-temoin "" punch VSM_DELAI=6200 VSM_GESTE_APRES="300:armer:0;600:touche:F9;5600:relever-decompte"
course p-arret "$maison" punch VSM_DELAI=6200 VSM_GESTE_APRES="300:armer:0;400:$ARRET;600:touche:F9;5600:relever-decompte"
course p-post "" punch VSM_DELAI=8200 \
    VSM_GESTE_APRES="300:armer:0;400:$ARRET;450:$POST1;600:touche:F9;5600:relever-decompte;7600:relever-decompte"
course p-sans "" base VSM_DELAI=6200 VSM_GESTE_APRES="300:armer:0;400:$ARRET;600:touche:F9;5600:relever-decompte"
course p-deja "" long VSM_DELAI=6800 \
    VSM_GESTE_APRES="300:armer:0;400:$ARRET;450:$SANS;600:cliquer:Play;5200:touche:F9;6200:relever-decompte"
course retenu "$maison" base VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" base VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

daq="$(debuts "$brouillon/aq.mid")"; dan="$(debuts "$brouillon/aq-annulee.mid")"
dte="$(debuts "$brouillon/temoin.mid")"; dde="$(debuts "$brouillon/defait.mid")"
echo "       débuts : avec l'option [$daq] ; annulée [$dan] ; témoin [$dte] ; Ctrl+Z [$dde]"
grep -h "^VSM_QUANTIFIER_PRISE\|^VSM_NOTE_ENTRANTE\|^VSM_PUNCH\|^VSM_QUANTIFIER : " "$brouillon"/aq.txt "$brouillon"/p-arret.txt "$brouillon"/p-post.txt \
    | sed 's/^/       /'
pt="$(releve p-temoin)"; pa="$(releve p-arret)"; pp="$(releve p-post 2)"; ps="$(releve p-sans)"; pd="$(releve p-deja)"
grep -h "^VSM_PUNCH" "$brouillon"/p-deja.txt | sed 's/^/       p-deja : /'
for c in p-temoin p-arret p-post p-sans p-deja; do grep -h "^VSM_DECOMPTE : " "$brouillon/$c.txt" | sed "s/^/       $c : /"; done
coches="$(grep -h "^VSM_MENU_LISTE : Enregistrement > \(Quantifier chaque prise\|Arrêter après la sortie de punch\)" "$brouillon/retenu.txt" | grep -c "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : Recording > \(Quantise each take\|Stop after punch-out\)" "$brouillon/anglais.txt")"
echo "       relancé : $coches entrée(s) cochée(s) ; en anglais : $en entrée(s)"

verdict "(1) avec l'option : 4 notes, toutes à un multiple de 120 ticks" \
    "$([ "$(nombre "$daq")" = 4 ] && [ "$(sur_grille "$daq")" = 1 ] && echo 1 || echo 0)"
verdict "(2) le témoin : 4 notes, au moins une hors grille" \
    "$([ "$(nombre "$dte")" = 4 ] && [ "$(sur_grille "$dte")" = 0 ] && echo 1 || echo 0)"
verdict "(3) Annuler la quantification : les instants joués reviennent (au moins une hors grille)" \
    "$([ "$(nombre "$dan")" = 4 ] && [ "$(sur_grille "$dan")" = 0 ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z après la prise quantifiée : 0 note, en un pas" \
    "$([ "$(nombre "$dde")" = 0 ] && [ "$dde" != "ABSENT" ] && echo 1 || echo 0)"
verdict "(5) le témoin du punch : en lecture au-delà de 4,5 s" \
    "$([ "$(etat "$pt")" = "en lecture" ] && au_dela "$(tete "$pt")" 4.5 && echo 1 || echo 0)"
verdict "(6) l'option : arrêté à 4,000 s, phase « arrêt »" \
    "$([ "$(etat "$pa")" = "arrêté" ] && [ "$(tete "$pa")" = "4.000" ] && [ "$(phase "$pa")" = "arrêt" ] && echo 1 || echo 0)"
verdict "(7) avec 1 mesure de post-roll : arrêté à 6,000 s" \
    "$([ "$(etat "$pp")" = "arrêté" ] && [ "$(tete "$pp")" = "6.000" ] && echo 1 || echo 0)"
verdict "(8) sans punch actif, l'option cochée : en lecture" \
    "$([ "$(etat "$ps")" = "en lecture" ] && au_dela "$(tete "$ps")" 2.5 && echo 1 || echo 0)"
verdict "(9) retenu au poste (2 cochées) ; en anglais (2 entrées)" "$([ "$coches" = 2 ] && [ "$en" = 2 ] && echo 1 || echo 0)"
verdict "(10) F9 en lecture après la sortie : toujours en lecture au-delà de 5 s" \
    "$([ "$(etat "$pd")" = "en lecture" ] && au_dela "$(tete "$pd")" 5.0 && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "PRISE-QUANTIFIEE-PUNCH : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PRISE-QUANTIFIEE-PUNCH : la prise tombe sur la grille en un pas, et le transport s'arrête à la sortie du punch"
