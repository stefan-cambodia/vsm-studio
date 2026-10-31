#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/interchange/ProjectDocument.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

using namespace vsm::interchange;
using namespace vsm::sequencer;

// D609.1 — LA CARTE DE BATTERIE AU RENDU HORS LIGNE, et au fichier. L'attendu écrit avant le code (ROADMAP-daw, D609,
// (b) et (c)) : une partie de toms General MIDI (41, 43, 45, 47) sur la TR-909 avec la carte 41 → 45, 43 → 47 est ÉGALE
// AU BIT à la même partie écrite directement en 45, 47, 45, 47 ; sans carte, elle en diffère (la 909 n'a rien en 41 ni
// en 43). Au fichier : la carte relue à l'identique ; sans carte, rien d'écrit.

namespace {
LoadedBundle toms(const std::array<uint8_t, 4>& hauteurs, std::map<uint8_t, uint8_t> carte) {
    LoadedBundle bundle;
    bundle.project.ticksPerQuarterNote = 480;
    Track piste;
    piste.name = "Toms";
    piste.instrumentId = "vsm.tr909";
    piste.channel = 9;
    uint64_t ids = 1;
    for (size_t i = 0; i < hauteurs.size(); ++i)
        piste.addNote(static_cast<Tick>(480 * i), static_cast<Tick>(480 * i + 240), hauteurs[i], 110, 9, ids);
    piste.drumMap = std::move(carte);
    bundle.project.tracks.push_back(std::move(piste));
    bundle.document = documentFromProject(bundle.project);
    return bundle;
}

vsm::audio::engine::RenderedAudio rendre(const LoadedBundle& bundle) {
    RenderOptions o;
    o.sampleRate = 8000.0;
    o.blockSize = 256;
    o.durationSeconds = 3.0;
    vsm::audio::engine::RenderedAudio sortie;
    const auto r = renderBundleToBuffer(bundle, sortie, o);
    VSM_ASSERT(r.success);
    return sortie;
}

double ecart(const vsm::audio::engine::RenderedAudio& a, const vsm::audio::engine::RenderedAudio& b) {
    double e = 0.0;
    for (size_t i = 0; i < std::min(a.left.size(), b.left.size()); ++i)
        e = std::max({e, static_cast<double>(std::abs(a.left[i] - b.left[i])),
                      static_cast<double>(std::abs(a.right[i] - b.right[i]))});
    return e;
}
} // namespace

VSM_TEST(the_drum_map_renders_exactly_like_the_part_written_at_its_output_notes_d609) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    const auto carte = rendre(toms({41, 43, 45, 47}, {{41, 45}, {43, 47}}));
    const auto directe = rendre(toms({45, 47, 45, 47}, {}));
    const auto temoin = rendre(toms({41, 43, 45, 47}, {}));
    double crete = 0.0;
    for (float v : directe.left) crete = std::max(crete, static_cast<double>(std::abs(v)));
    std::printf("      [D609.1] crête %.3f ; carte/directe %.2e ; sans carte/directe %.3f\n", crete, ecart(carte, directe),
                ecart(temoin, directe));
    VSM_ASSERT(crete > 0.05);
    VSM_ASSERT_EQ(carte.left.size(), directe.left.size());
    VSM_ASSERT(carte.left == directe.left);     // AU BIT
    VSM_ASSERT(carte.right == directe.right);
    VSM_ASSERT(ecart(temoin, directe) > 0.05);
}

VSM_TEST(the_drum_map_is_written_only_when_it_moves_a_note_and_reads_back_d609) {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tracks.emplace_back();
    project.tracks.emplace_back();
    project.tracks[1].drumMap = {{41, 45}, {43, 47}};
    const auto texte = projectDocumentToJson(documentFromProject(project)).toString();
    // UNE SEULE CARTE AU FICHIER : celle de la piste 1 ; la piste 0 n'écrit rien.
    size_t occurrences = 0;
    for (size_t p = texte.find("\"drumMap\""); p != std::string::npos; p = texte.find("\"drumMap\"", p + 1)) ++occurrences;
    VSM_ASSERT_EQ(occurrences, size_t(1));
    const auto relu = parseProjectDocument(texte);
    VSM_ASSERT(relu.success);
    VSM_ASSERT(relu.document.tracks[0].drumMap.empty());
    VSM_ASSERT(relu.document.tracks[1].drumMap == project.tracks[1].drumMap);
    VSM_ASSERT_EQ(relu.document.tracks[1].drumMapRejected, 0);
    // UNE ENTRÉE HORS BORNES, ou vers elle-même, est écartée — et COMPTÉE, pour être dite à l'ouverture.
    std::string abime = texte;
    const auto ou = abime.find("\"in\": 41");
    VSM_ASSERT(ou != std::string::npos);
    abime.replace(ou, 8, "\"in\": 200");
    const auto relu2 = parseProjectDocument(abime);
    VSM_ASSERT(relu2.success);
    VSM_ASSERT_EQ(relu2.document.tracks[1].drumMap.size(), size_t(1));
    VSM_ASSERT_EQ(relu2.document.tracks[1].drumMapRejected, 1);
    Project cible;
    cible.tracks.resize(2);
    const auto rapport = applyDocumentToProject(relu2.document, cible);
    bool dit = false;
    for (const auto& w : rapport.warnings) dit = dit || w.find("carte de batterie") != std::string::npos;
    VSM_ASSERT(dit);
}
