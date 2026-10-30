#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/PitchDetector.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vsm::audio::effect {

/// D605.1 — L'ACCORDEUR (le *Tuner* de Cubase, en insert).
///
/// IL NE TOUCHE PAS AU SON : `process` lit et n'écrit rien — la sortie égale l'entrée au bit près, sans latence. Il
/// mesure la fondamentale de la somme mono (`dsp::detectPitch`, YIN) sur 2 048 échantillons DÉCIMÉS — la moyenne de deux
/// à 44,1 ou 48 kHz, de quatre à 96 kHz : 4 096 échantillons d'entrée, de quoi voir 40 Hz —, recalculée tous les 1 024
/// échantillons décimés. Le fil audio écrit la lecture dans des atomiques ; l'interface les lit (`lecture`), et la
/// génération lui dit si la dernière mesure est récente.
class TunerEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kReference = 0, kNumParams };
    static constexpr int kFenetre = 2048;   ///< échantillons décimés mesurés
    static constexpr int kPas = 1024;       ///< échantillons décimés entre deux mesures

    TunerEffect() {
        parameterList_ = {{kReference, "Reference", 400.0f, 480.0f, 440.0f, "Hz"}};
        reference_.store(440.0f, std::memory_order_relaxed);
        tampon_.assign(kFenetre, 0.0f);
        lineaire_.assign(kFenetre, 0.0f);
        travail_.reserve(2 * (kFenetre + 2));   // les deux courbes du détecteur
    }
    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        facteur_ = std::max(1, static_cast<int>(std::lround(sampleRate_ / 24000.0)));
        reset();
    }
    void reset() override {
        std::fill(tampon_.begin(), tampon_.end(), 0.0f);
        position_ = 0;
        remplis_ = 0;
        accumule_ = 0.0f;
        dansLaSomme_ = 0;
        depuisMesure_ = 0;
        trouve_.store(false, std::memory_order_relaxed);
    }
    void process(float* left, float* right, int numSamples) override {
        // LECTURE SEULE : aucune écriture dans `left` ni `right`.
        for (int i = 0; i < numSamples; ++i) {
            accumule_ += 0.5f * (left[i] + right[i]);
            if (++dansLaSomme_ < facteur_) continue;
            tampon_[static_cast<size_t>(position_)] = accumule_ / static_cast<float>(facteur_);
            position_ = (position_ + 1) % kFenetre;
            accumule_ = 0.0f;
            dansLaSomme_ = 0;
            if (remplis_ < kFenetre) ++remplis_;
            if (++depuisMesure_ >= kPas && remplis_ == kFenetre) {
                depuisMesure_ = 0;
                mesurer();
            }
        }
    }
    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id == kReference) reference_.store(std::clamp(v, 400.0f, 480.0f), std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id == kReference ? reference_.load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Tuner"; }

    /// La dernière mesure, lue depuis l'interface. `generation` augmente à chaque mesure : une lecture dont la génération
    /// ne bouge plus est une lecture qui vieillit (le transport arrêté ne fait plus passer de son).
    dsp::PitchReading lecture(uint32_t& generation) const {
        dsp::PitchReading r;
        generation = generation_.load(std::memory_order_acquire);
        r.found = trouve_.load(std::memory_order_relaxed);
        r.hz = hz_.load(std::memory_order_relaxed);
        r.note = note_.load(std::memory_order_relaxed);
        r.cents = cents_.load(std::memory_order_relaxed);
        r.clarity = clarte_.load(std::memory_order_relaxed);
        return r;
    }

private:
    void mesurer() {
        // LA FENÊTRE DANS L'ORDRE DU TEMPS : la plus ancienne à `position_`.
        for (int i = 0; i < kFenetre; ++i)
            lineaire_[static_cast<size_t>(i)] = tampon_[static_cast<size_t>((position_ + i) % kFenetre)];
        const auto r = dsp::detectPitch(lineaire_.data(), kFenetre, sampleRate_ / facteur_,
                                        reference_.load(std::memory_order_relaxed), travail_);
        hz_.store(r.hz, std::memory_order_relaxed);
        note_.store(r.note, std::memory_order_relaxed);
        cents_.store(r.cents, std::memory_order_relaxed);
        clarte_.store(r.clarity, std::memory_order_relaxed);
        trouve_.store(r.found, std::memory_order_relaxed);
        generation_.fetch_add(1, std::memory_order_release);
    }

    vsm::audio::plugin::ParameterList parameterList_;
    std::atomic<float> reference_{440.0f};
    double sampleRate_ = 48000.0;
    int facteur_ = 2;
    std::vector<float> tampon_, lineaire_, travail_;
    int position_ = 0, remplis_ = 0, dansLaSomme_ = 0, depuisMesure_ = 0;
    float accumule_ = 0.0f;
    std::atomic<bool> trouve_{false};
    std::atomic<float> hz_{0.0f}, cents_{0.0f}, clarte_{0.0f};
    std::atomic<int> note_{0};
    std::atomic<uint32_t> generation_{0};
};

} // namespace vsm::audio::effect
