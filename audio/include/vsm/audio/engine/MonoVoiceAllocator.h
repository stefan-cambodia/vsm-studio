#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vsm::audio::engine {

/// Modélise le comportement d'allocation de voix typique d'un monosynth
/// analogique vintage (Minimoog, TB-303...) : UNE seule voix sonne à la
/// fois, avec une pile de notes tenues et priorité à la DERNIÈRE note
/// enfoncée. Relâcher la note active fait retomber sur la note précédente
/// encore tenue -- le classique jeu "en legato" des synthés mono, où
/// tenir un accord et relâcher les notes une à une fait "triller" entre
/// elles plutôt que de couper le son.
///
/// Comportement par défaut : chaque nouvelle note (même en legato)
/// redéclenche les enveloppes -- comportement du Minimoog Model D
/// d'origine (pas de commutateur "legato" dédié sur le hardware réel).
/// setLegatoMode(true) désactive ce retrigger quand une autre note est
/// déjà tenue : une commodité moderne, pas une prétention d'authenticité
/// matérielle absolue (voir section 27 du cahier des charges -- on ne
/// prétend pas reproduire à 100% un comportement non mesuré).
class MonoVoiceAllocator {
public:
    struct Result {
        bool shouldPlay = false; // false = plus aucune note tenue -> note off
        uint8_t note = 60;
        uint8_t velocity = 100;
        bool retrigger = true;   // l'appelant doit relancer les enveloppes
    };

    void setLegatoMode(bool enabled) { legatoMode_ = enabled; }
    bool legatoMode() const { return legatoMode_; }

    Result noteOn(uint8_t note, uint8_t velocity) {
        bool hadHeldNotes = count_ > 0;
        removeNote(note); // retire une occurrence précédente pour un retrigger propre
        if (count_ < kMaxHeld) stack_[count_++] = {note, velocity};

        Result r;
        r.shouldPlay = true;
        r.note = note;
        r.velocity = velocity;
        r.retrigger = !(legatoMode_ && hadHeldNotes);
        return r;
    }

    Result noteOff(uint8_t note) {
        removeNote(note);

        Result r;
        if (count_ == 0) {
            r.shouldPlay = false;
            return r;
        }
        const HeldNote& top = stack_[count_ - 1]; // note tenue la plus récente
        r.shouldPlay = true;
        r.note = top.note;
        r.velocity = top.velocity;
        r.retrigger = !legatoMode_; // en mode legato, jamais de retrigger en retombant
        return r;
    }

    bool hasHeldNotes() const { return count_ > 0; }
    size_t heldNoteCount() const { return count_; }
    void reset() { count_ = 0; }

private:
    struct HeldNote { uint8_t note = 0; uint8_t velocity = 0; };

    // Capacité fixe = 128 (nombre de numéros de note MIDI ; chaque note n'est
    // tenue qu'une fois grâce à removeNote). Pas de std::vector -> aucune
    // allocation dans noteOn/noteOff, donc sûr sur le thread audio ; supprime
    // aussi un faux positif -Wstringop-overflow de GCC sur le push_back.
    void removeNote(uint8_t note) {
        for (size_t i = 0; i < count_; ++i) {
            if (stack_[i].note == note) {
                for (size_t j = i + 1; j < count_; ++j) stack_[j - 1] = stack_[j];
                --count_;
                return;
            }
        }
    }

    static constexpr size_t kMaxHeld = 128;
    std::array<HeldNote, kMaxHeld> stack_{};
    size_t count_ = 0;
    bool legatoMode_ = false;
};

} // namespace vsm::audio::engine
