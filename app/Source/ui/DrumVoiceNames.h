#pragma once
#include <cstdint>
#include <string>

namespace vsm::app::ui {

/// LE NOM DE LA PIÈCE QU'UNE NOTE DÉCLENCHE sur une piste de batterie.
///
/// Le piano roll nommait toutes les touches par leur hauteur (« F#2 ») ; sur
/// une piste de batterie, un musicien attend « charleston fermé ». Les tables
/// suivent les en-têtes des machines du parc (TR808Synth.h, TR909Synth.h,
/// DrumsSynth.h, FmDrumsSynth.h, PercSynth.h) -- lues ici plutôt qu'exposées
/// par le moteur, pour ne pas faire dépendre une étiquette d'écran d'une
/// interface audio. Une machine inconnue retombe sur la convention General
/// MIDI, qui est aussi celle des projets reconstruits.
///
/// Rend une chaîne vide quand la note ne déclenche rien de nommé : le piano
/// roll retombe alors sur le nom de la hauteur.
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

} // namespace vsm::app::ui
