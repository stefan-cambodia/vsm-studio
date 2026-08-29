// Vérifie, SANS carte son ni câble, que la mesure de latence de l'application
// retrouve un aller-retour connu À L'ÉCHANTILLON PRÈS (étape D3.6 de
// docs/ROADMAP-daw.md).
//
// Pourquoi cet outil existe. Le critère de D3.6 est « une boucle physique
// enregistre à l'échantillon près ». La boucle, elle, demande un câble ; mais
// tout ce qui se trouve ENTRE le câble et le résultat -- l'émission du balayage
// dans le rappel audio, la capture de l'entrée, la corrélation -- n'en demande
// aucun. On branche donc un faux périphérique qui renvoie dans l'entrée ce que
// l'application vient d'écrire dans la sortie, avec le retard qu'on choisit, et
// on regarde si l'application retrouve ce retard.
//
// C'est le vrai chemin qui est éprouvé : le rappel de `AudioEngine`, ses
// tampons, son automate de mesure. Seul le pilote est remplacé.
//
// UNE LIMITE DE LA SIMULATION, ET ELLE N'EN EST PAS UNE DU MOTEUR. Un rappel
// audio LIT son entrée avant d'écrire sa sortie : dans une boucle simulée, ce
// qu'on écrit ne peut donc revenir qu'au bloc suivant, et l'aller-retour le plus
// court qu'on puisse imposer ici vaut une taille de bloc. Du vrai matériel peut
// faire mieux, et la mesure de l'application sait le voir -- c'est bien pour
// cela que son origine est l'entrée du bloc où le balayage part, et non le bloc
// suivant.
//
//     ./vsm-latency-check [aller-retour-en-echantillons] [taille-de-bloc] [frequence]

#include "audio/AudioEngine.h"

#include <cstdio>
#include <deque>
#include <vector>

namespace {

/// Le minimum que `AudioEngine::audioDeviceAboutToStart` a besoin de savoir :
/// une fréquence, une taille de bloc, des canaux et une latence annoncée. Tout
/// le reste de l'interface de JUCE est ici pour satisfaire le compilateur.
class FauxPeripherique : public juce::AudioIODevice {
public:
    FauxPeripherique(double frequence, int bloc, int latenceAnnoncee)
        : juce::AudioIODevice("boucle simulee", "test"),
          frequence_(frequence), bloc_(bloc), latence_(latenceAnnoncee) {}

    juce::StringArray getOutputChannelNames() override { return {"G", "D"}; }
    juce::StringArray getInputChannelNames() override { return {"E1", "E2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {frequence_}; }
    juce::Array<int> getAvailableBufferSizes() override { return {bloc_}; }
    int getDefaultBufferSize() override { return bloc_; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bloc_; }
    double getCurrentSampleRate() override { return frequence_; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return deuxCanaux(); }
    juce::BigInteger getActiveInputChannels() const override { return deuxCanaux(); }
    int getOutputLatencyInSamples() override { return latence_; }
    int getInputLatencyInSamples() override { return latence_; }

private:
    static juce::BigInteger deuxCanaux() {
        juce::BigInteger bits;
        bits.setBit(0);
        bits.setBit(1);
        return bits;
    }
    double frequence_;
    int bloc_;
    int latence_;
};

} // namespace

int main(int argc, char** argv) {
    const int allerRetour = argc >= 2 ? std::atoi(argv[1]) : 1234;
    const int bloc        = argc >= 3 ? std::atoi(argv[2]) : 256;
    const double freq     = argc >= 4 ? std::atof(argv[3]) : 48000.0;

    if (bloc <= 0 || freq <= 0.0 || allerRetour < bloc) {
        std::printf("Usage : vsm-latency-check [aller-retour-en-echantillons] "
                     "[taille-de-bloc] [frequence]\n"
                     "L'aller-retour doit valoir au moins une taille de bloc : un rappel "
                     "audio lit son entree avant d'ecrire sa sortie.\n");
        return 2;
    }
    // Un bloc de l'aller-retour vient de la structure du rappel lui-même ; le
    // reste est la longueur de la ligne à retard.
    const int retard = allerRetour - bloc;

    AudioEngine moteur;
    FauxPeripherique peripherique(freq, bloc, 64);
    moteur.audioDeviceAboutToStart(&peripherique);

    if (!moteur.startLatencyMeasurement()) {
        std::printf("ECHEC : la mesure n'a pas demarre.\n");
        return 1;
    }

    // LA BOUCLE SIMULÉE : ce que le moteur écrit dans la sortie revient dans
    // l'entrée `retard` échantillons plus tard. C'est exactement ce que fait un
    // câble, à ceci près qu'on connaît la réponse.
    std::deque<float> ligneARetard(static_cast<size_t>(retard), 0.0f);
    std::vector<float> sortieG(static_cast<size_t>(bloc), 0.0f);
    std::vector<float> sortieD(static_cast<size_t>(bloc), 0.0f);
    std::vector<float> entreeG(static_cast<size_t>(bloc), 0.0f);
    std::vector<float> entreeD(static_cast<size_t>(bloc), 0.0f);

    const int blocsMax = static_cast<int>(freq * 2.0) / bloc;
    for (int b = 0; b < blocsMax && moteur.latencyMeasurementRunning(); ++b) {
        std::fill(sortieG.begin(), sortieG.end(), 0.0f);
        std::fill(sortieD.begin(), sortieD.end(), 0.0f);

        const float* entrees[2] = {entreeG.data(), entreeD.data()};
        float* sorties[2] = {sortieG.data(), sortieD.data()};
        moteur.audioDeviceIOCallbackWithContext(entrees, 2, sorties, 2, bloc, {});

        // La sortie de CE bloc alimente l'entrée d'un bloc ultérieur.
        for (int i = 0; i < bloc; ++i) {
            ligneARetard.push_back(sortieG[static_cast<size_t>(i)]);
            entreeG[static_cast<size_t>(i)] = ligneARetard.front();
            entreeD[static_cast<size_t>(i)] = ligneARetard.front();
            ligneARetard.pop_front();
        }
    }

    const auto resultat = moteur.finishLatencyMeasurement();
    std::printf("Aller-retour impose : %d echantillons (%.2f ms) = %d de ligne + %d de bloc\n",
                allerRetour, allerRetour * 1000.0 / freq, retard, bloc);
    if (!resultat.trouve()) {
        std::printf("ECHEC : rien n'a ete retrouve.\n");
        return 1;
    }
    std::printf("Aller-retour trouve  : %d echantillons (%.2f ms), nettete %.1f\n",
                resultat.decalageEchantillons, resultat.decalageEchantillons * 1000.0 / freq,
                resultat.nettete);

    if (resultat.nettete < 10.0) {
        std::printf("ECHEC : pic trop peu net pour etre publie.\n");
        return 1;
    }
    if (resultat.decalageEchantillons != allerRetour) {
        std::printf("ECHEC : ecart de %d echantillon(s) -- le critere de D3.6 exige "
                     "l'echantillon pres.\n",
                     resultat.decalageEchantillons - allerRetour);
        return 1;
    }
    std::printf("OK : l'aller-retour est retrouve a l'echantillon pres.\n");
    return 0;
}
