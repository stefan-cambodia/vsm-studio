#include "ClipTranscriber.h"

namespace vsm::app {

ClipTranscriber::ClipTranscriber() : juce::Thread("transcription-clip") {}

ClipTranscriber::~ClipTranscriber() {
    cancel();
    stopThread(4000);
}

void ClipTranscriber::start(const juce::StringArray& commande, const juce::File& sortieJson) {
    if (isThreadRunning()) return;
    commande_ = commande;
    sortie_ = sortieJson;
    cancelled_.store(false);
    startThread();
}

void ClipTranscriber::cancel() {
    cancelled_.store(true);
    std::lock_guard<std::mutex> verrou(mutex_);
    if (process_) process_->kill();
}

void ClipTranscriber::run() {
    auto enfant = std::make_unique<juce::ChildProcess>();
    const bool demarre = enfant->start(commande_, juce::ChildProcess::wantStdOut
                                                   | juce::ChildProcess::wantStdErr);
    if (!demarre) {
        juce::MessageManager::callAsync([this] {
            if (onFinished)
                onFinished(false, juce::File(),
                            juce::String::fromUTF8(u8"le transcripteur n'a pas pu être lancé"));
        });
        return;
    }
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        process_ = std::move(enfant);
    }
    // LE JOURNAL ENTIER EST GARDÉ : c'est ce qu'on montre quand ça échoue, et
    // une transcription qui échoue sans dire pourquoi serait une panne muette.
    juce::String journal;
    char tampon[4096];
    while (!threadShouldExit()) {
        juce::ChildProcess* processus = nullptr;
        {
            std::lock_guard<std::mutex> verrou(mutex_);
            processus = process_.get();
        }
        if (processus == nullptr) break;
        const int lus = processus->readProcessOutput(tampon, sizeof(tampon));
        if (lus > 0) {
            journal += juce::String::fromUTF8(tampon, lus);
            continue;
        }
        if (!processus->isRunning()) break;
        wait(50);
    }
    bool succes = false;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        if (process_) {
            const auto code = process_->getExitCode();
            succes = !cancelled_.load() && code == 0 && sortie_.existsAsFile();
            if (code == 0 && !sortie_.existsAsFile() && !cancelled_.load())
                journal += juce::String::fromUTF8(u8"\n[le script est sorti en 0 sans écrire ")
                           + sortie_.getFullPathName() + "]";
            process_.reset();
        }
    }
    const juce::File sortie = sortie_;
    juce::MessageManager::callAsync([this, succes, sortie, journal] {
        if (onFinished) onFinished(succes, sortie, journal);
    });
}

} // namespace vsm::app
