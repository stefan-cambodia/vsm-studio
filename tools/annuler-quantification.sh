#!/usr/bin/env bash
# D566.1 — ANNULER ET FIGER LA QUANTIFICATION (piano roll ▸ Temps et durée ▸ Annuler la quantification (N), Figer la
# quantification (N) ; le *Reset Quantize* et le *Freeze Quantize* de Cubase). MESURÉ PAR LE .mid JOUÉ.
#
# LES RÈGLES GARDÉES. Une piste, grille du piano roll à la double croche (120) : 125-365 (60), 250-300 (62),
# 500-610 (64), 130-170 (65). Toutes choisies par « Tout sélectionner », sans souris.
#   (1) « Quantifier (100 %) » puis « Annuler la quantification (4) » : les notes jouées reviennent ; le TÉMOIN, la
#       quantification seule, les met sur la grille (le geste a donc eu lieu) ;
#   (2) quantifier, puis → (un pas de grille, À LA MAIN), puis annuler : chaque note revient de ce que la
#       quantification lui a fait, et GARDE le pas fait à la main — 245-485, 370-420, 620-730, 250-290. Une position
#       d'origine figée (Cubase) rendrait les notes jouées ; rien d'annulé, les notes quantifiées + 120 ;
#   (3) « Quantifier début ET fin » puis annuler : les FINS reviennent aussi (130-170 était allée à 120-240, H22) ;
#   (4) « Figer la quantification (4) » : les notes restent sur la grille, et le menu dit « (0) », grisé ;
#   (5) enregistré après la quantification, ROUVERT, puis annulé : les notes jouées (le déplacement a voyagé) ;
#   (6) Ctrl+Z après l'annulation : les notes quantifiées ;
#   (7) avant toute quantification : « Annuler la quantification (0) », grisée.
#
#   tools/annuler-quantification.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-annuler-quantif.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(125, 365, 60), (250, 300, 62), (500, 610, 64), (130, 170, 65)]
ev = []
for d, f, h in notes:
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « début-fin:hauteur » des notes d'un .mid, triées, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            out.append((ouvertes[x.note].pop(0), t, x.note))
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(out)) or "RIEN")
PY2
}
entree() {   # $1 = journal ; $2 = début du libellé -- l'entrée telle que VSM_MENU_LISTE l'a vue (la dernière)
    grep -h "^VSM_MENU_LISTE : Édition > Temps et durée > $2" "$1" | tail -1 | sed 's/^VSM_MENU_LISTE : Édition > Temps et durée > //'
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
TOUT="menu:Édition > Sélection > Tout sélectionner"
QUANT="menu:Édition > Temps et durée > Quantifier (100 %)"
DEUX="menu:Édition > Temps et durée > Quantifier début ET fin"
ANNULER="menu:Édition > Temps et durée > Annuler la quantification (4)"
FIGER="menu:Édition > Temps et durée > Figer la quantification (4)"

echo "=== D566.1 : annuler et figer la quantification ==="
course quantifie "$brouillon/base" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:exporter-midi:$brouillon/quantifie.mid"
course annule "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1500:lister-menus;1700:$ANNULER;2100:exporter-midi:$brouillon/annule.mid"
course main "$brouillon/base" VSM_DELAI=3000 \
    VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1600:touche:pianoroll:cursor right;1900:$ANNULER;2300:exporter-midi:$brouillon/main.mid"
course fins "$brouillon/base" VSM_DELAI=2800 VSM_GESTE_APRES="1000:$TOUT;1300:$DEUX;1700:$ANNULER;2100:exporter-midi:$brouillon/fins.mid"
course fige "$brouillon/base" VSM_DELAI=2800 \
    VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:$FIGER;2000:lister-menus;2200:exporter-midi:$brouillon/fige.mid"
cp -r "$brouillon/base" "$brouillon/enreg"
course enregistre "$brouillon/enreg" VSM_DELAI=2400 VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:enregistrer:$brouillon/enreg"
course rouvert "$brouillon/enreg" VSM_DELAI=2600 \
    VSM_GESTE_APRES="1000:$TOUT;1300:$ANNULER;1800:exporter-midi:$brouillon/rouvert.mid"
course annule-z "$brouillon/base" VSM_DELAI=3000 \
    VSM_GESTE_APRES="1000:$TOUT;1300:$QUANT;1700:$ANNULER;2000:touche:ctrl + Z;2400:exporter-midi:$brouillon/annule-z.mid"
course rien "$brouillon/base" VSM_DELAI=1800 VSM_GESTE_APRES="1000:$TOUT;1300:lister-menus"

JOUEES="125-365:60 130-170:65 250-300:62 500-610:64"
QUANTIFIEES="120-160:65 120-360:60 240-290:62 480-590:64"
q="$(notes "$brouillon/quantifie.mid")"; a="$(notes "$brouillon/annule.mid")"; m="$(notes "$brouillon/main.mid")"
f="$(notes "$brouillon/fins.mid")"; g="$(notes "$brouillon/fige.mid")"; r="$(notes "$brouillon/rouvert.mid")"
z="$(notes "$brouillon/annule-z.mid")"
avant="$(entree "$brouillon/annule.txt" "Annuler la quantification")"
fige_menu="$(entree "$brouillon/fige.txt" "Annuler la quantification")"
rien_menu="$(entree "$brouillon/rien.txt" "Annuler la quantification")"
grep -h "^VSM_QUANTIFIER : " "$brouillon"/annule.txt "$brouillon"/main.txt "$brouillon"/fins.txt "$brouillon"/fige.txt "$brouillon"/rouvert.txt \
    | sort | uniq -c | sed 's/^/       /'
echo "       quantifiées (témoin) : [$q]"
echo "       annulées : [$a] — menu [$avant]"
echo "       à la main puis annulées : [$m]"
echo "       début ET fin puis annulées : [$f]"
echo "       figées : [$g] — menu [$fige_menu]"
echo "       rouvert puis annulées : [$r] ; Ctrl+Z : [$z]"
echo "       avant toute quantification : [$rien_menu]"

verdict "(1) annuler rend les notes jouées ; le témoin quantifié est sur la grille" \
    "$([ "$a" = "$JOUEES" ] && [ "$q" = "$QUANTIFIEES" ] && [ "$avant" = "Annuler la quantification (4)" ] && echo 1 || echo 0)"
verdict "(2) le pas fait à la main reste : 245-485, 370-420, 620-730, 250-290" \
    "$([ "$m" = "245-485:60 250-290:65 370-420:62 620-730:64" ] && echo 1 || echo 0)"
verdict "(3) début ET fin : les fins reviennent aussi" "$([ "$f" = "$JOUEES" ] && echo 1 || echo 0)"
verdict "(4) figer : les notes restent sur la grille, le menu dit (0), grisé" \
    "$([ "$g" = "$QUANTIFIEES" ] && [ "$fige_menu" = "Annuler la quantification (0) [grisée]" ] && echo 1 || echo 0)"
verdict "(5) enregistré, rouvert, annulé : les notes jouées" \
    "$([ "$r" = "$JOUEES" ] && grep -q "^VSM_QUANTIFIER : 4 note(s) rendue(s)" "$brouillon/rouvert.txt" && echo 1 || echo 0)"
verdict "(6) Ctrl+Z rend les notes quantifiées, après une annulation qui a eu lieu" \
    "$([ "$z" = "$QUANTIFIEES" ] && grep -q "^VSM_QUANTIFIER : 4 note(s) rendue(s)" "$brouillon/annule-z.txt" && echo 1 || echo 0)"
verdict "(7) avant toute quantification : (0), grisée" "$([ "$rien_menu" = "Annuler la quantification (0) [grisée]" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ANNULER-QUANTIFICATION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ANNULER-QUANTIFICATION : la quantification se retire, et ce qu'on a fait à la main depuis reste"
