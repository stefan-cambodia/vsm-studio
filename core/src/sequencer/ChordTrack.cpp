#include "vsm/sequencer/ChordTrack.h"
#include <algorithm>
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
                                  const std::vector<ChordEvent>& accords) {
    ChordSnapReport bilan;
    if (selection.empty()) return bilan;
    for (auto& note : notes) {
        if (selection.count(note.id) == 0) continue;
        const ChordEvent* accord = chordAt(accords, note.startTick);
        if (accord == nullptr) { ++bilan.withoutChord; continue; }
        const uint16_t masque = chordMask(*accord);
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

} // namespace vsm::sequencer
