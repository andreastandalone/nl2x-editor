#include "LibraryPanel.h"

#include "NordLookAndFeel.h"

#include <algorithm>
#include <filesystem>

namespace {

constexpr const char* kLibraryFolderKey = "libraryFolder";
constexpr int kRefreshIntervalMs = 200;
constexpr int kRowHeight = 24;
constexpr int kStarWidth = 16;
// The tree's indent per level: room for the open/close triangle and little more.
constexpr int kIndentSize = 12;
constexpr int kTagHeight = 17;
constexpr int kTagPadding = 7;
constexpr int kTagGap = 4;
// The text a row keeps visible before its tags give way.
constexpr int kMinTextWidth = 70;
constexpr int kAllCategoriesId = 1;
constexpr int kNoCategoryId = 1;
// Combo box item ids of the categories: kFirstCategoryId + index in `categories`.
constexpr int kFirstCategoryId = 2;

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

// The places SoundLineage follows.
std::string placeOf(nl2x::Slot slot)
{
    return slotName(slot).toStdString();
}
constexpr const char* kPerformancePlace = "Performance";

// The sound in a file the editor writes (one dump), as the Library will read it.
std::optional<nl2x::SoundFingerprint> fingerprintOf(const std::vector<std::uint8_t>& bytes)
{
    const auto file = nl2x::recogniseLibraryFile({}, bytes);
    if (file.entries.size() != 1)
        return std::nullopt;
    return nl2x::SoundFingerprint::of(file.entries.front());
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
    g.drawText(text, kStarWidth, 0, width - kStarWidth, height, juce::Justification::centredLeft, true);
}

// A tag on a row: a small rounded chip, filled (the category) or outlined (a remark).
struct Tag {
    juce::String text;
    bool filled = true;
};

int tagWidth(const Tag& tag)
{
    return juce::GlyphArrangement::getStringWidthInt(juce::Font(NordFonts::tag()), tag.text) + 2 * kTagPadding;
}

void paintTag(juce::Graphics& g, const Tag& tag, juce::Rectangle<float> area)
{
    const auto radius = area.getHeight() * 0.5f;
    if (tag.filled) {
        g.setColour(NordPalette::tagFill);
        g.fillRoundedRectangle(area, radius);
        g.setColour(NordPalette::tagText);
    } else {
        g.setColour(NordPalette::outline);
        g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
        g.setColour(NordPalette::textDim);
    }
    g.setFont(NordFonts::tag());
    g.drawText(tag.text, area.reduced(static_cast<float>(kTagPadding) - 1.0f, 0.0f), juce::Justification::centred,
               true);
}

// A sound: a star when it is a favourite, the name the Library gives it (file or Bank position),
// the user's own name beside it, and at the right end its tags: the category, and whether it was
// recognised as an edited version. A tag that would leave the text too little room is left out.
void paintSoundRow(juce::Graphics& g, const nl2x::LibraryMetadata& metadata, const nl2x::LibraryEntry& entry,
                   const juce::String& prefix, int width, int height)
{
    const auto info = metadata.info(entry);
    if (info.favourite) {
        juce::Path star;
        const auto centre = juce::Point<float>(kStarWidth * 0.5f, static_cast<float>(height) * 0.5f);
        star.addStar(centre, 5, 2.5f, 6.0f, 0.0f);
        g.setColour(NordPalette::ledOn);
        g.fillPath(star);
    }

    std::vector<Tag> tags;
    if (! info.category.empty())
        tags.push_back({ utf8(info.category), true });
    // Recognised by similarity: the sound was changed since it was tagged (on the synth panel, say).
    if (const auto match = metadata.match(nl2x::SoundFingerprint::of(entry)); match && match->differences > 0)
        tags.push_back({ "changed", false });

    // The tags from the right end, the category nearest the text.
    int right = width - kTagGap;
    const auto tagHeight = static_cast<float>(std::min(height - 6, kTagHeight));
    const auto tagTop = (static_cast<float>(height) - tagHeight) * 0.5f;
    for (auto tag = tags.rbegin(); tag != tags.rend(); ++tag) {
        const int size = tagWidth(*tag);
        if (right - size < kStarWidth + kMinTextWidth)
            continue;
        right -= size;
        paintTag(g, *tag, { static_cast<float>(right), tagTop, static_cast<float>(size), tagHeight });
        right -= kTagGap;
    }

    const juce::Font font(NordFonts::body());
    g.setFont(font);
    auto text = juce::Rectangle<int>(kStarWidth, 0, std::max(0, right - kStarWidth), height);
    const auto label = prefix + utf8(entry.name) + kindSuffix(entry.kind());
    if (info.name.empty()) {
        g.setColour(NordPalette::text);
        g.drawText(label, text, juce::Justification::centredLeft, true);
        return;
    }
    // The Library's label dimmed, then the user's name in the text colour.
    const auto labelWidth = std::min(text.getWidth(), juce::GlyphArrangement::getStringWidthInt(font, label + "  "));
    g.setColour(NordPalette::textDim);
    g.drawText(label, text.removeFromLeft(labelWidth), juce::Justification::centredLeft, true);
    g.setColour(NordPalette::text);
    g.drawText(utf8(info.name), text, juce::Justification::centredLeft, true);
}

class EntryItem final : public juce::TreeViewItem {
public:
    EntryItem(LibraryPanel& owner, const nl2x::LibraryEntry& libraryEntry) : panel(owner), entry(libraryEntry) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override { return utf8(entry.name); }
    int getItemHeight() const override { return kRowHeight; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        paintSoundRow(g, panel.metadata(), entry, {}, width, height);
    }
    void itemClicked(const juce::MouseEvent&) override { panel.entryClicked(entry); }
    void itemDoubleClicked(const juce::MouseEvent&) override { panel.entryDoubleClicked(entry); }
    void itemSelectionChanged(bool isNowSelected) override { panel.entrySelected(isNowSelected ? &entry : nullptr); }

private:
    LibraryPanel& panel;
    const nl2x::LibraryEntry& entry;
};

// A file. One holding a single dump behaves as that entry; a Bank file opens into its entries.
// `base` is the folder its subfolder prefix is shown relative to.
class FileItem final : public juce::TreeViewItem {
public:
    FileItem(LibraryPanel& owner, const nl2x::LibraryFile& libraryFile, const std::filesystem::path& base)
        : panel(owner), file(libraryFile)
    {
        std::error_code error;
        auto relative = std::filesystem::relative(file.path.parent_path(), base, error);
        prefix = error || relative == "." ? juce::String() : utf8(relative.string()) + "/";
    }

    bool mightContainSubItems() override { return file.entries.size() > 1; }
    juce::String getUniqueName() const override { return utf8(file.path.string()); }
    int getItemHeight() const override { return kRowHeight; }
    // A Bank file is selected to send it to the synth (#12).
    bool canBeSelected() const override { return ! file.entries.empty(); }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (file.entries.size() == 1) {
            paintSoundRow(g, panel.metadata(), file.entries.front(), prefix, width, height);
            return;
        }
        juce::String text = prefix + utf8(file.name);
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

// The heading the Bank backups are listed under, closed until the user opens it: they are there
// to send a Bank back to the synth, not to browse sounds.
class BackupsItem final : public juce::TreeViewItem {
public:
    explicit BackupsItem(int fileCount) : count(fileCount) {}

    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "Backups heading"; }
    int getItemHeight() const override { return kRowHeight; }
    bool canBeSelected() const override { return false; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        paintRow(g, "Backups  (" + juce::String(count) + (count == 1 ? " file)" : " files)"), width, height, true);
    }
    void itemClicked(const juce::MouseEvent&) override { setOpen(! isOpen()); }

private:
    int count;
};

} // namespace

LibraryPanel::LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), selectedSlot(std::move(slotChoice))
{
    refreshButton.setTooltip("Read the Library folder again");
    refreshButton.onClick = [this] { rescan(); };

    tree.setRootItemVisible(false);
    tree.setIndentSize(kIndentSize);
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

    categoryFilter.setTooltip("Show only the sounds of one category");
    categoryFilter.onChange = [this] { filterChanged(); };
    favouritesFilter.setTooltip("Show only the sounds marked as favourites");
    favouritesFilter.onClick = [this] { filterChanged(); };

    infoTitle.setFont(NordFonts::small());
    infoTitle.setColour(juce::Label::textColourId, NordPalette::textDim);
    nameEditor.setFont(NordFonts::body());
    nameEditor.setTooltip("Your name for the selected sound, kept in library.json in the Library folder. Press "
                          "Return to keep it; leave it empty to use the file or Bank name.");
    nameEditor.onReturnKey = [this] { commitName(); };
    nameEditor.onFocusLost = [this] { commitName(); };
    nameEditor.onEscapeKey = [this] { showSelectedInfo(); };
    categoryBox.setTooltip("The category of the selected sound");
    categoryBox.onChange = [this] {
        if (selectedEntry == nullptr)
            return;
        auto info = libraryMetadata.info(*selectedEntry);
        const auto index = categoryBox.getSelectedId() - kFirstCategoryId;
        info.category = index >= 0 && index < static_cast<int>(categories.size())
                            ? categories[static_cast<std::size_t>(index)]
                            : std::string();
        storeInfo(info);
    };
    favouriteButton.setClickingTogglesState(true);
    favouriteButton.setTooltip("Mark the selected sound as a favourite");
    favouriteButton.onClick = [this] {
        if (selectedEntry == nullptr)
            return;
        auto info = libraryMetadata.info(*selectedEntry);
        info.favourite = favouriteButton.getToggleState();
        storeInfo(info);
    };

    statusLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setFont(NordFonts::small());
    slotLabel.setFont(NordFonts::small());
    slotLabel.setTooltip("Clicking a Program or Percussion Kit auditions it on the Slot tab last shown");

    for (auto* child : std::initializer_list<juce::Component*> { &refreshButton,
                                                                 &slotLabel, &categoryFilter, &favouritesFilter, &tree,
                                                                 &infoTitle, &nameEditor, &categoryBox,
                                                                 &favouriteButton, &saveSlotButton,
                                                                 &savePerformanceButton, &saveEntryButton, &statusLabel })
        addAndMakeVisible(child);

    rescan();
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

LibraryPanel::~LibraryPanel()
{
    stopTimer();
    nameEditor.onFocusLost = nullptr;
    tree.setRootItem(nullptr);
}

void LibraryPanel::paint(juce::Graphics& g)
{
    NordLookAndFeel::drawSection(g, getLocalBounds(), "Library");
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(8, 6);
    // The folder is shown and chosen in the Settings panel; Refresh reads it again.
    auto slotRow = area.removeFromTop(26);
    refreshButton.setBounds(slotRow.removeFromRight(74));
    slotRow.removeFromRight(4);
    slotLabel.setBounds(slotRow);
    area.removeFromTop(4);
    auto filterRow = area.removeFromTop(24);
    favouritesFilter.setBounds(filterRow.removeFromRight(130));
    filterRow.removeFromRight(4);
    categoryFilter.setBounds(filterRow);
    area.removeFromTop(6);

    statusLabel.setBounds(area.removeFromBottom(52));
    area.removeFromBottom(4);
    saveEntryButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    savePerformanceButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    saveSlotButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(10);
    auto infoRow = area.removeFromBottom(24);
    favouriteButton.setBounds(infoRow.removeFromRight(80));
    infoRow.removeFromRight(4);
    categoryBox.setBounds(infoRow);
    area.removeFromBottom(4);
    nameEditor.setBounds(area.removeFromBottom(24));
    infoTitle.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(6);
    tree.setBounds(area);
}

void LibraryPanel::timerCallback()
{
    // Edits keep the link between a Slot (or the Performance) and the Library sound it was
    // auditioned from; another sound arriving there breaks it.
    const auto& synth = processor.synth();
    for (auto slot : nl2x::kAllSlots) {
        const auto& state = synth.slotState(slot);
        lineage.observe(placeOf(slot), state.content == nl2x::SlotContent::Program && state.program
                                           ? std::optional(nl2x::SoundFingerprint::of(*state.program))
                                           : std::nullopt);
    }
    const auto performance = nl2x::performanceFile(synth);
    lineage.observe(kPerformancePlace, performance ? fingerprintOf(*performance) : std::nullopt);

    slotLabel.setText("Audition on " + slotName(selectedSlot()), juce::dontSendNotification);
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
    commitName();
    keepOpenness();
    selectedEntry = nullptr;
    selectedFile = nullptr;
    const auto current = folder();
    // The tree's items point into `files`: drop them first.
    tree.setRootItem(nullptr);
    root.reset();
    files = nl2x::scanLibrary(toPath(current));
    libraryMetadata = nl2x::LibraryMetadata::load(toPath(current));
    libraryMetadata.setLibraryContents(files);
    refreshCategoryChoices();
    rebuildTree();

    if (libraryMetadata.status() == nl2x::LibraryMetadataStatus::Unreadable)
        setStatus("library.json in the Library folder could not be read, so your names, categories and favourites "
                  "are not shown, and it is left untouched. Fix or remove it, then press Refresh.");
    else if (! current.isDirectory())
        setStatus("The Library folder does not exist yet; it is created when you save a .syx file.");
    else if (files.empty())
        setStatus("No .syx files in the Library folder.");
}

void LibraryPanel::rebuildTree()
{
    keepOpenness();
    tree.setRootItem(nullptr);
    root.reset();
    selectedEntry = nullptr;
    selectedFile = nullptr;
    saveEntryButton.setEnabled(false);

    root = std::make_unique<RootItem>();
    const auto base = toPath(folder());
    if (filter.isActive()) {
        for (const auto& ref : nl2x::filterLibrary(files, libraryMetadata, filter))
            root->addSubItem(new EntryItem(*this, *ref.entry));
    } else {
        int backupCount = 0;
        for (const auto& file : files) {
            if (file.isBackup)
                ++backupCount;
            else
                root->addSubItem(new FileItem(*this, file, base));
        }
        if (backupCount > 0) {
            auto* backups = new BackupsItem(backupCount);
            backups->setOpen(false);
            root->addSubItem(backups);
            for (const auto& file : files)
                if (file.isBackup)
                    backups->addSubItem(new FileItem(*this, file, base / std::string(nl2x::kLibraryBackupFolderName)));
        }
    }
    tree.setRootItem(root.get());
    showingFiltered = filter.isActive();
    if (! showingFiltered && unfilteredOpenness != nullptr)
        tree.restoreOpennessState(*unfilteredOpenness, false);
    showSelectedInfo();

    if (filter.isActive() && root->getNumSubItems() == 0)
        setStatus("No sound matches the filter.");
}

void LibraryPanel::keepOpenness()
{
    if (! showingFiltered && root != nullptr)
        unfilteredOpenness = tree.getOpennessState(true);
}

void LibraryPanel::refreshCategoryChoices()
{
    categories = libraryMetadata.categoryChoices();
    const auto chosen = filter.category;
    categoryFilter.clear(juce::dontSendNotification);
    categoryBox.clear(juce::dontSendNotification);
    categoryFilter.addItem("All categories", kAllCategoriesId);
    categoryBox.addItem("No category", kNoCategoryId);
    for (std::size_t i = 0; i < categories.size(); ++i) {
        const auto id = kFirstCategoryId + static_cast<int>(i);
        categoryFilter.addItem(utf8(categories[i]), id);
        categoryBox.addItem(utf8(categories[i]), id);
    }
    const auto found = std::find(categories.begin(), categories.end(), chosen);
    if (found == categories.end())
        filter.category.clear();
    categoryFilter.setSelectedId(found == categories.end()
                                     ? kAllCategoriesId
                                     : kFirstCategoryId + static_cast<int>(found - categories.begin()),
                                 juce::dontSendNotification);
}

void LibraryPanel::filterChanged()
{
    commitName();
    const auto index = categoryFilter.getSelectedId() - kFirstCategoryId;
    filter.category = index >= 0 && index < static_cast<int>(categories.size())
                          ? categories[static_cast<std::size_t>(index)]
                          : std::string();
    filter.favouritesOnly = favouritesFilter.getToggleState();
    setStatus({});
    rebuildTree();
}

void LibraryPanel::showSelectedInfo()
{
    const bool editable = selectedEntry != nullptr
                          && libraryMetadata.status() == nl2x::LibraryMetadataStatus::Ready;
    for (auto* control : std::initializer_list<juce::Component*> { &nameEditor, &categoryBox, &favouriteButton })
        control->setEnabled(editable);

    if (selectedEntry == nullptr) {
        infoTitle.setText("Select a sound to name or categorise it.",
                          juce::dontSendNotification);
        nameEditor.setText({}, false);
        nameEditor.setTextToShowWhenEmpty({}, NordPalette::textDim);
        categoryBox.setSelectedId(kNoCategoryId, juce::dontSendNotification);
        favouriteButton.setToggleState(false, juce::dontSendNotification);
        return;
    }

    const auto info = libraryMetadata.info(*selectedEntry);
    infoTitle.setText("Selected: " + utf8(selectedEntry->name), juce::dontSendNotification);
    nameEditor.setText(utf8(info.name), false);
    nameEditor.setTextToShowWhenEmpty(utf8(selectedEntry->name), NordPalette::textDim);
    const auto found = std::find(categories.begin(), categories.end(), info.category);
    categoryBox.setSelectedId(found == categories.end() ? kNoCategoryId
                                                        : kFirstCategoryId + static_cast<int>(found - categories.begin()),
                              juce::dontSendNotification);
    favouriteButton.setToggleState(info.favourite, juce::dontSendNotification);
}

void LibraryPanel::commitName()
{
    if (selectedEntry == nullptr || ! nameEditor.isEnabled())
        return;
    auto info = libraryMetadata.info(*selectedEntry);
    auto name = nameEditor.getText().trim().toStdString();
    // Typing the name the Library already gives the entry is the same as no name of one's own.
    if (name == selectedEntry->name)
        name.clear();
    if (name == info.name)
        return;
    info.name = name;
    storeInfo(info);
}

void LibraryPanel::storeInfo(const nl2x::SoundInfo& info)
{
    if (selectedEntry == nullptr)
        return;
    switch (libraryMetadata.set(*selectedEntry, info)) {
    case nl2x::MetadataSaveResult::Saved:
        break;
    case nl2x::MetadataSaveResult::RefusedUnreadable:
        setStatus("library.json in the Library folder could not be read, so it is left untouched and nothing was "
                  "saved. Fix or remove it, then press Refresh.");
        break;
    case nl2x::MetadataSaveResult::Failed:
        setStatus("Could not write " + utf8(libraryMetadata.filePath().string()) + ". Nothing was changed.");
        break;
    }
    // The sound stays in the list even if it no longer matches the filter, so that it does not
    // vanish under the pointer; changing the filter or Refresh applies it again.
    showSelectedInfo();
    tree.repaint();
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
    const auto* previous = selectedEntry;
    // A name being typed belongs to the sound it was typed for.
    if (entry != nullptr && entry != previous)
        commitName();
    if (entry != nullptr)
        selectedEntry = entry;
    else if (tree.getNumSelectedItems() == 0)
        selectedEntry = nullptr;
    saveEntryButton.setEnabled(selectedEntry != nullptr);
    if (selectedEntry != previous)
        showSelectedInfo();
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
    if (result == nl2x::AuditionResult::Sent) {
        const bool isPerformance = entry.kind() == nl2x::LibraryEntryKind::Performance;
        const auto place = isPerformance ? std::string(kPerformancePlace) : placeOf(slot);
        if (isPerformance)
            for (auto each : nl2x::kAllSlots)
                lineage.forget(placeOf(each));
        const auto fingerprint = nl2x::SoundFingerprint::of(entry);
        if (const auto match = libraryMetadata.match(fingerprint))
            lineage.loaded(place, match->soundId, fingerprint);
        else
            lineage.forget(place);
    }
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
    saveAs("Save " + slotName(slot) + " as .syx", slotName(slot).toStdString(), std::move(*bytes), placeOf(slot));
}

void LibraryPanel::savePerformance()
{
    auto bytes = nl2x::performanceFile(processor.synth());
    if (! bytes) {
        setStatus("The Performance has not been read from the synth yet.");
        return;
    }
    saveAs("Save the Performance as .syx", "Performance", std::move(*bytes), kPerformancePlace);
}

void LibraryPanel::saveSelectedEntry()
{
    if (selectedEntry == nullptr)
        return;
    const auto name = libraryMetadata.displayName(*selectedEntry);
    saveAs("Save " + utf8(name) + " as .syx", name,
           nl2x::editBufferDumpOf(*selectedEntry));
}

void LibraryPanel::saveAs(const juce::String& dialogTitle, const std::string& suggestedName,
                          std::vector<std::uint8_t> bytes, const std::string& place)
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
    chooser->launchAsync(flags, [this, fileBytes = std::move(bytes), place](const juce::FileChooser& chosen) {
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
            if (const auto sound = place.empty() ? std::nullopt : lineage.soundAt(place))
                if (const auto fingerprint = fingerprintOf(fileBytes))
                    libraryMetadata.followEdit(*sound, *fingerprint);
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
