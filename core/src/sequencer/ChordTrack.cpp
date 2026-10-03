#include "vsm/sequencer/ChordTrack.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <utility>

namespace vsm::sequencer {

namespace {

constexpr std::array<const char*, 12> kNoms = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// Le suffixe de chaque type. Lu dans les DEUX sens : une table, pas deux.
const std::vector<std::pair<ChordType, std::string>>& suffixes() {
    static const std::vector<std::pair<ChordType, std::string>> table = {
        {ChordType::Major, ""},          {ChordType::Minor, "m"},          {ChordType::Diminished, "dim"},
        {ChordType::Augmented, "aug"},   {ChordType::Sus2, "sus2"},        {ChordType::Sus4, "sus4"},
        {ChordType::Power, "5"},         {ChordType::Major7, "maj7"},      {ChordType::Minor7, "m7"},
        {ChordType::Dominant7, "7"},     {ChordType::Minor7Flat5, "m7b5"}, {ChordType::Major9, "maj9"},
        {ChordType::Minor9, "m9"},
    };
    return table;
}

/// Une note au début de `texte` (« C », « F# », « Bb ») : sa classe et sa longueur, ou -1.
std::pair<int, size_t> lireUneNote(const std::string& texte) {
    if (texte.empty()) return {-1, 0};
    static const int degres[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    const char lettre = texte[0];
    if (lettre < 'A' || lettre > 'G') return {-1, 0};
    int classe = degres[lettre - 'A'];
    size_t longueur = 1;
    if (texte.size() > 1 && texte[1] == '#') { classe += 1; longueur = 2; }
    else if (texte.size() > 1 && texte[1] == 'b') { classe += 11; longueur = 2; }
    return {classe % 12, longueur};
}

} // namespace

std::string chordSymbol(const ChordEvent& accord) {
    std::string symbole = kNoms[accord.root % 12];
    for (const auto& [type, suffixe] : suffixes())
        if (type == accord.type) symbole += suffixe;
    if (accord.bass >= 0) symbole += std::string("/") + kNoms[static_cast<size_t>(accord.bass % 12)];
    return symbole;
}

bool parseChordSymbol(const std::string& symbole, ChordEvent& sortie) {
    const auto [racine, longueur] = lireUneNote(symbole);
    if (racine < 0) return false;
    const size_t barre = symbole.find('/', longueur);
    const std::string suffixe = symbole.substr(longueur, barre == std::string::npos ? std::string::npos : barre - longueur);
    bool trouve = false;
    ChordType type = ChordType::Major;
    for (const auto& [t, s] : suffixes())
        if (s == suffixe) { type = t; trouve = true; }
    if (!trouve) return false;
    int basse = -1;
    if (barre != std::string::npos) {
        const std::string texteBasse = symbole.substr(barre + 1);
        const auto [classe, lu] = lireUneNote(texteBasse);
        if (classe < 0 || lu != texteBasse.size()) return false;
        basse = classe;
    }
    sortie.root = static_cast<uint8_t>(racine);
    sortie.type = type;
    sortie.bass = basse;
    return true;
}

const ChordEvent* chordAt(const std::vector<ChordEvent>& accords, Tick tick) {
    const ChordEvent* enVigueur = nullptr;
    for (const auto& accord : accords) {
        if (accord.tick > tick) break;
        enVigueur = &accord;
    }
    return enVigueur;
}

void setChordAt(std::vector<ChordEvent>& accords, const ChordEvent& accord) {
    auto ici = std::lower_bound(accords.begin(), accords.end(), accord.tick,
                                [](const ChordEvent& a, Tick t) { return a.tick < t; });
    if (ici != accords.end() && ici->tick == accord.tick) *ici = accord;
    else accords.insert(ici, accord);
}

bool removeChordAt(std::vector<ChordEvent>& accords, Tick tick) {
    auto ici = std::find_if(accords.begin(), accords.end(), [tick](const ChordEvent& a) { return a.tick == tick; });
    if (ici == accords.end()) return false;
    accords.erase(ici);
    return true;
}

uint16_t chordMask(const ChordEvent& accord) {
    uint16_t masque = 0;
    for (int intervalle : chordIntervals(accord.type))
        masque = static_cast<uint16_t>(masque | (1u << ((accord.root + intervalle) % 12)));
    if (accord.bass >= 0) masque = static_cast<uint16_t>(masque | (1u << (accord.bass % 12)));
    return masque;
}

ChordSnapReport snapNotesToChords(std::vector<Note>& notes, const NoteSelection& selection,
                                  const std::vector<ChordEvent>& accords,
                                  const std::vector<ClipPassage>& passages) {
    ChordSnapReport bilan;
    if (selection.empty()) return bilan;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        // D532.3 bis : LES HARMONIES SOUS LESQUELLES LA NOTE SONNE, une par début entendu —
        // 0 pour « avant le premier accord ». Le calage ne regarde que le masque : deux
        // accords de mêmes classes de hauteur ne sont pas une ambiguïté.
        bool entendue = false, sansAccord = false, plusieurs = false;
        uint16_t masque = 0;
        for (const auto& passage : passages) {
            const Tick sortie = passageOut(passage, note.startTick);
            if (sortie < 0) continue;
            entendue = true;
            const ChordEvent* accord = chordAt(accords, sortie);
            if (accord == nullptr) { sansAccord = true; continue; }
            const uint16_t ici = chordMask(*accord);
            if (masque != 0 && ici != masque) plusieurs = true;
            masque = ici;
        }
        if (!entendue) { ++bilan.unheard; continue; }
        if (plusieurs || (sansAccord && masque != 0)) { ++bilan.ambiguous; continue; }
        if (masque == 0) { ++bilan.withoutChord; continue; }
        auto dansLAccord = [masque](int hauteur) { return (masque >> (hauteur % 12)) & 1u; };
        if (dansLAccord(note.number)) { ++bilan.alreadyInChord; continue; }
        // La recherche symétrique de `snapNoteToScale` : la plus proche, le grave à égalité.
        for (int distance = 1; distance <= 6; ++distance) {
            const int bas = static_cast<int>(note.number) - distance;
            if (bas >= 0 && dansLAccord(bas)) { note.number = static_cast<uint8_t>(bas); ++bilan.moved; break; }
            const int haut = static_cast<int>(note.number) + distance;
            if (haut <= 127 && dansLAccord(haut)) { note.number = static_cast<uint8_t>(haut); ++bilan.moved; break; }
        }
    }
    return bilan;
}

std::optional<ChordEvent> detectChord(const std::array<double, 12>& poids, int basse) {
    double total = 0.0;
    for (double p : poids) total += p;
    if (total <= 0.0) return std::nullopt;
    // UNE CLASSE QUI SONNE pèse au moins 5 % : une note de passage brève ne fait pas l'accord.
    const double sonne = 0.05 * total;
    int classes = 0;
    for (double p : poids) if (p >= sonne) ++classes;
    if (classes < 3) return std::nullopt;   // une note seule, une quinte à vide ne sont pas des accords

    std::optional<ChordEvent> meilleur;
    double meilleurScore = -1e300, meilleurCouvert = 0.0;
    for (const ChordType type : allChordTypes()) {
        if (type == ChordType::Power) continue;
        const auto intervalles = chordIntervals(type);
        for (int racine = 0; racine < 12; ++racine) {
            double couvert = 0.0;
            int absentes = 0;
            for (int iv : intervalles) {
                const double p = poids[static_cast<size_t>((racine + iv) % 12)];
                couvert += p;
                if (p < sonne) ++absentes;
            }
            const double score = couvert - (total - couvert) - absentes * 0.25 * total;
            // À ÉGALITÉ, LE PREMIER — les types vont du plus simple au plus riche (`allChordTypes`).
            // Une règle explicite « le plus court » a été écrite puis retirée : avec la pénalité
            // d'absence, deux types de tailles différentes ne font jamais le même score, et un défaut
            // qui l'inversait ne faisait tomber aucun test (vu le 03/10).
            if (score <= meilleurScore + 1e-9) continue;
            ChordEvent accord;
            accord.root = static_cast<uint8_t>(racine);
            accord.type = type;
            meilleur = accord;
            meilleurScore = score;
            meilleurCouvert = couvert;
        }
    }
    if (!meilleur || meilleurCouvert < (2.0 / 3.0) * total) return std::nullopt;
    if (basse >= 0 && basse != meilleur->root) meilleur->bass = basse;
    return meilleur;
}

ChordDetectionReport chordsFromNotes(const std::vector<Note>& notes, const std::vector<ClipPassage>& passages,
                                     const TimeSignatureMap& signatures, uint16_t ppq, Tick from, Tick to) {
    ChordDetectionReport bilan;
    // CE QUI SONNE, sur la ligne de temps : (début, fin, hauteur), par les fenêtres de la piste.
    struct Entendue { Tick debut, fin; int hauteur; };
    std::vector<Entendue> entendues;
    for (const auto& n : notes) {
        if (n.muted) continue;
        for (const auto& p : passages) {
            const Tick t = passageOut(p, n.startTick);
            if (t < 0) continue;
            entendues.push_back({t, std::min(n.endTick + p.shift, p.outLimit), n.number});
        }
    }
    Tick mesure = signatures.tickAtBarBeat(signatures.barBeatAt(std::max<Tick>(0, from), ppq).bar, 0, ppq);
    while (mesure < to) {
        const Tick suivante = mesure + std::max<Tick>(1, signatures.ticksPerBar(mesure, ppq));
        ++bilan.bars;
        std::array<double, 12> poids{};
        int basseAuTemps = 999, basse = 999;
        for (const auto& e : entendues) {
            const Tick a = std::max(e.debut, mesure), b = std::min(e.fin, suivante);
            if (b <= a) continue;
            poids[static_cast<size_t>(e.hauteur % 12)] += static_cast<double>(b - a);
            basse = std::min(basse, e.hauteur);
            if (e.debut <= mesure && e.fin > mesure) basseAuTemps = std::min(basseAuTemps, e.hauteur);
        }
        const int laBasse = basseAuTemps != 999 ? basseAuTemps % 12 : (basse != 999 ? basse % 12 : -1);
        if (auto accord = detectChord(poids, laBasse)) {
            accord->tick = mesure;
            const bool pareil = !bilan.chords.empty() && bilan.chords.back().root == accord->root
                                && bilan.chords.back().type == accord->type && bilan.chords.back().bass == accord->bass;
            if (!pareil) bilan.chords.push_back(*accord);
        } else {
            ++bilan.barsWithoutChord;
        }
        mesure = suivante;
    }
    return bilan;
}

} // namespace vsm::sequencer
