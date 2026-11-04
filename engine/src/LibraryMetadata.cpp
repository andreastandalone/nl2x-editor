#include "nl2x/LibraryMetadata.h"

#include "LibraryGroups.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <variant>

namespace nl2x {

namespace {

// Written in the file; a file with a higher version came from a newer editor and is not
// overwritten, since this version would drop what it does not know.
constexpr int kFormatVersion = 1;
constexpr const char* kFormatName = "NL2X Editor Library";
// Far above any real file (about 100 bytes per sound).
constexpr std::uintmax_t kLargestMetadataFileSize = 16 * 1024 * 1024;
constexpr int kDeepestNesting = 32;

// ---- Fingerprints ----------------------------------------------------------------------------

template <typename Values>
void appendValues(std::vector<std::uint8_t>& out, const Values& values)
{
    for (const auto value : values)
        out.push_back(static_cast<std::uint8_t>(value));
}

std::string_view kindName(LibraryEntryKind kind)
{
    switch (kind) {
    case LibraryEntryKind::Program:
        return "program";
    case LibraryEntryKind::PercussionKit:
        return "kit";
    case LibraryEntryKind::Performance:
        return "performance";
    }
    return {};
}

std::optional<LibraryEntryKind> kindNamed(std::string_view name)
{
    for (auto kind : { LibraryEntryKind::Program, LibraryEntryKind::PercussionKit, LibraryEntryKind::Performance })
        if (kindName(kind) == name)
            return kind;
    return std::nullopt;
}

std::size_t valueCount(LibraryEntryKind kind)
{
    switch (kind) {
    case LibraryEntryKind::Program:
        return kProgramParameterCount;
    case LibraryEntryKind::PercussionKit:
        return kProgramParameterCount * kPercussionKitProgramCount;
    case LibraryEntryKind::Performance:
        return kPerformanceParameterCount;
    }
    return 0;
}

std::string hexOf(const std::vector<std::uint8_t>& values)
{
    std::string text;
    text.reserve(values.size() * 2);
    for (const auto value : values) {
        text += "0123456789abcdef"[value >> 4];
        text += "0123456789abcdef"[value & 0xF];
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> valuesOfHex(std::string_view text)
{
    if (text.size() % 2 != 0)
        return std::nullopt;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> values;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        values.push_back(static_cast<std::uint8_t>(high * 16 + low));
    }
    return values;
}

std::string indexKey(const SoundFingerprint& fingerprint)
{
    return std::string(kindName(fingerprint.kind)) + ":" + hexOf(fingerprint.values);
}

int idNumber(const std::string& id)
{
    if (id.size() < 2 || id[0] != 's')
        return 0;
    int number = 0;
    for (std::size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9' || number > 100'000'000)
            return 0;
        number = number * 10 + (id[i] - '0');
    }
    return number;
}

template <typename Sounds>
int highestIdNumber(const Sounds& sounds)
{
    int highest = 0;
    for (const auto& [id, sound] : sounds)
        highest = std::max(highest, idNumber(id));
    return highest;
}

// Ids sorted by number, so that the file lists sounds in the order they were tagged.
struct ByIdNumber {
    bool operator()(const std::string& a, const std::string& b) const
    {
        const auto na = idNumber(a);
        const auto nb = idNumber(b);
        return na != nb ? na < nb : a < b;
    }
};

// ---- JSON ------------------------------------------------------------------------------------

// Just enough JSON for this file: every value kind is parsed (so that a file edited by hand with
// extra fields still reads), only strings, booleans, numbers and objects are used.
struct JsonValue;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
struct JsonValue {
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<JsonObject>,
                 std::shared_ptr<std::vector<JsonValue>>>
        value;

    const JsonValue* member(std::string_view name) const
    {
        const auto* object = std::get_if<std::shared_ptr<JsonObject>>(&value);
        if (object == nullptr)
            return nullptr;
        for (const auto& [key, child] : **object)
            if (key == name)
                return &child;
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view jsonText) : text(jsonText) {}

    std::optional<JsonValue> parseDocument()
    {
        auto result = parseValue(0);
        skipSpace();
        if (! result || position != text.size())
            return std::nullopt;
        return result;
    }

private:
    void skipSpace()
    {
        while (position < text.size()
               && (text[position] == ' ' || text[position] == '\t' || text[position] == '\n' || text[position] == '\r'))
            ++position;
    }

    bool consume(std::string_view literal)
    {
        if (text.substr(position, literal.size()) != literal)
            return false;
        position += literal.size();
        return true;
    }

    std::optional<JsonValue> parseValue(int depth)
    {
        if (depth > kDeepestNesting)
            return std::nullopt;
        skipSpace();
        if (position >= text.size())
            return std::nullopt;
        const char c = text[position];
        if (c == '{')
            return parseObject(depth);
        if (c == '[')
            return parseArray(depth);
        if (c == '"') {
            auto string = parseString();
            if (! string)
                return std::nullopt;
            return JsonValue { std::move(*string) };
        }
        if (consume("true"))
            return JsonValue { true };
        if (consume("false"))
            return JsonValue { false };
        if (consume("null"))
            return JsonValue { nullptr };
        return parseNumber();
    }

    std::optional<JsonValue> parseObject(int depth)
    {
        ++position; // '{'
        auto object = std::make_shared<JsonObject>();
        skipSpace();
        if (consume("}"))
            return JsonValue { object };
        while (true) {
            skipSpace();
            if (position >= text.size() || text[position] != '"')
                return std::nullopt;
            auto key = parseString();
            skipSpace();
            if (! key || ! consume(":"))
                return std::nullopt;
            auto child = parseValue(depth + 1);
            if (! child)
                return std::nullopt;
            object->emplace_back(std::move(*key), std::move(*child));
            skipSpace();
            if (consume("}"))
                return JsonValue { object };
            if (! consume(","))
                return std::nullopt;
        }
    }

    std::optional<JsonValue> parseArray(int depth)
    {
        ++position; // '['
        auto array = std::make_shared<std::vector<JsonValue>>();
        skipSpace();
        if (consume("]"))
            return JsonValue { array };
        while (true) {
            auto child = parseValue(depth + 1);
            if (! child)
                return std::nullopt;
            array->push_back(std::move(*child));
            skipSpace();
            if (consume("]"))
                return JsonValue { array };
            if (! consume(","))
                return std::nullopt;
        }
    }

    std::optional<JsonValue> parseNumber()
    {
        const auto start = position;
        if (position < text.size() && text[position] == '-')
            ++position;
        bool digits = false;
        while (position < text.size()
               && ((text[position] >= '0' && text[position] <= '9') || text[position] == '.' || text[position] == 'e'
                   || text[position] == 'E' || text[position] == '+' || text[position] == '-')) {
            digits = digits || (text[position] >= '0' && text[position] <= '9');
            ++position;
        }
        if (! digits)
            return std::nullopt;
        const std::string number(text.substr(start, position - start));
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size())
            return std::nullopt;
        return JsonValue { value };
    }

    std::optional<unsigned> parseHex4()
    {
        if (position + 4 > text.size())
            return std::nullopt;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[position++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else
                return std::nullopt;
        }
        return value;
    }

    static void appendUtf8(std::string& out, unsigned codePoint)
    {
        if (codePoint < 0x80) {
            out += static_cast<char>(codePoint);
        } else if (codePoint < 0x800) {
            out += static_cast<char>(0xC0 | (codePoint >> 6));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codePoint >> 12));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codePoint >> 18));
            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::optional<std::string> parseString()
    {
        ++position; // '"'
        std::string out;
        while (position < text.size()) {
            const char c = text[position++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                return std::nullopt;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (position >= text.size())
                return std::nullopt;
            const char escaped = text[position++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out += escaped;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                auto unit = parseHex4();
                if (! unit)
                    return std::nullopt;
                unsigned codePoint = *unit;
                if (codePoint >= 0xD800 && codePoint < 0xDC00) {
                    if (! consume("\\u"))
                        return std::nullopt;
                    auto low = parseHex4();
                    if (! low || *low < 0xDC00 || *low >= 0xE000)
                        return std::nullopt;
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*low - 0xDC00);
                } else if (codePoint >= 0xDC00 && codePoint < 0xE000) {
                    return std::nullopt;
                }
                appendUtf8(out, codePoint);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::string_view text;
    std::size_t position = 0;
};

void writeJsonString(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (u < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", u);
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
}

// ---- The file --------------------------------------------------------------------------------

using Sounds = std::map<std::string, LibraryMetadata::Sound>;

struct ReadResult {
    LibraryMetadataStatus status = LibraryMetadataStatus::Ready;
    Sounds sounds;
};

const std::string* stringMember(const JsonValue& object, std::string_view name, bool& wrongType)
{
    const auto* member = object.member(name);
    if (member == nullptr)
        return nullptr;
    const auto* text = std::get_if<std::string>(&member->value);
    wrongType = wrongType || text == nullptr;
    return text;
}

std::optional<Sounds> soundsOf(const JsonValue& document)
{
    const auto* format = document.member("format");
    const auto* version = document.member("version");
    const auto* sounds = document.member("sounds");
    if (format == nullptr || version == nullptr || sounds == nullptr)
        return std::nullopt;
    const auto* formatName = std::get_if<std::string>(&format->value);
    const auto* versionNumber = std::get_if<double>(&version->value);
    const auto* soundArray = std::get_if<std::shared_ptr<std::vector<JsonValue>>>(&sounds->value);
    if (formatName == nullptr || *formatName != kFormatName || versionNumber == nullptr || soundArray == nullptr
        || *versionNumber < 1 || *versionNumber > kFormatVersion)
        return std::nullopt;

    Sounds result;
    for (const auto& value : **soundArray) {
        if (! std::holds_alternative<std::shared_ptr<JsonObject>>(value.value))
            return std::nullopt;
        bool wrongType = false;
        const auto* id = stringMember(value, "id", wrongType);
        const auto* kind = stringMember(value, "kind", wrongType);
        const auto* values = stringMember(value, "values", wrongType);
        const auto* name = stringMember(value, "name", wrongType);
        const auto* category = stringMember(value, "category", wrongType);
        if (wrongType || id == nullptr || kind == nullptr || values == nullptr || result.count(*id) > 0)
            return std::nullopt;
        LibraryMetadata::Sound sound;
        const auto entryKind = kindNamed(*kind);
        auto bytes = valuesOfHex(*values);
        if (! entryKind || ! bytes || bytes->size() != valueCount(*entryKind))
            return std::nullopt;
        sound.fingerprint = { *entryKind, std::move(*bytes) };
        if (name != nullptr)
            sound.info.name = *name;
        if (category != nullptr)
            sound.info.category = *category;
        if (const auto* favourite = value.member("favourite")) {
            const auto* flag = std::get_if<bool>(&favourite->value);
            if (flag == nullptr)
                return std::nullopt;
            sound.info.favourite = *flag;
        }
        if (const auto* autoAssigned = value.member("autoAssigned")) {
            const auto* flag = std::get_if<bool>(&autoAssigned->value);
            if (flag == nullptr)
                return std::nullopt;
            // A mark with no category marks nothing.
            sound.info.autoAssigned = *flag && ! sound.info.category.empty();
        }
        if (! sound.info.isEmpty())
            result[*id] = std::move(sound);
    }
    return result;
}

ReadResult readFile(const std::filesystem::path& path)
{
    std::error_code error;
    if (! std::filesystem::exists(path, error) && ! error)
        return {};

    ReadResult unreadable { LibraryMetadataStatus::Unreadable, {} };
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kLargestMetadataFileSize)
        return unreadable;
    std::ifstream stream(path, std::ios::binary);
    if (! stream)
        return unreadable;
    const std::string text { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    if (! stream.eof() && stream.fail())
        return unreadable;

    const auto document = JsonParser(text).parseDocument();
    if (! document)
        return unreadable;
    auto sounds = soundsOf(*document);
    if (! sounds)
        return unreadable;
    return { LibraryMetadataStatus::Ready, std::move(*sounds) };
}

std::string toJson(const Sounds& sounds)
{
    std::vector<std::string> ids;
    for (const auto& [id, sound] : sounds)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end(), ByIdNumber {});

    std::string out = "{\n  \"format\": ";
    writeJsonString(out, kFormatName);
    out += ",\n  \"version\": " + std::to_string(kFormatVersion) + ",\n  \"sounds\": [";
    bool first = true;
    for (const auto& id : ids) {
        const auto& sound = sounds.at(id);
        out += first ? "\n    { \"id\": " : ",\n    { \"id\": ";
        first = false;
        writeJsonString(out, id);
        if (! sound.info.name.empty()) {
            out += ", \"name\": ";
            writeJsonString(out, sound.info.name);
        }
        if (! sound.info.category.empty()) {
            out += ", \"category\": ";
            writeJsonString(out, sound.info.category);
            if (sound.info.autoAssigned)
                out += ", \"autoAssigned\": true";
        }
        if (sound.info.favourite)
            out += ", \"favourite\": true";
        out += ",\n      \"kind\": ";
        writeJsonString(out, kindName(sound.fingerprint.kind));
        out += ", \"values\": ";
        writeJsonString(out, hexOf(sound.fingerprint.values));
        out += " }";
    }
    out += first ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

// Writes `text` next to `path`, keeps the old file as .bak, then renames the new one over it
// (atomic on the same volume).
bool writeAtomically(const std::filesystem::path& path, const std::string& text)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (! stream)
            return false;
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        if (! stream) {
            stream.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    if (std::filesystem::exists(path, error)) {
        auto backup = path;
        backup += ".bak";
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, error);
        // A backup that could not be made does not stop the save: the new file is complete.
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace

SoundFingerprint SoundFingerprint::of(const LibraryEntry& entry)
{
    if (const auto* program = std::get_if<ProgramDump>(&entry.dump))
        return of(program->program);
    if (const auto* kit = std::get_if<PercussionKitDump>(&entry.dump)) {
        SoundFingerprint fingerprint { LibraryEntryKind::PercussionKit, {} };
        for (const auto& kitProgram : kit->kit)
            appendValues(fingerprint.values, kitProgram.bytes());
        return fingerprint;
    }
    return of(std::get<PerformanceDump>(entry.dump).performance);
}

SoundFingerprint SoundFingerprint::of(const Program& program)
{
    SoundFingerprint fingerprint { LibraryEntryKind::Program, {} };
    appendValues(fingerprint.values, program.bytes());
    return fingerprint;
}

SoundFingerprint SoundFingerprint::of(const Performance& performance)
{
    SoundFingerprint fingerprint { LibraryEntryKind::Performance, {} };
    appendValues(fingerprint.values, performance.bytes());
    return fingerprint;
}

int soundDifferences(const SoundFingerprint& a, const SoundFingerprint& b)
{
    if (a.kind != b.kind || a.values.size() != b.values.size())
        return -1;
    int count = 0;
    for (std::size_t i = 0; i < a.values.size(); ++i)
        count += a.values[i] != b.values[i] ? 1 : 0;
    return count;
}

bool LibraryFilter::matches(const SoundInfo& info) const
{
    return (category.empty() || info.category == category) && (! favouritesOnly || info.favourite);
}

LibraryMetadata LibraryMetadata::load(const std::filesystem::path& libraryFolder)
{
    LibraryMetadata metadata;
    metadata.folder = libraryFolder;
    auto read = readFile(metadata.filePath());
    metadata.currentStatus = read.status;
    metadata.sounds = std::move(read.sounds);
    metadata.reindex();
    return metadata;
}

std::filesystem::path LibraryMetadata::filePath() const
{
    return folder / kLibraryMetadataFileName;
}

void LibraryMetadata::reindex()
{
    exactIndex.clear();
    for (const auto& [id, sound] : sounds)
        // Two sounds with the same values (two windows tagged the same new sound at once): the
        // first one tagged wins.
        if (const auto [it, added] = exactIndex.emplace(indexKey(sound.fingerprint), id);
            ! added && ByIdNumber {}(id, it->second))
            it->second = id;
}

void LibraryMetadata::setLibraryContents(const std::vector<LibraryFile>& files)
{
    presentInLibrary.clear();
    for (const auto& file : files)
        if (! file.isBackup)
            for (const auto& entry : file.entries)
                presentInLibrary.insert(indexKey(SoundFingerprint::of(entry)));
}

std::optional<SoundMatch> LibraryMetadata::match(const SoundFingerprint& fingerprint) const
{
    if (const auto exact = exactIndex.find(indexKey(fingerprint)); exact != exactIndex.end())
        return SoundMatch { exact->second, 0 };

    std::optional<SoundMatch> best;
    bool tied = false;
    for (const auto& [id, sound] : sounds) {
        const auto differences = soundDifferences(sound.fingerprint, fingerprint);
        if (differences < 0 || differences > kSimilarSoundMaxDifferences)
            continue;
        // The sound is still in the Library as it was: this is a variation of it, not the sound.
        if (presentInLibrary.count(indexKey(sound.fingerprint)) > 0)
            continue;
        if (! best || differences < best->differences) {
            best = SoundMatch { id, differences };
            tied = false;
        } else if (differences == best->differences) {
            tied = true;
        }
    }
    if (tied)
        return std::nullopt;
    return best;
}

SoundInfo LibraryMetadata::info(const SoundFingerprint& fingerprint) const
{
    const auto found = match(fingerprint);
    return found ? sounds.at(found->soundId).info : SoundInfo {};
}

std::string LibraryMetadata::displayName(const LibraryEntry& entry) const
{
    auto name = info(entry).name;
    return name.empty() ? entry.name : name;
}

MetadataSaveResult LibraryMetadata::rewrite(const std::map<std::string, Sound>& newSounds)
{
    std::error_code error;
    const bool nothingToWrite = newSounds.empty() && ! std::filesystem::exists(filePath(), error);
    if (! nothingToWrite && ! writeAtomically(filePath(), toJson(newSounds)))
        return MetadataSaveResult::Failed;
    currentStatus = LibraryMetadataStatus::Ready;
    sounds = newSounds;
    reindex();
    return MetadataSaveResult::Saved;
}

MetadataSaveResult LibraryMetadata::set(const SoundFingerprint& fingerprint, const SoundInfo& newInfo)
{
    auto onDisk = readFile(filePath());
    if (onDisk.status == LibraryMetadataStatus::Unreadable) {
        currentStatus = LibraryMetadataStatus::Unreadable;
        sounds.clear();
        reindex();
        return MetadataSaveResult::RefusedUnreadable;
    }

    // Match against what is on disk, with this window's view of the Library.
    LibraryMetadata current = *this;
    current.sounds = onDisk.sounds;
    current.reindex();
    const auto found = current.match(fingerprint);

    auto newSounds = std::move(onDisk.sounds);
    auto stored = newInfo;
    if (stored.autoAssigned) {
        const auto* before = found ? &newSounds.at(found->soundId).info : nullptr;
        stored.autoAssigned = before != nullptr && before->autoAssigned && before->category == stored.category
                              && ! stored.category.empty();
    }
    if (found && stored.isEmpty()) {
        newSounds.erase(found->soundId);
    } else if (found) {
        newSounds[found->soundId] = { fingerprint, stored };
    } else if (! stored.isEmpty()) {
        newSounds["s" + std::to_string(highestIdNumber(newSounds) + 1)] = { fingerprint, stored };
    }
    return rewrite(newSounds);
}

MetadataSaveResult LibraryMetadata::autoAssign(const std::vector<AutoAssignment>& assignments)
{
    auto onDisk = readFile(filePath());
    if (onDisk.status == LibraryMetadataStatus::Unreadable) {
        currentStatus = LibraryMetadataStatus::Unreadable;
        sounds.clear();
        reindex();
        return MetadataSaveResult::RefusedUnreadable;
    }

    // Each assignment is matched against what is on disk plus the sounds added before it, as a
    // series of set() calls would, but the index grows one sound at a time instead of being rebuilt.
    LibraryMetadata current = *this;
    current.sounds = std::move(onDisk.sounds);
    current.reindex();
    int next = highestIdNumber(current.sounds) + 1;
    for (const auto& [fingerprint, category] : assignments) {
        if (category.empty())
            continue;
        if (const auto found = current.match(fingerprint)) {
            auto& sound = current.sounds.at(found->soundId);
            if (! sound.info.category.empty())
                continue;
            sound.info.category = category;
            sound.info.autoAssigned = true;
            if (sound.fingerprint != fingerprint) {
                sound.fingerprint = fingerprint;
                current.exactIndex.emplace(indexKey(fingerprint), found->soundId);
            }
        } else {
            const auto id = "s" + std::to_string(next++);
            SoundInfo info;
            info.category = category;
            info.autoAssigned = true;
            current.sounds[id] = { fingerprint, info };
            current.exactIndex.emplace(indexKey(fingerprint), id);
        }
    }
    return rewrite(current.sounds);
}

MetadataSaveResult LibraryMetadata::removeAutoAssigned()
{
    auto onDisk = readFile(filePath());
    if (onDisk.status == LibraryMetadataStatus::Unreadable) {
        currentStatus = LibraryMetadataStatus::Unreadable;
        sounds.clear();
        reindex();
        return MetadataSaveResult::RefusedUnreadable;
    }
    auto newSounds = std::move(onDisk.sounds);
    for (auto sound = newSounds.begin(); sound != newSounds.end();) {
        auto& info = sound->second.info;
        if (info.autoAssigned) {
            info.category.clear();
            info.autoAssigned = false;
        }
        sound = info.isEmpty() ? newSounds.erase(sound) : std::next(sound);
    }
    return rewrite(newSounds);
}

int LibraryMetadata::autoAssignedCount() const
{
    return static_cast<int>(std::count_if(sounds.begin(), sounds.end(),
                                          [](const auto& sound) { return sound.second.info.autoAssigned; }));
}

MetadataSaveResult LibraryMetadata::followEdit(const std::string& soundId, const SoundFingerprint& edited)
{
    auto onDisk = readFile(filePath());
    if (onDisk.status == LibraryMetadataStatus::Unreadable) {
        currentStatus = LibraryMetadataStatus::Unreadable;
        sounds.clear();
        reindex();
        return MetadataSaveResult::RefusedUnreadable;
    }
    const auto sound = onDisk.sounds.find(soundId);
    const auto key = indexKey(edited);
    const bool taken = std::any_of(onDisk.sounds.begin(), onDisk.sounds.end(), [&](const auto& other) {
        return other.first != soundId && indexKey(other.second.fingerprint) == key;
    });
    if (sound == onDisk.sounds.end() || taken || sound->second.fingerprint == edited) {
        sounds = std::move(onDisk.sounds);
        reindex();
        return MetadataSaveResult::Saved;
    }
    sound->second.fingerprint = edited;
    return rewrite(onDisk.sounds);
}

std::vector<std::string> LibraryMetadata::categoryChoices() const
{
    std::vector<std::string> choices(kLibraryCategories.begin(), kLibraryCategories.end());
    std::set<std::string> others;
    for (const auto& [id, sound] : sounds)
        if (! sound.info.category.empty()
            && std::find(choices.begin(), choices.end(), sound.info.category) == choices.end())
            others.insert(sound.info.category);
    choices.insert(choices.end(), others.begin(), others.end());
    return choices;
}

void SoundLineage::loaded(const std::string& place, const std::string& soundId, const SoundFingerprint& fingerprint)
{
    links[place] = { soundId, fingerprint };
}

void SoundLineage::observe(const std::string& place, const std::optional<SoundFingerprint>& current)
{
    const auto link = links.find(place);
    if (link == links.end())
        return;
    const auto differences = current ? soundDifferences(link->second.last, *current) : -1;
    if (differences < 0 || differences > kLineageMaxStepDifferences)
        links.erase(link);
    else
        link->second.last = *current;
}

void SoundLineage::forget(const std::string& place)
{
    links.erase(place);
}

std::optional<std::string> SoundLineage::soundAt(const std::string& place) const
{
    const auto link = links.find(place);
    if (link == links.end())
        return std::nullopt;
    return link->second.soundId;
}

std::vector<LibraryEntryRef> filterLibrary(const std::vector<LibraryFile>& files, const LibraryMetadata& metadata,
                                           const LibraryFilter& filter)
{
    std::vector<LibraryEntryRef> result;
    for (const auto& file : files) {
        if (file.isBackup)
            continue;
        for (const auto& entry : file.entries)
            if (filter.matches(metadata.info(entry)))
                result.push_back({ &file, &entry });
    }
    return result;
}

LibraryEntryGroup groupEntries(const std::vector<LibraryEntryRef>& entries)
{
    LibraryEntryGroup library;
    for (const auto& ref : entries)
        groupFor(library, ref.file->folders).entries.push_back(ref);
    return library;
}

} // namespace nl2x
