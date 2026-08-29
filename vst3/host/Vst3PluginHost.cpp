#include "Vst3PluginHost.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/plugin/PluginRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

// L'HÔTE VST3 DE JUCE EST OPTIONNEL À LA COMPILATION, et désactivé par défaut.
// Sans cette définition, `VST3PluginFormat` existe toujours et ne trouve jamais
// rien : la pire des pannes, silencieuse. On refuse de compiler plutôt que de
// livrer un hôte qui ne dira jamais pourquoi il ne charge personne.
#if ! JUCE_PLUGINHOST_VST3
#error "JUCE_PLUGINHOST_VST3 doit valoir 1 : sans lui, l'hote VST3 ne trouve jamais aucun plugin."
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace vsm::vst3 {
namespace {

using vsm::audio::plugin::MidiControlEvent;
using vsm::audio::plugin::MidiNoteEvent;
using vsm::audio::plugin::ParameterInfo;
using vsm::audio::plugin::ParameterList;
using vsm::audio::plugin::ParamId;
using vsm::audio::plugin::PresetState;

constexpr const char* kPrefix = "vst3:";

/// JUCE INITIALISÉ UNE FOIS, ET JAMAIS DÉTRUIT. Charger un module VST3 fait
/// tourner du code d'initialisation qui, sur toutes les plateformes, s'attend à
/// trouver une boucle de messages en place -- y compris quand l'hôte est un
/// outil en ligne de commande sans la moindre fenêtre.
///
/// LA FUITE EST DÉLIBÉRÉE. Détruire ce singleton à la fin du programme le
/// ferait passer APRÈS les objets statiques d'un plugin tiers dans un ordre que
/// personne ne contrôle, et un plugin qui parle à un gestionnaire de messages
/// déjà mort fait tomber le processus à la sortie -- une panne qui n'apparaît
/// qu'au dernier instant et qu'on impute au mauvais coupable. Un objet vivant
/// jusqu'à la fin du processus ne coûte rien et supprime la classe entière.
void ensureJuceIsUp() {
    static std::once_flag une_fois;
    std::call_once(une_fois, [] { new juce::ScopedJuceInitialiser_GUI(); });
}

juce::VST3PluginFormat& format() {
    static juce::VST3PluginFormat format;
    return format;
}

/// L'identifiant d'un plugin DANS son fichier. Un `.vst3` peut en contenir
/// plusieurs, et `uniqueId` est le seul champ que le format garantit stable
/// d'une version à l'autre -- le nom, lui, change au gré du marketing.
std::string identifiantDe(const juce::PluginDescription& description) {
    return std::to_string(description.uniqueId);
}

/// Un instrument VST3 présenté comme une machine VSM.
class Vst3Instrument final : public vsm::audio::plugin::ISynthPlugin {
public:
    Vst3Instrument(std::unique_ptr<juce::AudioPluginInstance> instance, std::string nom)
        : instance_(std::move(instance)), nom_(std::move(nom)) {}

    ~Vst3Instrument() override {
        if (instance_) instance_->releaseResources();
    }

    void initialize(double sampleRate, int maxBlockSize) override {
        if (!instance_) return;
        maxBloc_ = std::max(1, maxBlockSize);

        // TOUT CE QUI ALLOUE SE FAIT ICI. `process()` tourne sur le thread
        // audio : le tampon, la file MIDI et la liste de paramètres y sont
        // interdits d'agrandissement.
        const int sorties = std::max(2, instance_->getTotalNumOutputChannels());
        const int entrees = instance_->getTotalNumInputChannels();
        tampon_.setSize(std::max(sorties, entrees), maxBloc_, false, true, false);
        midi_.ensureSize(8192);

        instance_->setNonRealtime(false);
        instance_->setRateAndBufferSizeDetails(sampleRate, maxBloc_);
        instance_->prepareToPlay(sampleRate, maxBloc_);

        parametres_.clear();
        const auto& params = instance_->getParameters();
        parametres_.reserve(static_cast<size_t>(params.size()));
        for (int i = 0; i < params.size(); ++i) {
            ParameterInfo info;
            // L'INDICE FAIT OFFICE DE ParamId. Le VST3 a des identifiants
            // propres, mais `ISynthPlugin` parle en `uint32_t` et l'automation
            // du projet s'écrit en identités SÉMANTIQUES, qu'un plugin tiers
            // n'a pas : rien ici n'est écrit dans un fichier, et l'état qui
            // l'est passe par `saveNativeState()`.
            info.id = static_cast<ParamId>(i);
            info.name = params[i]->getName(64).toStdString();
            info.minValue = 0.0f;
            info.maxValue = 1.0f;   // un paramètre VST3 est toujours normalisé
            info.defaultValue = params[i]->getDefaultValue();
            info.unit = params[i]->getLabel().toStdString();
            parametres_.push_back(std::move(info));
        }
    }

    void process(const MidiNoteEvent* events, int numEvents,
                  float* outputL, float* outputR, int numSamples) override {
        if (!instance_ || numSamples <= 0) return;
        if (numSamples > maxBloc_) {
            // PLUS GRAND QUE CE QU'ON A PRÉPARÉ : on rend du silence plutôt que
            // d'agrandir un tampon sur le thread audio. Le graphe ne dépasse
            // jamais la taille annoncée à `initialize` ; si cela arrivait, un
            // silence audible vaut mieux qu'une allocation.
            std::fill(outputL, outputL + numSamples, 0.0f);
            std::fill(outputR, outputR + numSamples, 0.0f);
            return;
        }

        midi_.clear();
        // LES CONTRÔLES D'ABORD, dans l'ordre où ils sont arrivés : le graphe
        // les livre AVANT le bloc auquel ils s'appliquent (voir
        // `handleControlEvent`), et un pitch bend qui arriverait après sa note
        // s'entendrait comme un glissando d'un bloc.
        for (size_t i = 0; i < enAttente_; ++i)
            midi_.addEvent(controles_[i].message, controles_[i].offset);
        enAttente_ = 0;

        for (int i = 0; i < numEvents; ++i) {
            const auto& e = events[i];
            const int canal = static_cast<int>(e.channel) + 1; // JUCE compte de 1
            const float velocite = static_cast<float>(e.velocity) / 127.0f;
            midi_.addEvent(e.kind == MidiNoteEvent::Kind::NoteOn
                               ? juce::MidiMessage::noteOn(canal, e.note, velocite)
                               : juce::MidiMessage::noteOff(canal, e.note, velocite),
                           e.sampleOffset);
        }

        // `setSize` avec `avoidReallocating` : la taille descend, la mémoire
        // reste. Rien n'est demandé au système.
        tampon_.setSize(tampon_.getNumChannels(), numSamples, false, false, true);
        tampon_.clear();
        instance_->processBlock(tampon_, midi_);

        const int canaux = tampon_.getNumChannels();
        std::copy_n(tampon_.getReadPointer(0), numSamples, outputL);
        // UN INSTRUMENT MONO SE POSE AU MILIEU, il ne joue pas à gauche. C'est
        // ce que fait le mixeur pour une piste mono, et c'est ce qu'attend
        // quelqu'un qui charge un vieux monosynthé.
        std::copy_n(tampon_.getReadPointer(canaux > 1 ? 1 : 0), numSamples, outputR);
    }

    bool handleControlEvent(const MidiControlEvent& event) override {
        if (!instance_) return false;
        // MIS DE CÔTÉ, PAS ENVOYÉ : un plugin VST3 ne reçoit du MIDI que dans
        // `processBlock`. Le graphe livre les contrôles juste avant le bloc
        // qu'ils concernent, si bien que les garder ici ne retarde rien.
        if (enAttente_ >= controles_.size()) return false; // plein : compté comme ignoré

        const int canal = static_cast<int>(event.channel) + 1;
        juce::MidiMessage message;
        switch (event.kind) {
            case MidiControlEvent::Kind::PitchBend: {
                // L'UNITÉ DE VSM EST LE DEMI-TON, celle du câble est un entier
                // 14 bits. La conversion suppose la plage de deux demi-tons que
                // le moteur applique déjà partout ailleurs (voir
                // `kPitchBendRangeSemitones`) : en supposer une autre ferait
                // transposer les plugins tiers autrement que les machines.
                const float fraction = std::clamp(event.value / 2.0f, -1.0f, 1.0f);
                message = juce::MidiMessage::pitchWheel(
                    canal, static_cast<int>(std::lround(8192.0f + fraction * 8191.0f)));
                break;
            }
            case MidiControlEvent::Kind::ControlChange:
                message = juce::MidiMessage::controllerEvent(
                    canal, event.index, static_cast<int>(std::lround(event.value * 127.0f)));
                break;
            case MidiControlEvent::Kind::ChannelPressure:
                message = juce::MidiMessage::channelPressureChange(
                    canal, static_cast<int>(std::lround(event.value * 127.0f)));
                break;
            case MidiControlEvent::Kind::PolyPressure:
                message = juce::MidiMessage::aftertouchChange(
                    canal, event.index, static_cast<int>(std::lround(event.value * 127.0f)));
                break;
            case MidiControlEvent::Kind::ProgramChange:
                message = juce::MidiMessage::programChange(canal, event.index);
                break;
        }
        controles_[enAttente_++] = {message, event.sampleOffset};
        return true;
    }

    void setParameter(ParamId id, float value) override {
        if (!instance_) return;
        const auto& params = instance_->getParameters();
        if (static_cast<int>(id) >= params.size()) return;
        // `setValue` et non `setValueNotifyingHost` : la seconde prévient
        // l'hôte, et l'hôte, c'est nous -- elle nous renverrait notre propre
        // écriture, ce qui, appelée depuis le thread audio, est exactement ce
        // qu'il ne faut pas faire.
        params[static_cast<int>(id)]->setValue(std::clamp(value, 0.0f, 1.0f));
    }

    float getParameter(ParamId id) const override {
        if (!instance_) return 0.0f;
        const auto& params = instance_->getParameters();
        if (static_cast<int>(id) >= params.size()) return 0.0f;
        return params[static_cast<int>(id)]->getValue();
    }

    const ParameterList& parameterList() const override { return parametres_; }

    PresetState saveState() const override {
        PresetState state;
        state.pluginTypeId = nom_;
        for (const auto& info : parametres_)
            state.parameterValues[info.id] = getParameter(info.id);
        return state;
    }

    void loadState(const PresetState& state) override {
        for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
    }

    std::string saveNativeState() const override {
        if (!instance_) return {};
        // CE QUE LA TABLE DE PARAMÈTRES NE DIT PAS. Un instrument tiers porte
        // des échantillons chargés, des matrices, des tables dessinées : son
        // état ne se réduit pas à ses paramètres automatisables, et c'est
        // exactement pourquoi `saveNativeState()` existe.
        juce::MemoryBlock octets;
        instance_->getStateInformation(octets);
        if (octets.getSize() == 0) return {};
        return juce::Base64::toBase64(octets.getData(), octets.getSize()).toStdString();
    }

    bool loadNativeState(const std::string& texte) override {
        if (!instance_ || texte.empty()) return false;
        juce::MemoryOutputStream flux;
        if (!juce::Base64::convertFromBase64(flux, juce::String(texte))) return false;
        instance_->setStateInformation(flux.getData(), static_cast<int>(flux.getDataSize()));
        return true;
    }

    const char* machineName() const override { return nom_.c_str(); }

    /// ON NE SAIT PAS, ET ON NE FAIT PAS SEMBLANT. Aucun format de plugin ne
    /// publie son nombre de voix actives ; rendre un chiffre inventé ferait
    /// mentir l'affichage de charge, qui existe pour dire quand une machine
    /// sature. Zéro se lit comme « pas d'information », un chiffre faux non.
    int activeVoiceCount() const override { return 0; }

    int latencySamples() const override {
        return instance_ ? instance_->getLatencySamples() : 0;
    }

private:
    struct ControleEnAttente {
        juce::MidiMessage message;
        int offset = 0;
    };

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::string nom_;
    juce::AudioBuffer<float> tampon_;
    juce::MidiBuffer midi_;
    ParameterList parametres_;
    int maxBloc_ = 512;
    /// Assez pour une rafale de contrôleurs sur un sous-bloc d'automation
    /// (~1,3 ms) ; au-delà, `handleControlEvent` rend faux et le moteur COMPTE
    /// l'événement comme ignoré, ce qui est déjà son mécanisme de rapport.
    std::array<ControleEnAttente, 256> controles_{};
    size_t enAttente_ = 0;
};


/// Un effet VST3 présenté comme un insert VSM.
///
/// CE QUI LE DISTINGUE DE `Vst3Instrument` EST L'ENTRÉE, et c'est tout le sujet
/// de D7.3. Le tampon qu'on donne au plugin est REMPLI du signal de la piste
/// avant l'appel, et relu après : un effet qui recevrait du silence rendrait du
/// silence, ce qui est exactement la panne qu'un hôte sans entrées produit.
class Vst3Effect final : public vsm::audio::effect::IAudioEffect {
public:
    Vst3Effect(std::unique_ptr<juce::AudioPluginInstance> instance, std::string nom)
        : instance_(std::move(instance)), nom_(std::move(nom)) {}

    ~Vst3Effect() override {
        if (instance_) instance_->releaseResources();
    }

    void prepare(double sampleRate, int maxBlockSize) override {
        if (!instance_) return;
        maxBloc_ = std::max(1, maxBlockSize);

        // ASSEZ DE CANAUX POUR L'ENTRÉE **ET** LA SORTIE. JUCE traite en place :
        // un même tampon porte les deux, et le dimensionner sur la seule sortie
        // tronquerait l'entrée d'un plugin qui en demande plus (une chaîne
        // latérale, par exemple).
        const int canaux = std::max({2, instance_->getTotalNumInputChannels(),
                                      instance_->getTotalNumOutputChannels()});
        tampon_.setSize(canaux, maxBloc_, false, true, false);
        midi_.ensureSize(256);

        instance_->setNonRealtime(false);
        instance_->setRateAndBufferSizeDetails(sampleRate, maxBloc_);
        instance_->prepareToPlay(sampleRate, maxBloc_);

        parametres_.clear();
        const auto& params = instance_->getParameters();
        parametres_.reserve(static_cast<size_t>(params.size()));
        for (int i = 0; i < params.size(); ++i) {
            vsm::audio::plugin::ParameterInfo info;
            info.id = static_cast<ParamId>(i);
            info.name = params[i]->getName(64).toStdString();
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = params[i]->getDefaultValue();
            info.unit = params[i]->getLabel().toStdString();
            parametres_.push_back(std::move(info));
        }
    }

    void reset() override {
        if (instance_) instance_->reset();
    }

    void process(float* left, float* right, int numSamples) override {
        if (!instance_ || numSamples <= 0) return;
        if (numSamples > maxBloc_) return; // on laisse passer le signal INTACT

        tampon_.setSize(tampon_.getNumChannels(), numSamples, false, false, true);
        tampon_.clear();
        // L'ENTRÉE, LA VOICI. Un effet mono ne reçoit que le canal gauche ; le
        // faire, c'est mieux que de lui donner un canal muet et de se demander
        // ensuite pourquoi il ne réagit pas.
        std::copy_n(left, numSamples, tampon_.getWritePointer(0));
        if (tampon_.getNumChannels() > 1)
            std::copy_n(right, numSamples, tampon_.getWritePointer(1));

        midi_.clear();
        instance_->processBlock(tampon_, midi_);

        const int canaux = tampon_.getNumChannels();
        std::copy_n(tampon_.getReadPointer(0), numSamples, left);
        // UN EFFET MONO REND LE MÊME SIGNAL DES DEUX CÔTÉS plutôt que de laisser
        // la droite intacte : garder l'original à droite et le traité à gauche
        // donnerait une image stéréo que personne n'a demandée.
        std::copy_n(tampon_.getReadPointer(canaux > 1 ? 1 : 0), numSamples, right);
    }

    void setParameter(ParamId id, float value) override {
        if (!instance_) return;
        const auto& params = instance_->getParameters();
        if (static_cast<int>(id) >= params.size()) return;
        params[static_cast<int>(id)]->setValue(std::clamp(value, 0.0f, 1.0f));
    }

    float getParameter(ParamId id) const override {
        if (!instance_) return 0.0f;
        const auto& params = instance_->getParameters();
        if (static_cast<int>(id) >= params.size()) return 0.0f;
        return params[static_cast<int>(id)]->getValue();
    }

    const vsm::audio::plugin::ParameterList& parameterList() const override { return parametres_; }

    const char* effectName() const override { return nom_.c_str(); }

    /// LE RETARD EST DEMANDÉ AU PLUGIN, pas supposé nul. Sans cela, insérer un
    /// égaliseur à phase linéaire décalerait la piste sans que rien ne le dise
    /// -- exactement la panne que D4.5 a corrigée pour les effets internes.
    int latencySamples() const override {
        return instance_ ? instance_->getLatencySamples() : 0;
    }

    std::string saveNativeState() const override {
        if (!instance_) return {};
        juce::MemoryBlock octets;
        instance_->getStateInformation(octets);
        if (octets.getSize() == 0) return {};
        return juce::Base64::toBase64(octets.getData(), octets.getSize()).toStdString();
    }

    bool loadNativeState(const std::string& texte) override {
        if (!instance_ || texte.empty()) return false;
        juce::MemoryOutputStream flux;
        if (!juce::Base64::convertFromBase64(flux, juce::String(texte))) return false;
        instance_->setStateInformation(flux.getData(), static_cast<int>(flux.getDataSize()));
        return true;
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::string nom_;
    juce::AudioBuffer<float> tampon_;
    /// VIDE, ET PRÉSENT QUAND MÊME : `processBlock` en exige un, et le
    /// réallouer à chaque bloc serait une allocation sur le thread audio.
    juce::MidiBuffer midi_;
    vsm::audio::plugin::ParameterList parametres_;
    int maxBloc_ = 512;
};

} // namespace

std::vector<Vst3PluginInfo> scanVst3File(const std::string& vst3Path, std::string& outError) {
    ensureJuceIsUp();
    outError.clear();

    std::vector<Vst3PluginInfo> resultat;
    juce::OwnedArray<juce::PluginDescription> trouves;
    // `findAllTypesForFile` charge le module et l'interroge. UN FICHIER CASSÉ
    // REND UNE LISTE VIDE au lieu de faire tomber le processus : c'est la
    // promesse que doit tenir tout ce qui SCANNE, puisqu'on scanne par
    // définition des fichiers dont on ne sait rien.
    format().findAllTypesForFile(trouves, juce::String(vst3Path));
    if (trouves.isEmpty()) {
        outError = "aucun plugin VST3 dans « " + vst3Path + " »";
        return resultat;
    }

    for (const auto* description : trouves) {
        Vst3PluginInfo info;
        info.id = identifiantDe(*description);
        info.name = description->name.toStdString();
        info.vendor = description->manufacturerName.toStdString();
        info.version = description->version.toStdString();
        info.isInstrument = description->isInstrument;
        resultat.push_back(std::move(info));
    }
    return resultat;
}

vsm::audio::plugin::SynthPluginPtr createVst3Instrument(const std::string& vst3Path,
                                                         const std::string& pluginId,
                                                         std::string& outError) {
    ensureJuceIsUp();
    outError.clear();

    juce::OwnedArray<juce::PluginDescription> trouves;
    format().findAllTypesForFile(trouves, juce::String(vst3Path));
    if (trouves.isEmpty()) {
        outError = "aucun plugin VST3 dans « " + vst3Path + " »";
        return nullptr;
    }

    const juce::PluginDescription* choisi = nullptr;
    for (const auto* description : trouves) {
        if (!pluginId.empty()) {
            if (identifiantDe(*description) == pluginId) { choisi = description; break; }
            continue;
        }
        // SANS IDENTIFIANT, ON PREND LE PREMIER **INSTRUMENT**, pas le premier
        // plugin : un fichier qui commence par un effet donnerait sinon une
        // piste muette, et il faudrait le deviner à l'oreille.
        if (description->isInstrument) { choisi = description; break; }
    }
    if (choisi == nullptr) {
        outError = pluginId.empty()
                       ? "ce fichier ne contient aucun instrument VST3 (que des effets ?)"
                       : "aucun plugin d'identifiant « " + pluginId + " » dans ce fichier";
        return nullptr;
    }
    if (!choisi->isInstrument) {
        // UN EFFET N'EST PAS UN INSTRUMENT, et le poser sur une piste comme
        // s'il en était un donnerait du silence. Les effets viendront en D7.3,
        // quand l'hôte saura leur donner une entrée audio.
        outError = "« " + choisi->name.toStdString()
                   + " » est un effet, pas un instrument (voir D7.3)";
        return nullptr;
    }

    juce::String erreur;
    // La fréquence et la taille de bloc sont provisoires : `initialize()` les
    // fixera pour de bon dès que la session les connaîtra.
    auto instance = format().createInstanceFromDescription(*choisi, 48000.0, 512, erreur);
    if (!instance) {
        outError = erreur.isEmpty() ? std::string("instanciation refusée par le plugin")
                                    : erreur.toStdString();
        return nullptr;
    }
    return std::make_shared<Vst3Instrument>(std::move(instance), choisi->name.toStdString());
}

vsm::audio::effect::AudioEffectPtr createVst3Effect(const std::string& vst3Path,
                                                     const std::string& pluginId,
                                                     std::string& outError) {
    ensureJuceIsUp();
    outError.clear();

    juce::OwnedArray<juce::PluginDescription> trouves;
    format().findAllTypesForFile(trouves, juce::String(vst3Path));
    if (trouves.isEmpty()) {
        outError = "aucun plugin VST3 dans « " + vst3Path + " »";
        return nullptr;
    }

    const juce::PluginDescription* choisi = nullptr;
    for (const auto* description : trouves) {
        if (!pluginId.empty()) {
            if (identifiantDe(*description) == pluginId) { choisi = description; break; }
            continue;
        }
        // SANS IDENTIFIANT, LE PREMIER **EFFET** -- symétrique de ce que fait
        // `createVst3Instrument`, et pour la même raison : un fichier qui
        // commence par un instrument donnerait sinon un insert qui ignore le
        // signal, donc une piste muette à expliquer à l'oreille.
        if (!description->isInstrument) { choisi = description; break; }
    }
    if (choisi == nullptr) {
        outError = pluginId.empty()
                       ? "ce fichier ne contient aucun effet VST3 (que des instruments ?)"
                       : "aucun plugin d'identifiant « " + pluginId + " » dans ce fichier";
        return nullptr;
    }
    if (choisi->isInstrument) {
        outError = "« " + choisi->name.toStdString()
                   + " » est un instrument, pas un effet : il ne lirait pas le signal "
                     "de la piste";
        return nullptr;
    }

    juce::String erreur;
    auto instance = format().createInstanceFromDescription(*choisi, 48000.0, 512, erreur);
    if (!instance) {
        outError = erreur.isEmpty() ? std::string("instanciation refusée par le plugin")
                                    : erreur.toStdString();
        return nullptr;
    }
    return std::make_unique<Vst3Effect>(std::move(instance), choisi->name.toStdString());
}

std::string vst3InstrumentId(const std::string& vst3Path, const std::string& pluginId) {
    return std::string(kPrefix) + vst3Path + "#" + pluginId;
}

bool parseVst3InstrumentId(const std::string& instrumentId, std::string& outPath,
                            std::string& outPluginId) {
    const std::string prefixe = kPrefix;
    if (instrumentId.rfind(prefixe, 0) != 0) return false;
    const std::string reste = instrumentId.substr(prefixe.size());
    // Le séparateur se cherche PAR LA FIN, comme pour CLAP : un chemin peut
    // contenir un « # », un identifiant de plugin est un nombre qui n'en
    // contient pas.
    const size_t diese = reste.rfind('#');
    if (diese == std::string::npos) {
        outPath = reste;
        outPluginId.clear();
        return !outPath.empty();
    }
    outPath = reste.substr(0, diese);
    outPluginId = reste.substr(diese + 1);
    return !outPath.empty();
}

void installVst3Resolver() {
    auto& registre = vsm::audio::plugin::PluginRegistry::instance();
    // CELUI QUI ÉTAIT DÉJÀ LÀ EST GARDÉ. Le registre n'accepte qu'un
    // résolveur ; poser le nôtre en écrasant celui de CLAP ferait disparaître
    // les plugins CLAP selon l'ordre des appels, ce qui est exactement le genre
    // de règle que personne ne se rappelle et que rien ne signale.
    auto precedent = registre.externalResolver();
    registre.setExternalResolver(
        [precedent](const std::string& id) -> vsm::audio::plugin::SynthPluginPtr {
            std::string chemin, pluginId;
            if (parseVst3InstrumentId(id, chemin, pluginId)) {
                std::string erreur;
                return createVst3Instrument(chemin, pluginId, erreur);
            }
            return precedent ? precedent(id) : nullptr;
        });

    // ET LA FABRIQUE D'EFFETS (D7.3), par le même mécanisme et avec le même
    // enchaînement : un `vst3:` demandé comme insert charge le fichier et en
    // prend l'EFFET, là où le registre de machines en prend l'instrument.
    auto precedentEffet = vsm::audio::effect::EffectFactory::externalResolver();
    vsm::audio::effect::EffectFactory::setExternalResolver(
        [precedentEffet](const std::string& id) -> vsm::audio::effect::AudioEffectPtr {
            std::string chemin, pluginId;
            if (parseVst3InstrumentId(id, chemin, pluginId)) {
                std::string erreur;
                return createVst3Effect(chemin, pluginId, erreur);
            }
            return precedentEffet ? precedentEffet(id) : nullptr;
        });
}

} // namespace vsm::vst3
