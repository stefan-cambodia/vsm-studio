#include "DiskRecorder.h"

namespace {
/// Une seconde de tampon entre le thread audio et le disque. C'est beaucoup
/// plus que ce qu'il faut en régime normal, et c'est délibéré : le tampon
/// n'existe que pour absorber les à-coups du système de fichiers, qui se
/// comptent en centaines de millisecondes quand ils arrivent.
constexpr double kSecondesDeTampon = 1.0;
}

DiskRecorder::DiskRecorder() {
    thread_.startThread(juce::Thread::Priority::normal);
}

DiskRecorder::~DiskRecorder() {
    stop();
    thread_.stopThread(2000);
}

bool DiskRecorder::start(const juce::File& fichier, double sampleRate, int channels,
                          juce::String& erreur) {
    stop();
    erreur.clear();

    if (sampleRate <= 0.0 || channels <= 0) {
        erreur = "Frequence ou nombre de canaux invalide.";
        return false;
    }
    fichier.getParentDirectory().createDirectory();
    // Le fichier est REMPLACÉ s'il existe : l'appelant choisit un nom libre,
    // et se retrouver à ajouter des échantillons à la fin d'une prise
    // précédente serait pire que de l'écraser.
    fichier.deleteFile();

    auto flux = std::unique_ptr<juce::FileOutputStream>(fichier.createOutputStream());
    if (flux == nullptr) {
        erreur = "Impossible d'ecrire " + fichier.getFullPathName();
        return false;
    }

    // WAV 24 BITS À LA FRÉQUENCE DE LA CARTE. Vingt-quatre bits parce qu'une
    // prise se retouche -- on lui appliquera du gain, un fondu, un
    // rééchantillonnage -- et que seize bits ne laissent pas la marge pour ça.
    // La fréquence est celle de la CARTE et non 48 kHz en dur : rééchantillonner
    // à l'écriture altérerait l'enregistrement avant même qu'on l'écoute.
    juce::WavAudioFormat format;
    auto* redacteur = format.createWriterFor(flux.get(), sampleRate,
                                              static_cast<unsigned int>(channels), 24, {}, 0);
    if (redacteur == nullptr) {
        erreur = "Format WAV refuse pour " + juce::String(channels) + " canal/canaux a "
                 + juce::String(sampleRate, 0) + " Hz.";
        return false;
    }
    flux.release(); // le rédacteur possède désormais le flux

    const int tampon = juce::jmax(8192, static_cast<int>(sampleRate * kSecondesDeTampon));
    auto file = std::make_shared<juce::AudioFormatWriter::ThreadedWriter>(redacteur, thread_, tampon);

    file_ = fichier;
    channels_.store(channels, std::memory_order_release);
    sampleRate_.store(sampleRate, std::memory_order_release);
    framesWritten_.store(0, std::memory_order_relaxed);
    droppedBlocks_.store(0, std::memory_order_relaxed);
    writer_.store(std::move(file), std::memory_order_release);
    return true;
}

int64_t DiskRecorder::stop() {
    // Le rédacteur est d'abord RETIRÉ du chemin audio, puis relâché. Le thread
    // audio qui en tenait une copie la relâchera à la fin de son bloc ; la
    // destruction n'a donc lieu qu'une fois que plus personne ne s'en sert, et
    // sans que qui que ce soit ait eu à attendre.
    auto ancien = writer_.exchange(nullptr, std::memory_order_acq_rel);
    // AUCUNE PRISE EN COURS : on rend zéro, et surtout pas le compte de la
    // PRÉCÉDENTE. Un appelant qui arrête un enregistrement purement MIDI
    // croirait sinon avoir capté de l'audio.
    if (!ancien) return 0;
    ancien.reset();   // vide le tampon sur le disque et ferme le fichier
    return framesWritten_.load(std::memory_order_relaxed);
}

bool DiskRecorder::write(const float* const* data, int numSamples) {
    auto redacteur = writer_.load(std::memory_order_acquire);
    if (!redacteur || numSamples <= 0) return true;   // rien à écrire n'est pas un échec
    if (!redacteur->write(data, numSamples)) {
        droppedBlocks_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    framesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
    return true;
}
