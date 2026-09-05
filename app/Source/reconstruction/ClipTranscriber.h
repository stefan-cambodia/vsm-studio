#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// D20.4 de docs/ROADMAP-daw.md — TRANSCRIRE UN CLIP AUDIO EN MIDI.
//
// D9 reconstruit un FICHIER entier par la chaîne d'analyse ; ici, c'est le
// geste de Live (« Convert to MIDI ») : le clip qu'on a sous la souris devient
// des notes, sur une piste neuve. Le transcripteur est le même que celui de la
// chaîne (`analyse/transcrire_clip.py`, qui réemploie `extraire_notes`), lancé
// dans un PROCESSUS ENFANT par le même interpréteur que D9 a trouvé : Basic
// Pitch met plusieurs secondes à charger, et l'interface ne doit pas les
// attendre.
//
// Ce que cette classe fait : lancer la commande, lire ce qu'elle dit, et
// rapporter -- succès ou échec, avec le journal -- sur le thread de message.
// Ce qu'elle ne fait pas : lire le JSON et poser les notes ; c'est
// l'application qui connaît le projet.

namespace vsm::app {

class ClipTranscriber : private juce::Thread {
public:
    ClipTranscriber();
    ~ClipTranscriber() override;

    /// Lance `commande` ; `sortieJson` est le fichier que le script doit
    /// écrire, et son existence au retour est le critère de succès -- un code
    /// de sortie nul sans fichier est un échec qui se dit.
    void start(const juce::StringArray& commande, const juce::File& sortieJson);
    void cancel();
    bool isRunning() const { return isThreadRunning(); }

    /// (succès, fichier JSON, journal du processus). Sur le thread de message.
    std::function<void(bool, juce::File, juce::String)> onFinished;

private:
    void run() override;

    juce::StringArray commande_;
    juce::File sortie_;
    std::mutex mutex_;
    std::unique_ptr<juce::ChildProcess> process_;
    std::atomic<bool> cancelled_{false};
};

} // namespace vsm::app
