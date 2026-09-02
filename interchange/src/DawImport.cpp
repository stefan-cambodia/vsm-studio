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

// ============================================================================
// FL STUDIO (.flp)
// ============================================================================
//
// LA STRUCTURE EST CERTAINE, LE SENS DES IDENTIFIANTS EST RECONSTITUÉ, et le
// § 3 bis du CDC dit pourquoi la distinction compte. Ce qu'on peut affirmer :
// deux blocs `FLhd` et `FLdt`, ce dernier étant une suite d'événements dont la
// TAILLE se déduit de l'identifiant. Ce découpage se vérifie tout seul — s'il
// est faux, on n'atteint pas la fin du fichier exactement, et c'est ce que le
// lecteur contrôle.

namespace vsm::interchange {
namespace {

/// Les identifiants dont le sens est établi par la rétro-ingénierie publique.
/// Ils sont NOMMÉS ici plutôt que glissés en nombres dans le code : le jour où
/// l'un d'eux se révélera faux, il n'y aura qu'une ligne à corriger, et le
/// lecteur dira lui-même que quelque chose cloche (voir `eventsUnderstood`).
enum FlpEvent : uint8_t {
    kFlpNewChannel = 64,      ///< mot : le canal du rack dont on parle désormais
    kFlpNewPattern = 65,      ///< mot : le motif dont on parle désormais
    kFlpTempoAncien = 66,     ///< mot : BPM entier, versions anciennes
    kFlpTempo = 156,          ///< double-mot : BPM en MILLIÈMES
    kFlpNomDeCanal = 192,     ///< texte
    kFlpNomDeMotif = 193,     ///< texte
    kFlpTitre = 194,          ///< texte
    kFlpVersion = 199,        ///< texte
    kFlpNotesDuMotif = 224,   ///< données : blocs de 24 octets, une note chacun
};

uint16_t lireMot(const std::vector<uint8_t>& o, size_t i) {
    return static_cast<uint16_t>(o[i]) | static_cast<uint16_t>(static_cast<uint16_t>(o[i + 1]) << 8);
}
uint32_t lireDoubleMot(const std::vector<uint8_t>& o, size_t i) {
    return static_cast<uint32_t>(o[i]) | (static_cast<uint32_t>(o[i + 1]) << 8)
         | (static_cast<uint32_t>(o[i + 2]) << 16) | (static_cast<uint32_t>(o[i + 3]) << 24);
}

/// Les textes de FL sont en UTF-16 depuis la version 12, en Latin-1 avant.
/// On reconnaît l'UTF-16 à ses octets nuls en position impaire, et on convertit
/// en UTF-8 — sans quoi un nom de canal accentué arriverait haché.
std::string texteDeFl(const std::vector<uint8_t>& donnees) {
    bool utf16 = donnees.size() >= 4;
    for (size_t i = 1; i < donnees.size() && utf16; i += 2)
        if (donnees[i] != 0) utf16 = false;
    std::string sortie;
    if (utf16) {
        for (size_t i = 0; i + 1 < donnees.size(); i += 2) {
            const unsigned code = static_cast<unsigned>(donnees[i])
                                | (static_cast<unsigned>(donnees[i + 1]) << 8);
            if (code == 0) break;
            if (code < 0x80) {
                sortie += static_cast<char>(code);
            } else if (code < 0x800) {
                sortie += static_cast<char>(0xC0 | (code >> 6));
                sortie += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                sortie += static_cast<char>(0xE0 | (code >> 12));
                sortie += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                sortie += static_cast<char>(0x80 | (code & 0x3F));
            }
        }
    } else {
        for (uint8_t c : donnees) {
            if (c == 0) break;
            if (c < 0x80) sortie += static_cast<char>(c);
            else { sortie += static_cast<char>(0xC0 | (c >> 6));
                   sortie += static_cast<char>(0x80 | (c & 0x3F)); }
        }
    }
    return sortie;
}

/// Une note telle que FL l'écrit : vingt-quatre octets, dont on ne lit que ce
/// dont on est sûr.
struct NoteFl {
    uint32_t position = 0;    ///< en ticks PPQ, depuis le début du MOTIF
    uint16_t canal = 0;       ///< canal du rack
    uint32_t duree = 0;
    uint8_t hauteur = 60;
    uint8_t velocite = 100;
};

} // namespace

DawImportResult importFlStudio(const std::vector<uint8_t>& octets) {
    if (octets.size() < 8) throw DawImportError("fichier trop court pour un projet FL Studio");
    if (!(octets[0] == 'F' && octets[1] == 'L' && octets[2] == 'h' && octets[3] == 'd'))
        throw DawImportError("ce fichier n'est pas un projet FL Studio (bloc « FLhd » attendu)");

    const uint32_t tailleEnTete = lireDoubleMot(octets, 4);
    if (tailleEnTete < 6 || 8 + tailleEnTete > octets.size())
        throw DawImportError("en-tête FL Studio incohérent");
    const uint16_t nbCanaux = lireMot(octets, 10);
    const uint16_t ppq = lireMot(octets, 12);
    if (ppq == 0) throw DawImportError("PPQ nul : en-tête FL Studio illisible");

    size_t i = 8 + tailleEnTete;
    if (i + 8 > octets.size()
        || !(octets[i] == 'F' && octets[i + 1] == 'L' && octets[i + 2] == 'd' && octets[i + 3] == 't'))
        throw DawImportError("bloc de données « FLdt » introuvable");
    const uint32_t tailleDonnees = lireDoubleMot(octets, i + 4);
    i += 8;
    const size_t fin = i + tailleDonnees;
    if (fin > octets.size()) throw DawImportError("bloc de données annoncé plus long que le fichier");

    DawImportResult resultat;
    auto& rapport = resultat.report;
    rapport.sourceFormat = "FL Studio";
    Project& projet = resultat.project;
    projet.ticksPerQuarterNote = ppq;
    projet.title = "Import FL Studio";

    double bpm = 120.0;
    std::string titre;
    uint16_t canalCourant = 0, motifCourant = 0;
    std::vector<std::string> nomsDeCanaux(256);
    // Les notes, gardées par MOTIF : leur position est relative au motif, et
    // c'est seulement à la fin qu'on saura où poser chaque motif.
    std::vector<std::pair<uint16_t, std::vector<NoteFl>>> motifs;
    auto motif = [&](uint16_t numero) -> std::vector<NoteFl>& {
        for (auto& m : motifs) if (m.first == numero) return m.second;
        motifs.emplace_back(numero, std::vector<NoteFl>{});
        return motifs.back().second;
    };

    while (i < fin) {
        const uint8_t id = octets[i++];
        ++rapport.eventsRead;
        bool compris = false;

        if (id < 64) {
            if (i + 1 > fin) throw DawImportError("événement tronqué en fin de fichier");
            i += 1;
        } else if (id < 128) {
            if (i + 2 > fin) throw DawImportError("événement tronqué en fin de fichier");
            const uint16_t v = lireMot(octets, i);
            i += 2;
            if (id == kFlpNewChannel) { canalCourant = v; compris = true; }
            else if (id == kFlpNewPattern) { motifCourant = v; compris = true; }
            else if (id == kFlpTempoAncien && v >= 20 && v <= 999) { bpm = v; compris = true; }
        } else if (id < 192) {
            if (i + 4 > fin) throw DawImportError("événement tronqué en fin de fichier");
            const uint32_t v = lireDoubleMot(octets, i);
            i += 4;
            if (id == kFlpTempo && v >= 20000 && v <= 999000) {
                bpm = static_cast<double>(v) / 1000.0;   // MILLIÈMES de BPM
                compris = true;
            }
        } else {
            // Longueur en base 128, sept bits par octet, poids faible d'abord.
            size_t longueur = 0;
            int decalage = 0;
            for (;;) {
                if (i >= fin) throw DawImportError("longueur d'événement tronquée");
                const uint8_t o = octets[i++];
                longueur |= static_cast<size_t>(o & 0x7F) << decalage;
                if ((o & 0x80) == 0) break;
                decalage += 7;
                if (decalage > 28) throw DawImportError("longueur d'événement aberrante");
            }
            if (i + longueur > fin) throw DawImportError("événement annoncé plus long que le bloc");
            const std::vector<uint8_t> donnees(octets.begin() + static_cast<long>(i),
                                               octets.begin() + static_cast<long>(i + longueur));
            i += longueur;

            if (id == kFlpTitre) { titre = texteDeFl(donnees); compris = true; }
            else if (id == kFlpVersion) { rapport.sourceVersion = texteDeFl(donnees); compris = true; }
            else if (id == kFlpNomDeCanal) {
                if (canalCourant < nomsDeCanaux.size()) nomsDeCanaux[canalCourant] = texteDeFl(donnees);
                compris = true;
            } else if (id == kFlpNomDeMotif) {
                compris = true;
            } else if (id == kFlpNotesDuMotif) {
                // VINGT-QUATRE OCTETS PAR NOTE. Une taille qui n'en est pas un
                // multiple signifie que l'identifiant ne veut pas dire ce
                // qu'on croit : on le DIT plutôt que de lire n'importe quoi.
                if (longueur % 24 != 0) {
                    rapport.note("ATTENTION : un bloc de notes de " + std::to_string(longueur)
                                 + " octets n'est pas un multiple de 24 — il n'a pas été lu, et "
                                   "ce lecteur se trompe peut-être d'identifiant d'événement");
                } else {
                    auto& liste = motif(motifCourant);
                    for (size_t n = 0; n + 24 <= longueur; n += 24) {
                        NoteFl note;
                        note.position = lireDoubleMot(donnees, n);
                        note.canal = lireMot(donnees, n + 6);
                        note.duree = lireDoubleMot(donnees, n + 8);
                        note.hauteur = donnees[n + 12];
                        note.velocite = donnees[n + 21];
                        if (note.duree == 0) continue;
                        liste.push_back(note);
                    }
                    compris = true;
                }
            }
        }
        if (compris) ++rapport.eventsUnderstood;
    }

    if (!titre.empty()) projet.title = titre;
    projet.tempoMap.clearTempoChanges();
    projet.tempoMap.addTempoChange(0, static_cast<uint32_t>(std::llround(60000000.0 / bpm)));
    rapport.note("Tempo : " + std::to_string(static_cast<int>(std::llround(bpm))) + " BPM, "
                 + std::to_string(ppq) + " ticks par noire");
    if (rapport.sourceVersion.empty()) rapport.sourceVersion = "version non déclarée";

    // --- Où poser les motifs ------------------------------------------------
    // L'ORDRE DE LA PLAYLIST N'EST PAS LU, et c'est écrit plutôt que caché :
    // les motifs sont posés BOUT À BOUT dans l'ordre de leurs numéros. Un
    // morceau dont l'arrangement est deviné n'est pas le morceau du musicien.
    std::sort(motifs.begin(), motifs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Track> parCanal(nbCanaux == 0 ? 256 : nbCanaux + 1);
    std::vector<bool> canalUtilise(parCanal.size(), false);
    vsm::midi::Tick decalage = 0;

    for (const auto& [numero, notes] : motifs) {
        uint32_t finDuMotif = 0;
        for (const NoteFl& n : notes) finDuMotif = std::max(finDuMotif, n.position + n.duree);
        for (const NoteFl& n : notes) {
            if (n.canal >= parCanal.size()) continue;
            Note note;
            note.startTick = decalage + n.position;
            note.endTick = note.startTick + std::max<uint32_t>(1, n.duree);
            note.number = static_cast<uint8_t>(std::min<int>(127, n.hauteur));
            note.velocity = static_cast<uint8_t>(std::clamp<int>(n.velocite, 1, 127));
            parCanal[n.canal].notes.push_back(note);
            canalUtilise[n.canal] = true;
            ++rapport.notesImported;
        }
        // Arrondi à la mesure suivante (quatre noires) : poser deux motifs
        // bout à bout sans respecter la mesure décalerait tout le reste.
        const vsm::midi::Tick mesure = static_cast<vsm::midi::Tick>(ppq) * 4;
        decalage += ((finDuMotif + mesure - 1) / std::max<vsm::midi::Tick>(1, mesure)) * mesure;
    }
    rapport.clipsSeen = static_cast<int>(motifs.size());

    for (size_t c = 0; c < parCanal.size(); ++c) {
        if (!canalUtilise[c]) continue;
        Track& piste = parCanal[c];
        piste.name = (c < nomsDeCanaux.size() && !nomsDeCanaux[c].empty())
                   ? nomsDeCanaux[c] : ("Canal " + std::to_string(c + 1));
        piste.colorRgba = couleurDepuisIndice(static_cast<int>(c));
        std::sort(piste.notes.begin(), piste.notes.end(),
                  [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        ++rapport.midiTracksImported;
        ++rapport.tracksWithoutInstrument;
        rapport.note("Canal « " + piste.name + " » : " + std::to_string(piste.notes.size())
                     + " note(s), AUCUN instrument assigné — le générateur du rack "
                       "(Sytrus, Harmless, un VST…) n'existe pas ici");
        projet.tracks.push_back(std::move(piste));
    }

    if (motifs.size() > 1)
        rapport.note("ATTENTION : l'ARRANGEMENT n'est pas repris. Les "
                     + std::to_string(motifs.size())
                     + " motifs sont posés BOUT À BOUT dans l'ordre de leurs numéros, ce qui "
                       "n'est probablement pas l'ordre de votre playlist");
    rapport.note("Événements lus : " + std::to_string(rapport.eventsRead) + ", dont "
                 + std::to_string(rapport.eventsUnderstood) + " compris. Un import qui ne "
                   "comprend presque rien signale un lecteur qui se trompe, pas un projet vide");
    if (rapport.notesImported == 0)
        rapport.note("ATTENTION : AUCUNE note lue. Soit ce projet n'en contient pas, soit ce "
                     "lecteur ne reconnaît pas la version de FL Studio qui l'a écrit");
    return resultat;
}

DawImportResult importFlStudioFile(const std::string& chemin) {
    std::ifstream fichier(chemin, std::ios::binary);
    if (!fichier) throw DawImportError("fichier introuvable : " + chemin);
    const std::vector<uint8_t> octets((std::istreambuf_iterator<char>(fichier)),
                                      std::istreambuf_iterator<char>());
    return importFlStudio(octets);
}

} // namespace vsm::interchange
