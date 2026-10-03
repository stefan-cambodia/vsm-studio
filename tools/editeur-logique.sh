#!/usr/bin/env bash
# D535.1 bis — L'ÉDITEUR LOGIQUE AU PIANO ROLL : LA FENÊTRE, LE COMPTE, LE PAS, LES REFUS DITS.
#
# LA RÈGLE GARDÉE. Huit notes, dont trois « fantômes » brèves ET faibles (64, 72, 69), une note
# faible mais longue (62), une brève mais forte (65), et une à 120 :
#   (1) « vélocité < 30 et durée < 1/32 », supprimer → le .mid exporté, relu en MULTIENSEMBLE,
#       perd exactement les trois fantômes ; le journal dit « 3 répondaient, 3 changée(s) » ;
#       Ctrl+Z rend les huit ;
#   (2) choisir → « 3 note(s) choisie(s) », le .mid intact, aucun pas d'historique ;
#   (3) « vélocité < trente » → boîte « Règle illisible » qui cite « trente », .mid intact,
#       aucun pas ;
#   (4) « durée >= 480 », transposer +12 → la note à 120 est parmi celles qui répondent : RIEN
#       ne bouge (D536), « 1 refusée(s) », aucun pas ;
#   (5) le fichier de préférences du HOME de la course (1) porte la règle sous sa forme LISIBLE
#       française (« vélocité < 30 et durée < 1/32 ») et l'action 2 — et non la canonique en
#       ticks, qui changeait de sens d'une résolution à l'autre (complément de D535.1 bis) ;
#   (6) le compte de la fenêtre : « 3 note(s) sur 8 répondent » au journal ;
#   (7) rouverte sur ces préférences, la fenêtre montre la règle dans la langue de l'interface :
#       « vélocité < 30 et durée < 1/32 », et « velocity < 30 and length < 1/32 » sous
#       VSM_LANGUE=en (`VSM_LOGIQUE_REGLE`, lu au journal) ;
#   (8) D542.2 : choisir le préréglage « Fantômes » REMPLIT la règle (le journal la dit après le
#       choix) et l'action, et l'appliquer supprime les trois fantômes ; l'enregistrer sous « Mes
#       fantômes » le range dans les préférences ;
#   (9) rouverte sur ces préférences, la liste offre « Mes fantômes », et le choisir remplit la
#       même règle et supprime les mêmes notes.
# Chaque fichier se lit AVANT de juger, et les avertissements du journal sont relayés.
#
#   tools/editeur-logique.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-logique.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/huit/midi"
python3 - "$brouillon/huit" <<'PY'
import json, struct, sys
d = sys.argv[1]
# (début, hauteur, durée, vélocité)
NOTES = [(0, 60, 480, 100), (240, 64, 30, 20), (480, 67, 480, 90), (960, 72, 40, 15),
         (1440, 62, 480, 25), (1920, 65, 20, 100), (2400, 69, 50, 10), (2880, 120, 480, 80)]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = []
for debut, h, duree, v in NOTES:
    evs.append((debut, 1, bytes([0x90, h, v])))
    evs.append((debut + duree, 0, bytes([0x80, h, 0])))   # à tick égal, la fin avant le début
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x05Piano", 0
for tick, _, octets in evs:
    corps += vlq(tick - t) + octets
    t = tick
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
PY

MAISON=""
course() {   # $1 = nom ; $2 = VSM_OPTIONS ; $3 = VSM_GESTE_APRES ; $4 = préférences à reprendre ; $5 = langue
    local nom="$1" langue="${5:-fr}" entree="Éditeur logique…"
    [ "$langue" = en ] && entree="Logical editor…"
    MAISON="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    if [ -n "${4:-}" ]; then mkdir -p "$MAISON/VintageSynthMidiStudio" && cp "$4" "$MAISON/VintageSynthMidiStudio/"; fi
    env HOME="$MAISON" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/huit" VSM_TAILLE="1600x1000" VSM_DELAI=4500 \
        VSM_VUE="sans-rapport" VSM_MENU_CONTEXTE="pianoroll:$entree" VSM_OPTIONS="$2" VSM_GESTE_APRES="$3" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # le multiensemble des hauteurs jouées d'un .mid, trié — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
i, h = 14, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st = 0, 0
    while j < len(corps):
        while corps[j] & 0x80: j += 1
        j += 1
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: h.append(corps[j])
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(",".join(str(x) for x in sorted(h)) or "AUCUNE")
PY
}
pas() { grep "^VSM_HISTORIQUE_PAS" "$brouillon/$1.txt" | tail -1 | cut -d: -f2 | tr -d ' '; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
HUIT="60,62,64,65,67,69,72,120"

echo "=== D535.1 bis : l'éditeur logique au piano roll ==="
course supprimer "regle=vélocité < 30 et durée < 1/32;action=2" \
    "1500:exporter-midi:$brouillon/supprime.mid;1800:touche:ctrl + Z;2200:exporter-midi:$brouillon/annule.mid"
maison1="$MAISON"
h1="$(hauteurs "$brouillon/supprime.mid")"; h2="$(hauteurs "$brouillon/annule.mid")"
grep "^VSM_LOGIQUE" "$brouillon/supprimer.txt" | sed 's/^/       /'
echo "       supprimé : {$h1} ; après Ctrl+Z : {$h2}"
verdict "(1) les trois fantômes — et eux seuls — supprimés" "$([ "$h1" = "60,62,65,67,120" ] && echo 1 || echo 0)"
verdict "(1) le journal : 3 répondaient, 3 changée(s)" \
    "$(grep -q "^VSM_LOGIQUE : supprimer sur « velocite < 30 et duree < 60 » — 3 répondaient, 3 changée(s), 0 refusée(s)" "$brouillon/supprimer.txt" && echo 1 || echo 0)"
verdict "(1) Ctrl+Z rend les huit" "$([ "$h2" = "$HUIT" ] && echo 1 || echo 0)"
compte="$(grep "^VSM_LOGIQUE_COMPTE" "$brouillon/supprimer.txt" | tail -1)"
echo "       $compte"
verdict "(6) le compte de la fenêtre : 3 note(s) sur 8" "$(echo "$compte" | grep -q "3 note(s) sur 8 répondent" && echo 1 || echo 0)"
prefs="$maison1/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
lu="$(python3 - "$prefs" <<'PY'
import sys, xml.etree.ElementTree as ET
try:
    racine = ET.parse(sys.argv[1]).getroot()
except Exception:
    print("ABSENT"); sys.exit(0)
v = {e.get("name"): e.get("val") for e in racine.iter("VALUE")}
print(f"{v.get('editeurLogique.regle', '?')} | action {v.get('editeurLogique.action', '?')}")
PY
)"
echo "       préférences : $lu"
verdict "(5) la règle LISIBLE française et l'action retenues" "$([ "$lu" = "vélocité < 30 et durée < 1/32 | action 2" ] && echo 1 || echo 0)"

# (7) RÉOUVERTURE sur ces préférences, sans réponse de banc : la fenêtre reste ouverte jusqu'à la
# photo, et dit au journal la règle qu'elle montre.
for langue in fr en; do
    course "rouverte-$langue" "" "" "$prefs" "$langue"
    grep "^VSM_LOGIQUE_REGLE" "$brouillon/rouverte-$langue.txt" | sed "s/^/       ($langue) /"
done
verdict "(7) rouverte : « vélocité < 30 et durée < 1/32 » en français" \
    "$(grep -qx "VSM_LOGIQUE_REGLE : vélocité < 30 et durée < 1/32" "$brouillon/rouverte-fr.txt" && echo 1 || echo 0)"
verdict "(7) rouverte : « velocity < 30 and length < 1/32 » en anglais" \
    "$(grep -qx "VSM_LOGIQUE_REGLE : velocity < 30 and length < 1/32" "$brouillon/rouverte-en.txt" && echo 1 || echo 0)"

course choisir "regle=vélocité < 30 et durée < 1/32;action=1" \
    "1500:exporter-midi:$brouillon/choisi.mid;1800:relever-historique"
sel="$(grep "^VSM_SELECTION" "$brouillon/choisir.txt" | tail -1)"
h3="$(hauteurs "$brouillon/choisi.mid")"
echo "       $sel ; .mid {$h3} ; pas d'historique : $(pas choisir)"
verdict "(2) choisir : 3 notes choisies, .mid intact, aucun pas" \
    "$(echo "$sel" | grep -q "^VSM_SELECTION : 3 note(s) choisie(s)" && [ "$h3" = "$HUIT" ] && [ "$(pas choisir)" = 0 ] && echo 1 || echo 0)"

course illisible "regle=vélocité < trente;action=2" \
    "1500:exporter-midi:$brouillon/illisible.mid;1800:relever-historique"
boite="$(grep "VSM_BOITE.*Règle illisible" "$brouillon/illisible.txt" | head -1)"
h4="$(hauteurs "$brouillon/illisible.mid")"
echo "       ${boite:-aucune boîte} ; .mid {$h4} ; pas : $(pas illisible)"
verdict "(3) « vélocité < trente » refusé en citant « trente », rien de fait" \
    "$(echo "$boite" | grep -q "trente" && [ "$h4" = "$HUIT" ] && [ "$(pas illisible)" = 0 ] && echo 1 || echo 0)"

course transposer "regle=durée >= 480;action=4;valeur=12" \
    "1500:exporter-midi:$brouillon/transpose.mid;1800:relever-historique"
h5="$(hauteurs "$brouillon/transpose.mid")"
grep "^VSM_LOGIQUE :" "$brouillon/transposer.txt" | sed 's/^/       /'
echo "       .mid {$h5} ; pas : $(pas transposer)"
verdict "(4) transposer +12 avec une note à 120 : rien ne bouge, 1 refusée, aucun pas" \
    "$(grep -q "^VSM_LOGIQUE : transposer sur « duree >= 480 » — 4 répondaient, 0 changée(s), 1 refusée(s)" "$brouillon/transposer.txt" && [ "$h5" = "$HUIT" ] && [ "$(pas transposer)" = 0 ] && echo 1 || echo 0)"

# (8) et (9) — D542.2 : LES PRÉRÉGLAGES.
course prereglage "prereglage=2;enregistrer=Mes fantômes" "1500:exporter-midi:$brouillon/prereglage.mid"
maison8="$MAISON"
hp="$(hauteurs "$brouillon/prereglage.mid")"
grep "^VSM_LOGIQUE_REGLE\|^VSM_LOGIQUE : " "$brouillon/prereglage.txt" | sed 's/^/       /'
echo "       .mid {$hp}"
verdict "(8) « Fantômes » remplit la règle, et l'appliquer supprime les trois fantômes" \
    "$(grep "^VSM_LOGIQUE_REGLE" "$brouillon/prereglage.txt" | tail -1 | grep -qx "VSM_LOGIQUE_REGLE : vélocité < 30 et durée < 1/32" \
       && [ "$hp" = "60,62,65,67,120" ] && echo 1 || echo 0)"
verdict "(8) enregistré sous « Mes fantômes »" \
    "$(grep -q "^VSM_LOGIQUE : préréglage « Mes fantômes » enregistré — « vélocité < 30 et durée < 1/32 »" "$brouillon/prereglage.txt" && echo 1 || echo 0)"
course mien "prereglage=9" "1500:exporter-midi:$brouillon/mien.mid" \
    "$maison8/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
hm="$(hauteurs "$brouillon/mien.mid")"
grep "^VSM_LOGIQUE_PREREGLAGES" "$brouillon/mien.txt" | sed 's/^/       /'
verdict "(9) rouverte, la liste offre « Mes fantômes », et le choisir refait le même geste" \
    "$(grep -q "^VSM_LOGIQUE_PREREGLAGES : .* | Mes fantômes$" "$brouillon/mien.txt" \
       && grep "^VSM_LOGIQUE_REGLE" "$brouillon/mien.txt" | tail -1 | grep -qx "VSM_LOGIQUE_REGLE : vélocité < 30 et durée < 1/32" \
       && [ "$hm" = "60,62,65,67,120" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ÉDITEUR LOGIQUE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ÉDITEUR LOGIQUE : la règle se lit, se compte, s'applique et se défait"
