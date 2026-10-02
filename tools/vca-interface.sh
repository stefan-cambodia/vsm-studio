#!/usr/bin/env bash
# D532.1 bis — L'INTERFACE DU VCA : CRÉER, AFFECTER, ANNULER, ET N'EN MONTRER QUE CE QUI AGIT.
#
# LA RÈGLE GARDÉE.
#   (1) trois pistes A, B, C ; choisir A et B, « Nouveau VCA pour les pistes choisies (2) »,
#       enregistrer : le project.json porte une 4e piste « kind: vca », et « vca: 3 » sur A
#       et B, pas sur C ;
#   (2) Ctrl+Z, enregistrer : plus de VCA, ni aucune clé « vca » ;
#   (3) un projet où A et B sont au VCA : « Aucun VCA » sur A, enregistrer — A perd sa clé,
#       B la garde ;
#   (4) la console : la tranche du VCA masque trim, panoramique, délai, transposition, phase
#       et vumètre, celle de A n'en masque aucune ; la liste : la ligne du VCA dit
#       « VCA — 2 membre(s) » et n'a ni canal ni sortie (trois « Ch » et trois « -> Master »
#       pour quatre pistes) ;
#   (5) le VCA muet : les M de A et de B disent « Rendu muet par son VCA ».
# Chaque fichier se lit AVANT de juger (une comparaison dont un côté manque n'est pas un
# verdict), et les avertissements du journal sont relayés.
#
#   tools/vca-interface.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-vca-interface.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/trois/midi" "$brouillon/avec-vca/midi"
python3 - "$brouillon" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(nom, notes, tempo=False):
    evs = ([(0, b"\xff\x51\x03\x07\xa1\x20")] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
    t = 0
    for debut, hauteur, duree in notes:
        evs += [(debut - t, bytes([0x90, hauteur, 100])), (duree, bytes([0x80, hauteur, 0]))]
        t = debut + duree
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def entree(nom, canal, **extra):
    e = {"channel": canal, "color": "#FF6B9BFF", "effects": [], "instrument": {"preferredPlugin": "vsm.minimoog"},
         "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}, "name": nom}
    e.update(extra)
    return e
def projet(dossier, pistes_midi, entrees):
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes_midi), 480)
    for i, (nom, notes) in enumerate(pistes_midi):
        data += piste(nom, notes, tempo=(i == 0))
    open(f"{dossier}/midi/arrangement.mid", "wb").write(data)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": entrees}, open(f"{dossier}/project.json", "w"), indent=1)
abc = [("A", [(0, 48, 1920)]), ("B", [(0, 60, 1920)]), ("C", [(0, 67, 1920)])]
projet(d + "/trois", abc, [entree("A", 0), entree("B", 1), entree("C", 2)])
vca = {"channel": 3, "color": "#FF4BB3A6", "effects": [], "kind": "vca",
       "mix": {"muted": True, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0}, "name": "Cordes"}
projet(d + "/avec-vca", abc + [("Cordes", [])], [entree("A", 0, vca=3), entree("B", 1, vca=3), entree("C", 2), vca])
PY

course() {   # $1 = nom ; $2 = projet ; $3 = VSM_GESTE_APRES ; le reste : variables en plus
    local nom="$1" projet="$2" gestes="$3" maison
    shift 3
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/$projet" VSM_TAILLE="1600x1000" VSM_DELAI=4500 \
        VSM_VUE="sans-rapport" VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|GESTE|MENU|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# La forme du fichier, lue par Python : « n kinds vca-par-piste », ou « ABSENT ».
lire() {
    python3 - "$1/project.json" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("ABSENT"); sys.exit(0)
pistes = d.get("tracks", [])
print(len(pistes), ",".join(p.get("kind", "midi") for p in pistes),
      ",".join(str(p.get("vca", "-")) for p in pistes),
      # LA CLÉ CHERCHÉE DANS LA STRUCTURE, pas dans le texte : un projet titré « vca »
      # la portait dans son titre (première course, 02/10 08 h 47 — le même piège que
      # le test des versions de D532.2, titré « versions »).
      "cle-vca" if any("vca" in p or p.get("kind") == "vca" for p in pistes) else "sans-cle")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D532.1 bis : l'interface du VCA ==="
course nouveau trois "1200:choisir:0,1;1600:menu:Nouveau VCA pour les pistes choisies (2);2200:enregistrer:$brouillon/nouveau-ecrit;2600:touche:ctrl + Z;3000:enregistrer:$brouillon/annule-ecrit"
n1="$(lire "$brouillon/nouveau-ecrit")"; n2="$(lire "$brouillon/annule-ecrit")"
grep "VSM_VCA" "$brouillon/nouveau.txt" | sed 's/^/        /'
echo "       après le geste : $n1"
echo "       après Ctrl+Z   : $n2"
verdict "(1) le VCA naît en 4e piste et commande A et B, pas C" "$([ "$n1" = "4 midi,midi,midi,vca 3,3,-,- cle-vca" ] && echo 1 || echo 0)"
verdict "(2) Ctrl+Z : plus de VCA, ni de clé « vca »" "$([ "$n2" = "3 midi,midi,midi -,-,- sans-cle" ] && echo 1 || echo 0)"

course retrait avec-vca "1200:choisir:0;1600:menu:Aucun VCA;2000:enregistrer:$brouillon/retrait-ecrit"
n3="$(lire "$brouillon/retrait-ecrit")"
echo "       « Aucun VCA » sur A : $n3"
verdict "(3) A perd son VCA, B le garde" "$([ "$n3" = "4 midi,midi,midi,vca -,3,-,- cle-vca" ] && echo 1 || echo 0)"

course console avec-vca "" VSM_MIXEUR=1 VSM_TEXTES_LISTE=1
j="$brouillon/console.txt"
m_vca="$(grep '^VSM_MIXEUR : piste 3 ' "$j" | grep -o 'masquées [^ ]*' | cut -d' ' -f2)"
m_a="$(grep '^VSM_MIXEUR : piste 0 ' "$j" | grep -o 'masquées [^ ]*' | cut -d' ' -f2)"
echo "       console : tranche du VCA, masquées « ${m_vca:-?} » ; tranche de A, « ${m_a:-?} »"
verdict "(4) la tranche du VCA masque trim, panoramique, délai, transposition, phase et vumètre" \
    "$([ "${m_vca:-}" = "trim,pan,délai,transposition,phase,vumètre" ] && echo 1 || echo 0)"
verdict "(4 bis) la tranche de A n'en masque aucune" "$([ "${m_a:-}" = "aucune" ] && echo 1 || echo 0)"
membres="$(grep -c 'VSM_TEXTE : libellé : VCA — 2 membre(s)' "$j")"
canaux="$(grep -cE '^VSM_TEXTE : libellé : Ch [0-9]+$' "$j")"
sorties="$(grep -cE '^VSM_TEXTE : liste : -> Master$' "$j")"
echo "       liste : « VCA — 2 membre(s) » ×$membres, « Ch n » ×$canaux, « -> Master » ×$sorties"
verdict "(4 ter) la ligne du VCA dit ses membres, sans canal ni sortie" \
    "$([ "$membres" -ge 1 ] && [ "$canaux" = 3 ] && [ "$sorties" = 3 ] && echo 1 || echo 0)"
muets="$(grep -c '^VSM_TEXTE : infobulle : Rendu muet par son VCA$' "$j")"
echo "       « Rendu muet par son VCA » ×$muets (A et B, dans la liste et dans la console)"
verdict "(5) le VCA muet : ses membres disent pourquoi ils se taisent" "$([ "$muets" -ge 2 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
