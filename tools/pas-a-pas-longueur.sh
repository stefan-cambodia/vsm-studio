#!/usr/bin/env bash
# D626 — LA SAISIE PAS À PAS SUIT LA LONGUEUR D'INSERTION (le *Step Input* de Cubase : *Quantize* règle l'espacement,
# *Insert Length* la longueur), mesurée dans l'application : une note jouée s'écrit à la tête, longue de la longueur
# d'insertion quand elle est choisie, et la tête avance d'UN PAS DE GRILLE. Les notes lues dans le projet enregistré ;
# le témoin (D625) à côté.
#
# LES RÈGLES GARDÉES. La mécanique de `pas-a-pas.sh` (D500) : « Clavier d'ordinateur » et « Pas à pas » armés, les
# touches jouées en DIFFÉRÉ à 500 ms d'intervalle (`VSM_GESTE_APRES`), le projet enregistré ensuite. La grille de la
# barre à 1/16 = 120 ticks ; la suite A (do) S (ré) Entrée D (mi). L'ATTENDU ÉCRIT AVANT LE CODE (ROADMAP-daw, D626) :
#   (a) sans rien choisir : 60@0+120 62@120+120 64@360+120, la tête à 480 ;
#   (b) « Insertion : 1/8 » : 60@0+240 62@120+240 64@360+240 — les débuts inchangés —, la tête à 480 ;
#   (c) « Insertion : 1/32 » : 60@0+60 62@120+60 64@360+60, la tête à 480 ;
#   (d) « Insertion : 1/8 », puis Retour arrière après D, puis F : fa à 360, long de 240 (on recule d'un pas de grille) ;
#   (e) le témoin (le binaire de D625, la même ligne de gestes) : sous « Insertion : 1/8 », 60@0+120 — le défaut ;
#   (f) l'infobulle de « Pas à pas » nomme la longueur d'insertion, en français et en anglais.
#
#   tools/pas-a-pas-longueur.sh [binaire] [binaire témoin]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
TEMOIN="${2:-./build/temoin-D625/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-pas-a-pas-longueur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

INS="menu:Édition > Temps et durée > Longueur d'insertion > Insertion"
avertis=0
course() {   # $1 = binaire ; $2 = nom ; $3 = gestes ; le reste : variables. Un HOME neuf, aucun projet ouvert (D500).
    local bin="$1" nom="$2" gestes="$3" n
    shift 3
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_MENU="Clavier d'ordinateur" VSM_GESTE_PISTE="cliquer:Pas à pas" VSM_DELAI=4600 \
        VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    local motif="VSM_(GESTE_APRES|GESTE_PISTE|GESTE|TOUCHE|VUE|MENU|CLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|ÉCHEC|GRISÉ|JAMAIS|illisible)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    case "$nom" in t-*) ;; *) avertis=$((avertis + n)); grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2 ;; esac
}
# Les notes d'un projet enregistré : « hauteur@début+durée », triées — appariées dans l'ordre d'arrivée, comme
# l'application les relit (la leçon de `longueur-insertion.sh`).
notes() {
    python3 - "$1/midi/arrangement.mid" <<'PY'
import sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    print("ABSENT"); sys.exit(0)
out, ouvertes, i = [], {}, 14
while i < len(d) and d[i:i + 4] == b"MTrk":
    fin = i + 8 + int.from_bytes(d[i + 4:i + 8], "big"); j = i + 8; t = 0; statut = 0
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
        if d[j] & 0x80: statut = d[j]; j += 1
        taille = 1 if statut & 0xF0 in (0xC0, 0xD0) else 2
        h, vel = d[j], (d[j + 1] if taille == 2 else 0)
        if statut & 0xF0 == 0x90 and vel > 0: ouvertes.setdefault(h, []).append(t)
        elif statut & 0xF0 in (0x80, 0x90) and ouvertes.get(h): out.append((ouvertes[h].pop(0), h, t))
        j += taille
    i = fin
print(" ".join(f"{h}@{s}+{e - s}" for s, h, e in sorted(out)) if out else "AUCUNE")
PY
}
tete() { grep -o "VSM_TETE : tick -\?[0-9]*" "$brouillon/$1.txt" | tail -1 | grep -o "\-\?[0-9]*$"; }
rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
egal() { [ "$1" = "$2" ] && echo 1 || echo 0; }

echo "=== D626 : la saisie pas à pas suit la longueur d'insertion ==="
SUITE="900:touche:x11:A;1400:touche:x11:S;1900:touche:return;2400:touche:x11:D"
course "$BIN" a "$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/a-projet"
course "$BIN" b "400:$INS : 1/8;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/b-projet"
course "$BIN" c "400:$INS : 1/32;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/c-projet"
course "$BIN" d "400:$INS : 1/8;$SUITE;2900:touche:backspace;3400:touche:x11:F;3800:relever-tete;4100:enregistrer:$brouillon/d-projet"
# L'INFOBULLE, relevée sans le clavier ni le pas à pas — CORRIGÉ APRÈS LE PREMIER ESSAI : la course anglaise cherchait
# le menu FRANÇAIS « Clavier d'ordinateur », et l'application l'a dit (un avertissement du banc, pas du logiciel).
bulle() {   # $1 = nom ; le reste : variables
    local nom="$1"; shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" VSM_TEXTES_LISTE=1 \
        VSM_DELAI=1500 "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
}
bulle f
bulle en VSM_LANGUE=en

A="$(notes "$brouillon/a-projet")"; B="$(notes "$brouillon/b-projet")"; C="$(notes "$brouillon/c-projet")"; D="$(notes "$brouillon/d-projet")"
echo "       (a) $A ; tête $(tete a)"
echo "       (b) $B ; tête $(tete b) — $(grep -h "^VSM_INSERTION : " "$brouillon/b.txt" | head -1)"
echo "       (c) $C ; tête $(tete c)"
echo "       (d) $D ; tête $(tete d)"
BF="$(grep -h "^VSM_TEXTE : infobulle : Saisie pas à pas" "$brouillon/f.txt" | head -1)"
BE="$(grep -h "^VSM_TEXTE : infobulle : Step input" "$brouillon/en.txt" | head -1)"
echo "       (f) ${BF:-aucune infobulle « Saisie pas à pas » relevée}"
echo "           ${BE:-aucune infobulle « Step input » relevée}"

verdict "(a) sans rien choisir : 60@0+120 62@120+120 64@360+120, tête 480" \
    "$([ "$A" = "60@0+120 62@120+120 64@360+120" ] && [ "$(tete a)" = 480 ] && echo 1 || echo 0)"
verdict "(b) « Insertion : 1/8 » : 60@0+240 62@120+240 64@360+240, tête 480" \
    "$([ "$B" = "60@0+240 62@120+240 64@360+240" ] && [ "$(tete b)" = 480 ] && echo 1 || echo 0)"
verdict "(c) « Insertion : 1/32 » : 60@0+60 62@120+60 64@360+60, tête 480" \
    "$([ "$C" = "60@0+60 62@120+60 64@360+60" ] && [ "$(tete c)" = 480 ] && echo 1 || echo 0)"
verdict "(d) Retour arrière recule d'un pas de grille : 65@360+240, tête 480" \
    "$([ "$D" = "60@0+240 62@120+240 64@360+240 65@360+240" ] && [ "$(tete d)" = 480 ] && echo 1 || echo 0)"
verdict "(f) l'infobulle nomme la longueur d'insertion" "$(case "$BF" in *"longue de la longueur d'insertion"*) echo 1 ;; *) echo 0 ;; esac)"
verdict "(f) en anglais : « as long as the insert length »" "$(case "$BE" in *"as long as the insert length"*) echo 1 ;; *) echo 0 ;; esac)"
if [ -x "$TEMOIN" ]; then
    course "$TEMOIN" t-b "400:$INS : 1/8;$SUITE;3000:relever-tete;3300:enregistrer:$brouillon/t-b-projet"
    T="$(notes "$brouillon/t-b-projet")"
    echo "       témoin (D625) : $T ; tête $(tete t-b) — $(grep -h "^VSM_INSERTION : " "$brouillon/t-b.txt" | head -1)"
    verdict "(e) témoin (D625), sous « Insertion : 1/8 » : 60@0+120 62@120+120 64@360+120 — le défaut" \
        "$(egal "$T" "60@0+120 62@120+120 64@360+120")"
else
    echo "       témoin absent ($TEMOIN) : le témoin n'est PAS joué"
fi
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "PAS-A-PAS-LONGUEUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "PAS-A-PAS-LONGUEUR : la note dure la longueur d'insertion, la tête avance d'un pas de grille"
