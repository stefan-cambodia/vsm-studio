#include "UiScale.h"

namespace vsm::app::ui {

namespace {
constexpr const char* kSettingsFile = "VintageSynthMidiStudio";
constexpr const char* kKey          = "uiScale";
// 150 % PAR DÉFAUT, et ce n'est pas un chiffre choisi au jugé : les tailles
// d'origine (9 à 12 points) se sont révélées trop petites à lire à l'usage,
// et 150 % est le palier retenu après comparaison de rendus côte à côte. Un
// réglage d'accessibilité qu'il faut d'abord aller chercher dans un menu ne
// sert pas celui qui en a besoin.
constexpr float       kDefault      = 1.5f;
} // namespace

const juce::Array<float>& UiScale::steps() {
    static const juce::Array<float> paliers { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    return paliers;
}

juce::String UiScale::label(float factor) {
    return juce::String(juce::roundToInt(factor * 100.0f)) + " %";
}

juce::PropertiesFile& UiScale::properties() {
    // Un fichier de réglages propre à l'application, à l'emplacement que le
    // système réserve à cet usage. Il ne contient QUE des préférences
    // d'affichage : rien de ce qui touche à un projet ne passe par ici.
    static std::unique_ptr<juce::PropertiesFile> fichier = [] {
        juce::PropertiesFile::Options options;
        options.applicationName     = kSettingsFile;
        options.filenameSuffix      = "settings";
        options.folderName          = kSettingsFile;
        options.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile>(options);
    }();
    return *fichier;
}

float UiScale::current() {
    const float enregistre =
        static_cast<float>(properties().getDoubleValue(kKey, kDefault));
    // Borné aux paliers connus : un fichier de réglages édité à la main ne
    // doit pas pouvoir rendre l'application illisible ou inutilisable.
    return juce::jlimit(steps().getFirst(), steps().getLast(), enregistre);
}

void UiScale::apply(float factor) {
    const float borne = juce::jlimit(steps().getFirst(), steps().getLast(), factor);
    juce::Desktop::getInstance().setGlobalScaleFactor(borne);
    properties().setValue(kKey, static_cast<double>(borne));
    // Écrit TOUT DE SUITE, sans attendre la fermeture : une application qui
    // se termine mal ne doit pas faire perdre le réglage.
    properties().saveIfNeeded();
}

void UiScale::applySavedAtStartup() {
    juce::Desktop::getInstance().setGlobalScaleFactor(current());
}

} // namespace vsm::app::ui
