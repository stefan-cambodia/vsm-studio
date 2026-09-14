#!/usr/bin/env bash
# La garde de D305-D312 : CE QU'UN .MID DEVIENT À L'OUVERTURE.
#
# RÈGLES GARDÉES (14/09/2026) :
#   D305  un fichier de format 0 (seize canaux dans une piste) s'ouvre en une
#         piste par canal, nommée « <nom> · canal N » ;
#   D306  chaque piste ouverte porte un clip sur son matériau ;
#   D307  chaque piste reçoit une machine d'après son programme General MIDI
#         (le canal 10 un kit ; sans programme, le piano) -- ici SANS banque
#         installée (VSM_PROFILS sur un dossier vide), pour que la garde ne
#         dépende pas du poste ;
#   D310  une piste de conduite (tempo, signature) n'est pas créée ;
#   D311  un fichier sans rien à jouer le DIT (boîte), et le journal ne parle
#         de découpage que s'il a eu lieu ;
#   D312  l'export arrangé écrit les programmes du fichier tels quels.
#
# COMMENT. Trois fichiers sont ENGENDRÉS ici (aucun fixture binaire commis),
# ouverts par l'application sous un HOME de brouillon (D77), et le journal
# est jugé -- pas la photo. Rend 0 si tout tient, 1 sinon, 2 si le binaire
# manque.
#
#   tools/ouvrir-midi.sh
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
brouillon="${TMPDIR:-/tmp}/vsm-ouvrir-midi-$$"
mkdir -p "$brouillon/home" "$brouillon/profils-vide"
python3 - "$brouillon" <<'PY'
import struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7f]; n >>= 7
    while n: b.append((n & 0x7f) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(evs):  # evs : liste de (delta, octets)
    corps = b"".join(vlq(dt) + oct for dt, oct in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
def fichier(chemin, fmt, pistes):
    open(chemin, "wb").write(b"MThd" + struct.pack(">IHHH", 6, fmt, len(pistes), 480) + b"".join(pistes))
nom = lambda s: b"\xff\x03" + vlq(len(s)) + s.encode()
tempo = b"\xff\x51\x03\x07\xa1\x20"           # 120 BPM
sig = b"\xff\x58\x04\x04\x02\x18\x08"
note = lambda ch, n: [(0, bytes([0x90 | ch, n, 100])), (480, bytes([0x80 | ch, n, 0]))]
# A. format 0 : trois canaux dans une piste, avec leurs programmes
evs = [(0, nom("Mixdown")), (0, tempo), (0, sig), (0, bytes([0xc0, 38])), (0, bytes([0xc3, 89]))]
evs += note(0, 40) + note(9, 36) + note(3, 60) + note(0, 43)
fichier(d + "/format0.mid", 0, [piste(evs)])
# B. format 1 : un conducteur, puis « Lead » sans programme
fichier(d + "/conducteur.mid", 1, [piste([(0, tempo), (0, sig)]), piste([(0, nom("Lead"))] + note(0, 64) + note(0, 67))])
# C. rien à jouer : le conducteur seul
fichier(d + "/vide.mid", 1, [piste([(0, tempo), (0, sig)])])
PY
rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
lancer() {  # $1 fichier  $2... env supplémentaire ; le journal sur la sortie standard
    env HOME="$brouillon/home" VSM_PROFILS="$brouillon/profils-vide" VSM_DELAI=1500 "${@:2}" \
        VSM_VUE="ouvrir-midi:$brouillon/$1,sans-rapport,arrangement" VSM_CLIPS=1 VSM_TEXTES_LISTE=1 \
        timeout 25 "$BIN" 2>&1
}
echo "=== D305-D312 : ce qu'un .mid devient à l'ouverture (sans banque) ==="
j=$(lancer format0.mid)
verdict "format 0 → 3 pistes, découpées par canal"        "$(grep -c '^Ouvrir MIDI : 3 piste(s).*découpée(s) par canal' <<<"$j")"
verdict "nommées « Mixdown · canal 1 / 4 / 10 »"           "$(grep -cE 'VSM_PISTES : .*Mixdown · canal 1.*Mixdown · canal 4.*Mixdown · canal 10' <<<"$j")"
verdict "canal 1 (38) → vsm.tb303, canal 4 (89) → vsm.jupiter8, canal 10 → vsm.drums" \
        "$(grep -c 'programme GM 38 (Synth Bass 1) → vsm.tb303' <<<"$j")$(grep -c 'programme GM 89 (Pad 2 (warm)) → vsm.jupiter8' <<<"$j")$(grep -c 'canal 10, kit 0 (Standard Kit) → vsm.drums' <<<"$j")" 
[ "$(grep -c 'programme GM 38 (Synth Bass 1) → vsm.tb303' <<<"$j")" -eq 1 ] && [ "$(grep -c 'vsm.jupiter8' <<<"$j")" -ge 1 ] && [ "$(grep -c '→ vsm.drums' <<<"$j")" -eq 1 ] || rates=$((rates + 1))
verdict "3 clips, 0 « (Aucun) »"                            "$([ "$(grep -c 'VSM_CLIPS : 3 clip' <<<"$j")" -eq 1 ] && [ "$(grep -c 'VSM_TEXTE : liste : (Aucun)' <<<"$j")" -eq 0 ] && echo 1 || echo 0)"
j=$(lancer conducteur.mid)
verdict "format 1 : le conducteur n'est pas créé, « Lead » → piano (défaut de la norme)" \
        "$([ "$(grep -c '^Ouvrir MIDI : 1 piste(s).*1 piste(s) de conduite' <<<"$j")" -eq 1 ] && [ "$(grep -c '« Lead » : programme GM 0 (Acoustic Grand Piano) → vsm.piano \[aucun programme' <<<"$j")" -eq 1 ] && [ "$(grep -c 'découpée' <<<"$j")" -eq 0 ] && echo 1 || echo 0)"
j=$(lancer vide.mid)
verdict "rien à jouer : 0 piste, la boîte le dit, pas de « découpée »" \
        "$([ "$(grep -c '^Ouvrir MIDI : 0 piste(s)' <<<"$j")" -eq 1 ] && [ "$(grep -c 'VSM_BOITE : Ouvrir MIDI : vide.mid : aucune piste jouable' <<<"$j")" -eq 1 ] && [ "$(grep -c 'découpée' <<<"$j")" -eq 0 ] && echo 1 || echo 0)"
j=$(lancer format0.mid VSM_EXPORT_MIDI="$brouillon/export.mid")
progs=$(python3 - "$brouillon/export.mid" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read(); ntr = struct.unpack(">H", d[10:12])[0]; i = 14; out = []
def vlq(i):
    n = 0
    while True:
        b = d[i]; i += 1; n = (n << 7) | (b & 0x7f)
        if not b & 0x80: return n, i
for t in range(ntr):
    ln = struct.unpack(">I", d[i+4:i+8])[0]; j = i + 8; end = j + ln; st = 0
    while j < end:
        dt, j = vlq(j); b = d[j]
        if b == 0xff: l, j2 = vlq(j + 2); j = j2 + l; continue
        if b in (0xf0, 0xf7): l, j2 = vlq(j + 1); j = j2 + l; continue
        if b & 0x80: st = b; j += 1
        hi = st & 0xf0
        if hi == 0xc0: out.append(f"{st & 0x0f}:{d[j]}"); j += 1
        elif hi == 0xd0: j += 1
        else: j += 2
    i = end
print(" ".join(out))
PY
)
verdict "export : les programmes du fichier, tels quels (0:38 3:89), et le kit 0 dérivé de vsm.drums au canal 10 (D312)" \
        "$([ "$progs" = "0:38 3:89 9:0" ] || [ "$progs" = "0:38 3:89" ] && echo 1 || echo 0)"
echo "  (programmes exportés : $progs)"
rm -rf "$brouillon"
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
