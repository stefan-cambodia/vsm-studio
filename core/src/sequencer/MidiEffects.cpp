#include "vsm/sequencer/MidiEffects.h"
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
    if (type == "arpeggio")
        // `Division` en fractions de noire : 1 = la noire, 4 = la
        // double-croche. `Mode` : 0 montant, 1 descendant, 2 aller-retour.
        return {{"Division", 1.0f, 16.0f, 4.0f}, {"Mode", 0.0f, 2.0f, 0.0f}};
    return {};
}

std::vector<std::string> midiEffectTypes() { return {"transpose", "velocity", "arpeggio"}; }

std::string midiEffectDisplayName(const std::string& type) {
    if (type == "transpose") return "Transposition";
    if (type == "velocity") return "Vélocité";
    if (type == "arpeggio") return "Arpégiateur";
    // LE TYPE LUI-MÊME plutôt que « (inconnu) » : ce qu'on a chargé se lit,
    // même quand on ne sait pas le jouer.
    return type;
}

} // namespace vsm::sequencer
