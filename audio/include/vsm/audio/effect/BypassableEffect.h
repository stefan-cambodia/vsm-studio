#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace vsm::audio::effect {

/// UN INSERT QU'ON PEUT CONTOURNER (D15.1), sans le retirer de la chaîne.
///
/// CE QUE « CONTOURNER » VEUT DIRE ICI, et pourquoi ce n'est pas « éteindre ».
/// L'effet continue de tourner : il reçoit chaque bloc, tient son état (queue
/// de réverbération, mémoire de delay, enveloppe de compresseur), et déclare
/// la même latence qu'avant. Seule sa SORTIE est remplacée par le signal sec,
/// retardé d'exactement cette latence. Deux raisons, qui sont celles de Cubase
/// (bouton Bypass) plutôt que de Live (interrupteur du device) :
///
///  1. la compensation de latence de la piste ne bouge pas. Un effet qui
///     annonce 64 échantillons de retard et qu'on contourne DOIT en garder 64
///     -- sans quoi la piste se décale d'un coup par rapport aux autres, et le
///     « avec / sans » qu'on voulait comparer compare aussi un déplacement ;
///  2. le retour est sans transitoire : l'effet remis reprend là où il en est,
///     pas depuis un état vide.
///
/// Le prix est un effet qui calcule pour rien tant qu'il est contourné. C'est
/// le prix que Cubase paie aussi, et il achète les deux points ci-dessus.
///
/// LE DRAPEAU EST ATOMIQUE : la vue le bascule, le fil audio le lit ; aucune
/// chaîne n'est reconstruite ni republiée pour un contournement, donc aucun
/// clic de reconstruction.
///
/// LA LATENCE SE LIT À CHAQUE BLOC, pas une fois dans `prepare` : celle du
/// pitch shift est la moitié de son grain, un RÉGLAGE, posé après `prepare`
/// -- et le premier banc l'a montré (retard mémorisé 1 200, déclaré 1 632,
/// écart 0,93). La ligne est donc dimensionnée large : la latence connue à
/// `prepare` ou un huitième de seconde, le plus grand des deux, plus un bloc.
/// Un effet qui déclarerait davantage après coup est retardé de ce que la
/// ligne tient, et sa latence déclarée reste ce que la compensation lit.
class BypassableEffect final : public IAudioEffect {
public:
    explicit BypassableEffect(AudioEffectPtr inner) : inner_(std::move(inner)) {}

    IAudioEffect& inner() { return *inner_; }
    const IAudioEffect& inner() const { return *inner_; }

    void setBypassed(bool bypassed) { bypassed_.store(bypassed, std::memory_order_relaxed); }
    bool isBypassed() const { return bypassed_.load(std::memory_order_relaxed); }

    void prepare(double sampleRate, int maxBlockSize) override {
        inner_->prepare(sampleRate, maxBlockSize);
        maxBlock_ = std::max(1, maxBlockSize);
        const int plancher = static_cast<int>(sampleRate / 8.0);
        // Capacité : le retard plus un bloc entier, pour que l'écriture d'un
        // bloc ne rattrape jamais la lecture retardée.
        capacite_ = static_cast<size_t>(std::max({0, inner_->latencySamples(), plancher}) + maxBlock_);
        ligneL_.assign(capacite_, 0.0f);
        ligneR_.assign(capacite_, 0.0f);
        ecriture_ = 0;
    }

    void reset() override {
        inner_->reset();
        std::fill(ligneL_.begin(), ligneL_.end(), 0.0f);
        std::fill(ligneR_.begin(), ligneR_.end(), 0.0f);
        ecriture_ = 0;
    }

    void process(float* left, float* right, int numSamples) override {
        // Le sec entre TOUJOURS dans la ligne : le contournement peut être
        // demandé au bloc suivant, et le retard doit déjà être rempli de ce
        // qui s'est joué -- pas de zéros.
        if (capacite_ > 0) {
            for (int i = 0; i < numSamples; ++i) {
                ligneL_[ecriture_] = left[i];
                ligneR_[ecriture_] = right[i];
                if (++ecriture_ == capacite_) ecriture_ = 0;
            }
        }
        inner_->process(left, right, numSamples);
        if (!isBypassed() || capacite_ == 0) return;
        // Lecture à la latence DÉCLARÉE MAINTENANT derrière l'écriture : le
        // sec retardé d'autant, échantillon par échantillon.
        const size_t retard = std::min(static_cast<size_t>(std::max(0, inner_->latencySamples())),
                                       capacite_ - static_cast<size_t>(numSamples));
        size_t lecture = (ecriture_ + 2 * capacite_ - static_cast<size_t>(numSamples) - retard) % capacite_;
        for (int i = 0; i < numSamples; ++i) {
            left[i] = ligneL_[lecture];
            right[i] = ligneR_[lecture];
            if (++lecture == capacite_) lecture = 0;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float value) override { inner_->setParameter(id, value); }
    float getParameter(vsm::audio::plugin::ParamId id) const override { return inner_->getParameter(id); }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return inner_->parameterList(); }
    const char* effectName() const override { return inner_->effectName(); }
    int latencySamples() const override { return inner_->latencySamples(); }
    int sidechainBus() const override { return inner_->sidechainBus(); }
    void setSidechainInput(const float* l, const float* r, int n) override { inner_->setSidechainInput(l, r, n); }
    bool requiresRealtimeRender() const override { return inner_->requiresRealtimeRender(); }
    void setTransportInfo(const vsm::audio::plugin::TransportInfo& info) override { inner_->setTransportInfo(info); }
    std::string saveNativeState() const override { return inner_->saveNativeState(); }
    bool loadNativeState(const std::string& state) override { return inner_->loadNativeState(state); }

private:
    AudioEffectPtr inner_;
    std::atomic<bool> bypassed_{false};
    int maxBlock_ = 0;
    size_t capacite_ = 0;
    size_t ecriture_ = 0;
    std::vector<float> ligneL_, ligneR_;
};

} // namespace vsm::audio::effect
