#include "TestFramework.h"
#include "vsm/audio/engine/VoiceManager.h"

using namespace vsm::audio::engine;

namespace {

/// Voix factice minimale, implémente juste l'interface requise par
/// VoiceManager, sans aucune synthèse réelle -- suffisant pour tester
/// l'ALLOCATION indépendamment de tout DSP.
struct MockVoice {
    bool active = false;
    uint8_t channel_ = 0, note_ = 0;

    bool isActive() const { return active; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t /*velocity*/) {
        channel_ = channel;
        note_ = note;
        active = true;
    }
    void noteOff(uint8_t /*velocity*/) { active = false; }
};

} // namespace

VSM_TEST(voice_manager_allocates_free_voices_first) {
    VoiceManager<MockVoice, 4> manager;
    MockVoice* v1 = manager.noteOn(0, 60, 100);
    MockVoice* v2 = manager.noteOn(0, 62, 100);
    VSM_ASSERT(v1 != nullptr);
    VSM_ASSERT(v2 != nullptr);
    VSM_ASSERT(v1 != v2);
    VSM_ASSERT_EQ(manager.activeVoiceCount(), 2);
}

VSM_TEST(voice_manager_steals_oldest_voice_when_full) {
    VoiceManager<MockVoice, 2> manager;
    manager.noteOn(0, 60, 100); // la plus ancienne
    manager.noteOn(0, 62, 100);
    VSM_ASSERT_EQ(manager.activeVoiceCount(), 2);

    manager.noteOn(0, 64, 100); // doit voler la voix de la note 60 (la plus ancienne)

    bool foundNote60 = false, foundNote64 = false;
    manager.forEachVoice([&](MockVoice& v) {
        if (v.isActive() && v.note() == 60) foundNote60 = true;
        if (v.isActive() && v.note() == 64) foundNote64 = true;
    });
    VSM_ASSERT(!foundNote60); // volée
    VSM_ASSERT(foundNote64);  // nouvelle voix bien allouée
    VSM_ASSERT_EQ(manager.activeVoiceCount(), 2); // toujours au max, pas plus
}

VSM_TEST(voice_manager_note_off_releases_matching_voice_only) {
    VoiceManager<MockVoice, 4> manager;
    manager.noteOn(0, 60, 100);
    manager.noteOn(1, 60, 100); // même note, canal différent
    manager.noteOn(0, 62, 100);

    manager.noteOff(0, 60, 64); // ne doit relâcher QUE (canal 0, note 60)

    int activeCount = 0;
    bool channel1Note60StillActive = false;
    manager.forEachVoice([&](MockVoice& v) {
        if (v.isActive()) {
            ++activeCount;
            if (v.channel() == 1 && v.note() == 60) channel1Note60StillActive = true;
        }
    });
    VSM_ASSERT_EQ(activeCount, 2);
    VSM_ASSERT(channel1Note60StillActive);
}

VSM_TEST(voice_manager_note_off_releases_all_matching_voices) {
    // Retrigger rapide : deux voix peuvent porter la même (canal, note) --
    // noteOff doit toutes les relâcher, pas seulement la première trouvée.
    VoiceManager<MockVoice, 4> manager;
    MockVoice* first = manager.noteOn(0, 60, 100);
    first->active = false; // simule un release déjà entamé mais pas encore Idle -- ici on force juste
    // Comme MockVoice n'a pas d'état "release", on simplifie : deux voix
    // actives sur la même note simultanément (cas limite mais possible en
    // pratique juste après un retrigger, avant que l'ancienne ne se libère).
    first->active = true;
    manager.noteOn(0, 60, 100); // deuxième voix, même (canal, note)

    manager.noteOff(0, 60, 64);

    int activeCount = 0;
    manager.forEachVoice([&](MockVoice& v) { if (v.isActive()) ++activeCount; });
    VSM_ASSERT_EQ(activeCount, 0);
}

VSM_TEST(voice_manager_max_voices_constant_matches_template_parameter) {
    VoiceManager<MockVoice, 7> manager;
    VSM_ASSERT_EQ(manager.maxVoices(), static_cast<size_t>(7));
}
