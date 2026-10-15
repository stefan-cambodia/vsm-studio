#!/usr/bin/env bash
# D557.2 — ÉCLAIRCIR LES CONTRÔLEURS MIDI D'UNE PISTE (Piste ▸ Éclaircir les contrôleurs MIDI de la piste ▸ Écart N ; le
# *Thin Out Data* de Cubase), MESURÉ PAR LE .mid JOUÉ ET PAR LE MENU.
#
# LES RÈGLES GARDÉES. Une piste : deux notes, le programme 5, une molette (CC 1) qui monte de 0 à 20 un par un puis
# répète son 20, un volume (CC 7) trois fois à 100, un pli qui monte et revient au centre (0, 1000, 1100, 1200, 60, 0),
# une pression (10, 11, 12) — et des COMMANDES répétées : la banque (CC 0) et un RPN complet (101, 100, 6), deux fois.
#   (1) le TÉMOIN : le .mid joué rend tout cela (33 CC, 6 plis, 3 pressions, le programme 5) ;
#   (2) le MENU avant le geste : chaque écart dit combien partiraient — 3, 15, 19, 22 ; ces comptes sont REFAITS ici,
#       en Python, par la règle écrite dans `EventList.h`, et non recopiés du journal ;
#   (3) le geste « Écart 2 » : « 19 retiré(s) sur 34 continu(s), 8 commande(s) laissée(s) » dit ; au .mid, les notes et
#       le programme intacts, les commandes toutes là (8), la molette à 8, le volume à 1, le pli à 4, la pression à 2 ;
#       la DERNIÈRE valeur de chaque flux identique au témoin ; l'ÉCART REJOUÉ (la valeur tenue, flux par flux, sur
#       l'union des ticks des deux fichiers ; le pli divisé par 128) au plus 2, et non nul — mesuré ICI sur les deux
#       .mid, indépendamment du chiffre que l'application écrit ;
#   (4) le menu APRÈS : « Écart 2 (0 retiré(s)) [grisée] » — ce qui reste est déjà éclairci ;
#   (5) un pas : Ctrl+Z rend le témoin, après un geste qui a eu lieu dans la même course ;
#   (6) une piste SANS contrôleur : le sous-menu est grisé.
#
#   tools/eclaircir-controleurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-eclaircir.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def bend(tick, v):
    r = v + 8192
    return (tick, 2, bytes([0xE0, r & 0x7F, r >> 7]))
def projet(nom, avec_controleurs):
    ev = [(0, 0, bytes([0xC0, 5])), (0, 3, bytes([0x90, 60, 100])), (480, 0, bytes([0x80, 60, 0])),
          (960, 3, bytes([0x90, 64, 100])), (1440, 0, bytes([0x80, 64, 0]))]
    if avec_controleurs:
        for t in (0, 10):   # les commandes, deux fois : la banque, puis un RPN complet (plage du pli : 2 demi-tons)
            ev += [(t, 1, bytes([0xB0, 0, 0])), (t, 1, bytes([0xB0, 101, 0])), (t, 1, bytes([0xB0, 100, 0])),
                   (t, 1, bytes([0xB0, 6, 2]))]
        ev += [(v * 10, 2, bytes([0xB0, 1, v])) for v in range(21)] + [(210, 2, bytes([0xB0, 1, 20]))]
        ev += [(t, 2, bytes([0xB0, 7, 100])) for t in (0, 480, 960)]
        ev += [bend(t, v) for t, v in ((0, 0), (10, 1000), (20, 1100), (30, 1200), (40, 60), (50, 0))]
        ev += [(t, 2, bytes([0xD0, v])) for t, v in ((0, 10), (10, 11), (20, 12))]
    ev.sort(key=lambda e: (e[0], e[1]))
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
    t = 0
    for tick, _, m in ev:
        corps += vlq(tick - t) + m; t = tick
    corps += b"\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/{nom}/project.json", "w"), indent=1)
projet("base", True)
projet("nue", False)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# Les flux d'un .mid, en JSON : {flux: [[tick, valeur], ...]}, plus les notes, programmes et commandes.
flux() {
    "$PY" - "$1" <<'PY2'
import json, sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
COMMANDES = {0, 6, 96, 97, 98, 99, 100, 101} | set(range(32, 64)) | set(range(120, 128))
f, notes, prog, cmd = {}, [], [], 0
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            notes.append(f"{ouvertes[x.note].pop(0)}-{t}:{x.note}")
        elif x.type == "control_change":
            if x.control in COMMANDES: cmd += 1
            else: f.setdefault(f"cc{x.channel}.{x.control}", []).append([t, x.value])
        elif x.type == "pitchwheel": f.setdefault(f"pli{x.channel}", []).append([t, x.pitch / 128.0])
        elif x.type == "aftertouch": f.setdefault(f"pression{x.channel}", []).append([t, x.value])
        elif x.type == "polytouch": f.setdefault(f"poly{x.channel}.{x.note}", []).append([t, x.value])
        elif x.type == "program_change": prog.append(x.program)
print(json.dumps({"flux": f, "notes": sorted(notes), "programmes": prog, "commandes": cmd}))
PY2
}
# Ce que la règle de `EventList.h` retirerait, refait ici : par flux, un événement part quand il reste à l'écart ou
# moins de la dernière valeur GARDÉE ; le premier reste ; le dernier reste s'il change la valeur. Le pli compte en
# unités de 7 bits (÷128), ce qui revient à l'écart ×128 sur ses 14 bits.
attendus() {
    "$PY" - "$1" <<'PY2'
import json, sys
d = json.loads(open(sys.argv[1]).read())
out = []
for ecart in (0, 1, 2, 4):
    n = 0
    for nom, ev in d["flux"].items():
        gardee = ev[0][1]
        for j, (t, v) in enumerate(ev[1:], 1):
            if abs(v - gardee) > ecart or (j == len(ev) - 1 and v != gardee):
                gardee = v
            else:
                n += 1
    out.append(str(n))
print(" ".join(out))
PY2
}
# Comparaison du témoin et d'après : « comptes | dernières valeurs égales | écart rejoué ».
comparer() {
    "$PY" - "$1" "$2" <<'PY2'
import json, sys
a = json.loads(open(sys.argv[1]).read()); b = json.loads(open(sys.argv[2]).read())
def tenue(ev, t):
    v = 0.0
    for tt, vv in ev:
        if tt <= t: v = vv
        else: break
    return v
pire = 0.0
for nom in set(a["flux"]) | set(b["flux"]):
    fa, fb = a["flux"].get(nom, []), b["flux"].get(nom, [])
    for t in sorted({e[0] for e in fa} | {e[0] for e in fb}):
        pire = max(pire, abs(tenue(fa, t) - tenue(fb, t)))
dernieres = all(a["flux"][k][-1][1] == b["flux"].get(k, [[0, None]])[-1][1] for k in a["flux"])
comptes = " ".join(f"{k}:{len(v)}" for k, v in sorted(b["flux"].items()))
print(f"{comptes} | dernières {'égales' if dernieres else 'DIFFÉRENTES'} | écart {pire:.3f}")
PY2
}
resume() {   # notes, programmes, commandes, et le nombre d'événements par famille
    "$PY" -c "
import json, sys
d = json.loads(open(sys.argv[1]).read())
fam = {}
for k, v in d['flux'].items():
    f = 'CC' if k.startswith('cc') else k.rstrip('0123456789.')
    fam[f] = fam.get(f, 0) + len(v)
print(' '.join(d['notes']), '| programme', ','.join(map(str, d['programmes'])), '| commandes', d['commandes'],
      '|', ' '.join(f'{k} {v}' for k, v in sorted(fam.items())))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
SOUS="Piste > Éclaircir les contrôleurs MIDI de la piste"
GESTE="menu:$SOUS > Écart 2 (19 retiré(s))"
VUE="sans-rapport,arrangement,piste:0"

echo "=== D557.2 : éclaircir les contrôleurs MIDI d'une piste ==="
course eclaircir "$brouillon/base" VSM_DELAI=2800 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="900:lister-menus;1200:$GESTE;1500:lister-menus;1800:exporter-midi:$brouillon/apres.mid"
course temoin "$brouillon/base" VSM_DELAI=2000 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:exporter-midi:$brouillon/temoin.mid"
course annuler "$brouillon/base" VSM_DELAI=3000 VSM_VUE="$VUE" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:exporter-midi:$brouillon/annule.mid"
course nue "$brouillon/nue" VSM_DELAI=1800 VSM_VUE="$VUE" VSM_MENU_LISTE=1

for x in temoin apres annule; do flux "$brouillon/$x.mid" > "$brouillon/$x.json"; done
pret=1
for x in temoin apres annule; do grep -q '^{' "$brouillon/$x.json" || { echo "       $x.mid ABSENT ou illisible"; pret=0; }; done
if [ "$pret" = 1 ]; then
    t="$(resume "$brouillon/temoin.json")"; a="$(resume "$brouillon/apres.json")"; z="$(resume "$brouillon/annule.json")"
    att="$(attendus "$brouillon/temoin.json")"; c="$(comparer "$brouillon/temoin.json" "$brouillon/apres.json")"
else
    t=ABSENT; a=ABSENT; z=ABSENT; att=""; c=""
fi
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : $SOUS > Écart 2" "$brouillon/eclaircir.txt" | sed 's/^VSM_MENU_LISTE : //')
avant="$(grep -h "^VSM_MENU_LISTE : $SOUS > Écart" "$brouillon/eclaircir.txt" | head -4 | sed -n 's/.*(\([0-9]*\) retiré(s)).*/\1/p' | tr '\n' ' ' | sed 's/ $//')"
nue="$(grep -h "^VSM_MENU_LISTE : $SOUS" "$brouillon/nue.txt" | head -1 | sed 's/^VSM_MENU_LISTE : //')"
grep -h "^VSM_CONTROLEURS_ECLAIRCIS" "$brouillon/eclaircir.txt" | sed 's/^/       /'
echo "       témoin [$t]"
echo "       après  [$a]"
echo "       Ctrl+Z [$z]"
echo "       flux après : $c"
echo "       menu avant : [$avant] ; attendu par la règle refaite ici : [$att]"
for x in "${m[@]}"; do echo "       menu : $x"; done
echo "       piste nue : [$nue]"

verdict "(1) le témoin : 33 CC dont 8 commandes, 6 plis, 3 pressions, le programme 5" \
    "$([ "$t" = "0-480:60 960-1440:64 | programme 5 | commandes 8 | CC 25 pli 6 pression 3" ] && echo 1 || echo 0)"
verdict "(2) le menu dit 3, 15, 19, 22 — ce que la règle refaite ici retire" \
    "$([ "$avant" = "3 15 19 22" ] && [ "$att" = "3 15 19 22" ] && echo 1 || echo 0)"
verdict "(3) écart 2 : 19 retirés, dit ; notes, programme et commandes intacts ; dernières valeurs égales ; écart rejoué ≤ 2 et non nul" \
    "$(grep -q "^VSM_CONTROLEURS_ECLAIRCIS : 19 retiré(s) sur 34 continu(s), 8 commande(s) laissée(s), écart 2" "$brouillon/eclaircir.txt" \
       && [ "$a" = "0-480:60 960-1440:64 | programme 5 | commandes 8 | CC 9 pli 4 pression 2" ] \
       && echo "$c" | grep -q "^cc0.1:8 cc0.7:1 pli0:4 pression0:2 | dernières égales | écart 2.000$" && echo 1 || echo 0)"
verdict "(4) le menu après : l'écart 2 n'a plus rien à retirer, grisé" \
    "$([ "${#m[@]}" = 2 ] && echo "${m[0]}" | grep -q "(19 retiré(s))$" && echo "${m[1]}" | grep -q "(0 retiré(s)) \[grisée\]$" && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend le témoin" \
    "$([ "$z" = "$t" ] && [ "$t" != ABSENT ] && grep -q "^VSM_CONTROLEURS_ECLAIRCIS : 19 retiré" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(6) une piste sans contrôleur : le sous-menu grisé" "$(echo "$nue" | grep -q "de la piste \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ECLAIRCIR-CONTROLEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ECLAIRCIR-CONTROLEURS : la piste perd ses contrôleurs redondants, pas ses commandes, et l'écart tient"
