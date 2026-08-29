// Un instrument VST3 minimal, construit par ce dépôt POUR SE TESTER LUI-MÊME.
//
// À QUOI IL SERT ET À QUOI IL NE SERT PAS. Il n'est pas une machine du parc :
// il ne s'enregistre nulle part, ne sort pas de ce dossier, et personne ne le
// chargera pour faire de la musique. Il existe pour que l'hôte VST3 (D7.2) ait
// quelque chose à héberger dans les tests, sans dépendre d'un plugin tiers
// installé sur la machine -- exactement le rôle que l'adaptateur CLAP joue déjà
// pour l'hôte CLAP.
//
// CE QU'IL DOIT SAVOIR FAIRE, ET RIEN DE PLUS :
//   - sonner sur une note, se taire au relâchement (l'hôte doit prouver qu'un
//     instrument tiers JOUE) ;
//   - porter un paramètre visible (l'hôte doit prouver qu'il le voit) ;
//   - porter un état QUE SES PARAMÈTRES NE DÉCRIVENT PAS -- c'est le coeur de
//     la deuxième moitié du critère, « se sauvegarde dans le projet » : si
//     l'état d'un plugin tenait dans sa table de paramètres, il n'y aurait rien
//     à démontrer.

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace {

class TestInstrument final : public juce::AudioProcessor {
public:
    TestInstrument()
        : juce::AudioProcessor(BusesProperties().withOutput("Sortie",
                                                             juce::AudioChannelSet::stereo(), true)) {
        addParameter(niveau_ = new juce::AudioParameterFloat({"niveau", 1}, "Niveau",
                                                               0.0f, 1.0f, 0.5f));
    }

    void prepareToPlay(double sampleRate, int) override { frequenceEchantillonnage_ = sampleRate; }
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        buffer.clear();
        int position = 0;
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            rendre(buffer, position, metadata.samplePosition - position);
            position = metadata.samplePosition;
            if (message.isNoteOn()) {
                note_ = message.getNoteNumber();
                amplitude_ = message.getFloatVelocity();
            } else if (message.isNoteOff() && message.getNoteNumber() == note_) {
                amplitude_ = 0.0f;
            }
        }
        rendre(buffer, position, buffer.getNumSamples() - position);
    }

    // L'ÉTAT SAUVEGARDÉ PORTE PLUS QUE LE PARAMÈTRE. `marque_` n'est exposée
    // par aucun paramètre : c'est ce qui rend le test de sauvegarde
    // significatif. Un état qu'on pourrait reconstruire depuis la table de
    // paramètres ne prouverait rien.
    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::MemoryOutputStream flux(destination, false);
        flux.writeFloat(niveau_->get());
        flux.writeInt(marque_);
    }

    void setStateInformation(const void* donnees, int taille) override {
        juce::MemoryInputStream flux(donnees, static_cast<size_t>(taille), false);
        *niveau_ = flux.readFloat();
        marque_ = flux.readInt();
    }

    /// Lue et écrite par le test à travers l'hôte, via un message de contrôle :
    /// le numéro de programme sert de porte d'entrée, faute de paramètre.
    int marque_ = 0;

    const juce::String getName() const override { return "VSM Test Instrument"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { marque_ = index; }
    const juce::String getProgramName(int) override { return "Unique"; }
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    void rendre(juce::AudioBuffer<float>& buffer, int depart, int nombre) {
        if (nombre <= 0 || amplitude_ <= 0.0f) return;
        const double frequence = 440.0 * std::pow(2.0, (note_ - 69) / 12.0);
        const double increment = 2.0 * juce::MathConstants<double>::pi * frequence
                                 / frequenceEchantillonnage_;
        for (int i = 0; i < nombre; ++i) {
            const float valeur = static_cast<float>(std::sin(phase_)) * amplitude_
                                 * niveau_->get();
            phase_ += increment;
            for (int canal = 0; canal < buffer.getNumChannels(); ++canal)
                buffer.setSample(canal, depart + i, valeur);
        }
    }

    juce::AudioParameterFloat* niveau_ = nullptr;
    double frequenceEchantillonnage_ = 48000.0;
    double phase_ = 0.0;
    float amplitude_ = 0.0f;
    int note_ = 60;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestInstrument(); }
