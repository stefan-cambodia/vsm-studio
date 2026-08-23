#include "vsm/interchange/SoundFont.h"
#include "vsm/interchange/Json.h"
#include "vsm/interchange/MultisampleProfile.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/audio/io/WavFileWriter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace vsm::interchange {

namespace {

// --- lecture RIFF ----------------------------------------------------------
//
// Le SF2 est un RIFF : une suite de morceaux « quatre lettres + taille + corps »,
// imbriqués. Rien ici ne suppose que le fichier est bien formé : chaque lecture
// vérifie qu'elle tient dans les bornes. Un fichier tronqué doit produire un
// message, pas une lecture hors tableau.

struct Reader {
    const std::vector<uint8_t>& data;
    size_t position = 0;

    bool remaining(size_t count) const { return position + count <= data.size(); }
    uint32_t u32() { uint32_t v = 0; std::memcpy(&v, data.data() + position, 4); position += 4; return v; }
    uint16_t u16() { uint16_t v = 0; std::memcpy(&v, data.data() + position, 2); position += 2; return v; }
    int16_t  i16() { int16_t v = 0;  std::memcpy(&v, data.data() + position, 2); position += 2; return v; }
    uint8_t  u8()  { return data[position++]; }
    std::string fourcc() {
        std::string s(reinterpret_cast<const char*>(data.data() + position), 4);
        position += 4;
        return s;
    }
    std::string fixedString(size_t length) {
        std::string s(reinterpret_cast<const char*>(data.data() + position), length);
        position += length;
        const size_t nul = s.find('\0');
        if (nul != std::string::npos) s.resize(nul);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
};

struct Chunk { std::string id; size_t offset = 0; size_t size = 0; };

/// Morceaux de premier niveau à l'intérieur d'une LIST, indexés par identifiant.
bool collectChunks(const std::vector<uint8_t>& data, size_t begin, size_t end,
                   std::map<std::string, Chunk>& out, std::string& error) {
    size_t position = begin;
    while (position + 8 <= end) {
        Chunk chunk;
        chunk.id.assign(reinterpret_cast<const char*>(data.data() + position), 4);
        uint32_t size = 0;
        std::memcpy(&size, data.data() + position + 4, 4);
        chunk.offset = position + 8;
        chunk.size = size;
        if (chunk.offset + chunk.size > end) {
            error = "morceau « " + chunk.id + " » déborde de son conteneur : fichier tronqué";
            return false;
        }
        out[chunk.id] = chunk;
        position = chunk.offset + chunk.size + (chunk.size & 1u); // padding à l'octet pair
    }
    return true;
}

// --- générateurs SF2 -------------------------------------------------------

enum Generator : uint16_t {
    kStartAddrsOffset = 0,
    kEndAddrsOffset = 1,
    kStartloopAddrsOffset = 2,
    kEndloopAddrsOffset = 3,
    kStartAddrsCoarseOffset = 4,
    kEndAddrsCoarseOffset = 12,
    kPan = 17,
    kAttackVolEnv = 34,
    kReleaseVolEnv = 38,
    kInstrument = 41,
    kKeyRange = 43,
    kVelRange = 44,
    kStartloopAddrsCoarseOffset = 45,
    kInitialAttenuation = 48,
    kEndloopAddrsCoarseOffset = 50,
    kCoarseTune = 51,
    kFineTune = 52,
    kSampleID = 53,
    kSampleModes = 54,
    kScaleTuning = 56,
    kOverridingRootKey = 58,
};

/// Nom lisible d'un générateur, pour le rapport « ce que j'ai ignoré ». Seuls
/// ceux qu'on peut réellement rencontrer dans une banque d'instruments sont
/// nommés ; les autres sortent sous leur numéro, ce qui reste exploitable.
const char* generatorName(uint16_t oper) {
    switch (oper) {
        case 5:  return "modLfoToPitch";
        case 6:  return "vibLfoToPitch";
        case 7:  return "modEnvToPitch";
        case 8:  return "initialFilterFc";
        case 9:  return "initialFilterQ";
        case 10: return "modLfoToFilterFc";
        case 11: return "modEnvToFilterFc";
        case 13: return "modLfoToVolume";
        case 15: return "chorusEffectsSend";
        case 16: return "reverbEffectsSend";
        case 21: return "delayModLFO";
        case 22: return "freqModLFO";
        case 23: return "delayVibLFO";
        case 24: return "freqVibLFO";
        case 25: return "delayModEnv";
        case 26: return "attackModEnv";
        case 27: return "holdModEnv";
        case 28: return "decayModEnv";
        case 29: return "sustainModEnv";
        case 30: return "releaseModEnv";
        case 31: return "keynumToModEnvHold";
        case 32: return "keynumToModEnvDecay";
        case 33: return "delayVolEnv";
        case 35: return "holdVolEnv";
        case 36: return "decayVolEnv";
        case 37: return "sustainVolEnv";
        case 39: return "keynumToVolEnvHold";
        case 40: return "keynumToVolEnvDecay";
        case 46: return "keynum";
        case 47: return "velocity";
        case 57: return "exclusiveClass";
        default: return nullptr;
    }
}

/// Générateurs que la conversion APPLIQUE réellement. Tout ce qui n'est pas
/// dans cette liste et qui apparaît dans le fichier est rapporté.
bool isApplied(uint16_t oper) {
    switch (oper) {
        case kStartAddrsOffset: case kEndAddrsOffset:
        case kStartloopAddrsOffset: case kEndloopAddrsOffset:
        case kStartAddrsCoarseOffset: case kEndAddrsCoarseOffset:
        case kStartloopAddrsCoarseOffset: case kEndloopAddrsCoarseOffset:
        case kAttackVolEnv: case kReleaseVolEnv:
        case kInstrument: case kKeyRange: case kVelRange:
        case kInitialAttenuation: case kCoarseTune: case kFineTune:
        case kSampleID: case kSampleModes: case kOverridingRootKey:
        case kScaleTuning: case kPan:
            return true;
        default:
            return false;
    }
}

/// Un jeu de générateurs, additionné selon la règle du format : la zone
/// d'instrument POSE la valeur, la zone de preset l'AJOUTE.
struct GeneratorSet {
    std::map<uint16_t, int32_t> values;
    bool has(uint16_t oper) const { return values.count(oper) > 0; }
    int32_t get(uint16_t oper, int32_t fallback) const {
        auto found = values.find(oper);
        return found == values.end() ? fallback : found->second;
    }
};

struct RawZone {
    GeneratorSet generators;
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127;
};

struct SampleHeader {
    std::string name;
    uint32_t start = 0, end = 0, startLoop = 0, endLoop = 0;
    uint32_t sampleRate = 44100;
    uint8_t originalPitch = 60;
    int8_t pitchCorrection = 0;
    uint16_t sampleLink = 0;
    uint16_t sampleType = 1;
};

/// Timecents -> secondes. −12000 timecents = 1 ms, 0 = 1 s. La valeur
/// « absente » du format vaut −12000, soit une milliseconde.
float timecentsToSeconds(int32_t timecents) {
    return static_cast<float>(std::pow(2.0, static_cast<double>(timecents) / 1200.0));
}

struct Parsed {
    std::vector<uint8_t> bytes;
    std::map<std::string, Chunk> info, sdta, pdta;
    bool ok = false;
    std::string error;
};

Parsed openSoundFont(const std::string& path) {
    Parsed parsed;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { parsed.error = "fichier illisible : " + path; return parsed; }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    parsed.bytes.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(parsed.bytes.data()), size)) {
        parsed.error = "lecture incomplète : " + path;
        return parsed;
    }
    if (parsed.bytes.size() < 12) { parsed.error = "fichier trop court pour un RIFF"; return parsed; }

    Reader reader{parsed.bytes, 0};
    if (reader.fourcc() != "RIFF") { parsed.error = "ce n'est pas un fichier RIFF"; return parsed; }
    const uint32_t riffSize = reader.u32();
    if (reader.fourcc() != "sfbk") { parsed.error = "RIFF présent, mais ce n'est pas un SoundFont (sfbk)"; return parsed; }

    const size_t end = std::min(parsed.bytes.size(), size_t(8) + riffSize);
    size_t position = 12;
    while (position + 12 <= end) {
        Reader listReader{parsed.bytes, position};
        const std::string kind = listReader.fourcc();
        const uint32_t listSize = listReader.u32();
        const std::string name = listReader.fourcc();
        const size_t body = position + 12;
        const size_t bodyEnd = std::min(end, position + 8 + listSize);
        if (kind == "LIST") {
            std::map<std::string, Chunk>* target = nullptr;
            if (name == "INFO") target = &parsed.info;
            else if (name == "sdta") target = &parsed.sdta;
            else if (name == "pdta") target = &parsed.pdta;
            if (target != nullptr && !collectChunks(parsed.bytes, body, bodyEnd, *target, parsed.error))
                return parsed;
        }
        position = position + 8 + listSize + (listSize & 1u);
    }

    if (parsed.pdta.empty()) { parsed.error = "aucune section « pdta » : ce SoundFont n'a pas de preset"; return parsed; }
    parsed.ok = true;
    return parsed;
}

std::string infoString(const Parsed& parsed, const char* id) {
    auto found = parsed.info.find(id);
    if (found == parsed.info.end()) return {};
    Reader reader{parsed.bytes, found->second.offset};
    return reader.fixedString(found->second.size);
}

} // namespace

// ---------------------------------------------------------------------------

SoundFontIndex readSoundFontIndex(const std::string& path) {
    SoundFontIndex index;
    Parsed parsed = openSoundFont(path);
    if (!parsed.ok) { index.error = parsed.error; return index; }

    index.bankName = infoString(parsed, "INAM");
    index.engineers = infoString(parsed, "IENG");
    index.copyright = infoString(parsed, "ICOP");
    auto version = parsed.info.find("ifil");
    if (version != parsed.info.end() && version->second.size >= 4) {
        Reader reader{parsed.bytes, version->second.offset};
        index.majorVersion = reader.u16();
        index.minorVersion = reader.u16();
    }

    auto phdr = parsed.pdta.find("phdr");
    if (phdr == parsed.pdta.end()) { index.error = "section « phdr » absente"; return index; }

    // Chaque en-tête fait 38 octets ; le DERNIER est une sentinelle (« EOP »)
    // qui ne désigne pas un preset mais borne le précédent.
    const size_t count = phdr->second.size / 38;
    if (count < 2) { index.error = "aucun preset déclaré"; return index; }
    for (size_t i = 0; i + 1 < count; ++i) {
        Reader reader{parsed.bytes, phdr->second.offset + i * 38};
        SoundFontPreset preset;
        preset.name = reader.fixedString(20);
        preset.program = reader.u16();
        preset.bank = reader.u16();
        index.presets.push_back(std::move(preset));
    }
    index.success = true;
    return index;
}

SoundFontConversion convertSoundFontPreset(const std::string& path, int bank, int program,
                                            double maxSeconds) {
    SoundFontConversion conversion;
    Parsed parsed = openSoundFont(path);
    if (!parsed.ok) { conversion.error = parsed.error; return conversion; }

    auto need = [&](std::map<std::string, Chunk>& where, const char* id, Chunk& out) {
        auto found = where.find(id);
        if (found == where.end()) { conversion.error = std::string("section « ") + id + " » absente"; return false; }
        out = found->second;
        return true;
    };

    Chunk phdr, pbag, pgen, inst, ibag, igen, shdr, smpl;
    if (!need(parsed.pdta, "phdr", phdr) || !need(parsed.pdta, "pbag", pbag) ||
        !need(parsed.pdta, "pgen", pgen) || !need(parsed.pdta, "inst", inst) ||
        !need(parsed.pdta, "ibag", ibag) || !need(parsed.pdta, "igen", igen) ||
        !need(parsed.pdta, "shdr", shdr) || !need(parsed.sdta, "smpl", smpl))
        return conversion;

    if (parsed.sdta.count("sm24") > 0)
        conversion.notes.push_back("échantillons 24 bits présents (sm24) : les huit bits de poids "
                                    "faible sont ignorés, la conversion reste en 16 bits");
    if (parsed.pdta.count("pmod") > 0 || parsed.pdta.count("imod") > 0)
        conversion.notes.push_back("modulateurs présents : ignorés — ils décrivent des routages de "
                                    "molette et d'aftertouch que cette machine n'a pas");

    std::map<std::string, int> ignored;
    auto noteIgnored = [&](uint16_t oper) {
        const char* name = generatorName(oper);
        const std::string key = name != nullptr ? std::string(name)
                                                : ("générateur #" + std::to_string(oper));
        ++ignored[key];
    };

    // --- en-têtes d'échantillons
    const size_t sampleCount = shdr.size / 46;
    std::vector<SampleHeader> samples;
    samples.reserve(sampleCount);
    for (size_t i = 0; i + 1 < sampleCount; ++i) { // dernier = sentinelle « EOS »
        Reader reader{parsed.bytes, shdr.offset + i * 46};
        SampleHeader header;
        header.name = reader.fixedString(20);
        header.start = reader.u32();
        header.end = reader.u32();
        header.startLoop = reader.u32();
        header.endLoop = reader.u32();
        header.sampleRate = reader.u32();
        header.originalPitch = reader.u8();
        header.pitchCorrection = static_cast<int8_t>(reader.u8());
        header.sampleLink = reader.u16();
        header.sampleType = reader.u16();
        samples.push_back(std::move(header));
    }

    // --- lecture des sacs et des générateurs
    auto readBags = [&](const Chunk& bagChunk, const Chunk& genChunk, size_t firstBag, size_t lastBag,
                        std::vector<RawZone>& zones, GeneratorSet& globalSet, bool instrumentLevel) {
        const size_t bagCount = bagChunk.size / 4;
        for (size_t b = firstBag; b < lastBag && b + 1 <= bagCount; ++b) {
            Reader bagReader{parsed.bytes, bagChunk.offset + b * 4};
            const uint16_t genStart = bagReader.u16();
            Reader nextReader{parsed.bytes, bagChunk.offset + (b + 1) * 4};
            const uint16_t genEnd = (b + 1 < bagCount) ? nextReader.u16() : genStart;

            GeneratorSet set;
            RawZone zone;
            bool terminated = false;
            for (uint16_t g = genStart; g < genEnd; ++g) {
                Reader genReader{parsed.bytes, genChunk.offset + g * 4};
                const uint16_t oper = genReader.u16();
                if (oper == kKeyRange || oper == kVelRange) {
                    const uint8_t low = genReader.u8();
                    const uint8_t high = genReader.u8();
                    if (oper == kKeyRange) { zone.lowKey = low; zone.highKey = high; }
                    else { zone.lowVelocity = low; zone.highVelocity = high; }
                    set.values[oper] = (static_cast<int32_t>(high) << 8) | low;
                } else {
                    set.values[oper] = genReader.i16();
                }
                if (!isApplied(oper)) noteIgnored(oper);
                if ((instrumentLevel && oper == kSampleID) || (!instrumentLevel && oper == kInstrument))
                    terminated = true;
            }
            // Une zone qui ne se termine PAS par sampleID (ou instrument) est la
            // zone GLOBALE : ses générateurs servent de valeurs par défaut aux
            // autres. C'est une règle du format, et l'ignorer ferait perdre la
            // moitié des réglages d'une banque bien écrite.
            if (!terminated && zones.empty() && globalSet.values.empty()) globalSet = set;
            else if (terminated) { zone.generators = set; zones.push_back(std::move(zone)); }
        }
    };

    // --- trouver le preset demandé
    const size_t presetCount = phdr.size / 38;
    size_t presetIndex = presetCount; // introuvable
    size_t presetBagFirst = 0, presetBagLast = 0;
    for (size_t i = 0; i + 1 < presetCount; ++i) {
        Reader reader{parsed.bytes, phdr.offset + i * 38};
        reader.fixedString(20);
        const int presetNumber = reader.u16();
        const int bankNumber = reader.u16();
        const uint16_t bagIndex = reader.u16();
        Reader nextReader{parsed.bytes, phdr.offset + (i + 1) * 38};
        nextReader.fixedString(20);
        nextReader.u16(); nextReader.u16();
        const uint16_t nextBag = nextReader.u16();
        if (presetNumber == program && bankNumber == bank) {
            presetIndex = i;
            presetBagFirst = bagIndex;
            presetBagLast = nextBag;
            break;
        }
    }
    if (presetIndex == presetCount) {
        conversion.error = "aucun preset banque " + std::to_string(bank) + " programme "
                         + std::to_string(program) + " dans ce fichier";
        return conversion;
    }

    std::vector<RawZone> presetZones;
    GeneratorSet presetGlobal;
    readBags(pbag, pgen, presetBagFirst, presetBagLast, presetZones, presetGlobal, false);
    if (presetZones.empty()) { conversion.error = "ce preset ne désigne aucun instrument"; return conversion; }

    const size_t instrumentCount = inst.size / 22;
    const size_t smplFrames = smpl.size / 2;
    bool envelopeTaken = false;

    for (const auto& presetZone : presetZones) {
        const int32_t instrumentIndex = presetZone.generators.get(kInstrument, -1);
        if (instrumentIndex < 0 || static_cast<size_t>(instrumentIndex) + 1 >= instrumentCount) continue;

        Reader instReader{parsed.bytes, inst.offset + static_cast<size_t>(instrumentIndex) * 22};
        instReader.fixedString(20);
        const uint16_t bagFirst = instReader.u16();
        Reader nextInst{parsed.bytes, inst.offset + (static_cast<size_t>(instrumentIndex) + 1) * 22};
        nextInst.fixedString(20);
        const uint16_t bagLast = nextInst.u16();

        std::vector<RawZone> instrumentZones;
        GeneratorSet instrumentGlobal;
        readBags(ibag, igen, bagFirst, bagLast, instrumentZones, instrumentGlobal, true);

        for (const auto& zone : instrumentZones) {
            auto value = [&](uint16_t oper, int32_t fallback) {
                if (zone.generators.has(oper)) return zone.generators.get(oper, fallback);
                if (instrumentGlobal.has(oper)) return instrumentGlobal.get(oper, fallback);
                return fallback;
            };
            // La zone de PRESET ajoute, elle ne remplace pas (règle du format).
            auto added = [&](uint16_t oper) {
                int32_t total = 0;
                if (presetGlobal.has(oper)) total += presetGlobal.get(oper, 0);
                if (presetZone.generators.has(oper)) total += presetZone.generators.get(oper, 0);
                return total;
            };

            const int32_t sampleIndex = value(kSampleID, -1);
            if (sampleIndex < 0 || static_cast<size_t>(sampleIndex) >= samples.size()) continue;
            const SampleHeader& header = samples[static_cast<size_t>(sampleIndex)];

            // Étendues : la zone d'instrument POSE, la zone de preset RESTREINT.
            int lowKey = zone.generators.has(kKeyRange) ? zone.lowKey
                       : (instrumentGlobal.has(kKeyRange) ? (instrumentGlobal.get(kKeyRange, 0) & 0xFF) : 0);
            int highKey = zone.generators.has(kKeyRange) ? zone.highKey
                        : (instrumentGlobal.has(kKeyRange) ? ((instrumentGlobal.get(kKeyRange, 0x7F00) >> 8) & 0xFF) : 127);
            int lowVelocity = zone.generators.has(kVelRange) ? zone.lowVelocity
                            : (instrumentGlobal.has(kVelRange) ? (instrumentGlobal.get(kVelRange, 0) & 0xFF) : 0);
            int highVelocity = zone.generators.has(kVelRange) ? zone.highVelocity
                             : (instrumentGlobal.has(kVelRange) ? ((instrumentGlobal.get(kVelRange, 0x7F00) >> 8) & 0xFF) : 127);
            if (presetZone.generators.has(kKeyRange)) {
                lowKey = std::max(lowKey, presetZone.lowKey);
                highKey = std::min(highKey, presetZone.highKey);
            }
            if (presetZone.generators.has(kVelRange)) {
                lowVelocity = std::max(lowVelocity, presetZone.lowVelocity);
                highVelocity = std::min(highVelocity, presetZone.highVelocity);
            }
            if (lowKey > highKey || lowVelocity > highVelocity) continue; // zone vide après intersection

            // Bornes dans les données, décalages compris.
            const int64_t start = static_cast<int64_t>(header.start)
                                + value(kStartAddrsOffset, 0)
                                + 32768 * value(kStartAddrsCoarseOffset, 0);
            const int64_t end = static_cast<int64_t>(header.end)
                              + value(kEndAddrsOffset, 0)
                              + 32768 * value(kEndAddrsCoarseOffset, 0);
            if (start < 0 || end <= start || static_cast<size_t>(end) > smplFrames) continue;

            int64_t frames = end - start;
            if (maxSeconds > 0.0) {
                const int64_t limit = static_cast<int64_t>(maxSeconds * header.sampleRate);
                if (limit > 0) frames = std::min(frames, limit);
            }

            std::vector<float> audio(static_cast<size_t>(frames));
            for (int64_t i = 0; i < frames; ++i) {
                int16_t raw = 0;
                std::memcpy(&raw, parsed.bytes.data() + smpl.offset + static_cast<size_t>((start + i) * 2), 2);
                audio[static_cast<size_t>(i)] = static_cast<float>(raw) / 32768.0f;
            }

            vsm::audio::plugin::MultisampleZoneSpec spec;
            spec.program = 0; // un profil, un programme : la sélection s'est faite ici
            spec.lowNote = lowKey;
            spec.highNote = highKey;
            // Le SF2 numérote les vélocités à partir de 0, le profil à partir de
            // 1 : une vélocité nulle est un note-off, pas une nuance.
            spec.lowVelocity = std::max(1, lowVelocity);
            spec.highVelocity = highVelocity;
            spec.rootNote = value(kOverridingRootKey, -1) >= 0 ? value(kOverridingRootKey, 60)
                                                                : header.originalPitch;
            spec.tuneCents = static_cast<float>(header.pitchCorrection)
                           + static_cast<float>(100 * (value(kCoarseTune, 0) + added(kCoarseTune)))
                           + static_cast<float>(value(kFineTune, 0) + added(kFineTune));
            // Atténuation en centibels -> gain linéaire.
            const double attenuation = value(kInitialAttenuation, 0) + added(kInitialAttenuation);
            spec.level = static_cast<float>(std::pow(10.0, -attenuation / 200.0));

            const int32_t modes = value(kSampleModes, 0);
            if (modes == 1 || modes == 3) {
                const int64_t loopStart = static_cast<int64_t>(header.startLoop)
                                        + value(kStartloopAddrsOffset, 0)
                                        + 32768 * value(kStartloopAddrsCoarseOffset, 0) - start;
                const int64_t loopEnd = static_cast<int64_t>(header.endLoop)
                                      + value(kEndloopAddrsOffset, 0)
                                      + 32768 * value(kEndloopAddrsCoarseOffset, 0) - start;
                if (loopStart >= 0 && loopEnd > loopStart && loopEnd <= frames) {
                    spec.loopEnabled = true;
                    spec.loopStart = static_cast<uint64_t>(loopStart);
                    spec.loopEnd = static_cast<uint64_t>(loopEnd);
                }
            }
            if (value(kScaleTuning, 100) != 100)
                conversion.notes.push_back("zone « " + header.name + " » : scaleTuning "
                                            + std::to_string(value(kScaleTuning, 100))
                                            + " ignoré (le profil suit toujours le tempérament)");
            if (header.sampleType != 1 && header.sampleType != 0)
                conversion.notes.push_back("zone « " + header.name + " » : échantillon stéréo lié, "
                                            "converti en mono (le format de profil n'apparie pas "
                                            "encore les canaux gauche et droit d'un SF2)");

            if (!envelopeTaken) {
                conversion.attackSeconds = timecentsToSeconds(value(kAttackVolEnv, -12000));
                conversion.releaseSeconds = timecentsToSeconds(value(kReleaseVolEnv, -12000));
                envelopeTaken = true;
            }

            conversion.zones.push_back(std::move(spec));
            conversion.audioLeft.push_back(std::move(audio));
            conversion.audioRight.emplace_back();
            conversion.sampleRates.push_back(static_cast<double>(header.sampleRate));
        }
    }

    if (conversion.zones.empty()) { conversion.error = "ce preset n'a donné aucune zone jouable"; return conversion; }

    for (const auto& [name, count] : ignored) conversion.ignoredGenerators.emplace_back(name, count);
    conversion.success = true;
    return conversion;
}

SoundFontWriteResult writeSoundFontProfile(const SoundFontConversion& conversion,
                                            const std::string& folder,
                                            const std::string& profileName,
                                            const std::string& attribution) {
    SoundFontWriteResult result;
    if (!conversion.success) { result.error = "conversion en échec, rien à écrire"; return result; }
    if (attribution.empty()) {
        result.error = "attribution vide : licence inconnue, profil non écrit (§ 28)";
        return result;
    }

    std::error_code ignored;
    const std::filesystem::path base(folder);
    const std::filesystem::path samples = base / profileName;
    std::filesystem::create_directories(samples, ignored);

    vsm::audio::plugin::MultisampleProfileSpec spec;
    spec.name = profileName;
    spec.attribution = attribution;
    spec.programNames = {profileName};
    spec.zones = conversion.zones;

    for (size_t i = 0; i < spec.zones.size(); ++i) {
        const std::string relative = profileName + "/z" + std::to_string(i) + ".wav";
        spec.zones[i].relativePath = relative;
        spec.zones[i].samplePath = (base / relative).string();
        const auto& left = conversion.audioLeft[i];
        const auto& right = conversion.audioRight[i];
        vsm::audio::io::WavFileWriter::writeFile(left.data(), right.empty() ? nullptr : right.data(),
                                                  left.size(), conversion.sampleRates[i],
                                                  vsm::audio::io::SampleFormat::Int16,
                                                  spec.zones[i].samplePath);
        result.memoryBytes += left.size() * (right.empty() ? 1u : 2u) * sizeof(float);
    }

    const auto profilePath = base / (profileName + ".profile.json");
    std::ofstream profileFile(profilePath, std::ios::binary);
    if (!profileFile) { result.error = "écriture impossible : " + profilePath.string(); return result; }
    profileFile << multisampleProfileToJson(spec, folder).toString() << "\n";

    // Le preset porte l'enveloppe que le profil ne sait pas dire, et DÉSIGNE le
    // profil par son nom. Rien du SF2 n'est perdu en silence : ce qui n'entre
    // pas dans le profil entre ici, et ce qui n'entre nulle part est imprimé.
    SynthPreset preset;
    preset.name = profileName;
    preset.pluginId = "vsm.multisample";
    preset.machineName = "Multisample (acoustique échantillonné)";
    preset.profile = profileName + ".profile.json";
    preset.values["envelope.1.attack"] = conversion.attackSeconds;
    preset.values["envelope.1.release"] = conversion.releaseSeconds;
    preset.fidelity = Fidelity::Derived;

    const auto presetPath = base / (profileName + ".synth.json");
    std::ofstream presetFile(presetPath, std::ios::binary);
    if (!presetFile) { result.error = "écriture impossible : " + presetPath.string(); return result; }
    presetFile << synthPresetToJson(preset).toString() << "\n";

    result.success = true;
    result.profilePath = profilePath.string();
    result.presetPath = presetPath.string();
    return result;
}

// ---------------------------------------------------------------------------
// SF2 minimal, engendré — pour les tests, et pour rien d'autre.
// ---------------------------------------------------------------------------
//
// POURQUOI L'ÉCRIRE PLUTÔT QUE COMMETTRE UN FICHIER. Un SF2 d'essai commis est
// un binaire opaque dans le dépôt : personne ne relit ce qu'il contient, et le
// jour où un test échoue, on ne sait pas si c'est le lecteur ou le fichier. Ici,
// le contenu attendu est du CODE, relisible, et le test compare la lecture à ce
// que l'écriture a voulu dire. C'est aussi ce qui garantit qu'aucun test ne
// dépend d'une banque téléchargée, comme l'exige le § 8 du cahier des charges.

namespace {

void pushU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}
void pushU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}
void pushName(std::vector<uint8_t>& out, const std::string& name) {
    for (size_t i = 0; i < 20; ++i) out.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : 0u);
}
void pushChunk(std::vector<uint8_t>& out, const char* id, const std::vector<uint8_t>& body) {
    out.insert(out.end(), id, id + 4);
    pushU32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1u) out.push_back(0u); // RIFF aligne sur l'octet pair
}
void pushGenerator(std::vector<uint8_t>& out, uint16_t oper, int16_t amount) {
    pushU16(out, oper);
    pushU16(out, static_cast<uint16_t>(amount));
}
void pushRange(std::vector<uint8_t>& out, uint16_t oper, uint8_t low, uint8_t high) {
    pushU16(out, oper);
    out.push_back(low);
    out.push_back(high);
}

} // namespace

bool writeMinimalSoundFont(const std::string& path, std::string& outError) {
    constexpr uint32_t kRate = 44100;
    constexpr uint32_t kFrames = 4410;   // 100 ms
    constexpr uint32_t kGuard = 46;      // le format impose 46 trames nulles entre échantillons

    // Deux échantillons : la3 (220 Hz, racine 57) et la4 (440 Hz, racine 69).
    std::vector<uint8_t> pcm;
    std::vector<std::pair<uint32_t, uint32_t>> bounds;
    for (double frequency : {220.0, 440.0}) {
        const uint32_t start = static_cast<uint32_t>(pcm.size() / 2);
        for (uint32_t i = 0; i < kFrames; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const auto value = static_cast<int16_t>(std::lround(
                30000.0 * std::sin(2.0 * std::acos(-1.0) * frequency * t)));
            pushU16(pcm, static_cast<uint16_t>(value));
        }
        bounds.emplace_back(start, start + kFrames);
        for (uint32_t i = 0; i < kGuard; ++i) pushU16(pcm, 0u);
    }

    std::vector<uint8_t> shdr;
    const char* sampleNames[] = {"sinus220", "sinus440"};
    const uint8_t roots[] = {57, 69};
    for (size_t i = 0; i < 2; ++i) {
        pushName(shdr, sampleNames[i]);
        pushU32(shdr, bounds[i].first);
        pushU32(shdr, bounds[i].second);
        // Boucle sur un nombre ENTIER de périodes : 220 Hz à 44100 Hz fait
        // 200,45 trames par période, donc on boucle sur 2205 trames (11 périodes
        // à 220 Hz, 22 à 440) — exactement, ce qui rend la boucle silencieuse.
        pushU32(shdr, bounds[i].first + 1102);
        pushU32(shdr, bounds[i].first + 1102 + 2205);
        pushU32(shdr, kRate);
        shdr.push_back(roots[i]);
        shdr.push_back(0u);          // pitchCorrection
        pushU16(shdr, 0u);           // sampleLink
        pushU16(shdr, 1u);           // sampleType = monoSample
    }
    pushName(shdr, "EOS");
    for (int i = 0; i < 26; ++i) shdr.push_back(0u);

    // Instrument : quatre zones (deux étendues de notes × deux couches).
    struct ZoneDecl { uint8_t loKey, hiKey, loVel, hiVel, sample; int16_t attenuation; };
    const ZoneDecl declarations[] = {
        // L'atténuation est en CENTIBELS, pas en décibels : 60 cB = 6 dB, soit
        // la moitié de l'amplitude pour la couche douce. Confondre les deux
        // unités est l'erreur classique sur ce champ, et elle donne des couches
        // douces dix fois trop faibles.
        {48, 60,  1,  63, 0, 60},
        {48, 60, 64, 127, 0, 0},
        {61, 72,  1,  63, 1, 60},
        {61, 72, 64, 127, 1, 0},
    };

    std::vector<uint8_t> igen, ibag;
    uint16_t generatorIndex = 0;
    // Zone GLOBALE de l'instrument : elle ne se termine pas par sampleID, et
    // porte l'enveloppe commune. C'est la construction qu'une banque réelle
    // emploie, donc celle que le test doit éprouver.
    pushU16(ibag, generatorIndex); pushU16(ibag, 0u);
    pushGenerator(igen, kAttackVolEnv, -7200);   // 2^(-7200/1200) = 15,6 ms
    pushGenerator(igen, kReleaseVolEnv, -1200);  // 500 ms
    generatorIndex += 2;

    for (const auto& zone : declarations) {
        pushU16(ibag, generatorIndex); pushU16(ibag, 0u);
        pushRange(igen, kKeyRange, zone.loKey, zone.hiKey);
        pushRange(igen, kVelRange, zone.loVel, zone.hiVel);
        pushGenerator(igen, kSampleModes, 1);              // boucle continue
        pushGenerator(igen, kInitialAttenuation, zone.attenuation);
        pushGenerator(igen, 9 /* initialFilterQ */, 0);    // NON pris en charge : doit être rapporté
        pushGenerator(igen, kSampleID, static_cast<int16_t>(zone.sample));
        generatorIndex += 6;
    }
    pushU16(ibag, generatorIndex); pushU16(ibag, 0u);      // sac terminal
    pushGenerator(igen, 0u, 0);                            // générateur terminal

    std::vector<uint8_t> inst;
    pushName(inst, "essai");
    pushU16(inst, 0u);
    pushName(inst, "EOI");
    pushU16(inst, static_cast<uint16_t>(ibag.size() / 4 - 1));

    std::vector<uint8_t> pgen, pbag;
    pushU16(pbag, 0u); pushU16(pbag, 0u);
    pushGenerator(pgen, kInstrument, 0);
    pushU16(pbag, 1u); pushU16(pbag, 0u);                  // sac terminal
    pushGenerator(pgen, 0u, 0);

    std::vector<uint8_t> phdr;
    pushName(phdr, "Essai minimal");
    pushU16(phdr, 0u);   // programme
    pushU16(phdr, 0u);   // banque
    pushU16(phdr, 0u);   // premier sac
    pushU32(phdr, 0u); pushU32(phdr, 0u); pushU32(phdr, 0u);
    pushName(phdr, "EOP");
    pushU16(phdr, 0u); pushU16(phdr, 0u);
    pushU16(phdr, 1u);
    pushU32(phdr, 0u); pushU32(phdr, 0u); pushU32(phdr, 0u);

    std::vector<uint8_t> emptyModulator(10, 0u);

    std::vector<uint8_t> info;
    { std::vector<uint8_t> ifil; pushU16(ifil, 2u); pushU16(ifil, 1u); pushChunk(info, "ifil", ifil); }
    { std::vector<uint8_t> isng; const char* t = "EMU8000\0"; isng.assign(t, t + 8); pushChunk(info, "isng", isng); }
    { std::vector<uint8_t> inam; const char* t = "VSM SF2 minimal\0"; inam.assign(t, t + 16); pushChunk(info, "INAM", inam); }

    std::vector<uint8_t> sdta;
    pushChunk(sdta, "smpl", pcm);

    std::vector<uint8_t> pdta;
    pushChunk(pdta, "phdr", phdr);
    pushChunk(pdta, "pbag", pbag);
    pushChunk(pdta, "pmod", emptyModulator);
    pushChunk(pdta, "pgen", pgen);
    pushChunk(pdta, "inst", inst);
    pushChunk(pdta, "ibag", ibag);
    pushChunk(pdta, "imod", emptyModulator);
    pushChunk(pdta, "igen", igen);
    pushChunk(pdta, "shdr", shdr);

    std::vector<uint8_t> body;
    body.insert(body.end(), {'s', 'f', 'b', 'k'});
    body.insert(body.end(), {'L', 'I', 'S', 'T'});
    pushU32(body, static_cast<uint32_t>(info.size() + 4));
    body.insert(body.end(), {'I', 'N', 'F', 'O'});
    body.insert(body.end(), info.begin(), info.end());
    body.insert(body.end(), {'L', 'I', 'S', 'T'});
    pushU32(body, static_cast<uint32_t>(sdta.size() + 4));
    body.insert(body.end(), {'s', 'd', 't', 'a'});
    body.insert(body.end(), sdta.begin(), sdta.end());
    body.insert(body.end(), {'L', 'I', 'S', 'T'});
    pushU32(body, static_cast<uint32_t>(pdta.size() + 4));
    body.insert(body.end(), {'p', 'd', 't', 'a'});
    body.insert(body.end(), pdta.begin(), pdta.end());

    std::vector<uint8_t> file;
    file.insert(file.end(), {'R', 'I', 'F', 'F'});
    pushU32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) { outError = "écriture impossible : " + path; return false; }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) { outError = "écriture incomplète : " + path; return false; }
    return true;
}

} // namespace vsm::interchange
