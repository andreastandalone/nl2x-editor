#include "LibraryPanel.h"

#include "NordLookAndFeel.h"

#include <filesystem>

namespace {

constexpr const char* kLibraryFolderKey = "libraryFolder";
constexpr int kRefreshIntervalMs = 200;
constexpr int kRowHeight = 22;

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

juce::String kindSuffix(nl2x::LibraryEntryKind kind)
{
    switch (kind) {
    case nl2x::LibraryEntryKind::Program:
        return {};
    case nl2x::LibraryEntryKind::PercussionKit:
        return "  (Percussion Kit)";
    case nl2x::LibraryEntryKind::Performance:
        return "  (Performance)";
    }
    return {};
}

void paintRow(juce::Graphics& g, const juce::String& text, int width, int height, bool dimmed)
{
    g.setColour(dimmed ? NordPalette::textDim : NordPalette::text);
    g.setFont(NordFonts::body());
    g.drawText(text, 4, 0, width - 4, height, juce::Justification::centredLeft, true);
}

class EntryItem final : public juce::TreeViewItem {
public:
    EntryItem(LibraryPanel& owner, const nl2x::LibraryEntry& libraryEntry) : panel(owner), entry(libraryEntry) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override { return utf8(entry.name); }
    int getItemHeight() const override { return kRowHeight; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        paintRow(g, utf8(entry.name) + kindSuffix(entry.kind()), width, height, false);
    }
    void itemClicked(const juce::MouseEvent&) override { panel.entryClicked(entry); }
    void itemDoubleClicked(const juce::MouseEvent&) override { panel.entryDoubleClicked(entry); }
    void itemSelectionChanged(bool isNowSelected) override { panel.entrySelected(isNowSelected ? &entry : nullptr); }

private:
    LibraryPanel& panel;
    const nl2x::LibraryEntry& entry;
};

// A file. One holding a single dump behaves as that entry; a Bank file opens into its entries.
class FileItem final : public juce::TreeViewItem {
public:
    FileItem(LibraryPanel& owner, const nl2x::LibraryFile& libraryFile, const std::filesystem::path& folder)
        : panel(owner), file(libraryFile)
    {
        std::error_code error;
        auto relative = std::filesystem::relative(file.path.parent_path(), folder, error);
        prefix = error || relative == "." ? juce::String() : utf8(relative.string()) + "/";
    }

    bool mightContainSubItems() override { return file.entries.size() > 1; }
    juce::String getUniqueName() const override { return utf8(file.path.string()); }
    int getItemHeight() const override { return kRowHeight; }
    // A Bank file is selected to send it to the synth (#12).
    bool canBeSelected() const override { return ! file.entries.empty(); }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        juce::String text = prefix + utf8(file.name);
        if (file.entries.size() == 1)
            text << kindSuffix(file.entries.front().kind());
        else
            text << "  (" << utf8(std::string(nl2x::libraryFileKindName(file.kind)))
                 << (file.entries.empty() ? juce::String() : ", " + juce::String(file.entries.size()) + " entries")
                 << ")";
        paintRow(g, text, width, height, file.entries.empty());
    }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
            for (const auto& entry : file.entries)
                addSubItem(new EntryItem(panel, entry));
    }

    void itemClicked(const juce::MouseEvent&) override
    {
        if (file.entries.size() == 1)
            panel.entryClicked(file.entries.front());
    }
    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        if (file.entries.size() == 1)
            panel.entryDoubleClicked(file.entries.front());
        else if (! file.entries.empty())
            setOpen(! isOpen());
    }
    void itemSelectionChanged(bool isNowSelected) override
    {
        if (file.entries.size() == 1)
            panel.entrySelected(isNowSelected ? &file.entries.front() : nullptr);
        else
            panel.bankFileSelected(file, isNowSelected);
    }

private:
    LibraryPanel& panel;
    const nl2x::LibraryFile& file;
    juce::String prefix;
};

// The invisible root holding the files.
class RootItem final : public juce::TreeViewItem {
public:
    bool mightContainSubItems() override { return true; }
};

} // namespace

LibraryPanel::LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), selectedSlot(std::move(slotChoice))
{
    folderLabel.setFont(NordFonts::small());
    folderLabel.setColour(juce::Label::textColourId, NordPalette::textDim);
    folderLabel.setMinimumHorizontalScale(0.6f);
    refreshButton.setTooltip("Read the Library folder again");
    refreshButton.onClick = [this] { rescan(); };

    tree.setRootItemVisible(false);
    tree.setDefaultOpenness(false);
    tree.setTooltip("Click a Program or Percussion Kit to audition it on the selected Slot; double-click a "
                    "Performance to send it to the Performance Edit Buffer. Nothing is stored in the synth.");

    saveSlotButton.setTooltip("Save the selected Slot's Program as a new .syx file in the Library");
    saveSlotButton.onClick = [this] { saveSlot(); };
    savePerformanceButton.setTooltip("Save the current Performance as a new .syx file in the Library");
    savePerformanceButton.onClick = [this] { savePerformance(); };
    saveEntryButton.setTooltip("Save the selected Library entry (for example a Percussion Kit from a Bank file) "
                               "as its own .syx file");
    saveEntryButton.onClick = [this] { saveSelectedEntry(); };
    saveEntryButton.setEnabled(false);

    statusLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setFont(NordFonts::small());
    slotLabel.setFont(NordFonts::small());

    for (auto* child : std::initializer_list<juce::Component*> { &folderLabel, &refreshButton,
                                                                 &slotLabel, &tree, &saveSlotButton,
                                                                 &savePerformanceButton, &saveEntryButton, &statusLabel })
        addAndMakeVisible(child);

    rescan();
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

LibraryPanel::~LibraryPanel()
{
    stopTimer();
    tree.setRootItem(nullptr);
}

void LibraryPanel::paint(juce::Graphics& g)
{
    NordLookAndFeel::drawSection(g, getLocalBounds(), "Library");
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(8, 6);
    auto folderRow = area.removeFromTop(26);
    refreshButton.setBounds(folderRow.removeFromRight(70));
    folderRow.removeFromRight(4);
    folderLabel.setBounds(folderRow);
    area.removeFromTop(4);
    slotLabel.setBounds(area.removeFromTop(22));

    statusLabel.setBounds(area.removeFromBottom(60));
    area.removeFromBottom(4);
    saveEntryButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    savePerformanceButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    saveSlotButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(8);
    tree.setBounds(area);
}

void LibraryPanel::timerCallback()
{
    slotLabel.setText("Audition on " + slotName(selectedSlot()) + " (the Slot tab last shown)",
                      juce::dontSendNotification);
}

juce::File LibraryPanel::folder() const
{
    const auto saved = processor.midiPorts().userSettings().getValue(kLibraryFolderKey);
    if (saved.isNotEmpty() && juce::File::isAbsolutePath(saved))
        return juce::File(saved);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Nord Lead 2X");
}

void LibraryPanel::setFolder(const juce::File& newFolder)
{
    auto& settings = processor.midiPorts().userSettings();
    settings.setValue(kLibraryFolderKey, newFolder.getFullPathName());
    settings.saveIfNeeded();
    rescan();
}

void LibraryPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Choose the Library folder", folder());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& chosen) {
                             const auto result = chosen.getResult();
                             if (result != juce::File() && result.isDirectory())
                                 setFolder(result);
                         });
}

void LibraryPanel::rescan()
{
    const auto openness = tree.getOpennessState(true);
    tree.setRootItem(nullptr);
    root.reset();
    selectedEntry = nullptr;
    selectedFile = nullptr;
    saveEntryButton.setEnabled(false);

    const auto current = folder();
    folderLabel.setText(current.getFullPathName(), juce::dontSendNotification);
    folderLabel.setTooltip(current.getFullPathName());
    files = nl2x::scanLibrary(toPath(current));

    root = std::make_unique<RootItem>();
    for (const auto& file : files)
        root->addSubItem(new FileItem(*this, file, toPath(current)));
    tree.setRootItem(root.get());
    if (openness != nullptr)
        tree.restoreOpennessState(*openness, false);

    if (! current.isDirectory())
        setStatus("The Library folder does not exist yet; it is created when you save a .syx file.");
    else if (files.empty())
        setStatus("No .syx files in the Library folder.");
}

void LibraryPanel::entryClicked(const nl2x::LibraryEntry& entry)
{
    if (entry.kind() == nl2x::LibraryEntryKind::Performance) {
        setStatus("Double-click a Performance to send it to the Performance Edit Buffer. It replaces the Programs "
                  "of all four Slots.");
        return;
    }
    auditionEntry(entry);
}

void LibraryPanel::entryDoubleClicked(const nl2x::LibraryEntry& entry)
{
    if (entry.kind() == nl2x::LibraryEntryKind::Performance)
        auditionEntry(entry);
}

void LibraryPanel::entrySelected(const nl2x::LibraryEntry* entry)
{
    // A deselection may arrive after the next item's selection: keep the newer one.
    if (entry != nullptr)
        selectedEntry = entry;
    else if (tree.getNumSelectedItems() == 0)
        selectedEntry = nullptr;
    saveEntryButton.setEnabled(selectedEntry != nullptr);
}

void LibraryPanel::bankFileSelected(const nl2x::LibraryFile& file, bool isNowSelected)
{
    if (isNowSelected)
        selectedFile = &file;
    else if (selectedFile == &file)
        selectedFile = nullptr;
}

void LibraryPanel::auditionEntry(const nl2x::LibraryEntry& entry)
{
    const auto slot = selectedSlot();
    const auto result = processor.auditionFromLibrary(entry, slot);
    if (result != nl2x::AuditionResult::Sent)
        setStatus(utf8(nl2x::auditionRefusalText(result, slot)));
    else if (entry.kind() == nl2x::LibraryEntryKind::Performance)
        setStatus("Sent " + utf8(entry.name) + " to the Performance Edit Buffer. No Bank was written.");
    else
        setStatus("Auditioning " + utf8(entry.name) + " on " + slotName(slot) + ". No Bank was written.");
}

void LibraryPanel::saveSlot()
{
    const auto slot = selectedSlot();
    auto bytes = nl2x::slotProgramFile(processor.synth(), slot);
    if (! bytes) {
        setStatus(slotName(slot) + " has no Program to save: it has not been read from the synth, or it is playing "
                                   "a Percussion Kit.");
        return;
    }
    saveAs("Save " + slotName(slot) + " as .syx", slotName(slot).toStdString(), std::move(*bytes));
}

void LibraryPanel::savePerformance()
{
    auto bytes = nl2x::performanceFile(processor.synth());
    if (! bytes) {
        setStatus("The Performance has not been read from the synth yet.");
        return;
    }
    saveAs("Save the Performance as .syx", "Performance", std::move(*bytes));
}

void LibraryPanel::saveSelectedEntry()
{
    if (selectedEntry == nullptr)
        return;
    saveAs("Save " + utf8(selectedEntry->name) + " as .syx", selectedEntry->name,
           nl2x::editBufferDumpOf(*selectedEntry));
}

void LibraryPanel::saveAs(const juce::String& dialogTitle, const std::string& suggestedName,
                          std::vector<std::uint8_t> bytes)
{
    const auto current = folder();
    // So that the dialog opens in it.
    if (! current.isDirectory() && ! current.createDirectory()) {
        setStatus("Cannot create the Library folder " + current.getFullPathName() + ".");
        return;
    }
    const auto suggested = nl2x::uniqueLibraryPath(toPath(current), suggestedName);
    chooser = std::make_unique<juce::FileChooser>(dialogTitle, juce::File(utf8(suggested.string())), "*.syx");
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [this, fileBytes = std::move(bytes)](const juce::FileChooser& chosen) {
        const auto picked = chosen.getResult();
        if (picked == juce::File())
            return;
        // The dialog asked before replacing the file it returned; a file reached only by adding the
        // extension was never asked about, so it is not replaced.
        const auto target = picked.hasFileExtension(".syx") ? picked : picked.withFileExtension(".syx");
        const auto replace = target == picked ? nl2x::ReplaceExisting::Yes : nl2x::ReplaceExisting::No;
        switch (nl2x::saveLibraryFile(toPath(target), fileBytes, replace)) {
        case nl2x::LibrarySaveResult::Saved:
            setStatus("Saved " + target.getFileName() + ".");
            break;
        case nl2x::LibrarySaveResult::AlreadyExists:
            setStatus(target.getFileName() + " already exists and was not replaced. Choose another name.");
            break;
        case nl2x::LibrarySaveResult::Failed:
            setStatus("Could not write " + target.getFullPathName() + ".");
            break;
        }
        rescan();
    });
}

void LibraryPanel::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}
