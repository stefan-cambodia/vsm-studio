#include "vsm/sequencer/MidiEffects.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace vsm::sequencer {

namespace {

float parametre(const MidiEffect& effet, const std::string& nom, float defaut) {
    auto it = effet.parameters.find(nom);
    return it == effet.parameters.end() ? defaut : it->second;
}

// --- TRANSPOSITION ---------------------------------------------------------

std::vector<Note> transposer(const std::vector<Note>& notes, int demiTons,
                              MidiEffectReport* report) {
    std::vector<Note> sortie;
    sortie.reserve(notes.size());
    for (const auto& note : notes) {
        const int hauteur = static_cast<int>(note.number) + demiTons;
        // HORS 0..127, LA NOTE EST ÉCARTÉE ET COMPTÉE, jamais repliée à
        // l'octave : replier ferait sonner une note que personne n'a demandée.
        // C'est la règle de la transposition de piste (D17.5), et deux règles
        // pour la même question finiraient par se contredire.
        if (hauteur < 0 || hauteur > 127) {
            if (report) ++report->droppedOutOfRange;
            continue;
        }
        Note copie = note;
        copie.number = static_cast<uint8_t>(hauteur);
        sortie.push_back(copie);
    }
    return sortie;
}

// --- VÉLOCITÉ --------------------------------------------------------------

std::vector<Note> velocite(const std::vector<Note>& notes, float echelle, float decalage) {
    std::vector<Note> sortie = notes;
    for (auto& note : sortie) {
        const float v = static_cast<float>(note.velocity) * echelle + decalage;
        // BORNÉE À 1 ET NON À 0 : une vélocité nulle est un NoteOff déguisé
        // dans le MIDI, et une piste qu'on voulait seulement adoucir
        // s'arrêterait de sonner sans qu'aucune note ait disparu -- le genre
        // de silence qu'on met une heure à comprendre.
        note.velocity = static_cast<uint8_t>(std::clamp(std::lround(v), 1L, 127L));
    }
    return sortie;
}

// --- DURÉE (D573.3) --------------------------------------------------------

/// LA *LENGTH COMPRESSION* DES MODIFICATEURS MIDI DE CUBASE : chaque durée multipliée par `rapport` à partir du départ,
/// arrondie au tick, un tick au moins. UNE NOTE ALLONGÉE S'ARRÊTE AU PLUS TARD AU DÉPART DE LA SUIVANTE DE MÊME HAUTEUR
/// SUR LE MÊME CANAL : sa fin couperait sinon l'attaque d'après — un rapport de 4 sur des notes répétées rend un legato,
/// pas des notes qui se mangent. Une note qui chevauchait DÉJÀ la suivante garde sa durée d'origine : l'effet ne
/// raccourcit jamais en allongeant.
std::vector<Note> durees(const std::vector<Note>& notes, float rapport) {
    std::vector<Note> sortie = notes;
    std::map<std::pair<uint8_t, uint8_t>, std::vector<Tick>> departs;
    for (const auto& n : notes) departs[{n.channel, n.number}].push_back(n.startTick);
    for (auto& [cle, liste] : departs) std::sort(liste.begin(), liste.end());
    for (auto& note : sortie) {
        const Tick duree = note.endTick - note.startTick;
        Tick neuve = std::max<Tick>(1, static_cast<Tick>(std::llround(static_cast<double>(duree) * rapport)));
        if (neuve > duree) {
            const auto& liste = departs[{note.channel, note.number}];
            const auto suivante = std::upper_bound(liste.begin(), liste.end(), note.startTick);
            if (suivante != liste.end()) neuve = std::min(neuve, std::max(duree, *suivante - note.startTick));
        }
        note.endTick = note.startTick + neuve;
    }
    return sortie;
}

// --- PLAGE (D574.3) --------------------------------------------------------

/// LE *RANGE* DES MODIFICATEURS MIDI DE CUBASE. `mode` : 0 limiter les hauteurs (D'OCTAVES jusqu'à entrer dans la plage —
/// le *Note Limit*), 1 filtrer les hauteurs, 2 limiter les vélocités (bornées, 1 au moins : une vélocité nulle est un
/// NoteOff), 3 filtrer les vélocités. Ce qui est écarté est compté : une hauteur qu'aucune octave ne fait entrer dans une
/// plage plus étroite qu'elle n'est JAMAIS posée ailleurs (la règle de la transposition, D17.5).
std::vector<Note> plage(const std::vector<Note>& notes, int mode, int mini, int maxi, MidiEffectReport* report) {
    if (mini > maxi) {
        if (report) ++report->invertedRanges;
        return notes;
    }
    std::vector<Note> sortie;
    sortie.reserve(notes.size());
    for (const auto& note : notes) {
        Note copie = note;
        bool garde = true;
        if (mode == 0) {
            int h = note.number;
            while (h < mini) h += 12;
            while (h > maxi) h -= 12;
            garde = h >= mini && h <= maxi && h >= 0 && h <= 127;
            copie.number = static_cast<uint8_t>(std::clamp(h, 0, 127));
        } else if (mode == 1) {
            garde = note.number >= mini && note.number <= maxi;
        } else if (mode == 2) {
            copie.velocity = static_cast<uint8_t>(std::clamp(static_cast<int>(note.velocity), std::max(1, mini), std::max(1, maxi)));
        } else {
            garde = note.velocity >= mini && note.velocity <= maxi;
        }
        if (!garde) {
            if (report) ++report->outsideRange;
            continue;
        }
        sortie.push_back(copie);
    }
    return sortie;
}

// --- ALÉATOIRE (D575.3) -----------------------------------------------------

/// LE *RANDOM* DES MODIFICATEURS MIDI DE CUBASE, mais REPRODUCTIBLE : l'écart d'une note se tire de la graine et de CE
/// QU'ELLE EST — son départ, sa hauteur, son canal —, jamais de son identifiant, que le chargement redonne : un tirage qui
/// en dépendrait changerait le son d'un morceau rouvert. La même lecture, le même export, le même son (D21). `cible` :
/// 0 position (la durée gardée, bornée à 0), 1 hauteur (hors du clavier : écartée et comptée, D17.5), 2 vélocité (1..127),
/// 3 durée (un tick au moins).
std::vector<Note> aleatoire(const std::vector<Note>& notes, int cible, float mini, float maxi, uint64_t graine,
                            MidiEffectReport* report) {
    if (mini > maxi) std::swap(mini, maxi);
    const float borne = cible == 1 ? 48.0f : cible == 2 ? 127.0f : 240.0f;
    mini = std::clamp(mini, -borne, borne);
    maxi = std::clamp(maxi, -borne, borne);
    std::vector<Note> sortie;
    sortie.reserve(notes.size());
    for (const auto& note : notes) {
        const uint64_t cle = vsm::util::deriveSeed(static_cast<uint64_t>(note.startTick),
                                                   static_cast<uint64_t>(note.number) * 16u + note.channel);
        vsm::util::DeterministicRng tirage(vsm::util::deriveSeed(graine, cle));
        const auto ecart = static_cast<Tick>(std::llround(mini + (maxi - mini) * static_cast<double>(tirage.nextUnipolar())));
        Note copie = note;
        const Tick duree = note.endTick - note.startTick;
        if (cible == 0) {
            copie.startTick = std::max<Tick>(0, note.startTick + ecart);
            copie.endTick = copie.startTick + duree;
        } else if (cible == 1) {
            const Tick h = static_cast<Tick>(note.number) + ecart;
            if (h < 0 || h > 127) {
                if (report) ++report->droppedOutOfRange;
                continue;
            }
            copie.number = static_cast<uint8_t>(h);
        } else if (cible == 2) {
            copie.velocity = static_cast<uint8_t>(std::clamp<Tick>(static_cast<Tick>(note.velocity) + ecart, 1, 127));
        } else {
            copie.endTick = note.startTick + std::max<Tick>(1, duree + ecart);
        }
        sortie.push_back(copie);
    }
    return sortie;
}

// --- ARPÉGIATEUR -----------------------------------------------------------

/// Les notes qui SONNENT ENSEMBLE forment un accord. « Ensemble » veut dire
/// « qui commencent au même tick » : c'est ce qu'un accord joué au clavier
/// donne après quantification, et c'est ce qu'un pianiste appelle un accord.
/// Deux notes dont l'une commence au milieu de l'autre ne forment pas un
/// accord mais une tenue, et les arpéger déplacerait la seconde.
std::vector<Note> arpeger(const std::vector<Note>& notes, Tick pas, int mode) {
    if (pas <= 0) return notes;
    std::map<Tick, std::vector<Note>> parDebut;
    for (const auto& note : notes) parDebut[note.startTick].push_back(note);

    std::vector<Note> sortie;
    sortie.reserve(notes.size());
    uint64_t prochainId = 0;
    for (const auto& n : notes) prochainId = std::max(prochainId, n.id);

    for (auto& [debut, accord] : parDebut) {
        if (accord.size() < 2) { sortie.push_back(accord.front()); continue; }
        std::sort(accord.begin(), accord.end(),
                   [](const Note& a, const Note& b) { return a.number < b.number; });

        // L'ORDRE DE PARCOURS. Montant, descendant, ou aller-retour SANS
        // REDOUBLER LES EXTRÊMES : un aller-retour qui rejoue la note du haut
        // deux fois de suite marque un temps, ce qui n'est pas ce qu'on
        // entend d'un arpégiateur.
        std::vector<Note> ordre = accord;
        if (mode == 1) std::reverse(ordre.begin(), ordre.end());
        else if (mode == 2 && accord.size() > 2)
            for (size_t i = accord.size() - 1; i-- > 1;) ordre.push_back(accord[i]);

        // LA DURÉE DE L'ACCORD est celle de sa note la plus longue : l'arpège
        // remplit exactement la place que l'accord occupait, ni plus (il
        // mordrait sur ce qui suit) ni moins (il laisserait un trou).
        Tick fin = debut;
        for (const auto& n : accord) fin = std::max(fin, n.endTick);
        if (fin <= debut) { for (const auto& n : accord) sortie.push_back(n); continue; }

        size_t rang = 0;
        for (Tick t = debut; t < fin; t += pas, ++rang) {
            Note note = ordre[rang % ordre.size()];
            note.startTick = t;
            note.endTick = std::min(t + pas, fin);
            // UN IDENTIFIANT NEUF PAR ATTAQUE. Les identifiants servent à la
            // sélection, à l'automation liée et au tirage déterministe de
            // l'humanisation : seize attaques qui porteraient trois
            // identifiants se confondraient trois par trois.
            note.id = ++prochainId;
            sortie.push_back(note);
        }
    }
    std::sort(sortie.begin(), sortie.end(), [](const Note& a, const Note& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.number < b.number;
    });
    return sortie;
}

} // namespace

std::vector<Note> applyMidiEffects(const std::vector<MidiEffect>& effects,
                                    const std::vector<Note>& notes, uint16_t ppq,
                                    MidiEffectReport* report) {
    if (effects.empty()) return notes;
    std::vector<Note> courant = notes;
    for (const auto& effet : effects) {
        if (!effet.enabled) continue;
        if (effet.type == "transpose") {
            courant = transposer(courant,
                                  static_cast<int>(std::lround(parametre(effet, "Semitones", 0.0f))),
                                  report);
        } else if (effet.type == "velocity") {
            courant = velocite(courant, parametre(effet, "Scale", 1.0f),
                                parametre(effet, "Offset", 0.0f));
        } else if (effet.type == "random") {   // D575.3
            courant = aleatoire(courant, static_cast<int>(std::lround(parametre(effet, "Cible", 0.0f))),
                                parametre(effet, "Min", 0.0f), parametre(effet, "Max", 0.0f),
                                static_cast<uint64_t>(std::max(0L, std::lround(parametre(effet, "Graine", 1.0f)))), report);
        } else if (effet.type == "range") {   // D574.3
            courant = plage(courant, static_cast<int>(std::lround(parametre(effet, "Mode", 0.0f))),
                            static_cast<int>(std::lround(std::clamp(parametre(effet, "Min", 0.0f), 0.0f, 127.0f))),
                            static_cast<int>(std::lround(std::clamp(parametre(effet, "Max", 127.0f), 0.0f, 127.0f))), report);
        } else if (effet.type == "length") {   // D573.3
            courant = durees(courant, std::clamp(parametre(effet, "Ratio", 1.0f), 0.25f, 4.0f));
        } else if (effet.type == "arpeggio") {
            // LE PAS EST EN FRACTIONS DE NOIRE : « 4 » veut dire la
            // double-croche. En ticks, il dépendrait de `ppq` et un projet
            // relu à une autre résolution n'arpégerait plus pareil.
            const float division = std::max(1.0f, parametre(effet, "Division", 4.0f));
            const Tick pas = std::max<Tick>(1, static_cast<Tick>(std::lround(
                static_cast<double>(ppq) / static_cast<double>(division))));
            courant = arpeger(courant, pas,
                               static_cast<int>(std::lround(parametre(effet, "Mode", 0.0f))));
        } else {
            // PANNE MUETTE INTERDITE : un effet dont on ne connaît pas
            // l'identité ne fait rien, et on le compte.
            if (report) ++report->unknownEffects;
        }
    }
    return courant;
}

std::vector<MidiEffectParam> midiEffectParameters(const std::string& type) {
    if (type == "transpose") return {{"Semitones", -48.0f, 48.0f, 0.0f}};
    if (type == "velocity") return {{"Scale", 0.0f, 2.0f, 1.0f}, {"Offset", -64.0f, 64.0f, 0.0f}};
    if (type == "length") return {{"Ratio", 0.25f, 4.0f, 1.0f}};   // D573.3
    if (type == "random")   // D575.3
        return {{"Cible", 0.0f, 3.0f, 0.0f, {"Position", "Hauteur", "Vélocité", "Durée"}},
                {"Min", -240.0f, 240.0f, 0.0f},
                {"Max", -240.0f, 240.0f, 0.0f},
                {"Graine", 0.0f, 999.0f, 1.0f}};
    if (type == "range")   // D574.3
        return {{"Mode", 0.0f, 3.0f, 0.0f,
                 {"Limiter les hauteurs", "Filtrer les hauteurs", "Limiter les vélocités", "Filtrer les vélocités"}},
                {"Min", 0.0f, 127.0f, 0.0f},
                {"Max", 0.0f, 127.0f, 127.0f}};
    if (type == "arpeggio")
        // `Division` en fractions de noire : 1 = la noire, 4 = la
        // double-croche. `Mode` : 0 montant, 1 descendant, 2 aller-retour.
        return {{"Division", 1.0f, 16.0f, 4.0f},
                {"Mode", 0.0f, 2.0f, 0.0f, {"Montant", "Descendant", "Aller-retour"}}};   // D477
    return {};
}

// D573.3 : « length » APRÈS les trois d'avant — le menu les numérote dans cet ordre, et un banc qui ajoute la vélocité
// par son rang la trouve toujours au même.
std::vector<std::string> midiEffectTypes() { return {"transpose", "velocity", "arpeggio", "length", "range", "random"}; }

std::string midiEffectDisplayName(const std::string& type) {
    if (type == "transpose") return "Transposition";
    if (type == "velocity") return "Vélocité";
    if (type == "arpeggio") return "Arpégiateur";
    if (type == "length") return "Durée";   // D573.3
    if (type == "range") return "Plage";    // D574.3
    if (type == "random") return "Aléatoire";   // D575.3
    // LE TYPE LUI-MÊME plutôt que « (inconnu) » : ce qu'on a chargé se lit,
    // même quand on ne sait pas le jouer.
    return type;
}

} // namespace vsm::sequencer
