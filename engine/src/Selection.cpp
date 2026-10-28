#include "nl2x/Selection.h"

#include "nl2x/HardwareAssumptions.h"

#include <string_view>

namespace nl2x {

namespace {

// The ten letters of Performance names, I and K skipped [OFF-2X p.16, 32].
constexpr std::string_view kPerformanceLetters = "ABCDEFGHJL";

} // namespace

bool isValid(const ProgramSelection& selection)
{
    using hardware::kProgramBankCount;
    if (selection.kind == ProgramSelection::Kind::PercussionKit)
        return selection.bank >= 0 && selection.bank < hardware::kPercussionKitBankCount && selection.number >= 0
               && selection.number < hardware::kPercussionKitsPerBank;
    return selection.bank >= 0 && selection.bank < kProgramBankCount && selection.number >= 1
           && selection.number <= hardware::kProgramsPerProgramBank;
}

bool isValid(const PerformanceSelection& selection)
{
    return selection.bank >= 1 && selection.bank <= hardware::kPerformanceBankCount && selection.number >= 0
           && selection.number < hardware::kPerformancesPerBank;
}

std::string performanceName(int number)
{
    if (number < 0 || number >= hardware::kPerformancesPerBank)
        return {};
    std::string name;
    name += kPerformanceLetters[static_cast<std::size_t>(number / 10)];
    name += static_cast<char>('0' + number % 10);
    return name;
}

std::optional<BankAndProgramChange> midiSelection(const ProgramSelection& selection)
{
    if (! isValid(selection))
        return std::nullopt;
    const int programChange = selection.kind == ProgramSelection::Kind::PercussionKit
                                  ? hardware::kProgramChangeOfPercussionKitP0 + selection.number
                                  : hardware::kProgramChangeOfProgram1 + selection.number - 1;
    return BankAndProgramChange { static_cast<std::uint8_t>(hardware::kBankSelectOfProgramBank0 + selection.bank),
                                  static_cast<std::uint8_t>(programChange) };
}

std::optional<BankAndProgramChange> midiSelection(const PerformanceSelection& selection)
{
    if (! isValid(selection))
        return std::nullopt;
    return BankAndProgramChange {
        static_cast<std::uint8_t>(hardware::kBankSelectOfPerformanceBank1 + selection.bank - 1),
        static_cast<std::uint8_t>(selection.number),
    };
}

std::optional<ProgramSelection> programSelectionFromMidi(int bankSelect, int programChange)
{
    const int bank = bankSelect - hardware::kBankSelectOfProgramBank0;
    ProgramSelection selection;
    selection.bank = bank;
    if (programChange >= hardware::kProgramChangeOfPercussionKitP0) {
        selection.kind = ProgramSelection::Kind::PercussionKit;
        selection.number = programChange - hardware::kProgramChangeOfPercussionKitP0;
    } else {
        selection.number = programChange - hardware::kProgramChangeOfProgram1 + 1;
    }
    if (! isValid(selection))
        return std::nullopt;
    return selection;
}

std::optional<PerformanceSelection> performanceSelectionFromMidi(int bankSelect, int programChange)
{
    const PerformanceSelection selection { bankSelect - hardware::kBankSelectOfPerformanceBank1 + 1, programChange };
    if (! isValid(selection))
        return std::nullopt;
    return selection;
}

} // namespace nl2x
