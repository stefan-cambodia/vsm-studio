#include "vsm/interchange/DawImport.h"

#include "vsm/interchange/Inflate.h"
#include "vsm/interchange/NumberText.h"
#include "vsm/interchange/Xml.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace vsm::interchange {
namespace {

using vsm::sequencer::Note;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

/// La valeur d'un enfant `<Balise Value="..."/>`, la forme qu'Ableton emploie
/// partout. Rend `defaut` si l'enfant manque : les versions de Live n'écrivent
/// pas toutes les mêmes champs, et un absent est ordinaire.
std::string valeurDe(const XmlNode& parent, const std::string& balise,
                     const std::string& defaut = {}) {
    if (const XmlNode* n = parent.find(balise)) return n->attribute("Value", defaut);
    return defaut;
}

double nombre(const std::string& texte, double defaut) {
    // EN LOCALE C, TOUJOURS. Le fichier vient d'une autre machine, et un
    // `std::stod` sous une locale à virgule lirait « 0.25 » comme 0. C'est la
    // règle du dépôt sur les nombres qui traversent une frontière.
    return numberFromTextOr(texte, defaut);
}

/// La couleur d'index de Live, rendue en ARGB. Live ne stocke qu'un INDICE
/// dans sa palette ; on en fait une teinte stable plutôt que d'inventer des
/// valeurs exactes qu'on ne connaît pas.
uint32_t couleurDepuisIndice(int indice) {
    static constexpr uint32_t kPalette[] = {
        0xFFFF6B9Bu, 0xFFFFB86Bu, 0xFFFFE66Bu, 0xFFB8FF6Bu, 0xFF6BFF9Bu,
        0xFF6BFFE6u, 0xFF6BB8FFu, 0xFF9B6BFFu, 0xFFE66BFFu, 0xFFFF6BB8u,
    };
    if (indice < 0) return 0xFF6B9BFFu;
    return kPalette[static_cast<size_t>(indice) % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

/// Lit les notes d'un clip et les ajoute à la piste.
///
/// LA STRUCTURE D'UN `.als` EST PROFONDE ET CHANGE D'UNE VERSION À L'AUTRE :
/// on cherche donc les balises par DESCENDANCE (`find`) et non par un chemin
/// exact. Suivre le chemin rendrait le lecteur cassant à chaque mise à jour de
/// Live, pour un gain nul — les noms de balises, eux, sont stables depuis des
/// années.
int lireLesNotesDUnClip(const XmlNode& clip, Track& piste, uint16_t tpq) {
    const double debutDuClip = nombre(valeurDe(clip, "CurrentStart", "0"), 0.0);
    int compte = 0;

    for (const XmlNode* keyTrack : clip.findAll("KeyTrack")) {
        const std::string hauteurTexte = valeurDe(*keyTrack, "MidiKey", "");
        if (hauteurTexte.empty()) continue;
        const int hauteur = static_cast<int>(nombre(hauteurTexte, -1.0));
        if (hauteur < 0 || hauteur > 127) continue;

        for (const XmlNode* evenement : keyTrack->findAll("MidiNoteEvent")) {
            const double temps = nombre(evenement->attribute("Time", "0"), 0.0);
            const double duree = nombre(evenement->attribute("Duration", "0"), 0.0);
            const double velocite = nombre(evenement->attribute("Velocity", "100"), 100.0);
            if (duree <= 0.0) continue;

            Note note;
            // Les temps de Live sont en NOIRES, pas en ticks : une noire vaut
            // `ticksPerQuarterNote`, et c'est toute la conversion.
            const double debut = (debutDuClip + temps) * static_cast<double>(tpq);
            note.startTick = static_cast<vsm::midi::Tick>(std::max<long long>(0, std::llround(debut)));
            note.endTick = note.startTick
                         + static_cast<vsm::midi::Tick>(std::max<long long>(
                               1, std::llround(duree * static_cast<double>(tpq))));
            note.number = static_cast<uint8_t>(hauteur);
            note.velocity = static_cast<uint8_t>(std::clamp(velocite, 1.0, 127.0));
            note.channel = piste.channel;
            piste.notes.push_back(note);
            ++compte;
        }
    }
    return compte;
}

} // namespace

DawImportResult importAbletonLive(const std::vector<uint8_t>& octets) {
    if (octets.empty()) throw DawImportError("fichier vide");

    const auto clair = Inflate::any(octets);
    const std::string texte(clair.begin(), clair.end());
    XmlDocument doc;
    try {
        doc = parseXml(texte);
    } catch (const XmlError& erreur) {
        throw DawImportError(std::string("ce fichier n'est pas un projet Ableton lisible : ")
                             + erreur.what());
    }
    if (!doc.root || doc.root->name != "Ableton")
        throw DawImportError("ce fichier n'est pas un projet Ableton Live "
                             "(élément racine « Ableton » attendu)");

    DawImportResult resultat;
    auto& rapport = resultat.report;
    rapport.sourceFormat = "Ableton Live";
    rapport.sourceVersion = doc.root->attribute("Creator", doc.root->attribute("MajorVersion", "?"));
    rapport.note("Projet lu : " + rapport.sourceVersion);

    Project& projet = resultat.project;
    projet.ticksPerQuarterNote = 480;

    // --- Le tempo ----------------------------------------------------------
    // Il vit dans la piste MASTER, sous un `Tempo`. Une automation de tempo
    // existe aussi dans Live ; on ne prend que la valeur manuelle et on le DIT.
    const XmlNode* tempo = doc.root->find("Tempo");
    double bpm = 120.0;
    if (tempo != nullptr) {
        const std::string manuel = valeurDe(*tempo, "Manual", "");
        if (!manuel.empty()) bpm = nombre(manuel, 120.0);
    }
    if (bpm < 20.0 || bpm > 999.0) {
        rapport.note("Tempo illisible : 120 BPM retenu par défaut");
        bpm = 120.0;
    }
    projet.tempoMap.clearTempoChanges();
    projet.tempoMap.addTempoChange(0, static_cast<uint32_t>(std::llround(60000000.0 / bpm)));
    rapport.note("Tempo : " + std::to_string(static_cast<int>(std::llround(bpm))) + " BPM");
    if (tempo != nullptr && tempo->find("FloatEvent") != nullptr)
        rapport.note("ATTENTION : ce projet a une AUTOMATION de tempo, qui n'est pas reprise — "
                     "seul le tempo de base est importé");

    // --- Les pistes --------------------------------------------------------
    for (const XmlNode* audio : doc.root->findAll("AudioTrack")) {
        ++rapport.audioTracksSeen;
        const std::string nom = valeurDe(*audio, "EffectiveName", "sans nom");
        rapport.note("Piste AUDIO « " + nom + " » NON importée : un clip audio renvoie à un "
                     "fichier extérieur au projet, que VSM Studio ne peut pas reprendre ici");
    }

    for (const XmlNode* midi : doc.root->findAll("MidiTrack")) {
        Track piste;
        piste.name = valeurDe(*midi, "EffectiveName", "");
        if (piste.name.empty()) piste.name = valeurDe(*midi, "UserName", "Piste");
        piste.colorRgba = couleurDepuisIndice(
            static_cast<int>(nombre(valeurDe(*midi, "Color", "-1"), -1.0)));

        // `Speaker` est le haut-parleur de Live : éteint = piste muette.
        const std::string hautParleur = valeurDe(*midi, "Speaker", "true");
        piste.muted = (hautParleur == "false");
        piste.solo = (valeurDe(*midi, "Solo", "false") == "true");

        int notesDeLaPiste = 0;
        const auto clips = midi->findAll("MidiClip");
        rapport.clipsSeen += static_cast<int>(clips.size());
        for (const XmlNode* clip : clips)
            notesDeLaPiste += lireLesNotesDUnClip(*clip, piste, projet.ticksPerQuarterNote);

        std::sort(piste.notes.begin(), piste.notes.end(),
                  [](const Note& a, const Note& b) { return a.startTick < b.startTick; });

        rapport.notesImported += notesDeLaPiste;
        ++rapport.midiTracksImported;
        ++rapport.tracksWithoutInstrument;
        rapport.note("Piste MIDI « " + piste.name + " » : " + std::to_string(notesDeLaPiste)
                     + " note(s) reprise(s), AUCUN instrument assigné — l'instrument du projet "
                       "d'origine n'existe pas ici et ses réglages n'ont pas d'équivalent");
        projet.tracks.push_back(std::move(piste));
    }

    if (projet.tracks.empty() && rapport.audioTracksSeen == 0)
        rapport.note("ATTENTION : aucune piste trouvée — ce projet est peut-être vide, "
                     "ou d'une version de Live que ce lecteur ne reconnaît pas");

    projet.title = "Import Ableton";
    rapport.note("Total : " + std::to_string(rapport.midiTracksImported) + " piste(s) MIDI, "
                 + std::to_string(rapport.notesImported) + " note(s), "
                 + std::to_string(rapport.audioTracksSeen) + " piste(s) audio ignorée(s)");
    return resultat;
}

DawImportResult importAbletonLiveFile(const std::string& chemin) {
    std::ifstream fichier(chemin, std::ios::binary);
    if (!fichier) throw DawImportError("fichier introuvable : " + chemin);
    const std::vector<uint8_t> octets((std::istreambuf_iterator<char>(fichier)),
                                      std::istreambuf_iterator<char>());
    return importAbletonLive(octets);
}

} // namespace vsm::interchange
