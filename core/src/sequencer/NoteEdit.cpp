#include "vsm/sequencer/NoteEdit.h"
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

void transposeNotes(std::vector<Note>& notes, const NoteSelection& selection, int semitones) {
    forEachSelected(notes, selection, [semitones](Note& n) {
        n.number = clampNoteNumber(static_cast<int>(n.number) + semitones);
    });
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

} // namespace vsm::sequencer
