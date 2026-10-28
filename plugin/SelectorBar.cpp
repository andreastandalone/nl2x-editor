#include "SelectorBar.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Selection.h"

namespace {

constexpr int kRefreshIntervalMs = 50;
constexpr int kRowHeight = 26;

// ComboBox item IDs (0 means "nothing selected" in JUCE). Program menu: Programs 1–99 as IDs
// 1–99, then Percussion Kits P0–P9.
constexpr int kFirstKitId = 100;
// Bank menus: Bank n as ID n + 1.
constexpr int kBankIdOffset = 1;

const juce::String kNeedsP = "The synth must have P (Program Change) on: SHIFT + PROG/CTRL until the display "
                             "shows P, then UP.";

int programItemId(const nl2x::ProgramSelection& selection)
{
    return selection.kind == nl2x::ProgramSelection::Kind::PercussionKit ? kFirstKitId + selection.number
                                                                         : selection.number;
}

juce::String slotName(nl2x::Slot slot)
{
    return "Slot " + juce::String::charToString(nl2x::slotLetter(slot));
}

} // namespace

SelectorBar::SelectorBar(NL2XEditorProcessor& owner)
    : processor(owner)
{
    for (int bank = 1; bank <= nl2x::hardware::kPerformanceBankCount; ++bank)
        performanceBank.addItem("Bank " + juce::String(bank), bank + kBankIdOffset);
    for (int number = 0; number < nl2x::hardware::kPerformancesPerBank; ++number)
        performanceNumber.addItem(juce::String(nl2x::performanceName(number)), number + 1);
    performanceBank.setTextWhenNothingSelected("Bank");
    performanceNumber.setTextWhenNothingSelected("-");
    for (auto* box : { &performanceBank, &performanceNumber }) {
        box->setTooltip("Select a stored Performance on the synth (Global Channel). " + kNeedsP);
        box->onChange = [this] { performanceChosen(); };
        addAndMakeVisible(box);
    }
    addAndMakeVisible(performanceLabel);
    resultLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(resultLabel);

    for (auto slot : nl2x::kAllSlots) {
        auto& selector = slotSelectors[nl2x::slotIndex(slot)];
        selector.name.setText(juce::String::charToString(nl2x::slotLetter(slot)), juce::dontSendNotification);
        selector.name.setJustificationType(juce::Justification::centredRight);
        for (int bank = 0; bank < nl2x::hardware::kProgramBankCount; ++bank)
            selector.bank.addItem("Bank " + juce::String(bank), bank + kBankIdOffset);
        for (int number = 1; number <= nl2x::hardware::kProgramsPerProgramBank; ++number)
            selector.program.addItem(juce::String(number), number);
        selector.program.addSeparator();
        for (int kit = 0; kit < nl2x::hardware::kPercussionKitsPerBank; ++kit)
            selector.program.addItem("P" + juce::String(kit), kFirstKitId + kit);
        selector.bank.setTextWhenNothingSelected("Bank");
        selector.program.setTextWhenNothingSelected("-");
        for (auto* box : { &selector.bank, &selector.program }) {
            box->setTooltip("Select a stored Program or Percussion Kit (P0-P9, Banks 0-3) for " + slotName(slot)
                            + " on its MIDI channel. " + kNeedsP);
            box->onChange = [this, slot] { slotChosen(slot); };
            addAndMakeVisible(box);
        }
        addAndMakeVisible(selector.name);
    }

    refresh();
    startTimer(kRefreshIntervalMs);
}

SelectorBar::~SelectorBar()
{
    stopTimer();
}

void SelectorBar::resized()
{
    auto area = getLocalBounds();

    auto performanceRow = area.removeFromTop(kRowHeight);
    performanceLabel.setBounds(performanceRow.removeFromLeft(90));
    performanceBank.setBounds(performanceRow.removeFromLeft(90));
    performanceRow.removeFromLeft(4);
    performanceNumber.setBounds(performanceRow.removeFromLeft(70));
    performanceRow.removeFromLeft(12);
    resultLabel.setBounds(performanceRow);

    area.removeFromTop(8);
    auto slotRow = area.removeFromTop(kRowHeight);
    const int width = slotRow.getWidth() / static_cast<int>(slotSelectors.size());
    for (auto& selector : slotSelectors) {
        auto cell = slotRow.removeFromLeft(width).withTrimmedRight(8);
        selector.name.setBounds(cell.removeFromLeft(24));
        cell.removeFromLeft(4);
        selector.bank.setBounds(cell.removeFromLeft(cell.getWidth() / 2));
        cell.removeFromLeft(4);
        selector.program.setBounds(cell);
    }
}

void SelectorBar::timerCallback()
{
    if (processor.synth().stateRevision() != shownRevision)
        refresh();
}

void SelectorBar::refresh()
{
    const auto& synth = processor.synth();
    shownRevision = synth.stateRevision();
    const auto& selection = synth.selection();

    // With nothing known the menus keep what the user chose so far, so that a Bank can be chosen
    // before the Program.
    if (const auto& performance = selection.performance) {
        performanceBank.setSelectedId(performance->bank + kBankIdOffset, juce::dontSendNotification);
        performanceNumber.setSelectedId(performance->number + 1, juce::dontSendNotification);
    }

    for (auto slot : nl2x::kAllSlots) {
        auto& selector = slotSelectors[nl2x::slotIndex(slot)];
        const auto& program = selection.programs[nl2x::slotIndex(slot)];
        if (program) {
            selector.bank.setSelectedId(program->bank + kBankIdOffset, juce::dontSendNotification);
            selector.program.setSelectedId(programItemId(*program), juce::dontSendNotification);
        }
        // Percussion Kits exist in Banks 0–3 only [OFF-2X p.13].
        const int bank = selector.bank.getSelectedId() - kBankIdOffset;
        for (int kit = 0; kit < nl2x::hardware::kPercussionKitsPerBank; ++kit)
            selector.program.setItemEnabled(kFirstKitId + kit, bank < nl2x::hardware::kPercussionKitBankCount);
        const bool channelOn = synth.slotChannel(slot).has_value();
        selector.bank.setEnabled(channelOn);
        selector.program.setEnabled(channelOn);
    }
}

void SelectorBar::slotChosen(nl2x::Slot slot)
{
    auto& selector = slotSelectors[nl2x::slotIndex(slot)];
    const int bankId = selector.bank.getSelectedId();
    const int programId = selector.program.getSelectedId();
    // Both halves are needed; the other one is chosen next.
    if (bankId == 0 || programId == 0)
        return;

    nl2x::ProgramSelection selection;
    selection.bank = bankId - kBankIdOffset;
    if (programId >= kFirstKitId) {
        selection.kind = nl2x::ProgramSelection::Kind::PercussionKit;
        selection.number = programId - kFirstKitId;
    } else {
        selection.number = programId;
    }
    showResult(processor.selectProgram(slot, selection), slotName(slot));
    refresh();
}

void SelectorBar::performanceChosen()
{
    const int bankId = performanceBank.getSelectedId();
    const int numberId = performanceNumber.getSelectedId();
    if (bankId == 0 || numberId == 0)
        return;
    showResult(processor.selectPerformance({ bankId - kBankIdOffset, numberId - 1 }), "The Performance");
    refresh();
}

void SelectorBar::showResult(nl2x::SelectionResult result, const juce::String& what)
{
    juce::String text;
    switch (result) {
    case nl2x::SelectionResult::Sent:
        break;
    case nl2x::SelectionResult::RefusedInvalid:
        text = "There is no such Program, Percussion Kit or Performance (Kits are in Banks 0-3 only).";
        break;
    case nl2x::SelectionResult::RefusedSlotChannelOff:
        text = what + ": its MIDI channel is off, so it cannot be selected.";
        break;
    case nl2x::SelectionResult::RefusedSlotOnGlobalChannel:
        text = what + " is on the Global Channel, where a Program Change selects a Performance. Give it its "
                      "own channel.";
        break;
    case nl2x::SelectionResult::RefusedNoGlobalChannel:
        text = what + " cannot be selected before the synth is connected.";
        break;
    case nl2x::SelectionResult::RefusedMemoryBusy:
        text = "Nothing can be selected while the synth's memory is being read or written (Synth memory tab): "
               "it would change the Bank selected on the synth. Finish or cancel that first.";
        break;
    }
    resultLabel.setText(text, juce::dontSendNotification);
}
