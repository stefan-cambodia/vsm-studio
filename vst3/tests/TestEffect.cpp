// Un EFFET VST3 minimal, construit par ce dépôt pour se tester lui-même (D7.3).
//
// Même rôle que `TestInstrument.cpp`, et même limite : ce n'est pas un effet du
// parc, personne ne le chargera pour faire de la musique. Il existe pour que
// l'hôte ait quelque chose à héberger dans les tests, sans dépendre d'un plugin
// tiers installé sur la machine.
//
// CE QU'IL DOIT SAVOIR FAIRE, ET RIEN DE PLUS :
//   - LIRE SON ENTRÉE. C'est tout l'objet de l'étape : un effet qui rendrait un
//     signal sans regarder celui qu'on lui donne passerait pour fonctionnel
//     alors que l'hôte ne lui aurait rien transmis. Il inverse donc le signe,
//     ce qui ne peut pas se produire par hasard.
//   - porter un paramètre visible ;
//   - porter un état QUE SON PARAMÈTRE NE DÉCRIT PAS, pour que « se recharge à
//     l'identique » ait quelque chose à prouver.

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

class TestEffect final : public juce::AudioProcessor {
public:
    TestEffect()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Entree", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Sortie", juce::AudioChannelSet::stereo(), true)) {
        addParameter(gain_ = new juce::AudioParameterFloat({"gain", 1}, "Gain", 0.0f, 2.0f, 1.0f));
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        // INVERSION DE SIGNE ET GAIN. L'inversion est délibérée : c'est ce qui
        // rend le test capable de distinguer « l'effet a traité mon signal » de
        // « l'effet a rendu quelque chose ».
        buffer.applyGain(-gain_->get());
    }

    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::MemoryOutputStream flux(destination, false);
        flux.writeFloat(gain_->get());
        flux.writeInt(marque_);
    }

    void setStateInformation(const void* donnees, int taille) override {
        juce::MemoryInputStream flux(donnees, static_cast<size_t>(taille), false);
        *gain_ = flux.readFloat();
        marque_ = flux.readInt();
    }

    const juce::String getName() const override { return "VSM Test Effect"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 2; }
    int getCurrentProgram() override { return marque_; }
    void setCurrentProgram(int index) override { marque_ = index; }
    const juce::String getProgramName(int index) override { return index == 0 ? "A" : "B"; }
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    juce::AudioParameterFloat* gain_ = nullptr;
    /// Non exposée par un paramètre : c'est ce qui rend le test d'état
    /// significatif.
    int marque_ = 0;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestEffect(); }
