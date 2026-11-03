#include "LibrarianView.h"

#include "NordLookAndFeel.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/LibraryMetadata.h"

#include <algorithm>
#include <filesystem>

namespace {

constexpr int kRefreshIntervalMs = 100;
constexpr int kFirstBank = nl2x::hardware::kFirstRamProgramBank;
constexpr int kLastBank = nl2x::hardware::kLastRamProgramBank;
constexpr int kPrograms = nl2x::hardware::kProgramsPerRamBank;
constexpr int kColumns = 5;
constexpr int kRows = (kPrograms + kColumns - 1) / kColumns;
constexpr int kCellGap = 3;
constexpr int kNumberWidth = 26;
constexpr int kStarWidth = 14;
// The name a location keeps visible before its tags give way.
constexpr int kMinNameWidth = 64;
constexpr int kToolRowHeight = 30;
constexpr int kButtonRowHeight = 28;
constexpr int kStatusHeight = 26;
constexpr int kProgressWidth = 180;
// The most locations a send confirmation lists one by one.
constexpr std::size_t kMostListed = 12;
constexpr const char* kLocationDragPrefix = "nl2x-location:";

juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

std::filesystem::path toPath(const juce::File& file)
{
    return std::filesystem::path(file.getFullPathName().toStdString());
}

juce::String slotName(nl2x::Slot slot)
{
    return "Slot " + juce::String::charToString(nl2x::slotLetter(slot));
}

juce::String locationName(nl2x::BankLocation location)
{
    return "Program " + juce::String(location.program) + " of Bank " + juce::String(location.bank);
}

// Where every backup goes (spec #1, user story 62), whatever the Library folder is.
juce::File backupFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Nord Lead 2X")
        .getChildFile("Backups");
}

nl2x::WallClockTime wallClockNow()
{
    const auto now = juce::Time::getCurrentTime();
    return { now.getYear(), now.getMonth() + 1, now.getDayOfMonth(), now.getHours(), now.getMinutes(),
             now.getSeconds() };
}

std::string fingerprintKey(const nl2x::SoundFingerprint& fingerprint)
{
    return std::string(fingerprint.values.begin(), fingerprint.values.end());
}

juce::String stageText(const nl2x::MemoryTaskStatus& status)
{
    const auto progress = " (" + juce::String(status.done) + " of " + juce::String(status.total) + ")";
    switch (status.stage) {
    case nl2x::MemoryStage::Idle:
    case nl2x::MemoryStage::Finished:
    case nl2x::MemoryStage::Failed:
        return {};
    case nl2x::MemoryStage::ReadingBackup:
        return "Backing up Bank " + juce::String(status.bank) + progress + ". Nothing is written yet.";
    case nl2x::MemoryStage::AwaitingConfirmation:
        return "Waiting for your confirmation. Nothing is written yet.";
    case nl2x::MemoryStage::CheckingSelectedBank:
        return "Checking which Bank is selected on the synth.";
    case nl2x::MemoryStage::Writing:
        return "Writing to Bank " + juce::String(status.bank) + progress + ". Do not change the Bank on the synth.";
    case nl2x::MemoryStage::Verifying:
        return "Reading back what was written" + progress + ".";
    case nl2x::MemoryStage::ReadingBank:
        return "Reading Bank " + juce::String(status.bank) + " from the synth" + progress + ". Nothing is written.";
    }
    return {};
}

std::optional<nl2x::BankLocation> draggedLocation(const juce::var& description)
{
    const auto text = description.toString();
    if (! text.startsWith(kLocationDragPrefix))
        return std::nullopt;
    const auto parts = juce::StringArray::fromTokens(text.fromFirstOccurrenceOf(kLocationDragPrefix, false, false), ":", {});
    if (parts.size() != 2)
        return std::nullopt;
    return nl2x::BankLocation { parts[0].getIntValue(), parts[1].getIntValue() };
}

} // namespace

// A Bank tab: shows the Bank, and takes a Program Bank file dropped on it.
class LibrarianView::BankTab final : public juce::TextButton, public juce::DragAndDropTarget {
public:
    BankTab(LibrarianView& owner, int tabBank) : view(owner), bankNumber(tabBank)
    {
        setComponentID("bankTab" + juce::String(tabBank));
        setTooltip("Show Bank " + juce::String(tabBank) + ". Drop a Program Bank file from the Library here to replace "
                   "its Programs (nothing is written until you send).");
        onClick = [this] { view.showBank(bankNumber); };
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return view.accepts(details.description, true);
    }
    void itemDragEnter(const SourceDetails&) override { setHovered(true); }
    void itemDragExit(const SourceDetails&) override { setHovered(false); }
    void itemDropped(const SourceDetails& details) override
    {
        setHovered(false);
        view.dropped(details.description, std::nullopt, bankNumber);
    }
    void paintOverChildren(juce::Graphics& g) override
    {
        if (dropHover) {
            g.setColour(NordPalette::pending);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), NordMetrics::cornerRadius, 2.0f);
        }
    }

private:
    void setHovered(bool hovered)
    {
        dropHover = hovered;
        repaint();
    }

    LibrarianView& view;
    int bankNumber;
    bool dropHover = false;
};

// The 99 locations of the shown Bank, in columns of kRows, numbered as the synth shows them.
class LibrarianView::BankGrid final : public juce::Component,
                                       public juce::DragAndDropTarget,
                                       public juce::TooltipClient {
public:
    explicit BankGrid(LibrarianView& owner) : view(owner) { setComponentID("bankGrid"); }

    int selectedProgram = 0;

    std::optional<int> programAt(juce::Point<int> point) const
    {
        if (! getLocalBounds().contains(point))
            return std::nullopt;
        const int column = point.x * kColumns / std::max(1, getWidth());
        const int row = point.y * kRows / std::max(1, getHeight());
        const int program = column * kRows + row + 1;
        if (program > kPrograms)
            return std::nullopt;
        return program;
    }

    juce::Rectangle<int> cellBounds(int program) const
    {
        const int column = (program - 1) / kRows;
        const int row = (program - 1) % kRows;
        const int x0 = column * getWidth() / kColumns;
        const int x1 = (column + 1) * getWidth() / kColumns;
        const int y0 = row * getHeight() / kRows;
        const int y1 = (row + 1) * getHeight() / kRows;
        return juce::Rectangle<int>(x0, y0, x1 - x0, y1 - y0).withTrimmedRight(kCellGap).withTrimmedBottom(kCellGap - 1);
    }

    void paint(juce::Graphics& g) override
    {
        auto& librarian = view.processor.librarian();
        const int shown = view.bank;
        bool anyKnown = false;
        for (int program = 1; program <= kPrograms; ++program) {
            const nl2x::BankLocation location { shown, program };
            const auto text = view.describe(location);
            anyKnown = anyKnown || text.known;
            paintCell(g, program, text, librarian.isPending(location));
        }
        if (! anyKnown)
            paintEmptyNotice(g);
    }

    void mouseMove(const juce::MouseEvent& e) override { setHover(programAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHover(std::nullopt); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        pressed = programAt(e.getPosition());
        dragging = false;
        if (! pressed)
            return;
        selectedProgram = *pressed;
        repaint();
        const nl2x::BankLocation location { view.bank, *pressed };
        if (! e.mods.isPopupMenu() && suggestedTags[static_cast<std::size_t>(*pressed)].contains(e.position)) {
            pressed.reset();
            view.acceptSuggestion(location);
            return;
        }
        if (e.mods.isPopupMenu())
            view.locationMenu(location);
        else
            view.locationClicked(location);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! pressed || dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 5)
            return;
        if (! view.processor.librarian().program({ view.bank, *pressed }))
            return;
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container == nullptr)
            return;
        dragging = true;
        const auto cell = cellBounds(*pressed);
        auto image = createComponentSnapshot(cell, true, 2.0f);
        image.multiplyAllAlphas(0.7f);
        const auto offset = cell.getPosition() - e.getMouseDownPosition();
        container->startDragging(juce::String(kLocationDragPrefix) + juce::String(view.bank) + ":" + juce::String(*pressed),
                                 this, juce::ScaledImage(image, 2.0f), false, &offset);
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return view.accepts(details.description, false);
    }
    void itemDragMove(const SourceDetails& details) override
    {
        const auto target = programAt(details.localPosition.toInt());
        if (target != dropTarget) {
            dropTarget = target;
            repaint();
        }
    }
    void itemDragExit(const SourceDetails&) override
    {
        dropTarget.reset();
        repaint();
    }
    void itemDropped(const SourceDetails& details) override
    {
        const auto target = programAt(details.localPosition.toInt());
        dropTarget.reset();
        repaint();
        view.dropped(details.description,
                     target ? std::optional(nl2x::BankLocation { view.bank, *target }) : std::nullopt, view.bank);
    }

    juce::String getTooltip() override
    {
        const auto program = programAt(getMouseXYRelative());
        return program ? view.tooltipFor({ view.bank, *program }) : juce::String();
    }

private:
    void setHover(std::optional<int> program)
    {
        if (program != hover) {
            hover = program;
            repaint();
        }
    }

    void paintCell(juce::Graphics& g, int program, const LibrarianView::LocationText& text, bool pending)
    {
        const auto cell = cellBounds(program);
        const auto area = cell.toFloat();
        g.setColour(NordPalette::location);
        g.fillRoundedRectangle(area, 3.0f);
        if (pending) {
            g.setColour(NordPalette::pending.withAlpha(0.16f));
            g.fillRoundedRectangle(area, 3.0f);
        }
        if (pending) {
            g.setColour(NordPalette::pending);
            g.fillRoundedRectangle(area.withWidth(4.0f), 2.0f);
        }
        if (dropTarget == program) {
            g.setColour(NordPalette::pending);
            g.drawRoundedRectangle(area.reduced(1.0f), 3.0f, 2.0f);
        } else if (selectedProgram == program) {
            g.setColour(NordPalette::text.withAlpha(0.7f));
            g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);
        } else if (hover == program) {
            g.setColour(NordPalette::outline);
            g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);
        }

        auto content = cell.withTrimmedLeft(4);
        g.setFont(NordFonts::small());
        g.setColour(pending ? NordPalette::pending : NordPalette::textDim);
        g.drawText(juce::String(program), content.removeFromLeft(kNumberWidth), juce::Justification::centredRight, false);
        content.removeFromLeft(6);
        content.removeFromRight(3);

        if (text.favourite) {
            juce::Path star;
            const auto starArea = content.removeFromLeft(kStarWidth).toFloat();
            star.addStar(starArea.getCentre(), 5, 2.5f, 5.5f, 0.0f);
            g.setColour(NordPalette::ledOn);
            g.fillPath(star);
            content.removeFromLeft(2);
        }

        // Tags from the right end, the category (or a suggested one) nearest the name, as in the
        // Library.
        enum class Style { Filled, Outlined, Suggested };
        std::vector<std::pair<juce::String, Style>> tags;
        if (text.category.isNotEmpty())
            tags.emplace_back(text.category, Style::Filled);
        else if (text.suggested.isNotEmpty())
            tags.emplace_back(text.suggested, Style::Suggested);
        if (text.changed)
            tags.emplace_back("changed", Style::Outlined);
        auto& suggestedTag = suggestedTags[static_cast<std::size_t>(program)];
        suggestedTag = {};
        const auto tagHeight = static_cast<float>(std::min(content.getHeight() - 6, NordMetrics::tagHeight));
        const auto tagTop = static_cast<float>(content.getY()) + (static_cast<float>(content.getHeight()) - tagHeight) * 0.5f;
        for (auto tag = tags.rbegin(); tag != tags.rend(); ++tag) {
            const bool suggested = tag->second == Style::Suggested;
            const int size = NordLookAndFeel::tagWidth(suggested ? NordLookAndFeel::suggestedTagText(tag->first)
                                                                  : tag->first);
            if (content.getWidth() - size - NordMetrics::tagGap < kMinNameWidth)
                continue;
            const auto chip = content.removeFromRight(size);
            const juce::Rectangle<float> tagArea { static_cast<float>(chip.getX()), tagTop, static_cast<float>(size), tagHeight };
            if (suggested) {
                NordLookAndFeel::drawSuggestedTag(g, tag->first, tagArea);
                suggestedTag = tagArea;
            } else {
                NordLookAndFeel::drawTag(g, tag->first, tagArea, tag->second == Style::Filled);
            }
            content.removeFromRight(NordMetrics::tagGap);
        }

        g.setFont(NordFonts::body());
        if (! text.known) {
            g.setColour(NordPalette::textDim.withAlpha(0.6f));
            g.drawText("?", content, juce::Justification::centredLeft, false);
        } else if (text.name.isEmpty()) {
            g.setColour(NordPalette::textDim.withAlpha(0.6f));
            g.drawText(juce::String::fromUTF8("\xE2\x80\x93"), content, juce::Justification::centredLeft, false);
        } else {
            g.setColour(text.userName ? NordPalette::text : NordPalette::textDim);
            g.drawText(text.name, content, juce::Justification::centredLeft, true);
        }
    }

    void paintEmptyNotice(juce::Graphics& g)
    {
        const auto status = view.processor.librarian().synthBanksStatus();
        juce::String notice;
        if (status.stage == nl2x::SynthBanksStage::Reading)
            notice = "Reading the synth's Banks" + juce::String::fromUTF8("\xE2\x80\xA6");
        else if (view.processor.synth().connectionStatus() != nl2x::ConnectionStatus::Connected)
            notice = "The synth's Banks are read as soon as it answers. You can also drop a Bank file from the Library "
                     "on a Bank tab.";
        else
            notice = "Bank " + juce::String(view.bank) + " has not been read. Press Read again, or drop a Bank file "
                                                         "from the Library on its tab.";
        const auto box = getLocalBounds().withSizeKeepingCentre(std::min(getWidth() - 40, 560), 64);
        g.setColour(NordPalette::panelGrey.withAlpha(0.92f));
        g.fillRoundedRectangle(box.toFloat(), NordMetrics::cornerRadius);
        g.setColour(NordPalette::text);
        g.setFont(NordFonts::body());
        g.drawFittedText(notice, box.reduced(14, 6), juce::Justification::centred, 2);
    }

    LibrarianView& view;
    std::optional<int> hover;
    std::optional<int> pressed;
    std::optional<int> dropTarget;
    bool dragging = false;
    // Where each location's suggested category tag was last painted (empty: none), by Program
    // number.
    std::array<juce::Rectangle<float>, kPrograms + 1> suggestedTags {};
};

LibrarianView::LibrarianView(NL2XEditorProcessor& owner, LibraryPanel& library, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), libraryPanel(library), selectedSlot(std::move(slotChoice))
{
    for (int each = kFirstBank; each <= kLastBank; ++each) {
        auto& tab = bankTabs[static_cast<std::size_t>(each - kFirstBank)];
        tab = std::make_unique<BankTab>(*this, each);
        addAndMakeVisible(*tab);
    }
    bankTabs[0]->setToggleState(true, juce::dontSendNotification);

    grid = std::make_unique<BankGrid>(*this);
    addAndMakeVisible(*grid);

    pendingLabel.setFont(NordFonts::body());
    pendingLabel.setColour(juce::Label::textColourId, NordPalette::pending);
    hintLabel.setFont(NordFonts::small());
    hintLabel.setColour(juce::Label::textColourId, NordPalette::textDim);
    hintLabel.setJustificationType(juce::Justification::centredRight);

    readButton.setComponentID("readBanks");
    readButton.setTooltip("Read Programs 1-99 of Banks 0-3 from the synth again. Nothing is written, and your "
                          "changes not sent yet are kept.");
    readButton.onClick = [this] { readBanks(); };
    sendButton.setComponentID("sendBank");
    sendButton.onClick = [this] { send(); };
    discardButton.setTooltip("Drop every change of the shown Bank not sent yet: it shows what the synth holds again");
    discardButton.onClick = [this] {
        processor.librarian().discardChanges(bank);
        setMessage("The changes to Bank " + juce::String(bank) + " were discarded. Nothing was written.");
    };
    replaceButton.setTooltip("Fill the shown Bank with the Programs of a Bank file (nothing is written until you "
                             "send). You can also drop a Bank file from the Library on a Bank tab.");
    replaceButton.onClick = [this] { showReplaceMenu(); };
    saveButton.setTooltip("Save the shown Bank, with its changes, as a Bank file in the Library folder");
    saveButton.onClick = [this] { saveBankFile(); };
    cancelButton.setTooltip("Stop the read or write under way; Programs not written yet stay as they were");
    cancelButton.onClick = [this] { processor.synthMemory().cancel(); };

    statusLabel.setFont(NordFonts::small());
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale(0.8f);

    for (auto* child : std::initializer_list<juce::Component*> { &pendingLabel, &hintLabel, &readButton, &sendButton,
                                                                 &discardButton, &replaceButton, &saveButton,
                                                                 &cancelButton, &statusLabel })
        addAndMakeVisible(child);

    refresh();
    startTimer(kRefreshIntervalMs);
}

LibrarianView::~LibrarianView()
{
    stopTimer();
}

void LibrarianView::paint(juce::Graphics& g)
{
    NordLookAndFeel::drawSection(g, getLocalBounds(), "Synth Banks");
    if (progress >= 0.0 && ! progressArea.isEmpty()) {
        const auto bar = progressArea.toFloat().withSizeKeepingCentre(static_cast<float>(progressArea.getWidth()), 8.0f);
        g.setColour(NordPalette::knobTrack);
        g.fillRoundedRectangle(bar, 4.0f);
        g.setColour(NordPalette::valueArc);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(std::clamp(progress, 0.0, 1.0))), 4.0f);
    }
}

void LibrarianView::resized()
{
    auto area = getLocalBounds().withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(10, 8);

    auto top = area.removeFromTop(kToolRowHeight);
    for (auto& tab : bankTabs) {
        tab->setBounds(top.removeFromLeft(104));
        top.removeFromLeft(4);
    }
    top.removeFromLeft(10);
    readButton.setBounds(top.removeFromRight(120));
    top.removeFromRight(10);
    pendingLabel.setBounds(top.removeFromLeft(130));
    hintLabel.setBounds(top);
    area.removeFromTop(8);

    auto status = area.removeFromBottom(kStatusHeight);
    cancelButton.setBounds(status.removeFromRight(90));
    status.removeFromRight(8);
    progressArea = status.removeFromRight(kProgressWidth);
    status.removeFromRight(8);
    statusLabel.setBounds(status);
    area.removeFromBottom(6);

    auto buttons = area.removeFromBottom(kButtonRowHeight);
    sendButton.setBounds(buttons.removeFromLeft(220));
    buttons.removeFromLeft(8);
    discardButton.setBounds(buttons.removeFromLeft(150));
    saveButton.setBounds(buttons.removeFromRight(180));
    buttons.removeFromRight(8);
    replaceButton.setBounds(buttons.removeFromRight(220));
    area.removeFromBottom(8);

    grid->setBounds(area);
}

void LibrarianView::tabShown()
{
    if (processor.librarian().synthBanksStatus().stage == nl2x::SynthBanksStage::NotRead)
        wantsFirstRead = true;
    refresh();
}

void LibrarianView::showBank(int newBank)
{
    if (newBank < kFirstBank || newBank > kLastBank)
        return;
    bank = newBank;
    for (int each = kFirstBank; each <= kLastBank; ++each)
        bankTabs[static_cast<std::size_t>(each - kFirstBank)]->setToggleState(each == bank, juce::dontSendNotification);
    grid->selectedProgram = 0;
    refresh();
}

void LibrarianView::rebuildNames()
{
    namesRevision = libraryPanel.contentsRevision();
    libraryNames.clear();
    // A file of its own names a sound better than a numbered entry of a Bank file: those first.
    for (const bool singles : { true, false })
        for (const auto& file : libraryPanel.libraryFiles()) {
            if (file.isBackup || (file.entries.size() == 1) != singles)
                continue;
            for (const auto& entry : file.entries)
                if (entry.kind() == nl2x::LibraryEntryKind::Program)
                    libraryNames.emplace(fingerprintKey(nl2x::SoundFingerprint::of(entry)), entry.name);
        }
}

LibrarianView::LocationText LibrarianView::describeProgram(const std::optional<nl2x::Program>& program) const
{
    LocationText text;
    if (! program)
        return text;
    text.known = true;
    const auto& metadata = libraryPanel.metadata();
    const auto fingerprint = nl2x::SoundFingerprint::of(*program);
    const auto info = metadata.info(fingerprint);
    text.category = utf8(info.category);
    if (info.category.empty())
        if (const auto suggestion = libraryPanel.suggestionFor(fingerprint))
            text.suggested = utf8(suggestion->category);
    text.favourite = info.favourite;
    if (const auto match = metadata.match(fingerprint))
        text.changed = match->differences > 0;
    if (! info.name.empty()) {
        text.name = utf8(info.name);
        text.userName = true;
    } else if (const auto found = libraryNames.find(fingerprintKey(fingerprint)); found != libraryNames.end()) {
        text.name = utf8(found->second);
    }
    return text;
}

LibrarianView::LocationText LibrarianView::describe(nl2x::BankLocation location) const
{
    return describeProgram(processor.librarian().program(location));
}

LibrarianView::LocationText LibrarianView::describeSynth(nl2x::BankLocation location) const
{
    return describeProgram(processor.librarian().synthProgram(location));
}

juce::String LibrarianView::tooltipFor(nl2x::BankLocation location) const
{
    const auto text = describe(location);
    const auto nameOf = [](const LocationText& each) {
        return ! each.known ? juce::String("not read yet")
                            : each.name.isEmpty() ? juce::String("a sound with no name") : each.name;
    };
    juce::String tip = locationName(location) + ": " + nameOf(text);
    if (text.category.isNotEmpty())
        tip << " (" << text.category << ")";
    else if (text.suggested.isNotEmpty())
        tip << " (suggested: " << text.suggested << "; click the dashed tag to keep it)";
    if (processor.librarian().isPending(location))
        tip << ". To send: it replaces " << nameOf(describeSynth(location)) << " on the synth";
    tip << ".\nClick to audition it on " << slotName(selectedSlot())
        << "; drag it onto another location to copy it there; right-click to undo a change.";
    return tip;
}

void LibrarianView::timerCallback()
{
    refresh();
}

void LibrarianView::refresh()
{
    auto& librarian = processor.librarian();
    auto& memory = processor.synthMemory();
    const auto& status = memory.status();
    const bool busy = memory.busy();
    const bool controls = processor.controlsSynth();
    const bool connected = processor.synth().connectionStatus() == nl2x::ConnectionStatus::Connected;

    // Not while a dialog is open (a Store asking where): the read would make that Store wait.
    const bool dialogOpen = juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
    if (wantsFirstRead && isVisible() && controls && connected && ! busy && ! dialogOpen) {
        if (librarian.readSynthBanks(NL2XEditorProcessor::engineTimeMs()) == nl2x::MemoryRefusal::None)
            message.clear();
        wantsFirstRead = librarian.synthBanksStatus().stage == nl2x::SynthBanksStage::NotRead;
    }

    // A task that just ended: its outcome replaces the last message.
    if (wasBusy && ! busy) {
        message = utf8(status.message);
        if (status.backupFile && message.indexOf("ackup") < 0)
            message << "  Backup: " << utf8(status.backupFile->string());
    }

    wasBusy = busy;

    const auto names = libraryPanel.contentsRevision();
    const auto revision = processor.synth().stateRevision();
    const auto information = libraryPanel.metadataRevision();
    if (names != namesRevision || revision != shownRevision || information != metadataRevision) {
        if (names != namesRevision)
            rebuildNames();
        shownRevision = revision;
        metadataRevision = information;
        for (int each = kFirstBank; each <= kLastBank; ++each) {
            const auto count = librarian.pendingPrograms(each).size();
            bankTabs[static_cast<std::size_t>(each - kFirstBank)]->setButtonText(
                "Bank " + juce::String(each) + (count > 0 ? "  (" + juce::String(count) + ")" : juce::String()));
        }
        grid->repaint();
    }

    const auto pending = librarian.pendingPrograms(bank);
    const auto whole = librarian.wholeBankSource(bank);
    pendingLabel.setText(pending.empty() ? juce::String()
                                         : juce::String(pending.size()) + (whole ? " to send (file)" : " to send"),
                         juce::dontSendNotification);
    pendingLabel.setTooltip(whole ? "Bank " + juce::String(bank) + " was replaced with \"" + utf8(*whole) + "\"."
                                  : juce::String());
    hintLabel.setText("Click a sound to audition it on " + slotName(selectedSlot()), juce::dontSendNotification);

    sendButton.setButtonText("Send Bank " + juce::String(bank) + juce::String::fromUTF8(" to synth\xE2\x80\xA6"));
    sendButton.setTooltip("Write the " + juce::String(pending.size()) + " locations marked \"to send\" into Bank "
                          + juce::String(bank) + " on the synth, after a backup and your confirmation. The other "
                                                 "locations are not touched.");
    sendButton.setEnabled(! pending.empty() && ! busy && controls);
    discardButton.setEnabled(! pending.empty());
    saveButton.setEnabled(librarian.program({ bank, 1 }).has_value());
    readButton.setEnabled(! busy && controls);
    readButton.setButtonText(librarian.synthBanksStatus().stage == nl2x::SynthBanksStage::NotRead ? "Read Banks"
                                                                                                    : "Read again");
    cancelButton.setVisible(busy);

    juce::String statusText;
    double newProgress = -1.0;
    if (busy) {
        statusText = stageText(status);
        if (status.total > 0)
            newProgress = static_cast<double>(status.done) / static_cast<double>(status.total);
    } else if (message.isNotEmpty()) {
        statusText = message;
    } else if (const auto banks = librarian.synthBanksStatus(); banks.stage == nl2x::SynthBanksStage::Failed) {
        statusText = "Reading the synth's Banks stopped: " + utf8(banks.message);
    } else if (! controls) {
        statusText = "Another NL2X Editor controls the synth: this one cannot read or write its Banks.";
    }
    statusLabel.setText(statusText, juce::dontSendNotification);
    statusLabel.setTooltip(statusText);
    if (! juce::approximatelyEqual(newProgress, progress)) {
        progress = newProgress;
        repaint(progressArea);
    }

    if (status.stage == nl2x::MemoryStage::AwaitingConfirmation && status.plan && status.plan->token != askedToken)
        askForConfirmation(*status.plan);
}

void LibrarianView::setMessage(const juce::String& text)
{
    message = text;
    refresh();
}

void LibrarianView::showLibrarianRefusal(nl2x::LibrarianRefusal refusal)
{
    if (refusal != nl2x::LibrarianRefusal::None)
        setMessage(utf8(nl2x::librarianRefusalText(refusal)));
}

void LibrarianView::showMemoryRefusal(nl2x::MemoryRefusal refusal)
{
    setMessage(refusal == nl2x::MemoryRefusal::None ? juce::String() : utf8(nl2x::memoryRefusalText(refusal)));
}

bool LibrarianView::accepts(const juce::var& description, bool onBankTab) const
{
    if (libraryPanel.draggedBankFile(description) != nullptr)
        return true;
    if (onBankTab)
        return false;
    return libraryPanel.draggedEntry(description) != nullptr || draggedLocation(description).has_value();
}

void LibrarianView::dropped(const juce::var& description, std::optional<nl2x::BankLocation> location, int targetBank)
{
    auto& librarian = processor.librarian();
    if (const auto* file = libraryPanel.draggedBankFile(description)) {
        replaceWith(*file, targetBank);
        return;
    }
    if (! location)
        return;
    if (const auto* entry = libraryPanel.draggedEntry(description)) {
        const auto refusal = librarian.put(*location, *entry);
        if (refusal != nl2x::LibrarianRefusal::None) {
            showLibrarianRefusal(refusal);
            return;
        }
        grid->selectedProgram = location->program;
        setMessage(locationName(*location) + " will hold " + utf8(libraryPanel.metadata().displayName(*entry))
                   + ": to send. Nothing is written until you press Send.");
        return;
    }
    if (const auto from = draggedLocation(description); from && ! (*from == *location)) {
        const auto refusal = librarian.copy(*from, *location);
        if (refusal != nl2x::LibrarianRefusal::None) {
            showLibrarianRefusal(refusal);
            return;
        }
        grid->selectedProgram = location->program;
        setMessage(locationName(*location) + " will hold a copy of Program " + juce::String(from->program)
                   + ": to send. Nothing is written until you press Send.");
    }
}

void LibrarianView::locationClicked(nl2x::BankLocation location)
{
    const auto program = processor.librarian().program(location);
    if (! program) {
        setMessage(locationName(location) + " has not been read from the synth yet.");
        return;
    }
    const auto slot = selectedSlot();
    const auto result = processor.auditionProgram(*program, slot);
    if (result != nl2x::AuditionResult::Sent) {
        setMessage(utf8(nl2x::auditionRefusalText(result, slot)));
        return;
    }
    const auto text = describe(location);
    setMessage("Auditioning " + locationName(location) + (text.name.isEmpty() ? juce::String() : " (" + text.name + ")")
               + (processor.librarian().isPending(location) ? ", to send," : juce::String()) + " on " + slotName(slot)
               + ". Nothing was stored.");
}

void LibrarianView::locationMenu(nl2x::BankLocation location)
{
    auto& librarian = processor.librarian();
    const auto safe = juce::Component::SafePointer<LibrarianView>(this);
    juce::PopupMenu menu;
    menu.addSectionHeader(locationName(location));
    menu.addItem("Audition on " + slotName(selectedSlot()), librarian.program(location).has_value(), false,
                 [safe, location] {
                     if (safe != nullptr)
                         safe->locationClicked(location);
                 });
    menu.addItem("Undo change (keep what the synth holds)", librarian.isPending(location), false, [safe, location] {
        if (safe == nullptr)
            return;
        safe->processor.librarian().undo(location);
        safe->setMessage(locationName(location) + " keeps what the synth holds.");
    });
    if (const auto program = librarian.program(location)) {
        const auto fingerprint = nl2x::SoundFingerprint::of(*program);
        if (const auto suggestion = libraryPanel.suggestionFor(fingerprint)) {
            menu.addSeparator();
            menu.addItem("Keep suggested category: " + utf8(suggestion->category), [safe, fingerprint] {
                if (safe != nullptr)
                    safe->libraryPanel.acceptSuggestion(fingerprint);
            });
            menu.addItem("Dismiss suggested category", [safe, fingerprint] {
                if (safe != nullptr)
                    safe->libraryPanel.dismissSuggestion(fingerprint);
            });
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void LibrarianView::acceptSuggestion(nl2x::BankLocation location)
{
    if (const auto program = processor.librarian().program(location))
        libraryPanel.acceptSuggestion(nl2x::SoundFingerprint::of(*program));
}

void LibrarianView::readBanks()
{
    const auto refusal = processor.librarian().readSynthBanks(NL2XEditorProcessor::engineTimeMs());
    wantsFirstRead = false;
    showMemoryRefusal(refusal);
}

void LibrarianView::send()
{
    message.clear();
    showMemoryRefusal(processor.librarian().prepareSend(bank, toPath(backupFolder()), wallClockNow(),
                                                         NL2XEditorProcessor::engineTimeMs()));
}

void LibrarianView::replaceWith(const nl2x::LibraryFile& file, int targetBank)
{
    const auto refusal = processor.librarian().replaceWithFile(targetBank, file);
    if (refusal != nl2x::LibrarianRefusal::None) {
        showLibrarianRefusal(refusal);
        return;
    }
    showBank(targetBank);
    setMessage("Bank " + juce::String(targetBank) + " now holds the Programs of \"" + utf8(file.name)
               + "\", all to send. Nothing is written until you press Send.");
}

void LibrarianView::showReplaceMenu()
{
    const auto safe = juce::Component::SafePointer<LibrarianView>(this);
    const int target = bank;
    const auto addFile = [this, safe, target](juce::PopupMenu& menu, const nl2x::LibraryFile& file) {
        juce::StringArray parts;
        for (const auto& folder : file.folders)
            parts.add(utf8(folder));
        parts.add(utf8(file.name));
        const auto description = libraryPanel.dragDescriptionOf(file);
        menu.addItem(parts.joinIntoString(" / "), [safe, description, target] {
            if (safe != nullptr)
                if (const auto* found = safe->libraryPanel.draggedBankFile(description))
                    safe->replaceWith(*found, target);
        });
    };

    juce::PopupMenu menu;
    menu.addSectionHeader("Replace Bank " + juce::String(target) + " with");
    juce::PopupMenu backups;
    for (const auto& file : libraryPanel.libraryFiles()) {
        if (file.kind != nl2x::LibraryFileKind::ProgramBank)
            continue;
        if (file.isBackup)
            addFile(backups, file);
        else
            addFile(menu, file);
    }
    if (backups.getNumItems() > 0)
        menu.addSubMenu("Backups", backups);
    menu.addSeparator();
    menu.addItem(juce::String::fromUTF8("Another file\xE2\x80\xA6"), [safe, target] {
        if (safe == nullptr)
            return;
        safe->chooser = std::make_unique<juce::FileChooser>("Replace Bank " + juce::String(target) + " with a Bank file",
                                                            safe->libraryPanel.folder(), "*.syx");
        safe->chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safe, target](const juce::FileChooser& chosen) {
                                       if (safe == nullptr || chosen.getResult() == juce::File())
                                           return;
                                       const auto file = nl2x::openLibraryFile(toPath(chosen.getResult()));
                                       safe->replaceWith(file, target);
                                   });
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&replaceButton));
}

void LibrarianView::saveBankFile()
{
    const auto folder = libraryPanel.folder();
    if (! folder.isDirectory() && ! folder.createDirectory()) {
        setMessage("Cannot create the Library folder " + folder.getFullPathName() + ".");
        return;
    }
    const int target = bank;
    const auto suggested = nl2x::uniqueLibraryPath(
        toPath(folder), "Bank " + std::to_string(target) + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d").toStdString());
    chooser = std::make_unique<juce::FileChooser>("Save Bank " + juce::String(target) + " as .syx",
                                                  juce::File(utf8(suggested.string())), "*.syx");
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<LibrarianView>(this), target](const juce::FileChooser& chosen) {
        if (safe == nullptr)
            return;
        const auto picked = chosen.getResult();
        if (picked == juce::File())
            return;
        // The dialog asked before replacing the file it returned, not one reached by adding ".syx".
        const auto file = picked.hasFileExtension(".syx") ? picked : picked.withFileExtension(".syx");
        const auto replace = file == picked ? nl2x::ReplaceExisting::Yes : nl2x::ReplaceExisting::No;
        const auto refusal = safe->processor.librarian().saveBankFile(target, toPath(file), replace);
        if (refusal != nl2x::LibrarianRefusal::None) {
            safe->showLibrarianRefusal(refusal);
            return;
        }
        safe->libraryPanel.rescan();
        safe->setMessage("Saved Bank " + juce::String(target) + " as " + file.getFileName() + " in the Library.");
    });
}

void LibrarianView::askForLocation(const juce::String& title, const juce::String& text,
                                   std::function<void(nl2x::BankLocation)> store)
{
    auto* window = new juce::AlertWindow(title, text, juce::MessageBoxIconType::NoIcon, isShowing() ? this : nullptr);
    juce::StringArray banks;
    for (int each = kFirstBank; each <= kLastBank; ++each)
        banks.add("Bank " + juce::String(each));
    juce::StringArray programs;
    for (int program = 1; program <= kPrograms; ++program)
        programs.add(juce::String(program));
    window->addComboBox("bank", banks, "Bank");
    window->addComboBox("program", programs, "Program");
    auto* bankBox = window->getComboBoxComponent("bank");
    auto* programBox = window->getComboBoxComponent("program");
    bankBox->setSelectedItemIndex(bank - kFirstBank, juce::dontSendNotification);
    programBox->setSelectedItemIndex(lastStoreProgram - 1, juce::dontSendNotification);
    const auto safe = juce::Component::SafePointer<LibrarianView>(this);
    // The grid follows the choice, so the location about to be replaced is in view.
    bankBox->onChange = [safe, bankBox] {
        if (safe != nullptr)
            safe->showBank(bankBox->getSelectedItemIndex() + kFirstBank);
    };
    programBox->onChange = [safe, programBox] {
        if (safe != nullptr) {
            safe->grid->selectedProgram = programBox->getSelectedItemIndex() + 1;
            safe->grid->repaint();
        }
    };
    window->addButton(juce::String::fromUTF8("Store\xE2\x80\xA6"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe, window, onChosen = std::move(store)](int result) {
                                // Called before the window is deleted.
                                if (result != 1 || safe == nullptr)
                                    return;
                                const nl2x::BankLocation location {
                                    window->getComboBoxComponent("bank")->getSelectedItemIndex() + kFirstBank,
                                    window->getComboBoxComponent("program")->getSelectedItemIndex() + 1,
                                };
                                safe->lastStoreProgram = location.program;
                                safe->showBank(location.bank);
                                safe->grid->selectedProgram = location.program;
                                onChosen(location);
                            }),
                            true);
}

void LibrarianView::askWhereToStoreSlot(nl2x::Slot slot)
{
    askForLocation("Store " + slotName(slot),
                   "Where should " + slotName(slot)
                       + "'s Program be stored? The Bank is backed up first, and you confirm before anything is "
                         "written.",
                   [this, slot](nl2x::BankLocation location) {
                       message.clear();
                       showMemoryRefusal(processor.synthMemory().prepareStoreProgram(
                           slot, location.bank, location.program, toPath(backupFolder()), wallClockNow(),
                           NL2XEditorProcessor::engineTimeMs()));
                   });
}

void LibrarianView::askWhereToStore(const nl2x::LibraryEntry& entry)
{
    const auto description = libraryPanel.dragDescriptionOf(entry);
    const auto name = utf8(libraryPanel.metadata().displayName(entry));
    askForLocation("Store " + name,
                   "Where should \"" + name
                       + "\" be stored? The Bank is backed up first, and you confirm before anything is written.",
                   [this, description](nl2x::BankLocation location) {
                       const auto* found = libraryPanel.draggedEntry(description);
                       if (found == nullptr) {
                           setMessage("The Library was read again meanwhile: choose the sound again.");
                           return;
                       }
                       message.clear();
                       showMemoryRefusal(processor.librarian().prepareStore(*found, location, toPath(backupFolder()),
                                                                            wallClockNow(),
                                                                            NL2XEditorProcessor::engineTimeMs()));
                   });
}

void LibrarianView::askForConfirmation(const nl2x::MemoryWritePlan& plan)
{
    askedToken = plan.token;
    juce::String details;
    const auto& status = processor.synthMemory().status();
    if (! status.message.empty())
        details << utf8(status.message) << "\n\n";
    // What each location will hold and what it holds now, while the list stays readable.
    if (plan.operation == nl2x::MemoryOperation::SendPrograms && plan.programs.size() <= kMostListed) {
        const auto nameOf = [](const LocationText& text) {
            return ! text.known ? juce::String("unknown") : text.name.isEmpty() ? juce::String("unnamed") : text.name;
        };
        for (const auto program : plan.programs) {
            const nl2x::BankLocation location { plan.bank, program };
            details << "Program " << program << ": " << nameOf(describe(location)) << "  (replaces "
                    << nameOf(describeSynth(location)) << ")\n";
        }
        details << "\n";
    }
    details << utf8(plan.details);

    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle(utf8(plan.question))
                             .withMessage(details)
                             .withButton("Overwrite")
                             .withButton("Cancel")
                             .withAssociatedComponent(isShowing() ? this : nullptr);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<LibrarianView>(this), plan](int result) {
        if (safe == nullptr)
            return;
        if (result != 1) {
            safe->processor.synthMemory().cancel();
            return;
        }
        if (plan.needsNoBackupConsent)
            safe->askForNoBackupConsent(plan);
        else
            safe->confirm(plan.token, nl2x::NoBackupConsent::NotGiven);
    });
}

void LibrarianView::askForNoBackupConsent(const nl2x::MemoryWritePlan& plan)
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle("No backup possible")
                             .withMessage(utf8(plan.noBackupWarning))
                             .withButton("Write without a backup")
                             .withButton("Cancel")
                             .withAssociatedComponent(isShowing() ? this : nullptr);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<LibrarianView>(this), plan](int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->confirm(plan.token, nl2x::NoBackupConsent::Given);
        else
            safe->processor.synthMemory().cancel();
    });
}

void LibrarianView::confirm(std::uint64_t token, nl2x::NoBackupConsent consent)
{
    const auto result = processor.synthMemory().confirm(token, consent, NL2XEditorProcessor::engineTimeMs());
    if (result == nl2x::MemoryConfirmResult::RefusedNoSuchPlan)
        setMessage("That question is no longer current: nothing was written.");
    else
        refresh();
}
