#include "TestFramework.h"
#include "vsm/interchange/PatchRenderService.h"
#include <cmath>
#include <sstream>

using namespace vsm::interchange;

namespace {
PatchRenderRequest simpleRequest(const std::string& machine = "vsm.minimoog") {
    PatchRenderRequest request;
    request.machineId = machine;
    request.sampleRate = 44100.0;
    request.durationSeconds = 0.4;
    request.notes.push_back({45, 100, 0.0, 0.3});
    return request;
}
} // namespace

VSM_TEST(patch_service_renders_a_note) {
    PatchRenderService service;
    const PatchRenderResponse response = service.render(simpleRequest());
    VSM_ASSERT(response.ok);
    VSM_ASSERT_EQ(response.frames, static_cast<size_t>(std::llround(0.4 * 44100.0)));
    VSM_ASSERT(response.peak > 0.01f);
}

VSM_TEST(patch_service_applies_semantic_parameters) {
    // Le service doit accepter les identifiants SÉMANTIQUES : c'est ce qui
    // permet au client d'écrire un patch sans connaître la machine.
    PatchRenderService service;

    PatchRenderRequest dark = simpleRequest();
    dark.parameters = {{"filter.1.cutoff", 300.0f}};
    PatchRenderRequest bright = simpleRequest();
    bright.parameters = {{"filter.1.cutoff", 9000.0f}};

    const auto darkResponse = service.render(dark);
    const auto brightResponse = service.render(bright);
    VSM_ASSERT(darkResponse.ok && brightResponse.ok);
    // Le filtre agit vraiment : deux rendus identiques signifieraient que les
    // paramètres n'atteignent pas la machine.
    VSM_ASSERT(std::abs(darkResponse.peak - brightResponse.peak) > 1e-4f);
}

VSM_TEST(patch_service_is_deterministic_across_requests) {
    // Condition de toute optimisation : deux requêtes identiques doivent
    // donner exactement le même son. Sans cela, l'optimiseur comparerait le
    // bruit résiduel de la note précédente d'une évaluation à l'autre.
    PatchRenderService service;
    PatchRenderRequest request = simpleRequest();
    request.parameters = {{"filter.1.cutoff", 1200.0f}};
    request.returnAudio = "base64-f32-mono";

    const auto first = service.render(request);
    const auto second = service.render(request);
    VSM_ASSERT(first.ok && second.ok);
    VSM_ASSERT_EQ(first.audioBase64, second.audioBase64);
}

VSM_TEST(patch_service_does_not_leak_the_previous_note) {
    // Piège classique d'un service qui garde ses machines en mémoire : la
    // queue de la note précédente s'ajoute à la suivante. Ici, un rendu sans
    // note doit être SILENCIEUX même juste après un rendu fort.
    PatchRenderService service;
    PatchRenderRequest loud = simpleRequest();
    loud.parameters = {{"filter.1.cutoff", 9000.0f}};
    VSM_ASSERT(service.render(loud).ok);

    PatchRenderRequest silent = simpleRequest();
    silent.notes.clear();
    const auto response = service.render(silent);
    VSM_ASSERT(response.ok);
    VSM_ASSERT(response.peak < 1e-4f);
}

VSM_TEST(patch_service_reports_unknown_machine_and_parameters) {
    PatchRenderService service;
    PatchRenderRequest unknown = simpleRequest("vsm.machine-inexistante");
    const auto failure = service.render(unknown);
    VSM_ASSERT(!failure.ok);
    VSM_ASSERT(failure.error.find("machine inconnue") != std::string::npos);

    // Un paramètre absent de la machine visée est SIGNALÉ, pas appliqué en
    // douce à autre chose (règle « aucune approximation silencieuse »).
    PatchRenderRequest strange = simpleRequest();
    strange.parameters = {{"fm.operator.3.ratio", 2.0f}};
    const auto response = service.render(strange);
    VSM_ASSERT(response.ok);
    VSM_ASSERT_EQ(response.warnings.size(), size_t{1});
    VSM_ASSERT(response.warnings[0].find("fm.operator.3.ratio") != std::string::npos);
}

VSM_TEST(patch_service_returns_audio_as_base64_floats) {
    PatchRenderService service;
    PatchRenderRequest request = simpleRequest();
    request.returnAudio = "base64-f32-mono";
    const auto response = service.render(request);
    VSM_ASSERT(response.ok);
    // 4 octets par échantillon, encodés 4 caractères pour 3 octets.
    const size_t expectedBytes = response.frames * sizeof(float);
    const size_t expectedChars = ((expectedBytes + 2) / 3) * 4;
    VSM_ASSERT_EQ(response.audioBase64.size(), expectedChars);
}

VSM_TEST(patch_request_parsing_rejects_nonsense) {
    VSM_ASSERT(!parsePatchRequest("pas du json").success);
    VSM_ASSERT(!parsePatchRequest(R"({"machine":"vsm.minimoog","duration":-1})").success);
    // Garde-fou : une durée démesurée ferait allouer des gigaoctets.
    VSM_ASSERT(!parsePatchRequest(R"({"machine":"vsm.minimoog","duration":10000})").success);

    const auto ok = parsePatchRequest(
        R"({"id":7,"machine":"vsm.tb303","duration":0.5,"sampleRate":44100,
            "parameters":{"filter.1.cutoff":700},"notes":[{"note":45,"velocity":110,"start":0,"duration":0.4}]})");
    VSM_ASSERT(ok.success);
    VSM_ASSERT_EQ(ok.request.requestId, std::string("7"));
    VSM_ASSERT_EQ(ok.request.machineId, std::string("vsm.tb303"));
    VSM_ASSERT_EQ(ok.request.notes.size(), size_t{1});
    VSM_ASSERT_EQ(ok.request.parameters.size(), size_t{1});
}

VSM_TEST(patch_service_loop_answers_one_line_per_request) {
    // Le protocole est fait pour être lu ligne à ligne par un client : une
    // réponse par requête, jamais d'objet réparti sur plusieurs lignes.
    std::istringstream input(
        R"({"id":1,"machine":"vsm.minimoog","duration":0.2,"notes":[{"note":60,"duration":0.1}]})" "\n"
        R"({"id":2,"machine":"vsm.inconnue","duration":0.2})" "\n");
    std::ostringstream output, diagnostics;
    runPatchRenderLoop(input, output, diagnostics);

    const std::string text = output.str();
    size_t lines = 0;
    for (char c : text) if (c == '\n') ++lines;
    VSM_ASSERT_EQ(lines, size_t{2});
    VSM_ASSERT(text.find("\"ok\":true") != std::string::npos);
    VSM_ASSERT(text.find("\"ok\":false") != std::string::npos);
    VSM_ASSERT(text.find("machine inconnue") != std::string::npos);
}
