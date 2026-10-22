#include "vsm/sequencer/NoteEdit.h"
#include <map>
#include <set>
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

namespace {

/// Applique `fn` à chaque note sélectionnée. Centralisé pour que toutes les
/// opérations partagent exactement la même règle « une sélection vide ne fait
/// rien » (voir l'en-tête).
template <typename Fn>
void forEachSelected(std::vector<Note>& notes, const NoteSelection& selection, Fn fn) {
    if (selection.empty()) return;
    for (auto& note : notes)
        if (selection.count(note.id) > 0) fn(note);
}

/// Pointeurs vers les notes sélectionnées, triés par tick de début puis par
/// hauteur : l'ordre canonique de toutes les opérations qui dépendent de la
/// position (rampe de vélocité, rétrograde, arpège). Deux notes exactement
/// simultanées et de même hauteur ne peuvent pas exister musicalement, donc
/// l'ordre est total et le résultat reproductible.
std::vector<Note*> selectedSortedByTime(std::vector<Note>& notes, const NoteSelection& selection) {
    std::vector<Note*> result;
    result.reserve(selection.size());
    for (auto& note : notes)
        if (selection.count(note.id) > 0) result.push_back(&note);
    std::sort(result.begin(), result.end(), [](const Note* a, const Note* b) {
        if (a->startTick != b->startTick) return a->startTick < b->startTick;
        return a->number < b->number;
    });
    return result;
}

uint8_t clampVelocity(int v) { return static_cast<uint8_t>(std::clamp(v, 1, 127)); }
uint8_t clampNoteNumber(int n) { return static_cast<uint8_t>(std::clamp(n, 0, 127)); }

} // namespace

// ---------------------------------------------------------------------------
// Gammes
// ---------------------------------------------------------------------------

uint16_t scaleMask(ScaleType type) {
    // Bit i = le degré i (en demi-tons depuis la fondamentale) appartient à la
    // gamme. Écrit en binaire pour que la forme de la gamme reste lisible
    // (bit 0 = fondamentale, à droite... donc on lit les intervalles à
    // l'envers : c'est le prix d'une table compacte, d'où les commentaires).
    switch (type) {
        case ScaleType::Chromatic:       return 0b111111111111;
        case ScaleType::Major:           return 0b101010110101; // 0 2 4 5 7 9 11
        case ScaleType::NaturalMinor:    return 0b010110101101; // 0 2 3 5 7 8 10
        case ScaleType::HarmonicMinor:   return 0b100110101101; // 0 2 3 5 7 8 11
        case ScaleType::MelodicMinor:    return 0b101010101101; // 0 2 3 5 7 9 11
        case ScaleType::Dorian:          return 0b011010101101; // 0 2 3 5 7 9 10
        case ScaleType::Phrygian:        return 0b010110101011; // 0 1 3 5 7 8 10
        case ScaleType::Lydian:          return 0b101011010101; // 0 2 4 6 7 9 11
        case ScaleType::Mixolydian:      return 0b011010110101; // 0 2 4 5 7 9 10
        case ScaleType::Locrian:         return 0b010101101011; // 0 1 3 5 6 8 10
        case ScaleType::PentatonicMajor: return 0b001010010101; // 0 2 4 7 9
        case ScaleType::PentatonicMinor: return 0b010010101001; // 0 3 5 7 10
        case ScaleType::Blues:           return 0b010011101001; // 0 3 5 6 7 10
        case ScaleType::WholeTone:       return 0b010101010101; // 0 2 4 6 8 10
    }
    return 0b111111111111;
}

bool isNoteInScale(uint8_t noteNumber, Scale scale) {
    const int degree = ((static_cast<int>(noteNumber) - static_cast<int>(scale.root)) % 12 + 12) % 12;
    return (scaleMask(scale.type) & (1u << degree)) != 0;
}

uint8_t snapNoteToScale(uint8_t noteNumber, Scale scale) {
    if (isNoteInScale(noteNumber, scale)) return noteNumber;
    // Recherche symétrique autour de la note : on préfère la plus proche, et
    // le grave à égalité de distance (choix arbitraire mais FIXÉ, pour que
    // deux exécutions donnent le même résultat -- une contrainte de gamme qui
    // "hésite" serait invivable à l'usage).
    for (int distance = 1; distance <= 6; ++distance) {
        const int down = static_cast<int>(noteNumber) - distance;
        if (down >= 0 && isNoteInScale(static_cast<uint8_t>(down), scale)) return static_cast<uint8_t>(down);
        const int up = static_cast<int>(noteNumber) + distance;
        if (up <= 127 && isNoteInScale(static_cast<uint8_t>(up), scale)) return static_cast<uint8_t>(up);
    }
    return noteNumber;
}

const char* scaleTypeName(ScaleType type) {
    switch (type) {
        case ScaleType::Chromatic:       return "Chromatique";
        case ScaleType::Major:           return "Majeure";
        case ScaleType::NaturalMinor:    return "Mineure naturelle";
        case ScaleType::HarmonicMinor:   return "Mineure harmonique";
        case ScaleType::MelodicMinor:    return "Mineure mélodique";
        case ScaleType::Dorian:          return "Dorien";
        case ScaleType::Phrygian:        return "Phrygien";
        case ScaleType::Lydian:          return "Lydien";
        case ScaleType::Mixolydian:      return "Mixolydien";
        case ScaleType::Locrian:         return "Locrien";
        case ScaleType::PentatonicMajor: return "Pentatonique majeure";
        case ScaleType::PentatonicMinor: return "Pentatonique mineure";
        case ScaleType::Blues:           return "Blues";
        case ScaleType::WholeTone:       return "Par tons";
    }
    return "?";
}

const char* scaleTypeId(ScaleType type) {
    switch (type) {
        case ScaleType::Chromatic:       return "chromatic";
        case ScaleType::Major:           return "major";
        case ScaleType::NaturalMinor:    return "naturalMinor";
        case ScaleType::HarmonicMinor:   return "harmonicMinor";
        case ScaleType::MelodicMinor:    return "melodicMinor";
        case ScaleType::Dorian:          return "dorian";
        case ScaleType::Phrygian:        return "phrygian";
        case ScaleType::Lydian:          return "lydian";
        case ScaleType::Mixolydian:      return "mixolydian";
        case ScaleType::Locrian:         return "locrian";
        case ScaleType::PentatonicMajor: return "pentatonicMajor";
        case ScaleType::PentatonicMinor: return "pentatonicMinor";
        case ScaleType::Blues:           return "blues";
        case ScaleType::WholeTone:       return "wholeTone";
    }
    return "chromatic";
}

bool scaleTypeFromId(const std::string& id, ScaleType& out) {
    for (const auto type : allScaleTypes())
        if (id == scaleTypeId(type)) { out = type; return true; }
    return false;
}

std::vector<ScaleType> allScaleTypes() {
    return { ScaleType::Chromatic, ScaleType::Major, ScaleType::NaturalMinor,
             ScaleType::HarmonicMinor, ScaleType::MelodicMinor, ScaleType::Dorian,
             ScaleType::Phrygian, ScaleType::Lydian, ScaleType::Mixolydian,
             ScaleType::Locrian, ScaleType::PentatonicMajor, ScaleType::PentatonicMinor,
             ScaleType::Blues, ScaleType::WholeTone };
}

std::string noteNumberToName(uint8_t noteNumber) {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = static_cast<int>(noteNumber) / 12 - 1; // convention : 60 = C4
    return std::string(names[noteNumber % 12]) + std::to_string(octave);
}

// ---------------------------------------------------------------------------
// Accords
// ---------------------------------------------------------------------------

const char* chordTypeName(ChordType type) {
    switch (type) {
        case ChordType::Major:       return "Majeur";
        case ChordType::Minor:       return "Mineur";
        case ChordType::Diminished:  return "Diminué";
        case ChordType::Augmented:   return "Augmenté";
        case ChordType::Sus2:        return "Sus2";
        case ChordType::Sus4:        return "Sus4";
        case ChordType::Power:       return "Quinte (power)";
        case ChordType::Major7:      return "Majeur 7";
        case ChordType::Minor7:      return "Mineur 7";
        case ChordType::Dominant7:   return "Dominante 7";
        case ChordType::Minor7Flat5: return "Mineur 7 b5";
        case ChordType::Major9:      return "Majeur 9";
        case ChordType::Minor9:      return "Mineur 9";
    }
    return "?";
}

std::vector<ChordType> allChordTypes() {
    return { ChordType::Major, ChordType::Minor, ChordType::Diminished, ChordType::Augmented,
             ChordType::Sus2, ChordType::Sus4, ChordType::Power, ChordType::Major7,
             ChordType::Minor7, ChordType::Dominant7, ChordType::Minor7Flat5,
             ChordType::Major9, ChordType::Minor9 };
}

std::vector<int> chordIntervals(ChordType type) {
    switch (type) {
        case ChordType::Major:       return {0, 4, 7};
        case ChordType::Minor:       return {0, 3, 7};
        case ChordType::Diminished:  return {0, 3, 6};
        case ChordType::Augmented:   return {0, 4, 8};
        case ChordType::Sus2:        return {0, 2, 7};
        case ChordType::Sus4:        return {0, 5, 7};
        case ChordType::Power:       return {0, 7};
        case ChordType::Major7:      return {0, 4, 7, 11};
        case ChordType::Minor7:      return {0, 3, 7, 10};
        case ChordType::Dominant7:   return {0, 4, 7, 10};
        case ChordType::Minor7Flat5: return {0, 3, 6, 10};
        case ChordType::Major9:      return {0, 4, 7, 11, 14};
        case ChordType::Minor9:      return {0, 3, 7, 10, 14};
    }
    return {0};
}

// ---------------------------------------------------------------------------
// Opérations
// ---------------------------------------------------------------------------

size_t notesLeavingMidiRange(const std::vector<Note>& notes, const NoteSelection& selection, int semitones) {
    size_t dehors = 0;
    for (const auto& n : notes) {
        if (selection.count(n.id) == 0) continue;
        const int cible = static_cast<int>(n.number) + semitones;
        if (cible < 0 || cible > 127) ++dehors;
    }
    return dehors;
}

size_t transposeNotes(std::vector<Note>& notes, const NoteSelection& selection, int semitones) {
    // D536 : tout ou rien — jamais `clampNoteNumber` ici (voir l'en-tête).
    const size_t dehors = notesLeavingMidiRange(notes, selection, semitones);
    if (dehors > 0) return dehors;
    forEachSelected(notes, selection, [semitones](Note& n) {
        n.number = static_cast<uint8_t>(static_cast<int>(n.number) + semitones);
    });
    return 0;
}

void nudgeNotes(std::vector<Note>& notes, const NoteSelection& selection, int64_t deltaTicks) {
    forEachSelected(notes, selection, [deltaTicks](Note& n) {
        const Tick duration = n.durationTicks();
        const int64_t newStart = static_cast<int64_t>(n.startTick) + deltaTicks;
        n.startTick = static_cast<Tick>(std::max<int64_t>(0, newStart));
        n.endTick = n.startTick + duration;
    });
}

void setNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, Tick lengthTicks) {
    const Tick length = std::max<Tick>(1, lengthTicks);
    forEachSelected(notes, selection, [length](Note& n) { n.endTick = n.startTick + length; });
}

void scaleNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, float factor) {
    if (factor <= 0.0f) return;
    forEachSelected(notes, selection, [factor](Note& n) {
        const double scaled = static_cast<double>(n.durationTicks()) * static_cast<double>(factor);
        n.endTick = n.startTick + static_cast<Tick>(std::max<int64_t>(1, std::llround(scaled)));
    });
}

void scaleNoteTimes(std::vector<Note>& notes, const NoteSelection& selection, double factor) {
    if (factor <= 0.0 || selection.empty()) return;
    Tick ancre = -1;
    for (const auto& n : notes)
        if (selection.count(n.id) > 0 && (ancre < 0 || n.startTick < ancre)) ancre = n.startTick;
    if (ancre < 0) return;
    forEachSelected(notes, selection, [factor, ancre](Note& n) {
        const double depart = static_cast<double>(n.startTick - ancre) * factor;
        const double duree = static_cast<double>(n.durationTicks()) * factor;
        n.startTick = ancre + static_cast<Tick>(std::llround(depart));
        n.endTick = n.startTick + static_cast<Tick>(std::max<int64_t>(1, std::llround(duree)));
    });
}

void applyLegato(std::vector<Note>& notes, const NoteSelection& selection) {
    if (selection.empty()) return;
    // Toutes les notes de la piste servent de référence (pas seulement les
    // sélectionnées) : sinon « legato » sur une sélection partielle produirait
    // des notes qui traversent des notes non sélectionnées.
    std::vector<const Note*> byTime;
    byTime.reserve(notes.size());
    for (const auto& n : notes) byTime.push_back(&n);
    std::sort(byTime.begin(), byTime.end(),
              [](const Note* a, const Note* b) { return a->startTick < b->startTick; });

    for (auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        Tick nextStart = 0;
        bool found = false;
        for (const Note* other : byTime) {
            if (other->startTick > note.startTick) { nextStart = other->startTick; found = true; break; }
        }
        if (found && nextStart > note.startTick) note.endTick = nextStart;
    }
}

size_t deleteDoubleNotes(std::vector<Note>& notes, const NoteSelection& selection, Tick toleranceTicks) {
    std::vector<size_t> choisies;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selection.count(notes[i].id) > 0) choisies.push_back(i);
    std::sort(choisies.begin(), choisies.end(), [&notes](size_t a, size_t b) {
        const Note& x = notes[a];
        const Note& y = notes[b];
        if (x.channel != y.channel) return x.channel < y.channel;
        if (x.number != y.number) return x.number < y.number;
        if (x.startTick != y.startTick) return x.startTick < y.startTick;
        return x.id < y.id;
    });
    // LA MEILLEURE D'UN GROUPE : celle qui sonne, puis la plus forte, puis la plus longue, puis la première.
    const auto meilleure = [&notes](size_t a, size_t b) {
        const Note& x = notes[a];
        const Note& y = notes[b];
        if (x.muted != y.muted) return !x.muted;
        if (x.velocity != y.velocity) return x.velocity > y.velocity;
        const Tick dx = x.endTick - x.startTick, dy = y.endTick - y.startTick;
        if (dx != dy) return dx > dy;
        return x.startTick < y.startTick;
    };
    std::set<uint64_t> retirees;
    for (size_t i = 0; i < choisies.size();) {
        const Note& premiere = notes[choisies[i]];
        size_t j = i + 1;
        while (j < choisies.size() && notes[choisies[j]].channel == premiere.channel
               && notes[choisies[j]].number == premiere.number
               && notes[choisies[j]].startTick - premiere.startTick < toleranceTicks)
            ++j;
        size_t garde = choisies[i];
        for (size_t k = i + 1; k < j; ++k)
            if (meilleure(choisies[k], garde)) garde = choisies[k];
        for (size_t k = i; k < j; ++k)
            if (choisies[k] != garde) retirees.insert(notes[choisies[k]].id);
        i = j;
    }
    if (retirees.empty()) return 0;
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&retirees](const Note& n) { return retirees.count(n.id) > 0; }),
                notes.end());
    return retirees.size();
}

void removeOverlaps(std::vector<Note>& notes, const NoteSelection& selection) {
    if (selection.empty()) return;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        for (const auto& other : notes) {
            if (other.id == note.id || other.number != note.number) continue;
            if (other.startTick > note.startTick && other.startTick < note.endTick)
                note.endTick = other.startTick;
        }
        if (note.endTick <= note.startTick) note.endTick = note.startTick + 1;
    }
}

MonoOverlaps removeOverlapsMono(std::vector<Note>& notes, const NoteSelection& selection) {
    MonoOverlaps r;
    if (selection.empty()) return r;
    std::vector<size_t> ligne;   // les indices des notes choisies, dans l'ordre des départs, la plus aiguë d'abord
    for (size_t i = 0; i < notes.size(); ++i)
        if (selection.count(notes[i].id) > 0) ligne.push_back(i);
    std::sort(ligne.begin(), ligne.end(), [&](size_t a, size_t b) {
        if (notes[a].startTick != notes[b].startTick) return notes[a].startTick < notes[b].startTick;
        return notes[a].number > notes[b].number;
    });
    std::set<uint64_t> retirees;
    std::vector<size_t> gardees;
    for (size_t i : ligne) {
        if (!gardees.empty() && notes[gardees.back()].startTick == notes[i].startTick) {
            retirees.insert(notes[i].id);   // même départ qu'une plus aiguë déjà gardée
            continue;
        }
        gardees.push_back(i);
    }
    for (size_t k = 0; k + 1 < gardees.size(); ++k) {
        Note& n = notes[gardees[k]];
        const Tick suivante = notes[gardees[k + 1]].startTick;
        if (n.endTick > suivante) { n.endTick = suivante; ++r.shortened; }
    }
    r.removed = retirees.size();
    if (!retirees.empty())
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const Note& n) { return retirees.count(n.id) > 0; }),
                    notes.end());
    return r;
}

MonoOverlaps restrictPolyphony(std::vector<Note>& notes, const NoteSelection& selection, int voices) {
    MonoOverlaps r;
    if (selection.empty() || voices < 1) return r;
    std::vector<size_t> ligne;   // les notes choisies, dans l'ordre des départs, la plus aiguë d'abord
    for (size_t i = 0; i < notes.size(); ++i)
        if (selection.count(notes[i].id) > 0) ligne.push_back(i);
    std::sort(ligne.begin(), ligne.end(), [&](size_t a, size_t b) {
        if (notes[a].startTick != notes[b].startTick) return notes[a].startTick < notes[b].startTick;
        return notes[a].number > notes[b].number;
    });
    const size_t n = static_cast<size_t>(voices);
    std::set<uint64_t> retirees;
    std::vector<size_t> sonnent;   // les notes gardées qui sonnent encore, la plus ancienne d'abord
    for (size_t k = 0; k < ligne.size();) {
        const Tick t = notes[ligne[k]].startTick;
        size_t fin = k;
        while (fin < ligne.size() && notes[ligne[fin]].startTick == t) ++fin;
        sonnent.erase(std::remove_if(sonnent.begin(), sonnent.end(), [&](size_t i) { return notes[i].endTick <= t; }),
                      sonnent.end());
        // LE GROUPE : ses `n` plus aiguës, les autres retirées.
        const size_t garde = std::min(n, fin - k);
        for (size_t j = k + garde; j < fin; ++j) retirees.insert(notes[ligne[j]].id);
        // LES PLUS ANCIENNES s'arrêtent où le groupe attaque. `sonnent` est dans l'ordre des départs, et à départ
        // égal la plus aiguë d'abord : la plus grave d'un même départ est donc la DERNIÈRE de son groupe -- on
        // choisit explicitement, plutôt que de compter sur l'ordre.
        while (sonnent.size() + garde > n) {
            auto victime = std::min_element(sonnent.begin(), sonnent.end(), [&](size_t a, size_t b) {
                if (notes[a].startTick != notes[b].startTick) return notes[a].startTick < notes[b].startTick;
                return notes[a].number < notes[b].number;
            });
            notes[*victime].endTick = t;
            ++r.shortened;
            sonnent.erase(victime);
        }
        for (size_t j = k; j < k + garde; ++j) sonnent.push_back(ligne[j]);
        k = fin;
    }
    r.removed = retirees.size();
    if (!retirees.empty())
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const Note& x) { return retirees.count(x.id) > 0; }),
                    notes.end());
    return r;
}

namespace {
std::vector<CcPoint> pedaleTriee(const std::vector<CcPoint>& ccs) {
    std::vector<CcPoint> p;
    for (const auto& cc : ccs)
        if (cc.controller == 64) p.push_back(cc);
    std::stable_sort(p.begin(), p.end(), [](const CcPoint& a, const CcPoint& b) { return a.tick < b.tick; });
    return p;
}
} // namespace

size_t countPedalPresses(const std::vector<CcPoint>& controlChanges) {
    size_t appuis = 0;
    bool enfoncee = false;
    for (const auto& cc : pedaleTriee(controlChanges)) {
        const bool bas = cc.value >= 64;
        if (bas && !enfoncee) ++appuis;
        enfoncee = bas;
    }
    return appuis;
}

PedalConversion pedalsToNoteLength(std::vector<Note>& notes, std::vector<CcPoint>& controlChanges, Tick materialEnd) {
    PedalConversion r;
    const auto pedale = pedaleTriee(controlChanges);
    if (pedale.empty()) return r;
    // Les frappes de chaque hauteur, dans l'ordre : la suivante borne la note.
    std::map<uint8_t, std::vector<Tick>> frappes;
    for (const auto& n : notes) frappes[n.number].push_back(n.startTick);
    for (auto& [h, v] : frappes) std::sort(v.begin(), v.end());
    for (auto& n : notes) {
        // L'ÉTAT À LA FIN DE LA NOTE : le dernier CC 64 posé STRICTEMENT avant elle.
        auto apres = std::lower_bound(pedale.begin(), pedale.end(), n.endTick,
                                      [](const CcPoint& cc, Tick t) { return cc.tick < t; });
        if (apres == pedale.begin() || std::prev(apres)->value < 64) continue;
        // LE RELÂCHEMENT : le premier CC 64 sous 64 posé à la fin de la note ou après.
        auto releve = std::find_if(apres, pedale.end(), [](const CcPoint& cc) { return cc.value < 64; });
        Tick fin = releve != pedale.end() ? releve->tick : materialEnd;
        const auto& v = frappes[n.number];
        auto suivante = std::upper_bound(v.begin(), v.end(), n.startTick);
        if (suivante != v.end() && *suivante < fin) fin = *suivante;
        if (fin <= n.endTick) continue;
        if (releve == pedale.end() && fin == materialEnd) ++r.heldToEnd;
        n.endTick = fin;
        ++r.lengthened;
    }
    const auto avant = controlChanges.size();
    controlChanges.erase(std::remove_if(controlChanges.begin(), controlChanges.end(),
                                        [](const CcPoint& cc) { return cc.controller == 64; }),
                         controlChanges.end());
    r.pedalEvents = avant - controlChanges.size();
    return r;
}

size_t splitNotes(std::vector<Note>& notes, const NoteSelection& selection, Tick atTick,
                   uint64_t& idCounter, NoteSelection* newIds) {
    if (selection.empty()) return 0;
    std::vector<Note> created;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        if (atTick <= note.startTick || atTick >= note.endTick) continue; // le point de coupe doit traverser la note
        Note second = note;
        second.startTick = atTick;
        second.id = ++idCounter;
        note.endTick = atTick;
        created.push_back(second);
    }
    for (const auto& n : created) {
        notes.push_back(n);
        if (newIds) newIds->insert(n.id);
    }
    return created.size();
}

size_t joinNotes(std::vector<Note>& notes, NoteSelection& selection) {
    if (selection.size() < 2) return 0;
    // Regroupement par hauteur : joindre des notes de hauteurs différentes
    // n'aurait pas de sens musical (ce serait une seule note à deux hauteurs).
    std::vector<Note*> selected = selectedSortedByTime(notes, selection);
    std::set<uint64_t> removed;
    for (size_t i = 0; i < selected.size(); ++i) {
        Note* first = selected[i];
        if (removed.count(first->id) > 0) continue;
        for (size_t j = i + 1; j < selected.size(); ++j) {
            Note* next = selected[j];
            if (removed.count(next->id) > 0 || next->number != first->number) continue;
            first->endTick = std::max(first->endTick, next->endTick);
            removed.insert(next->id);
        }
    }
    if (removed.empty()) return 0;
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                                [&removed](const Note& n) { return removed.count(n.id) > 0; }),
                 notes.end());
    for (uint64_t id : removed) selection.erase(id);
    return removed.size();
}

void reverseNotesInTime(std::vector<Note>& notes, const NoteSelection& selection) {
    std::vector<Note*> sorted = selectedSortedByTime(notes, selection);
    if (sorted.size() < 2) return;
    Tick windowStart = sorted.front()->startTick;
    Tick windowEnd = 0;
    for (const Note* n : sorted) windowEnd = std::max(windowEnd, n->endTick);

    for (Note* n : sorted) {
        const Tick duration = n->durationTicks();
        // Miroir de l'intervalle [start, end) dans la fenêtre de la sélection.
        const Tick newStart = windowStart + (windowEnd - n->endTick);
        n->startTick = newStart;
        n->endTick = newStart + duration;
    }
}

void mirrorNotesPitch(std::vector<Note>& notes, const NoteSelection& selection) {
    if (selection.empty()) return;
    int lowest = 127, highest = 0;
    for (const auto& n : notes) {
        if (selection.count(n.id) == 0) continue;
        lowest = std::min(lowest, static_cast<int>(n.number));
        highest = std::max(highest, static_cast<int>(n.number));
    }
    if (lowest > highest) return;
    const int axisTimesTwo = lowest + highest; // évite un axe à moitié de demi-ton
    forEachSelected(notes, selection, [axisTimesTwo](Note& n) {
        n.number = clampNoteNumber(axisTimesTwo - static_cast<int>(n.number));
    });
}

void setVelocity(std::vector<Note>& notes, const NoteSelection& selection, uint8_t velocity) {
    const uint8_t v = clampVelocity(static_cast<int>(velocity));
    forEachSelected(notes, selection, [v](Note& n) { n.velocity = v; });
}

void scaleVelocity(std::vector<Note>& notes, const NoteSelection& selection, float factor) {
    forEachSelected(notes, selection, [factor](Note& n) {
        n.velocity = clampVelocity(static_cast<int>(std::lround(static_cast<float>(n.velocity) * factor)));
    });
}

void compressVelocity(std::vector<Note>& notes, const NoteSelection& selection, float amount) {
    // LA MOYENNE D'ABORD, SUR LA SÉLECTION SEULE : comprimer vers la moyenne
    // de TOUTES les notes ferait dépendre le résultat de ce qu'on n'a pas
    // choisi, et deux sélections successives ne donneraient pas ce que la
    // sélection réunie donne.
    long long somme = 0;
    size_t combien = 0;
    for (const Note& n : notes)
        if (selection.count(n.id) > 0) { somme += n.velocity; ++combien; }
    if (combien == 0) return;

    // ARRONDIE, et c'est ce qui rend le cas 0 exact : toutes les notes
    // reçoivent le MÊME entier, pas des arrondis voisins d'un même réel.
    const long long moyenne =
        (somme + static_cast<long long>(combien) / 2) / static_cast<long long>(combien);
    const float garde = std::clamp(amount, 0.0f, 1.0f);
    // À 1, ON NE TOUCHE À RIEN, littéralement : la vélocité n'est pas
    // recalculée puis réécrite identique, elle est laissée en place. Un aller
    // simple par le flottant suffirait ici, mais l'exactitude d'un cas neutre
    // ne doit pas reposer sur un arrondi qui tombe juste.
    if (garde >= 1.0f) return;
    forEachSelected(notes, selection, [moyenne, garde](Note& n) {
        const double v = static_cast<double>(moyenne)
                         + static_cast<double>(garde)
                               * (static_cast<double>(n.velocity) - static_cast<double>(moyenne));
        n.velocity = clampVelocity(static_cast<int>(std::llround(v)));
    });
}

void limitVelocity(std::vector<Note>& notes, const NoteSelection& selection,
                    uint8_t minVelocity, uint8_t maxVelocity) {
    int bas = static_cast<int>(minVelocity);
    int haut = static_cast<int>(maxVelocity);
    if (bas > haut) std::swap(bas, haut);
    forEachSelected(notes, selection, [bas, haut](Note& n) {
        n.velocity = clampVelocity(std::clamp(static_cast<int>(n.velocity), bas, haut));
    });
}

void rampVelocity(std::vector<Note>& notes, const NoteSelection& selection,
                   uint8_t fromVelocity, uint8_t toVelocity) {
    std::vector<Note*> sorted = selectedSortedByTime(notes, selection);
    if (sorted.empty()) return;
    if (sorted.size() == 1) { sorted[0]->velocity = clampVelocity(static_cast<int>(toVelocity)); return; }

    // Interpolation sur la POSITION TEMPORELLE, pas sur le rang : un crescendo
    // doit suivre le temps musical, pas le nombre de notes -- sinon un passage
    // dense monterait plus vite qu'un passage aéré de même durée.
    const Tick first = sorted.front()->startTick;
    Tick last = first;
    for (const Note* n : sorted) last = std::max(last, n->startTick);
    const double span = static_cast<double>(last - first);

    for (Note* n : sorted) {
        const double t = span > 0.0 ? static_cast<double>(n->startTick - first) / span : 1.0;
        const double v = static_cast<double>(fromVelocity) + t * (static_cast<double>(toVelocity) - static_cast<double>(fromVelocity));
        n->velocity = clampVelocity(static_cast<int>(std::llround(v)));
    }
}

void randomizeVelocity(std::vector<Note>& notes, const NoteSelection& selection, int amount, uint64_t seed) {
    if (amount <= 0) return;
    forEachSelected(notes, selection, [amount, seed](Note& n) {
        vsm::util::DeterministicRng rng(vsm::util::deriveSeed(seed, n.id));
        const int offset = static_cast<int>(std::lround(rng.nextBipolar() * static_cast<float>(amount)));
        n.velocity = clampVelocity(static_cast<int>(n.velocity) + offset);
    });
}

void constrainNotesToScale(std::vector<Note>& notes, const NoteSelection& selection, Scale scale) {
    forEachSelected(notes, selection, [scale](Note& n) { n.number = snapNoteToScale(n.number, scale); });
}

void setNotesMuted(std::vector<Note>& notes, const NoteSelection& selection, bool muted) {
    forEachSelected(notes, selection, [muted](Note& n) { n.muted = muted; });
}

void toggleNotesMuted(std::vector<Note>& notes, const NoteSelection& selection) {
    forEachSelected(notes, selection, [](Note& n) { n.muted = !n.muted; });
}

NoteSelection duplicateNotes(std::vector<Note>& notes, const NoteSelection& selection,
                              Tick offsetTicks, uint64_t& idCounter) {
    NoteSelection created;
    if (selection.empty()) return created;
    std::vector<Note> copies;
    for (const auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        Note copy = note;
        copy.startTick = note.startTick + offsetTicks;
        copy.endTick = note.endTick + offsetTicks;
        copy.id = ++idCounter;
        copies.push_back(copy);
        created.insert(copy.id);
    }
    for (const auto& c : copies) notes.push_back(c);
    return created;
}

size_t arpeggiateNotes(std::vector<Note>& notes, const NoteSelection& selection,
                        Tick stepTicks, ArpeggioMode mode, uint64_t seed) {
    if (selection.empty() || stepTicks == 0) return 0;
    std::vector<Note*> sorted = selectedSortedByTime(notes, selection);

    size_t moved = 0;
    size_t i = 0;
    while (i < sorted.size()) {
        // Un "accord" = les notes sélectionnées qui démarrent au même tick.
        const Tick chordTick = sorted[i]->startTick;
        std::vector<Note*> chord;
        while (i < sorted.size() && sorted[i]->startTick == chordTick) chord.push_back(sorted[i++]);
        if (chord.size() < 2) continue;

        std::vector<Note*> order = chord; // déjà trié grave -> aigu par selectedSortedByTime
        switch (mode) {
            case ArpeggioMode::Up: break;
            case ArpeggioMode::Down: std::reverse(order.begin(), order.end()); break;
            case ArpeggioMode::UpDown: {
                // montée puis redescente sans répéter les extrêmes
                std::vector<Note*> updown = order;
                for (size_t k = order.size() - 1; k > 0; --k) updown.push_back(order[k - 1]);
                order.assign(updown.begin(), updown.begin() + static_cast<long>(chord.size()));
                break;
            }
            case ArpeggioMode::Random: {
                // Mélange REPRODUCTIBLE : Fisher-Yates piloté par le RNG seedé
                // du projet (jamais std::shuffle + random_device, qui rendrait
                // l'édition non reproductible d'une session à l'autre).
                vsm::util::DeterministicRng rng(
                    vsm::util::deriveSeed(seed, static_cast<uint64_t>(chordTick)));
                for (size_t k = order.size(); k > 1; --k) {
                    const size_t j = static_cast<size_t>(rng.nextUInt64() % k);
                    std::swap(order[k - 1], order[j]);
                }
                break;
            }
        }

        for (size_t k = 0; k < order.size(); ++k) {
            Note* n = order[k];
            const Tick duration = n->durationTicks();
            n->startTick = chordTick + static_cast<Tick>(k) * stepTicks;
            n->endTick = n->startTick + duration;
            ++moved;
        }
    }
    return moved;
}

NoteSelection insertChord(std::vector<Note>& notes, Tick startTick, Tick lengthTicks,
                           uint8_t rootNote, ChordType type, uint8_t channel,
                           uint8_t velocity, uint64_t& idCounter) {
    NoteSelection created;
    const Tick length = std::max<Tick>(1, lengthTicks);
    for (int interval : chordIntervals(type)) {
        const int number = static_cast<int>(rootNote) + interval;
        if (number > 127) continue; // un accord qui dépasse l'aigu perd ses notes hautes, il n'est pas replié
        Note note;
        note.startTick = startTick;
        note.endTick = startTick + length;
        note.channel = channel;
        note.number = static_cast<uint8_t>(number);
        note.velocity = clampVelocity(static_cast<int>(velocity));
        note.id = ++idCounter;
        notes.push_back(note);
        created.insert(note.id);
    }
    return created;
}

// ---------------------------------------------------------------------------
// Sélection
// ---------------------------------------------------------------------------

NoteSelection selectAllNotes(const std::vector<Note>& notes) {
    NoteSelection result;
    for (const auto& n : notes) result.insert(n.id);
    return result;
}

NoteSelection invertNoteSelection(const std::vector<Note>& notes, const NoteSelection& selection) {
    NoteSelection result;
    for (const auto& n : notes)
        if (selection.count(n.id) == 0) result.insert(n.id);
    return result;
}

NoteSelection selectNotesWithSamePitch(const std::vector<Note>& notes, const NoteSelection& selection) {
    std::set<uint8_t> pitches;
    for (const auto& n : notes)
        if (selection.count(n.id) > 0) pitches.insert(n.number);

    NoteSelection result;
    for (const auto& n : notes)
        if (pitches.count(n.number) > 0) result.insert(n.id);
    return result;
}

NoteSelection selectNotesWithSamePitchClass(const std::vector<Note>& notes, const NoteSelection& selection) {
    std::set<int> noms;
    for (const auto& n : notes)
        if (selection.count(n.id) > 0) noms.insert(n.number % 12);
    NoteSelection result = selection;
    for (const auto& n : notes)
        if (noms.count(n.number % 12) > 0) result.insert(n.id);
    return result;
}

NoteSelection selectNotesBelowVelocity(const std::vector<Note>& notes, uint8_t belowVelocity) {
    NoteSelection result;
    for (const auto& n : notes)
        if (n.velocity < belowVelocity) result.insert(n.id);
    return result;
}

NoteSelection selectNotesShorterThan(const std::vector<Note>& notes, Tick shorterThanTicks) {
    NoteSelection result;
    for (const auto& n : notes)
        if (n.durationTicks() < shorterThanTicks) result.insert(n.id);
    return result;
}

NoteSelection selectNotesInTimeRange(const std::vector<Note>& notes, Tick fromTick, Tick toTick) {
    NoteSelection result;
    for (const auto& n : notes)
        if (n.startTick >= fromTick && n.startTick < toTick) result.insert(n.id);
    return result;
}

SelectionStats computeSelectionStats(const std::vector<Note>& notes, const NoteSelection& selection) {
    SelectionStats stats;
    if (selection.empty()) return stats;

    bool first = true;
    int velocitySum = 0;
    for (const auto& n : notes) {
        if (selection.count(n.id) == 0) continue;
        if (first) {
            stats.startTick = n.startTick;
            stats.endTick = n.endTick;
            stats.lowestNote = stats.highestNote = n.number;
            first = false;
        } else {
            stats.startTick = std::min(stats.startTick, n.startTick);
            stats.endTick = std::max(stats.endTick, n.endTick);
            stats.lowestNote = std::min(stats.lowestNote, n.number);
            stats.highestNote = std::max(stats.highestNote, n.number);
        }
        velocitySum += n.velocity;
        ++stats.count;
    }
    if (stats.count > 0)
        stats.averageVelocity = static_cast<float>(velocitySum) / static_cast<float>(stats.count);
    return stats;
}


// ---------------------------------------------------------------------------
// Notes douteuses
// ---------------------------------------------------------------------------

bool isNoteDoubtful(const Note& note, float threshold) {
    return note.confidence < threshold;
}

size_t countDoubtfulNotes(const std::vector<Note>& notes, float threshold) {
    size_t count = 0;
    for (const auto& n : notes)
        if (isNoteDoubtful(n, threshold)) ++count;
    return count;
}

NoteSelection selectDoubtfulNotes(const std::vector<Note>& notes, float threshold) {
    NoteSelection result;
    for (const auto& n : notes)
        if (isNoteDoubtful(n, threshold)) result.insert(n.id);
    return result;
}

// D223 (A39) : LES MOINS SÛRES, EN PROPORTION -- parce que le seuil absolu ne
// désigne plus une minorité. Mesuré sur six reconstructions : « douteuse » couvre
// de 53 à 91 % des notes, et une marque qui porte sur trois notes sur quatre ne
// dit plus par où commencer. Ce geste-ci ne dépend d'aucun seuil : il rend les
// `part` × N notes de plus basse confiance, ce qu'on veut vraiment d'une telle
// marque -- les pires d'abord.
//
// À ÉGALITÉ DE CONFIANCE, L'ORDRE EST CELUI DES NOTES (début, hauteur,
// identifiant) : sans cela, deux appels sur le même morceau rendraient deux
// sélections différentes, et la touche qui mène « à la suivante » sauterait.
NoteSelection selectLeastConfident(const std::vector<Note>& notes, double part) {
    NoteSelection result;
    if (notes.empty() || part <= 0.0) return result;
    const double borne = part >= 1.0 ? 1.0 : part;
    // Au moins UNE note dès qu'il y en a : « les 10 % les moins sûres » d'un
    // morceau de trois notes en rend une, et non zéro.
    const size_t combien = std::max<size_t>(
        1, static_cast<size_t>(std::llround(static_cast<double>(notes.size()) * borne)));
    std::vector<const Note*> triees;
    triees.reserve(notes.size());
    for (const auto& n : notes) triees.push_back(&n);
    std::stable_sort(triees.begin(), triees.end(), [](const Note* a, const Note* b) {
        if (a->confidence != b->confidence) return a->confidence < b->confidence;
        if (a->startTick != b->startTick) return a->startTick < b->startTick;
        if (a->number != b->number) return a->number < b->number;
        return a->id < b->id;
    });
    for (size_t i = 0; i < combien && i < triees.size(); ++i) result.insert(triees[i]->id);
    return result;
}

namespace {

/// Clé d'ordre total sur les notes : début, hauteur, identifiant.
struct NoteOrderKey {
    Tick start;
    uint8_t number;
    uint64_t id;
    bool operator<(const NoteOrderKey& o) const {
        if (start != o.start) return start < o.start;
        if (number != o.number) return number < o.number;
        return id < o.id;
    }
};

NoteOrderKey keyOf(const Note& n) { return {n.startTick, n.number, n.id}; }

} // namespace

uint64_t nextDoubtfulNote(const std::vector<Note>& notes, const NoteSelection& selection,
                          Tick playheadTick, bool forward, float threshold) {
    // Le point de départ : la borne de la sélection dans le sens du parcours,
    // sinon la tête de lecture. `inclusive` dit si une douteuse qui commence
    // EXACTEMENT au point de départ compte : oui depuis la tête de lecture
    // (« la douteuse à partir d'ici »), non depuis une sélection (sinon la
    // note déjà sélectionnée serait rendue à nouveau, et on ne bougerait pas).
    bool fromSelection = false;
    NoteOrderKey origin{playheadTick, 0, 0};
    for (const auto& n : notes) {
        if (selection.count(n.id) == 0) continue;
        const NoteOrderKey k = keyOf(n);
        if (!fromSelection || (forward ? origin < k : k < origin)) origin = k;
        fromSelection = true;
    }

    const Note* best = nullptr;       // la plus proche après (ou avant) l'origine
    const Note* wrap = nullptr;       // la première (ou dernière) du morceau
    for (const auto& n : notes) {
        if (!isNoteDoubtful(n, threshold)) continue;
        const NoteOrderKey k = keyOf(n);
        bool beyond;
        if (fromSelection) beyond = forward ? origin < k : k < origin;
        else               beyond = forward ? n.startTick >= playheadTick : n.startTick < playheadTick;
        if (beyond && (!best || (forward ? k < keyOf(*best) : keyOf(*best) < k))) best = &n;
        if (!wrap || (forward ? k < keyOf(*wrap) : keyOf(*wrap) < k)) wrap = &n;
    }
    if (best) return best->id;
    return wrap ? wrap->id : 0;
}

// --- D566.1 : annuler et figer la quantification ------------------------------

namespace {
bool porteUnDeplacement(const Note& n) { return n.quantizeShiftStart != 0 || n.quantizeShiftEnd != 0; }
} // namespace

size_t countQuantizedNotes(const std::vector<Note>& notes, const NoteSelection& selection) {
    size_t n = 0;
    for (const auto& note : notes)
        if (selection.count(note.id) > 0 && porteUnDeplacement(note)) ++n;
    return n;
}

size_t resetQuantize(std::vector<Note>& notes, const NoteSelection& selection) {
    size_t rendues = 0;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0 || !porteUnDeplacement(note)) continue;
        const Tick duree = note.durationTicks();
        const Tick debut = std::max<Tick>(0, note.startTick - note.quantizeShiftStart);
        Tick fin = note.endTick - note.quantizeShiftEnd;
        // UNE FIN SUR OU AVANT LE DÉBUT garde la durée que la note a : un geste fait à la main depuis (une durée
        // raccourcie, un legato) peut rendre le déplacement de la fin plus grand que la note.
        if (fin <= debut) fin = debut + std::max<Tick>(1, duree);
        note.startTick = debut;
        note.endTick = fin;
        note.quantizeShiftStart = 0;
        note.quantizeShiftEnd = 0;
        ++rendues;
    }
    if (rendues > 0)
        std::stable_sort(notes.begin(), notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return rendues;
}

size_t freezeQuantize(std::vector<Note>& notes, const NoteSelection& selection) {
    size_t figees = 0;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0 || !porteUnDeplacement(note)) continue;
        note.quantizeShiftStart = 0;
        note.quantizeShiftEnd = 0;
        ++figees;
    }
    return figees;
}


float noteHue(NoteColorMode mode, const Note& note) {
    switch (mode) {
        case NoteColorMode::Pitch: return static_cast<float>(note.number % 12) / 12.0f;
        case NoteColorMode::Channel: return static_cast<float>(note.channel % 16) / 16.0f;
        case NoteColorMode::Track: break;
    }
    return -1.0f;
}

} // namespace vsm::sequencer
