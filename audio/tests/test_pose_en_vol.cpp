#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <memory>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;
using namespace vsm::audio::plugin;

// D579 bis de docs/ROADMAP-daw.md — UNE TÊTE POSÉE PENDANT UN BLOC EN VOL N'EST PAS ÉCRASÉE PAR SA FIN.
//
// `processBlock` lit la position au début du bloc et l'écrivait à la fin : la machine d'arrêt de l'interface
// (`transport_.stop()` puis `seekAllViews(fin)` — D553.2, D578.2, D579.3) qui posait la tête ENTRE les deux la voyait
// remplacée par la fin du bloc, 20 ms plus loin à 48 kHz. Le fil de l'interface n'est pas pilotable d'ici ; la machine
// de test fait la même chose au même endroit — pendant son propre rendu, au milieu du bloc.

namespace {

struct MachineQuiPose : ISynthPlugin {
    std::function<void()> pendantLeRendu;
    void initialize(double, int) override {}
    void process(const MidiNoteEvent*, int, float* outL, float* outR, int n) override {
        for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
        if (pendantLeRendu) {
            auto geste = std::move(pendantLeRendu);
            pendantLeRendu = nullptr;   // une fois
            geste();
        }
    }
    void setParameter(ParamId, float) override {}
    float getParameter(ParamId) const override { return 0.0f; }
    const ParameterList& parameterList() const override {
        static ParameterList vide;
        return vide;
    }
    PresetState saveState() const override { return {}; }
    void loadState(const PresetState&) override {}
    const char* machineName() const override { return "Pose en vol"; }
    int activeVoiceCount() const override { return 0; }
};

/// La tête après UN bloc de 256 échantillons à 8 000 Hz (32 ms), le graphe parti de 3,99 s ; `poser` vrai : la machine
/// arrête le graphe et pose la tête à 4,0 s pendant son rendu.
double teteApresUnBloc(bool poser, bool& rendue) {
    Project projet;
    projet.ticksPerQuarterNote = 480;   // 120 BPM : 960 ticks par seconde
    Track piste;
    uint64_t ids = 1;
    piste.addNote(3800, 4400, 60, 100, 0, ids);   // une note sous la tête : la piste a de quoi rendre
    projet.tracks.push_back(piste);

    ProcessGraph graphe;
    graphe.prepare(8000.0, 256);
    graphe.setProject(projet);
    auto machine = std::make_shared<MachineQuiPose>();
    rendue = false;
    machine->pendantLeRendu = [&graphe, &rendue, poser] {
        rendue = true;
        if (!poser) return;
        graphe.setPlaying(false);
        graphe.seekSeconds(4.0);
    };
    graphe.setTrackInstrumentInstance(0, machine, "test:pose");
    graphe.seekSeconds(3.99);
    graphe.setPlaying(true);
    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    graphe.processBlock(gauche.data(), droite.data(), 256);
    return graphe.currentSeconds();
}

} // namespace

VSM_TEST(une_tete_posee_pendant_un_bloc_en_vol_n_est_pas_ecrasee_par_sa_fin) {
    bool rendue = false;
    const double tete = teteApresUnBloc(true, rendue);
    VSM_ASSERT(rendue);   // la pose a bien eu lieu PENDANT le bloc
    VSM_ASSERT_NEAR(tete, 4.0, 1e-9);
}

VSM_TEST(sans_pose_pendant_le_bloc_la_tete_avance_d_un_bloc) {
    // Le témoin : la correction ne touche pas à un bloc joué sans geste.
    bool rendue = false;
    const double tete = teteApresUnBloc(false, rendue);
    VSM_ASSERT(rendue);
    VSM_ASSERT_NEAR(tete, 3.99 + 256.0 / 8000.0, 1e-9);
}
