#include "TestFramework.h"
#include "vsm/audio/util/LockFreeRingBuffer.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace vsm::audio::util;

VSM_TEST(ring_buffer_push_pop_basic) {
    LockFreeRingBuffer<int, 8> buffer;
    VSM_ASSERT(buffer.push(1));
    VSM_ASSERT(buffer.push(2));
    VSM_ASSERT(buffer.push(3));

    int value = 0;
    VSM_ASSERT(buffer.pop(value)); VSM_ASSERT_EQ(value, 1);
    VSM_ASSERT(buffer.pop(value)); VSM_ASSERT_EQ(value, 2);
    VSM_ASSERT(buffer.pop(value)); VSM_ASSERT_EQ(value, 3);
    VSM_ASSERT(!buffer.pop(value)); // vide
}

VSM_TEST(ring_buffer_reports_full_correctly) {
    LockFreeRingBuffer<int, 4> buffer; // capacité utile = 3 (une case sacrifiée)
    VSM_ASSERT(buffer.push(1));
    VSM_ASSERT(buffer.push(2));
    VSM_ASSERT(buffer.push(3));
    VSM_ASSERT(!buffer.push(4)); // plein, refusé proprement (pas de blocage, pas de perte silencieuse)
}

VSM_TEST(ring_buffer_wraps_around_correctly) {
    LockFreeRingBuffer<int, 4> buffer;
    int value = 0;

    for (int round = 0; round < 100; ++round) {
        VSM_ASSERT(buffer.push(round));
        VSM_ASSERT(buffer.push(round * 10));
        VSM_ASSERT(buffer.pop(value)); VSM_ASSERT_EQ(value, round);
        VSM_ASSERT(buffer.pop(value)); VSM_ASSERT_EQ(value, round * 10);
    }
}

VSM_TEST(ring_buffer_concurrent_producer_consumer_preserves_order_and_count) {
    constexpr int kTotal = 200000;
    LockFreeRingBuffer<int, 1024> buffer;
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        int i = 0;
        while (i < kTotal) {
            if (buffer.push(i)) ++i;
            // sinon : file pleine, le consommateur va vider, on réessaie sans dormir
            // (le thread audio ne dort jamais -- c'est exactement ce comportement
            // qu'on veut valider ici)
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::vector<int> received;
    received.reserve(kTotal);
    int value = 0;
    while (received.size() < static_cast<size_t>(kTotal)) {
        if (buffer.pop(value)) received.push_back(value);
    }
    producer.join();

    VSM_ASSERT(producerDone.load());
    VSM_ASSERT_EQ(received.size(), static_cast<size_t>(kTotal));
    for (int i = 0; i < kTotal; ++i)
        VSM_ASSERT_EQ(received[static_cast<size_t>(i)], i); // ordre préservé, aucune perte, aucun doublon
}
