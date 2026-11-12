#!/usr/bin/env bash
# D624 — LA PRISE QUANTIFIÉE SUIT LA GRILLE DU RANG, par la même fonction que « Quantifier » (`quantizeNotesByRow`, au
# cœur) : « Quantifier chaque prise » (D579.1) quantifiait à la seule grille de la barre, quand « Quantifier la dernière
# prise » suivait la grille de chaque rang depuis D622 — deux gestes du même nom, deux prises différentes.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM ; la barre à 1/16 : 120 ticks). Une piste « Batterie » VIDE sur la
# TR-909, canal 10, armée (`armer:0`), le rang 38 réglé à 1/8 (240 ticks) par le projet (`"drumGrid"`). La prise : quatre
# frappes de 38 à 350 ms d'écart (336 ticks) et deux de 36, par `note-entrante:`, le point d'entrée du matériel.
# L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D624) :
#   (b) « Quantifier chaque prise » : les quatre frappes de 38 sur des multiples de 240, toutes distinctes ; les deux de 36
#       sur des multiples de 120 ; le journal dit « 4 au rang » ; le TÉMOIN (D623) : au moins une frappe de 38 hors des
#       multiples de 240 (trois écarts de 336 ticks arrondis à 120 en contiennent un de 360) ;
#   (c) sans l'option, puis « Quantifier la dernière prise » : les quatre de 38 sur des multiples de 240 — sur le binaire
#       neuf ET sur le témoin (ce geste-là n'a pas changé) ;
#   (d) Ctrl+Z après la prise de (b) : 0 note, en un pas ; le libellé du menu dit la règle, en français et en anglais.
# D631 — LA PASSE EMPILÉE (le « remis » de D624), un jeu à part : la boucle ACTIVE de 0 à 1 920 ; « Empiler les prises » ;
# la même suite jouée dans deux passages ; le projet enregistré, ses prises relues (`project.json` et `midi/prises.mid`) :
#   (e) deux prises bornées par la boucle ; dans chacune, quatre 38 sur des multiples de 240, distincts, deux 36 sur 120 ;
#       le journal dit « 4 au rang » deux fois ; le témoin (D623) : dans l'une au moins, un 38 hors des multiples de 240.
#
#   tools/prise-par-rang.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D623/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-prise-par-rang.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1] + "/projet"; os.makedirs(f"{b}/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x08Batterie\x00\xff\x2f\x00"   # une piste VIDE : la prise seule
open(f"{b}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "prise", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 9, "color": "#FF6B9BFF", "effects": [], "name": "Batterie",
                       "drumGrid": [{"in": 38, "grid": "1/8"}],
                       "instrument": {"preferredPlugin": "vsm.tr909"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{b}/project.json", "w"), indent=1)
# D631 : le même projet, la boucle ACTIVE sur la première mesure
import shutil
shutil.copytree(b, sys.argv[1] + "/boucle")
pj = json.load(open(sys.argv[1] + "/boucle/project.json"))
pj["transport"]["loop"] = {"enabled": True, "startTick": 0, "endTick": 1920}
json.dump(pj, open(sys.argv[1] + "/boucle/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Un HOME neuf, une copie neuve du projet.
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    cp -r "$brouillon/${JEU:-projet}" "$brouillon/p-$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/p-$nom" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=4200 "$@" VSM_GESTE_APRES="$gestes" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE|TOUCHE|VUE|MENU|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|grisée|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
debuts() {   # $1 = fichier ; $2 = hauteur : les débuts des notes de cette hauteur, triés ; « RIEN » ; « ABSENT »
    python3 - "$1" "$2" <<'PY2'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
h = int(sys.argv[2]); out, i = [], 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; st = 0
    while j < fin:
        v = 0
        while True:
            c = d[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if d[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if d[j] == 0xFF else 1); n = 0
            while True:
                c = d[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if d[j] & 0x80: st = d[j]; j += 1
        tl = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
        if st & 0xF0 == 0x90 and d[j] == h and d[j + 1] > 0: out.append(t)
        j += tl
    i = fin
print(" ".join(str(x) for x in sorted(out)) or "RIEN")
PY2
}
nombre() { case "$1" in ABSENT|RIEN) echo 0 ;; *) echo "$1" | wc -w ;; esac; }
multiples() {   # $1 = débuts ; $2 = pas : tous multiples : 1 ; sinon 0
    case "$1" in ABSENT|RIEN) echo 0; return ;; esac
    for t in $1; do [ $((t % $2)) -eq 0 ] || { echo 0; return; }; done
    echo 1
}
distincts() { case "$1" in ABSENT|RIEN) echo 0 ;; *) echo "$1" | tr ' ' '\n' | sort -u | wc -l ;; esac; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

AQ="menu:Enregistrement > Quantifier chaque prise"
SANS="menu:Enregistrement > Sans décompte"
DERNIERE="menu:Enregistrement > Quantifier la dernière prise (grille du piano roll, ou du rang)"
DERNIERE_TEMOIN="menu:Enregistrement > Quantifier la dernière prise (grille du piano roll)"
JOUER="1100:note-entrante:38:100:60;1250:note-entrante:36:100:60;1450:note-entrante:38:100:60;1800:note-entrante:38:100:60;1950:note-entrante:36:100:60;2150:note-entrante:38:100:60"
PRISE="300:armer:0;400:$SANS;800:touche:F9;$JOUER;2600:touche:F9"

echo "=== D624 : la prise quantifiée suit la grille du rang ==="
course "$BIN" aq "300:armer:0;400:$SANS;500:$AQ;800:touche:F9;$JOUER;2600:touche:F9;3100:exporter-midi:$brouillon/aq.mid"
course "$BIN" derniere "$PRISE;2900:$DERNIERE;3400:exporter-midi:$brouillon/derniere.mid"
course "$BIN" brute "$PRISE;3100:exporter-midi:$brouillon/brute.mid"
course "$BIN" defait "300:armer:0;400:$SANS;500:$AQ;800:touche:F9;$JOUER;2600:touche:F9;2900:touche:ctrl + Z;3300:exporter-midi:$brouillon/defait.mid"
course "$BIN" fr "900:lister-menus" VSM_DELAI=1600
course "$BIN" en "900:lister-menus" VSM_DELAI=1600 VSM_LANGUE=en

a38="$(debuts "$brouillon/aq.mid" 38)"; a36="$(debuts "$brouillon/aq.mid" 36)"
d38="$(debuts "$brouillon/derniere.mid" 38)"; b38="$(debuts "$brouillon/brute.mid" 38)"
z38="$(debuts "$brouillon/defait.mid" 38)"; z36="$(debuts "$brouillon/defait.mid" 36)"
journal="$(grep -h "^VSM_QUANTIFIER_PRISE : " "$brouillon/aq.txt" | head -1)"
echo "       chaque prise : 38 [$a38] ; 36 [$a36] — $journal"
echo "       la dernière prise : 38 [$d38] ; la prise brute (ni l'un ni l'autre) : 38 [$b38] ; Ctrl+Z : 38 [$z38], 36 [$z36]"
lfr="$(grep -ch "^VSM_MENU_LISTE : Enregistrement > \(Quantifier la dernière prise (grille du piano roll, ou du rang)\|(les prises MIDI, à la grille du piano roll ou à celle du rang ; une prise audio ne se quantifie pas)\)" "$brouillon/fr.txt")"
len="$(grep -ch "^VSM_MENU_LISTE : Recording > \(Quantise the last take (piano roll grid, or the row's)\|(MIDI takes, to the piano roll grid or the row's; an audio take is not quantised)\)" "$brouillon/en.txt")"

verdict "(b) chaque prise : 4 frappes de 38, sur des multiples de 240, distinctes" \
    "$([ "$(nombre "$a38")" = 4 ] && [ "$(multiples "$a38" 240)" = 1 ] && [ "$(distincts "$a38")" = 4 ] && echo 1 || echo 0)"
verdict "(b) chaque prise : 2 frappes de 36, sur des multiples de 120" \
    "$([ "$(nombre "$a36")" = 2 ] && [ "$(multiples "$a36" 120)" = 1 ] && echo 1 || echo 0)"
verdict "(b) le journal dit « 4 au rang »" "$(echo "$journal" | grep -q ", 4 au rang$" && echo 1 || echo 0)"
verdict "(b) le témoin sans quantification : 4 frappes de 38, au moins une hors des multiples de 120 (elles ont bougé)" \
    "$([ "$(nombre "$b38")" = 4 ] && [ "$(multiples "$b38" 120)" = 0 ] && echo 1 || echo 0)"
verdict "(c) la dernière prise : 4 frappes de 38 sur des multiples de 240" \
    "$([ "$(nombre "$d38")" = 4 ] && [ "$(multiples "$d38" 240)" = 1 ] && echo 1 || echo 0)"
verdict "(d) Ctrl+Z après la prise quantifiée : 0 note, en un pas" \
    "$([ "$z38" = RIEN ] && [ "$z36" = RIEN ] && echo 1 || echo 0)"
verdict "(d) les deux libellés disent la règle, en français ($lfr) et en anglais ($len)" "$([ "$lfr" = 2 ] && [ "$len" = 2 ] && echo 1 || echo 0)"
# D631 : LA PASSE EMPILÉE — la suite de (b) dans le premier passage, puis 2 s plus tard dans le second.
prises() {   # $1 = dossier d'un projet enregistré : une ligne par prise de « Batterie », « nom|début|fin|38 …|36 … »
    python3 - "$1" <<'PY3'
import json, sys
d = sys.argv[1]
try:
    pj = json.load(open(d + "/project.json")); raw = open(d + "/midi/prises.mid", "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
pistes, i = [], 14
while i < len(raw) and raw[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(raw[i + 4:i + 8], "big"); j = i + 8; t = 0; st = 0; ons = []
    while j < fin:
        v = 0
        while True:
            c = raw[j]; j += 1; v = (v << 7) | (c & 0x7F)
            if not c & 0x80: break
        t += v
        if raw[j] in (0xFF, 0xF0, 0xF7):
            k = j + (2 if raw[j] == 0xFF else 1); n = 0
            while True:
                c = raw[k]; k += 1; n = (n << 7) | (c & 0x7F)
                if not c & 0x80: break
            j = k + n; continue
        if raw[j] & 0x80: st = raw[j]; j += 1
        tl = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
        if st & 0xF0 == 0x90 and raw[j + 1] > 0: ons.append((t, raw[j]))
        j += tl
    pistes.append(ons); i = fin
for tr in pj.get("tracks", []):
    for tk in tr.get("takes", []):
        ons = pistes[tk["midiTrack"]] if 0 <= tk.get("midiTrack", -1) < len(pistes) else []
        h38 = " ".join(str(t) for t, h in sorted(ons) if h == 38) or "RIEN"
        h36 = " ".join(str(t) for t, h in sorted(ons) if h == 36) or "RIEN"
        print(f"{tk['name']}|{tk['startTick']}|{tk['endTick']}|{h38}|{h36}")
PY3
}
EMPILER="menu:Enregistrement > Empiler les prises"
JOUER2="3100:note-entrante:38:100:60;3250:note-entrante:36:100:60;3450:note-entrante:38:100:60;3800:note-entrante:38:100:60;3950:note-entrante:36:100:60;4150:note-entrante:38:100:60"
BOUCLE="300:armer:0;400:$SANS;500:$AQ;600:$EMPILER;800:touche:F9;$JOUER;$JOUER2;4500:touche:F9;4900:enregistrer:$brouillon"
JEU=boucle course "$BIN" boucle "$BOUCLE/boucle-projet" VSM_DELAI=5600
juge_prises() {   # $1 = dossier ; $2 = 1 si l'on attend 240 partout, 0 si l'on attend au moins un 38 hors de 240 (le témoin)
    local lignes n ok=1 hors=0 pleine=0 nom de a l38 l36
    lignes="$(prises "$1")"; n=$(printf '%s\n' "$lignes" | grep -c '|')
    while IFS='|' read -r nom de a l38 l36; do
        [ -z "$nom" ] && continue
        echo "       $nom [$de → $a] : 38 [$l38] ; 36 [$l36]" >&2
        # CORRIGÉ APRÈS LE PREMIER ESSAI, et dit (ROADMAP-daw, D631) : la DERNIÈRE passe se ferme à l'ARRÊT, pas au bout de
        # la boucle (`closePass(…, finSecondes)` à l'arrêt) — « Prise 2 [0 → 1635] ». Chaque prise commence au début de la
        # boucle et finit au plus à sa fin ; l'une au moins la remplit.
        [ "$de" = 0 ] && [ "$a" -le 1920 ] || ok=0
        [ "$a" = 1920 ] && pleine=1
        [ "$(nombre "$l38")" = 4 ] && [ "$(distincts "$l38")" = 4 ] && [ "$(multiples "$l36" 120)" = 1 ] && [ "$(nombre "$l36")" = 2 ] || ok=0
        [ "$(multiples "$l38" 240)" = 1 ] || hors=1
    done <<< "$lignes"
    if [ "$2" = 1 ]; then [ "$n" = 2 ] && [ "$ok" = 1 ] && [ "$pleine" = 1 ] && [ "$hors" = 0 ] && echo 1 || echo 0
    else [ "$n" = 2 ] && [ "$hors" = 1 ] && echo 1 || echo 0; fi
}
echo "       la passe empilée (D631) : $(grep -c "^VSM_QUANTIFIER_PRISE : .*, 4 au rang$" "$brouillon/boucle.txt") ligne(s) « 4 au rang »"
verdict "(e, D631) deux prises dans la boucle (la première pleine, la dernière close à l'arrêt) ; dans chacune, quatre 38 sur des multiples de 240, distincts, deux 36 sur 120" \
    "$(juge_prises "$brouillon/boucle-projet" 1)"
verdict "(e, D631) le journal dit « 4 au rang » deux fois" \
    "$([ "$(grep -c "^VSM_QUANTIFIER_PRISE : .*, 4 au rang$" "$brouillon/boucle.txt")" = 2 ] && echo 1 || echo 0)"
if [ -x "$TEMOIN" ]; then
    avant=$avertis
    course "$TEMOIN" t-aq "300:armer:0;400:$SANS;500:$AQ;800:touche:F9;$JOUER;2600:touche:F9;3100:exporter-midi:$brouillon/t-aq.mid"
    course "$TEMOIN" t-derniere "$PRISE;2900:$DERNIERE_TEMOIN;3400:exporter-midi:$brouillon/t-derniere.mid"
    avertis_temoin=$((avertis - avant)); avertis=$avant
    t38="$(debuts "$brouillon/t-aq.mid" 38)"; td38="$(debuts "$brouillon/t-derniere.mid" 38)"
    echo "       témoin (D623) : chaque prise, 38 [$t38] ; la dernière prise, 38 [$td38] ; $avertis_temoin avertissement(s)"
    verdict "(b) témoin (D623), chaque prise : 4 frappes de 38 sur 120, au moins une hors des multiples de 240" \
        "$([ "$(nombre "$t38")" = 4 ] && [ "$(multiples "$t38" 120)" = 1 ] && [ "$(multiples "$t38" 240)" = 0 ] && echo 1 || echo 0)"
    verdict "(c) témoin (D623), la dernière prise : 4 frappes de 38 sur des multiples de 240 (ce geste n'a pas changé)" \
        "$([ "$(nombre "$td38")" = 4 ] && [ "$(multiples "$td38" 240)" = 1 ] && echo 1 || echo 0)"
    avant=$avertis
    JEU=boucle course "$TEMOIN" t-boucle "$BOUCLE/t-boucle-projet" VSM_DELAI=5600
    avertis=$avant
    echo "       témoin (D623), la passe empilée :"
    verdict "(e, D631) témoin (D623) : deux prises, et dans l'une au moins un 38 hors des multiples de 240" \
        "$(juge_prises "$brouillon/t-boucle-projet" 0)"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "PRISE-PAR-RANG : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PRISE-PAR-RANG : la prise quantifiée et « Quantifier la dernière prise » suivent toutes deux la grille du rang"
