#include "nl2x/Library.h"

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace nl2x {

namespace {

// Far above any Nord Lead 2X file (a Program Bank is 24 391 bytes): anything larger is not one,
// and reading it would only stall the scan.
constexpr std::uintmax_t kLargestLibraryFileSize = 4 * 1024 * 1024;

// The ten letters of the synth's Performance names A0–L9; I and K are skipped [OFF-2X p.16].
constexpr std::array<char, 10> kPerformanceLetters { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'L' };
constexpr std::size_t kPercussionKitsPerBank = 10;

// U+2013 EN DASH, as in the spec's "Bank N – P".
constexpr const char* kEntrySeparator = " \xE2\x80\x93 ";

std::optional<LibraryEntry> entryOf(std::span<const std::uint8_t> message)
{
    switch (message.size()) {
    case kProgramDumpSize:
        if (auto dump = parseProgramDump(message))
            return LibraryEntry { {}, *dump };
        break;
    case kPercussionKitDumpSize:
        if (auto dump = parsePercussionKitDump(message))
            return LibraryEntry { {}, *dump };
        break;
    case kPerformanceDumpSize:
        if (auto dump = parsePerformanceDump(message))
            return LibraryEntry { {}, *dump };
        break;
    default:
        break;
    }
    return std::nullopt;
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isHidden(const std::filesystem::path& path)
{
    const auto name = path.filename().string();
    return ! name.empty() && name.front() == '.';
}

bool isEditBufferDump(const LibraryEntry& entry)
{
    if (const auto* program = std::get_if<ProgramDump>(&entry.dump))
        return program->type == hardware::kProgramEditBufferDumpType && program->spec < kAllSlots.size();
    if (const auto* kit = std::get_if<PercussionKitDump>(&entry.dump))
        return kit->type == hardware::kProgramEditBufferDumpType
               && kit->spec >= hardware::kPercussionKitEditBufferSpecOfSlotA
               && kit->spec < hardware::kPercussionKitEditBufferSpecOfSlotA + kAllSlots.size();
    const auto& performance = std::get<PerformanceDump>(entry.dump);
    return performance.type == hardware::kPerformanceEditBufferDumpType;
}

std::vector<std::uint8_t> entrySysEx(const LibraryEntry& entry)
{
    return std::visit([](const auto& dump) { return toSysEx(dump); }, entry.dump);
}

} // namespace

std::string_view libraryFileKindName(LibraryFileKind kind)
{
    switch (kind) {
    case LibraryFileKind::Program:
        return "Program";
    case LibraryFileKind::PercussionKit:
        return "Percussion Kit";
    case LibraryFileKind::Performance:
        return "Performance";
    case LibraryFileKind::ProgramBank:
        return "Program Bank";
    case LibraryFileKind::PerformanceBank:
        return "Performance Bank";
    case LibraryFileKind::Mixed:
        return "Programs and Performances";
    case LibraryFileKind::Unknown:
        return "not a Nord Lead 2X file";
    }
    return {};
}

std::string libraryEntryName(const std::string& fileName, LibraryEntryKind kind, std::size_t index)
{
    std::size_t digits = 0;
    while (digits < fileName.size() && std::isdigit(static_cast<unsigned char>(fileName[fileName.size() - 1 - digits])))
        ++digits;
    const auto bank = digits > 0 ? "Bank " + fileName.substr(fileName.size() - digits) : fileName;

    std::string location;
    switch (kind) {
    case LibraryEntryKind::Program:
        location = std::to_string(index + 1);
        break;
    case LibraryEntryKind::PercussionKit:
        location = index < kPercussionKitsPerBank ? "P" + std::to_string(index) : std::to_string(index + 1);
        break;
    case LibraryEntryKind::Performance:
        if (index < kPerformanceLetters.size() * 10)
            location = std::string(1, kPerformanceLetters[index / 10]) + std::to_string(index % 10);
        else
            location = std::to_string(index + 1);
        break;
    }
    return bank + kEntrySeparator + location;
}

LibraryFile recogniseLibraryFile(const std::string& name, std::span<const std::uint8_t> bytes)
{
    LibraryFile file;
    file.name = name;

    for (const auto& message : splitSysExMessages(bytes)) {
        if (auto entry = entryOf(message))
            file.entries.push_back(std::move(*entry));
        else
            ++file.ignoredMessageCount;
    }

    std::array<std::size_t, 3> counts {};
    for (const auto& entry : file.entries)
        ++counts[static_cast<std::size_t>(entry.kind())];
    const auto programsOrKits = counts[0] + counts[1];
    const auto performances = counts[2];

    if (file.entries.empty())
        file.kind = LibraryFileKind::Unknown;
    else if (file.entries.size() == 1)
        file.kind = static_cast<LibraryFileKind>(file.entries.front().kind());
    else if (performances == 0)
        file.kind = LibraryFileKind::ProgramBank;
    else if (programsOrKits == 0)
        file.kind = LibraryFileKind::PerformanceBank;
    else
        file.kind = LibraryFileKind::Mixed;

    if (file.entries.size() == 1) {
        file.entries.front().name = name;
    } else {
        std::array<std::size_t, 3> next {};
        for (auto& entry : file.entries) {
            auto& index = next[static_cast<std::size_t>(entry.kind())];
            entry.name = libraryEntryName(name, entry.kind(), index++);
        }
    }
    return file;
}

LibraryFile openLibraryFile(const std::filesystem::path& path)
{
    LibraryFile unreadable;
    unreadable.path = path;
    unreadable.name = path.stem().string();

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kLargestLibraryFileSize)
        return unreadable;

    std::ifstream stream(path, std::ios::binary);
    if (! stream)
        return unreadable;
    const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };

    auto file = recogniseLibraryFile(unreadable.name, bytes);
    file.path = path;
    return file;
}

std::vector<LibraryFile> scanLibrary(const std::filesystem::path& folder)
{
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    if (! std::filesystem::is_directory(folder, error))
        return {};

    std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied,
                                                     error);
    const std::filesystem::recursive_directory_iterator end;
    for (; ! error && it != end; it.increment(error)) {
        const auto& path = it->path();
        if (isHidden(path)) {
            if (it->is_directory(error))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error) && lowercase(path.extension().string()) == ".syx")
            paths.push_back(path);
    }

    std::sort(paths.begin(), paths.end());
    std::vector<LibraryFile> files;
    files.reserve(paths.size());
    for (const auto& path : paths)
        files.push_back(openLibraryFile(path));
    return files;
}

std::vector<std::uint8_t> toSysEx(const LibraryFile& file)
{
    std::vector<std::uint8_t> bytes;
    for (const auto& entry : file.entries) {
        const auto message = entrySysEx(entry);
        bytes.insert(bytes.end(), message.begin(), message.end());
    }
    return bytes;
}

std::vector<std::uint8_t> editBufferDumpOf(const LibraryEntry& entry)
{
    if (isEditBufferDump(entry))
        return entrySysEx(entry);

    if (const auto* program = std::get_if<ProgramDump>(&entry.dump)) {
        auto dump = *program;
        dump.type = hardware::kProgramEditBufferDumpType;
        dump.spec = static_cast<std::uint8_t>(slotIndex(Slot::A));
        return toSysEx(dump);
    }
    if (const auto* kit = std::get_if<PercussionKitDump>(&entry.dump)) {
        auto dump = *kit;
        dump.type = hardware::kProgramEditBufferDumpType;
        dump.spec = hardware::kPercussionKitEditBufferSpecOfSlotA;
        return toSysEx(dump);
    }
    auto dump = std::get<PerformanceDump>(entry.dump);
    dump.type = hardware::kPerformanceEditBufferDumpType;
    dump.spec = 0;
    return toSysEx(dump);
}

AuditionResult audition(Engine& engine, const LibraryEntry& entry, Slot slot, TimeMs timeMs)
{
    if (const auto* program = std::get_if<ProgramDump>(&entry.dump))
        return engine.auditionProgram(slot, program->program, timeMs);
    if (const auto* kit = std::get_if<PercussionKitDump>(&entry.dump))
        return engine.auditionPercussionKit(slot, kit->kit, timeMs);
    return engine.auditionPerformance(std::get<PerformanceDump>(entry.dump).performance, timeMs);
}

std::optional<std::vector<std::uint8_t>> slotProgramFile(const Engine& engine, Slot slot)
{
    const auto& state = engine.slotState(slot);
    if (state.content != SlotContent::Program || ! state.program)
        return std::nullopt;
    const ProgramDump dump { engine.globalChannel().value_or(hardware::kFactoryGlobalChannel),
                             hardware::kProgramEditBufferDumpType, static_cast<std::uint8_t>(slotIndex(slot)),
                             *state.program };
    return toSysEx(dump);
}

std::optional<std::vector<std::uint8_t>> performanceFile(const Engine& engine)
{
    if (! engine.performance())
        return std::nullopt;
    auto performance = *engine.performance();
    for (auto slot : kAllSlots) {
        const auto& state = engine.slotState(slot);
        if (state.content == SlotContent::Program && state.program)
            performance.setProgram(slot, *state.program);
    }
    const PerformanceDump dump { engine.globalChannel().value_or(hardware::kFactoryGlobalChannel),
                                 hardware::kPerformanceEditBufferDumpType, 0, performance };
    return toSysEx(dump);
}

std::string libraryFileName(const std::string& name)
{
    std::string cleaned;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        // '/' and ':' are refused by macOS Finder; the rest by Windows, for files that travel.
        const bool refused = u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
                             || c == '<' || c == '>' || c == '|';
        cleaned += refused ? '-' : c;
    }
    const auto first = cleaned.find_first_not_of(" .");
    const auto last = cleaned.find_last_not_of(" .");
    cleaned = first == std::string::npos ? std::string() : cleaned.substr(first, last - first + 1);
    if (cleaned.empty())
        cleaned = "Untitled";
    if (lowercase(std::filesystem::path(cleaned).extension().string()) != ".syx")
        cleaned += ".syx";
    return cleaned;
}

std::filesystem::path uniqueLibraryPath(const std::filesystem::path& folder, const std::string& name)
{
    const std::filesystem::path fileName = libraryFileName(name);
    const auto stem = fileName.stem().string();
    const auto extension = fileName.extension().string();

    auto path = folder / fileName;
    std::error_code error;
    for (int number = 2; std::filesystem::exists(path, error); ++number)
        path = folder / (stem + " " + std::to_string(number) + extension);
    return path;
}

LibrarySaveResult saveLibraryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                  ReplaceExisting replace)
{
    std::error_code error;
    if (std::filesystem::exists(path, error) && replace == ReplaceExisting::No)
        return LibrarySaveResult::AlreadyExists;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return LibrarySaveResult::Failed;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (! stream)
        return LibrarySaveResult::Failed;
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    return stream ? LibrarySaveResult::Saved : LibrarySaveResult::Failed;
}

} // namespace nl2x
