#pragma once
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// LES PIÈCES DE BATTERIE DES MACHINES DU PARC, et ce qu'il faut pour passer de l'une à l'autre.
//
// D109 : LE NOM DE LA PIÈCE QU'UNE NOTE DÉCLENCHE. Les tables suivent les en-têtes des machines (TR808Synth.h,
// TR909Synth.h, DrumsSynth.h, FmDrumsSynth.h, PercSynth.h) ; une machine inconnue retombe sur General MIDI, la convention
// des projets reconstruits. D610 : elles passent au CŒUR (elles vivaient dans `app/Source/ui`), parce que la règle qui
// adapte une partie General MIDI à une machine s'y teste ; l'application garde ses noms d'écran par un en-tête qui
// renvoie ici — un nom d'écran ne dépend toujours d'aucune interface audio.

namespace vsm::sequencer {

/// Rend une chaîne vide quand la note ne déclenche rien de nommé.
inline std::string drumVoiceName(const std::string& instrumentId, uint8_t note) {
    struct Entry { uint8_t note; const char* name; };
    static const Entry tr808[] = {{36, "grosse caisse"}, {38, "caisse claire"}, {39, "clap"},
                                  {42, "charleston fermé"}, {46, "charleston ouvert"}, {56, "cowbell"}};
    static const Entry tr909[] = {{36, "grosse caisse"}, {38, "caisse claire"}, {39, "clap"},
                                  {42, "charleston fermé"}, {46, "charleston ouvert"}, {49, "crash"},
                                  {45, "tom grave"}, {47, "tom médium"}, {50, "tom aigu"}};
    static const Entry drums[] = {{36, "grosse caisse"}, {38, "caisse claire"}, {41, "tom grave"},
                                  {45, "tom médium"}, {48, "tom aigu"}, {42, "charleston fermé"},
                                  {44, "charleston pédale"}, {46, "charleston ouvert"}, {49, "crash"},
                                  {51, "ride"}};
    static const Entry fmdrums[] = {{36, "grosse caisse"}, {38, "caisse claire"}, {39, "clap"},
                                    {42, "charleston fermé"}, {46, "charleston ouvert"}, {45, "tom"},
                                    {49, "cloche"}};
    static const Entry perc[] = {{54, "tambourin"}, {56, "cowbell"}, {60, "bongo aigu"}, {61, "bongo grave"},
                                 {62, "conga aiguë étouffée"}, {63, "conga aiguë"}, {64, "conga grave"},
                                 {65, "timbale aiguë"}, {66, "timbale grave"}, {70, "maracas"},
                                 {75, "claves"}, {76, "wood block aigu"}, {77, "wood block grave"}};
    // General MIDI (canal 10) : le repli, et la convention des projets reconstruits.
    static const Entry gm[] = {{35, "grosse caisse 2"}, {36, "grosse caisse"}, {37, "rim"},
                               {38, "caisse claire"}, {39, "clap"}, {40, "caisse claire 2"},
                               {41, "tom très grave"}, {42, "charleston fermé"}, {43, "tom grave"},
                               {44, "charleston pédale"}, {45, "tom médium grave"}, {46, "charleston ouvert"},
                               {47, "tom médium"}, {48, "tom aigu"}, {49, "crash"}, {50, "tom très aigu"},
                               {51, "ride"}, {52, "china"}, {53, "cloche de ride"}, {54, "tambourin"},
                               {55, "splash"}, {56, "cowbell"}, {57, "crash 2"}, {59, "ride 2"},
                               {60, "bongo aigu"}, {61, "bongo grave"}, {62, "conga aiguë étouffée"},
                               {63, "conga aiguë"}, {64, "conga grave"}, {65, "timbale aiguë"},
                               {66, "timbale grave"}, {70, "maracas"}, {75, "claves"},
                               {76, "wood block aigu"}, {77, "wood block grave"}};
    const Entry* table = gm;
    size_t count = sizeof(gm) / sizeof(gm[0]);
    if (instrumentId == "vsm.tr808")        { table = tr808;   count = sizeof(tr808) / sizeof(tr808[0]); }
    else if (instrumentId == "vsm.tr909")   { table = tr909;   count = sizeof(tr909) / sizeof(tr909[0]); }
    else if (instrumentId == "vsm.drums")   { table = drums;   count = sizeof(drums) / sizeof(drums[0]); }
    else if (instrumentId == "vsm.fmdrums") { table = fmdrums; count = sizeof(fmdrums) / sizeof(fmdrums[0]); }
    else if (instrumentId == "vsm.perc")    { table = perc;    count = sizeof(perc) / sizeof(perc[0]); }
    for (size_t i = 0; i < count; ++i)
        if (table[i].note == note) return table[i].name;
    return {};
}

/// D610.1 — LA CATÉGORIE D'UNE PIÈCE, lue à son NOM (dans n'importe laquelle des tables) : une table de correspondances
/// écrite à la main par machine serait une seconde vérité à côté des noms.
enum class DrumCategory {
    None, Kick, Snare, Rim, Clap, HatClosed, HatOpen, Tom, Crash, Ride, Cowbell, Bell, Tambourine, Bongo, Conga, Timbale,
    Maracas, Claves, WoodBlock, Other
};

inline DrumCategory drumCategoryOf(const std::string& nom) {
    const auto commence = [&nom](const char* p) { return nom.rfind(p, 0) == 0; };
    const auto contient = [&nom](const char* p) { return nom.find(p) != std::string::npos; };
    if (nom.empty()) return DrumCategory::None;
    if (commence("grosse caisse")) return DrumCategory::Kick;
    if (commence("caisse claire")) return DrumCategory::Snare;
    if (commence("rim")) return DrumCategory::Rim;
    if (commence("clap")) return DrumCategory::Clap;
    if (commence("charleston fermé") || commence("charleston pédale")) return DrumCategory::HatClosed;
    if (commence("charleston ouvert")) return DrumCategory::HatOpen;
    if (commence("tom")) return DrumCategory::Tom;
    if (contient("crash") || contient("splash") || contient("china")) return DrumCategory::Crash;
    if (contient("ride")) return DrumCategory::Ride;   // « cloche de ride » compris, AVANT « cloche »
    if (commence("cowbell")) return DrumCategory::Cowbell;
    if (commence("cloche")) return DrumCategory::Bell;
    if (commence("tambourin")) return DrumCategory::Tambourine;
    if (commence("bongo")) return DrumCategory::Bongo;
    if (commence("conga")) return DrumCategory::Conga;
    if (commence("timbale")) return DrumCategory::Timbale;
    if (commence("maracas")) return DrumCategory::Maracas;
    if (commence("claves")) return DrumCategory::Claves;
    if (commence("wood block")) return DrumCategory::WoodBlock;
    return DrumCategory::Other;
}

/// La catégorie General MIDI d'une note (la table de repli : un identifiant de machine vide).
inline DrumCategory generalMidiCategory(uint8_t note) { return drumCategoryOf(drumVoiceName(std::string(), note)); }

/// Vrai si `jouee`, sur la machine, est une pièce de la MÊME catégorie que la pièce General MIDI `ecrite` — ce que
/// la mesure de D610 compte (« sonne dans sa catégorie »).
inline bool soundsInCategory(const std::string& instrumentId, uint8_t ecrite, uint8_t jouee) {
    const DrumCategory c = generalMidiCategory(ecrite);
    return c != DrumCategory::None && drumCategoryOf(drumVoiceName(instrumentId, jouee)) == c;
}

/// D610.1 — ADAPTER DES NOTES GENERAL MIDI À LA MACHINE. Pour chaque note écrite qui est une pièce General MIDI : si la
/// machine la nomme dans la même catégorie, rien ; sinon la pièce de la même catégorie la plus proche en hauteur (à
/// égalité, la plus grave) ; aucune : `sansEquivalent`. Les notes qui ne sont pas des pièces General MIDI ne sont pas
/// touchées.
struct DrumAdaptation {
    std::map<uint8_t, uint8_t> entries;      ///< note écrite → note jouée, les seules qui bougent
    std::vector<uint8_t> sansEquivalent;     ///< pièces General MIDI que la machine n'a dans aucune catégorie voisine
};

inline DrumAdaptation adaptGeneralMidi(const std::string& instrumentId, const std::vector<uint8_t>& ecrites) {
    DrumAdaptation r;
    for (uint8_t g : ecrites) {
        const DrumCategory c = generalMidiCategory(g);
        if (c == DrumCategory::None || soundsInCategory(instrumentId, g, g)) continue;
        int meilleure = -1;
        for (int n = 0; n < 128; ++n) {
            if (drumCategoryOf(drumVoiceName(instrumentId, static_cast<uint8_t>(n))) != c) continue;
            if (meilleure < 0 || std::abs(n - g) < std::abs(meilleure - g)) meilleure = n;   // à égalité : la plus grave
        }
        if (meilleure < 0) r.sansEquivalent.push_back(g);
        else r.entries[g] = static_cast<uint8_t>(meilleure);
    }
    return r;
}

} // namespace vsm::sequencer
