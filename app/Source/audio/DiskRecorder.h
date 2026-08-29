#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>

/// L'ÉCRITURE D'UNE PRISE AUDIO SUR LE DISQUE, PENDANT QUE ÇA JOUE (D3.4).
///
/// LE PROBLÈME, ET IL N'EST PAS ANODIN. Le rappel audio reçoit les échantillons
/// d'entrée et doit rendre la main en quelques millisecondes ; écrire un
/// fichier depuis là -- un `write()` système, une allocation, l'attente d'un
/// disque -- produirait des craquements à la première hésitation du système de
/// fichiers. Un enregistrement de dix minutes ne peut pas dépendre de la bonne
/// humeur du noyau.
///
/// La séparation est donc stricte : le thread audio ne fait que DÉPOSER ses
/// blocs dans une file, et un thread de fond les écrit. C'est exactement ce que
/// fait `juce::AudioFormatWriter::ThreadedWriter`, qui est employé ici plutôt
/// que réécrit -- il fait partie de JUCE, il est éprouvé, et le § 0 de
/// `ROADMAP-daw.md` n'interdit que les dépendances à TÉLÉCHARGER.
///
/// CE QUI EST À NOUS, en revanche, c'est la façon dont le thread audio accède
/// au rédacteur. Le canevas fourni par JUCE prend un verrou dans le rappel
/// audio ; ce projet ne s'y autorise nulle part. On publie donc le rédacteur
/// par un `std::atomic<std::shared_ptr<>>`, comme le graphe le fait pour ses
/// instruments et ses chaînes d'effets : le thread audio en prend une copie qui
/// le maintient en vie le temps de l'appel, et l'arrêt n'a besoin d'attendre
/// personne.
class DiskRecorder {
public:
    DiskRecorder();
    ~DiskRecorder();

    DiskRecorder(const DiskRecorder&) = delete;
    DiskRecorder& operator=(const DiskRecorder&) = delete;

    /// Thread UI. Ouvre le fichier et démarre l'écriture de fond. Rend faux et
    /// remplit `erreur` si le fichier ne peut pas être créé -- une prise qui
    /// n'a nulle part où aller doit se savoir AVANT qu'on joue, pas après.
    bool start(const juce::File& fichier, double sampleRate, int channels, juce::String& erreur);

    /// Thread UI. Ferme le fichier et attend que tout soit sur le disque.
    /// Rend le nombre de trames réellement écrites.
    int64_t stop();

    bool isRecording() const { return writer_.load(std::memory_order_acquire) != nullptr; }

    /// THREAD AUDIO. Dépose un bloc dans la file. Renvoie faux si elle est
    /// pleine -- ce qui veut dire que le disque n'a pas suivi, et que le
    /// fichier a un trou. Ce n'est pas rattrapable ici, mais c'est COMPTÉ.
    bool write(const float* const* data, int numSamples);

    /// Nombre de canaux ouverts. Le rappel audio doit en fournir exactement
    /// autant : `ThreadedWriter::write` n'accepte pas de canal manquant.
    int channels() const { return channels_.load(std::memory_order_acquire); }
    double sampleRate() const { return sampleRate_.load(std::memory_order_acquire); }
    int64_t framesWritten() const { return framesWritten_.load(std::memory_order_relaxed); }
    /// Blocs perdus faute de place dans la file : un trou dans le fichier.
    /// Doit rester à zéro sur une machine saine.
    uint64_t droppedBlocks() const { return droppedBlocks_.load(std::memory_order_relaxed); }
    juce::File file() const { return file_; }

private:
    /// Le thread d'écriture, démarré une fois et gardé en vie : le lancer au
    /// début de chaque prise ferait payer la création du thread juste au
    /// moment où l'on a le moins de marge.
    juce::TimeSliceThread thread_ { "VSM ecriture disque" };
    std::atomic<std::shared_ptr<juce::AudioFormatWriter::ThreadedWriter>> writer_{nullptr};
    std::atomic<int> channels_{0};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int64_t> framesWritten_{0};
    std::atomic<uint64_t> droppedBlocks_{0};
    juce::File file_;
};
