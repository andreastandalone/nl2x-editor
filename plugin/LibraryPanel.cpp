#include "LibraryPanel.h"

#include "NordLookAndFeel.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace {

constexpr const char* kLibraryFolderKey = "libraryFolder";
// The tree's openness (TreeView::getOpennessState), kept across editor reopens.
constexpr const char* kLibraryOpennessKey = "libraryOpenness";
// The sounds whose suggested category the user dismissed: fingerprint keys, one per line.
constexpr const char* kDismissedSuggestionsKey = "dismissedCategorySuggestions";
constexpr int kRefreshIntervalMs = 200;
constexpr int kRowHeight = 26;
constexpr int kStarWidth = 16;
// The tree's indent per level: room for the open/close triangle and little more.
constexpr int kIndentSize = 12;
constexpr int kTagHeight = NordMetrics::tagHeight;
constexpr int kTagGap = NordMetrics::tagGap;
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

// A sound's kind and values as text, to key the suggestions by.
std::string fingerprintKey(const nl2x::SoundFingerprint& fingerprint)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string key(1, static_cast<char>('0' + static_cast<int>(fingerprint.kind)));
    key += ':';
    for (const auto value : fingerprint.values) {
        key += kHex[value >> 4];
        key += kHex[value & 0x0f];
    }
    return key;
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

// A tag on a row: a small rounded chip, filled (the category), darker and edged (an auto-assigned
// category), outlined (a remark) or dashed (a suggested category).
struct Tag {
    enum class Style { Filled, Auto, Outlined, Suggested };
    juce::String text;
    Style style = Style::Filled;
};

// A sound: a star when it is a favourite, the name the Library gives it (file or Bank position),
// the user's own name beside it, and at the right end its tags: the category (or a suggested one),
// and whether it was recognised as an edited version. A tag that would leave the text too little
// room is left out. Returns where the suggested category's tag is, empty when none is drawn.
juce::Rectangle<float> paintSoundRow(juce::Graphics& g, const LibraryPanel& panel, const nl2x::LibraryEntry& entry,
                                     const juce::String& prefix, int width, int height)
{
    const auto& metadata = panel.metadata();
    const auto fingerprint = nl2x::SoundFingerprint::of(entry);
    const auto info = metadata.info(fingerprint);
    if (info.favourite) {
        juce::Path star;
        const auto centre = juce::Point<float>(kStarWidth * 0.5f, static_cast<float>(height) * 0.5f);
        star.addStar(centre, 5, 2.5f, 6.0f, 0.0f);
        g.setColour(NordPalette::ledOn);
        g.fillPath(star);
    }

    std::vector<Tag> tags;
    if (! info.category.empty())
        tags.push_back({ utf8(info.category), info.autoAssigned ? Tag::Style::Auto : Tag::Style::Filled });
    else if (const auto suggestion = panel.suggestionFor(fingerprint))
        tags.push_back({ utf8(suggestion->category), Tag::Style::Suggested });
    // Recognised by similarity: the sound was changed since it was tagged (on the synth panel, say).
    if (const auto match = metadata.match(fingerprint); match && match->differences > 0)
        tags.push_back({ "changed", Tag::Style::Outlined });

    // The tags from the right end, the category nearest the text.
    juce::Rectangle<float> suggestedTag;
    int right = width - kTagGap;
    const auto tagHeight = static_cast<float>(std::min(height - 6, kTagHeight));
    const auto tagTop = (static_cast<float>(height) - tagHeight) * 0.5f;
    for (auto tag = tags.rbegin(); tag != tags.rend(); ++tag) {
        const bool suggested = tag->style == Tag::Style::Suggested;
        const int size = NordLookAndFeel::tagWidth(suggested ? NordLookAndFeel::suggestedTagText(tag->text) : tag->text);
        if (right - size < kStarWidth + kMinTextWidth)
            continue;
        right -= size;
        const juce::Rectangle<float> area { static_cast<float>(right), tagTop, static_cast<float>(size), tagHeight };
        if (suggested) {
            NordLookAndFeel::drawSuggestedTag(g, tag->text, area);
            suggestedTag = area;
        } else if (tag->style == Tag::Style::Auto) {
            NordLookAndFeel::drawAutoTag(g, tag->text, area);
        } else {
            NordLookAndFeel::drawTag(g, tag->text, area, tag->style == Tag::Style::Filled);
        }
        right -= kTagGap;
    }

    const juce::Font font(NordFonts::body());
    g.setFont(font);
    auto text = juce::Rectangle<int>(kStarWidth, 0, std::max(0, right - kStarWidth), height);
    const auto label = prefix + utf8(entry.name) + kindSuffix(entry.kind());
    if (info.name.empty()) {
        g.setColour(NordPalette::text);
        g.drawText(label, text, juce::Justification::centredLeft, true);
        return suggestedTag;
    }
    // The Library's label dimmed, then the user's name in the text colour.
    const auto labelWidth = std::min(text.getWidth(), juce::GlyphArrangement::getStringWidthInt(font, label + "  "));
    g.setColour(NordPalette::textDim);
    g.drawText(label, text.removeFromLeft(labelWidth), juce::Justification::centredLeft, true);
    g.setColour(NordPalette::text);
    g.drawText(utf8(info.name), text, juce::Justification::centredLeft, true);
    return suggestedTag;
}

// A click on a row's suggested category keeps it (and does not audition the sound).
bool clickedSuggestedTag(LibraryPanel& panel, const nl2x::LibraryEntry& entry, juce::Rectangle<float> suggestedTag,
                         const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! suggestedTag.contains(e.position))
        return false;
    panel.acceptSuggestion(nl2x::SoundFingerprint::of(entry));
    return true;
}

class EntryItem final : public juce::TreeViewItem {
public:
    EntryItem(LibraryPanel& owner, const nl2x::LibraryEntry& libraryEntry) : panel(owner), entry(libraryEntry) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override { return utf8(entry.name); }
    int getItemHeight() const override { return kRowHeight; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        suggestedTag = paintSoundRow(g, panel, entry, {}, width, height);
    }
    void itemClicked(const juce::MouseEvent& e) override
    {
        if (clickedSuggestedTag(panel, entry, suggestedTag, e))
            return;
        if (e.mods.isPopupMenu())
            panel.entryMenu(entry);
        else
            panel.entryClicked(entry);
    }
    void itemDoubleClicked(const juce::MouseEvent&) override { panel.entryDoubleClicked(entry); }
    void itemSelectionChanged(bool isNowSelected) override { panel.entrySelected(isNowSelected ? &entry : nullptr); }
    // A Program is dragged onto a Bank location of the Librarian (#44).
    juce::var getDragSourceDescription() override
    {
        return entry.kind() == nl2x::LibraryEntryKind::Program ? panel.dragDescriptionOf(entry) : juce::var();
    }

private:
    LibraryPanel& panel;
    const nl2x::LibraryEntry& entry;
    juce::Rectangle<float> suggestedTag;
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
            suggestedTag = paintSoundRow(g, panel, file.entries.front(), prefix, width, height);
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

    void itemClicked(const juce::MouseEvent& e) override
    {
        if (file.entries.size() != 1 || clickedSuggestedTag(panel, file.entries.front(), suggestedTag, e))
            return;
        if (e.mods.isPopupMenu())
            panel.entryMenu(file.entries.front());
        else
            panel.entryClicked(file.entries.front());
    }
    // A single Program goes onto a Bank location, a Program Bank file onto a Bank (#44).
    juce::var getDragSourceDescription() override
    {
        if (file.entries.size() == 1)
            return file.entries.front().kind() == nl2x::LibraryEntryKind::Program
                       ? panel.dragDescriptionOf(file.entries.front())
                       : juce::var();
        return file.kind == nl2x::LibraryFileKind::ProgramBank ? panel.dragDescriptionOf(file) : juce::var();
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
    juce::Rectangle<float> suggestedTag;
};

// The invisible root holding the files.
class RootItem final : public juce::TreeViewItem {
public:
    bool mightContainSubItems() override { return true; }
};

// The outline of a small folder, 11 x 9, its left edge at `left` and centred on `mid`: the mark
// of a group heading and the icon of the Finder button.
juce::Path folderMark(float left, float mid)
{
    juce::Path folder;
    folder.startNewSubPath(left, mid - 4.0f);
    folder.lineTo(left + 4.0f, mid - 4.0f);
    folder.lineTo(left + 5.5f, mid - 2.5f);
    folder.lineTo(left + 11.0f, mid - 2.5f);
    folder.lineTo(left + 11.0f, mid + 5.0f);
    folder.lineTo(left, mid + 5.0f);
    folder.closeSubPath();
    return folder;
}

// A subfolder of the Library folder (#44): a heading named after it, with a small folder drawn
// where a sound's star goes, opened and closed by a click. Closed until the user opens it; the
// tree remembers it (keyed by the folder's name among its siblings).
class GroupItem final : public juce::TreeViewItem {
public:
    GroupItem(const std::string& folderName, juce::String countText) : name(utf8(folderName)), count(std::move(countText))
    {
    }

    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "Folder " + name; }
    int getItemHeight() const override { return kRowHeight; }
    bool canBeSelected() const override { return false; }
    void paintItem(juce::Graphics& g, int width, int height) override
    {
        g.setColour(NordPalette::textDim);
        g.strokePath(folderMark(2.0f, static_cast<float>(height) * 0.5f), juce::PathStrokeType(1.2f));

        const juce::Font font(NordFonts::body());
        g.setFont(font);
        g.setColour(NordPalette::text);
        const auto nameWidth = std::min(width - kStarWidth, juce::GlyphArrangement::getStringWidthInt(font, name + "  "));
        g.drawText(name, kStarWidth, 0, nameWidth, height, juce::Justification::centredLeft, true);
        g.setColour(NordPalette::textDim);
        g.drawText(count, kStarWidth + nameWidth, 0, std::max(0, width - kStarWidth - nameWidth), height,
                   juce::Justification::centredLeft, true);
    }
    void itemClicked(const juce::MouseEvent&) override { setOpen(! isOpen()); }

private:
    juce::String name;
    juce::String count;
};

juce::String countText(std::size_t count, const char* singular, const char* plural)
{
    return "(" + juce::String(count) + " " + (count == 1 ? singular : plural) + ")";
}

std::size_t groupFileCount(const nl2x::LibraryGroup& group)
{
    auto count = group.files.size();
    for (const auto& inner : group.groups)
        count += groupFileCount(inner);
    return count;
}

std::size_t entryCount(const nl2x::LibraryEntryGroup& group)
{
    auto count = group.entries.size();
    for (const auto& inner : group.groups)
        count += entryCount(inner);
    return count;
}

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

// A group's subfolders first, each under its heading, then its files.
void addGroup(LibraryPanel& panel, juce::TreeViewItem& parent, const nl2x::LibraryGroup& group)
{
    for (const auto& inner : group.groups) {
        auto* item = new GroupItem(inner.name, countText(groupFileCount(inner), "file", "files"));
        parent.addSubItem(item);
        addGroup(panel, *item, inner);
    }
    for (const auto* file : group.files)
        parent.addSubItem(new FileItem(panel, *file, file->path.parent_path()));
}

// The same for a filtered Library, its groups open so that every matching sound shows.
void addFilteredGroup(LibraryPanel& panel, juce::TreeViewItem& parent, const nl2x::LibraryEntryGroup& group)
{
    for (const auto& inner : group.groups) {
        auto* item = new GroupItem(inner.name, countText(entryCount(inner), "sound", "sounds"));
        item->setOpen(true);
        parent.addSubItem(item);
        addFilteredGroup(panel, *item, inner);
    }
    for (const auto& ref : group.entries)
        parent.addSubItem(new EntryItem(panel, *ref.entry));
}

} // namespace

LibraryPanel::LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), selectedSlot(std::move(slotChoice))
{
    refreshButton.setTooltip("Read the Library folder again");
    refreshButton.onClick = [this] { rescan(); };
    autoTagButton.setTooltip("Auto-tag: give every sound with no category its suggested one at once (you confirm "
                             "first), or undo that");
    autoTagButton.onClick = [this] { showAutoTagMenu(); };
    juce::DrawablePath finderIcon;
    finderIcon.setPath(folderMark(0.0f, 4.5f));
    finderIcon.setFill(juce::FillType());
    finderIcon.setStrokeFill(NordPalette::text);
    finderIcon.setStrokeType(juce::PathStrokeType(1.2f));
    finderButton.setImages(&finderIcon);
    finderButton.setEdgeIndent(7);
    finderButton.setTooltip("Show the Library folder in the Finder, to add or move Bank files and subfolders");
    finderButton.onClick = [this] {
        // Opens the folder itself (revealToUser would only select it in its parent).
        const auto current = folder();
        if (current.isDirectory() || current.createDirectory())
            current.startAsProcess();
    };

    tree.setComponentID("libraryTree");
    favouritesFilter.setComponentID("favouritesOnly");
    tree.setRootItemVisible(false);
    tree.setIndentSize(kIndentSize);
    tree.setDefaultOpenness(false);
    tree.setTooltip("Click a Program or Percussion Kit to audition it on the selected Slot; double-click a "
                    "Performance to send it to the Performance Edit Buffer. Nothing is stored in the synth. In the "
                    "Librarian tab, drag a Program onto a Bank location, or a Bank file onto a Bank. A dashed tag "
                    "such as \"Bass?\" is a suggested category: click it to keep it, or right-click the sound to "
                    "dismiss it. A darker tag is a category given by Auto-tag: right-click the sound to keep or remove "
                    "it.");

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
        // Picked by hand: the user's own, no longer auto-assigned.
        info.autoAssigned = false;
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
    slotLabel.setTooltip("The Slot that clicking a Program or Percussion Kit auditions it on: the Slot tab last shown");

    for (auto* child : std::initializer_list<juce::Component*> { &refreshButton, &autoTagButton, &finderButton,
                                                                 &slotLabel, &categoryFilter, &favouritesFilter, &tree,
                                                                 &infoTitle, &nameEditor, &categoryBox,
                                                                 &favouriteButton, &saveSlotButton,
                                                                 &savePerformanceButton, &saveEntryButton, &statusLabel })
        addAndMakeVisible(child);

    // The groups and files left open when the editor last closed.
    unfilteredOpenness = processor.midiPorts().userSettings().getXmlValue(kLibraryOpennessKey);
    for (const auto& key : juce::StringArray::fromLines(processor.midiPorts().userSettings().getValue(kDismissedSuggestionsKey)))
        if (key.isNotEmpty())
            dismissedSuggestions.insert(key.toStdString());
    rescan();
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

LibraryPanel::~LibraryPanel()
{
    keepOpenness();
    if (unfilteredOpenness != nullptr) {
        auto& settings = processor.midiPorts().userSettings();
        settings.setValue(kLibraryOpennessKey, unfilteredOpenness.get());
        settings.saveIfNeeded();
    }
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
    // The folder is shown and chosen in the Settings panel; Refresh reads it again, Finder shows it.
    auto slotRow = area.removeFromTop(26);
    refreshButton.setBounds(slotRow.removeFromRight(70));
    slotRow.removeFromRight(4);
    autoTagButton.setBounds(slotRow.removeFromRight(80));
    slotRow.removeFromRight(4);
    finderButton.setBounds(slotRow.removeFromRight(36));
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
    // The Slot label's text depends on its width.
    timerCallback();
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

    // "Audition on Slot A" when the panel is wide enough, otherwise only the Slot (the tooltip says
    // what it is for).
    const auto full = "Audition on " + slotName(selectedSlot());
    const auto room = slotLabel.getWidth() - slotLabel.getBorderSize().getLeftAndRight();
    const bool fits = juce::GlyphArrangement::getStringWidthInt(juce::Font(NordFonts::small()), full) <= room;
    slotLabel.setText(fits ? full : slotName(selectedSlot()), juce::dontSendNotification);
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
    ++scanCount;
    libraryMetadata = nl2x::LibraryMetadata::load(toPath(current));
    libraryMetadata.setLibraryContents(files);
    findSuggestions();
    ++metadataChanges;
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
        addFilteredGroup(*this, *root, nl2x::groupEntries(nl2x::filterLibrary(files, libraryMetadata, filter)));
    } else {
        addGroup(*this, *root, nl2x::groupLibrary(files));
        const auto backupCount = std::count_if(files.begin(), files.end(), [](const auto& file) { return file.isBackup; });
        if (backupCount > 0) {
            auto* backups = new BackupsItem(static_cast<int>(backupCount));
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
    infoTitle.setText("Selected: " + utf8(selectedEntry->name) + (info.autoAssigned ? "  (category by Auto-tag)" : ""),
                      juce::dontSendNotification);
    nameEditor.setText(utf8(info.name), false);
    nameEditor.setTextToShowWhenEmpty(utf8(selectedEntry->name), NordPalette::textDim);
    const auto found = std::find(categories.begin(), categories.end(), info.category);
    const auto suggestion = info.category.empty() ? suggestionFor(nl2x::SoundFingerprint::of(*selectedEntry))
                                                  : std::nullopt;
    if (suggestion) {
        // Nothing chosen yet: the box shows the suggestion until a category is picked.
        categoryBox.setSelectedId(0, juce::dontSendNotification);
        categoryBox.setTextWhenNothingSelected("Suggested: " + utf8(suggestion->category));
    } else {
        categoryBox.setSelectedId(found == categories.end()
                                      ? kNoCategoryId
                                      : kFirstCategoryId + static_cast<int>(found - categories.begin()),
                                  juce::dontSendNotification);
    }
    favouriteButton.setToggleState(info.favourite, juce::dontSendNotification);
}

std::optional<nl2x::CategorySuggestion> LibraryPanel::suggestionFor(const nl2x::SoundFingerprint& fingerprint) const
{
    if (! libraryMetadata.info(fingerprint).category.empty())
        return std::nullopt;
    const auto key = fingerprintKey(fingerprint);
    if (dismissedSuggestions.contains(key))
        return std::nullopt;
    if (const auto found = suggestions.find(key); found != suggestions.end())
        return found->second;
    if (fingerprint.kind != nl2x::LibraryEntryKind::Program)
        return std::nullopt;
    if (const auto known = otherSuggestions.find(key); known != otherSuggestions.end())
        return known->second;
    std::vector<std::int8_t> values(fingerprint.values.size());
    std::transform(fingerprint.values.begin(), fingerprint.values.end(), values.begin(),
                   [](std::uint8_t value) { return static_cast<std::int8_t>(value); });
    const auto program = nl2x::Program::fromValues(values);
    auto suggestion = program ? nl2x::suggestCategory(*program, learned) : std::nullopt;
    otherSuggestions.emplace(key, suggestion);
    return suggestion;
}

void LibraryPanel::acceptSuggestion(const nl2x::SoundFingerprint& fingerprint)
{
    const auto suggestion = suggestionFor(fingerprint);
    if (! suggestion)
        return;
    auto info = libraryMetadata.info(fingerprint);
    info.category = suggestion->category;
    info.autoAssigned = false;
    storeInfo(fingerprint, info);
}

void LibraryPanel::keepAutoAssigned(const nl2x::SoundFingerprint& fingerprint)
{
    auto info = libraryMetadata.info(fingerprint);
    if (! info.autoAssigned)
        return;
    info.autoAssigned = false;
    storeInfo(fingerprint, info);
}

void LibraryPanel::removeAutoAssigned(const nl2x::SoundFingerprint& fingerprint)
{
    auto info = libraryMetadata.info(fingerprint);
    if (! info.autoAssigned)
        return;
    info.category.clear();
    info.autoAssigned = false;
    storeInfo(fingerprint, info);
}

void LibraryPanel::dismissSuggestion(const nl2x::SoundFingerprint& fingerprint)
{
    dismissedSuggestions.insert(fingerprintKey(fingerprint));
    juce::StringArray keys;
    for (const auto& key : dismissedSuggestions)
        keys.add(key);
    auto& settings = processor.midiPorts().userSettings();
    settings.setValue(kDismissedSuggestionsKey, keys.joinIntoString("\n"));
    settings.saveIfNeeded();
    ++metadataChanges;
    showSelectedInfo();
    tree.repaint();
}

void LibraryPanel::findSuggestions()
{
    learned = nl2x::LearnedCategories::of(libraryMetadata);
    suggestions.clear();
    otherSuggestions.clear();
    for (const auto& file : files) {
        if (file.isBackup)
            continue;
        for (const auto& entry : file.entries) {
            auto suggestion = nl2x::suggestCategory(file, entry, learned);
            if (! suggestion)
                continue;
            // The same sound in its own named file and in a Bank file: the name is more trusted.
            const auto [found, added] = suggestions.emplace(fingerprintKey(nl2x::SoundFingerprint::of(entry)), *suggestion);
            if (! added && suggestion->source < found->second.source)
                found->second = std::move(*suggestion);
        }
    }
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
    if (selectedEntry != nullptr)
        storeInfo(nl2x::SoundFingerprint::of(*selectedEntry), info);
}

void LibraryPanel::storeInfo(const nl2x::SoundFingerprint& fingerprint, const nl2x::SoundInfo& info)
{
    ++metadataChanges;
    const auto before = libraryMetadata.info(fingerprint);
    const auto result = libraryMetadata.set(fingerprint, info);
    // A category set or removed by hand teaches the suggestions something new.
    const auto after = libraryMetadata.info(fingerprint);
    if (before.category != after.category || before.autoAssigned != after.autoAssigned)
        findSuggestions();
    switch (result) {
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

void LibraryPanel::entryMenu(const nl2x::LibraryEntry& entry)
{
    const auto fingerprint = nl2x::SoundFingerprint::of(entry);
    const auto suggestion = suggestionFor(fingerprint);
    const auto info = libraryMetadata.info(fingerprint);
    if (entry.kind() != nl2x::LibraryEntryKind::Program && ! suggestion && ! info.autoAssigned)
        return;
    // The entry lives in `files`: the menu finds it again by its description, so that a rescan
    // while the menu is open leaves nothing dangling.
    const auto description = dragDescriptionOf(entry);
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    juce::PopupMenu menu;
    if (entry.kind() == nl2x::LibraryEntryKind::Program) {
        menu.addItem("Audition on " + slotName(selectedSlot()), [safe, description] {
            if (safe != nullptr)
                if (const auto* found = safe->draggedEntry(description))
                    safe->auditionEntry(*found);
        });
        menu.addItem(juce::String::fromUTF8("Store to Bank/Program\xE2\x80\xA6"), static_cast<bool>(onStoreEntry), false,
                     [safe, description] {
                         if (safe != nullptr && safe->onStoreEntry)
                             if (const auto* found = safe->draggedEntry(description))
                                 safe->onStoreEntry(*found);
                     });
    }
    if (suggestion) {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addItem("Keep suggested category: " + utf8(suggestion->category), [safe, fingerprint] {
            if (safe != nullptr)
                safe->acceptSuggestion(fingerprint);
        });
        menu.addItem("Dismiss suggested category", [safe, fingerprint] {
            if (safe != nullptr)
                safe->dismissSuggestion(fingerprint);
        });
    }
    if (info.autoAssigned) {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addItem("Keep category given by Auto-tag: " + utf8(info.category), [safe, fingerprint] {
            if (safe != nullptr)
                safe->keepAutoAssigned(fingerprint);
        });
        menu.addItem("Remove category given by Auto-tag", [safe, fingerprint] {
            if (safe != nullptr)
                safe->removeAutoAssigned(fingerprint);
        });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void LibraryPanel::showAutoTagMenu()
{
    commitName();
    const bool ready = libraryMetadata.status() == nl2x::LibraryMetadataStatus::Ready;
    auto plan = ready ? autoTagPlan() : std::vector<nl2x::LibraryMetadata::AutoAssignment> {};
    const auto assigned = libraryMetadata.autoAssignedCount();
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    const auto ellipsis = juce::String::fromUTF8("\xE2\x80\xA6");
    juce::PopupMenu menu;
    menu.addItem(plan.empty() ? juce::String("Auto-tag: no suggestion to keep")
                              : "Auto-tag " + countText(plan.size(), "sound", "sounds") + ellipsis,
                 ! plan.empty(), false, [safe, plan] {
                     if (safe != nullptr)
                         safe->autoTag(plan);
                 });
    menu.addItem(assigned == 0 ? juce::String("Undo auto-tag")
                               : "Undo auto-tag (" + countText(static_cast<std::size_t>(assigned), "sound", "sounds") + ")" + ellipsis,
                 ready && assigned > 0, false, [safe] {
                     if (safe != nullptr)
                         safe->undoAutoTag();
                 });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoTagButton));
}

std::vector<nl2x::LibraryMetadata::AutoAssignment> LibraryPanel::autoTagPlan() const
{
    std::vector<nl2x::LibraryMetadata::AutoAssignment> plan;
    std::set<std::string> seen;
    for (const auto& file : files) {
        if (file.isBackup)
            continue;
        for (const auto& entry : file.entries) {
            auto fingerprint = nl2x::SoundFingerprint::of(entry);
            if (! seen.insert(fingerprintKey(fingerprint)).second)
                continue;
            // None for a sound with a category of its own or a dismissed suggestion.
            if (const auto suggestion = suggestionFor(fingerprint))
                plan.push_back({ std::move(fingerprint), suggestion->category });
        }
    }
    return plan;
}

void LibraryPanel::autoTag(const std::vector<nl2x::LibraryMetadata::AutoAssignment>& plan)
{
    // "Bass 312, Keys 240, Percussion 89": the most first.
    std::map<std::string, int> counts;
    for (const auto& assignment : plan)
        ++counts[assignment.category];
    std::vector<std::pair<std::string, int>> byCount(counts.begin(), counts.end());
    std::stable_sort(byCount.begin(), byCount.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    juce::StringArray parts;
    for (const auto& [category, count] : byCount)
        parts.add(utf8(category) + " " + juce::String(count));

    const auto message = "Give " + countText(plan.size(), "sound", "sounds")
                         + " with no category of their own the category suggested for them:\n\n"
                         + parts.joinIntoString(", ")
                         + ".\n\nThe categories are written to library.json, marked as given by Auto-tag (a darker "
                           "tag). Picking or keeping one by hand makes it yours; Undo auto-tag removes the others. "
                           "Dismissed suggestions are left out.";
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Auto-tag the Library")
                             .withMessage(message)
                             .withButton("Auto-tag")
                             .withButton("Cancel")
                             .withAssociatedComponent(isShowing() ? this : nullptr);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<LibraryPanel>(this), plan](int result) {
        if (safe == nullptr || result != 1)
            return;
        const auto before = safe->libraryMetadata.autoAssignedCount();
        const auto saved = safe->libraryMetadata.autoAssign(plan);
        safe->metadataReplaced(saved);
        if (saved == nl2x::MetadataSaveResult::Saved)
            safe->setStatus("Auto-tag gave " + countText(static_cast<std::size_t>(std::max(0, safe->libraryMetadata.autoAssignedCount() - before)),
                                                     "sound", "sounds")
                            + " a category. Undo auto-tag, in the same menu, removes those you do not touch.");
    });
}

void LibraryPanel::undoAutoTag()
{
    const auto count = libraryMetadata.autoAssignedCount();
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Undo auto-tag")
                             .withMessage("Remove the category Auto-tag gave " + countText(static_cast<std::size_t>(count), "sound", "sounds")
                                          + "? Categories you picked or kept by hand stay.")
                             .withButton("Remove")
                             .withButton("Cancel")
                             .withAssociatedComponent(isShowing() ? this : nullptr);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<LibraryPanel>(this)](int result) {
        if (safe == nullptr || result != 1)
            return;
        const auto before = safe->libraryMetadata.autoAssignedCount();
        const auto saved = safe->libraryMetadata.removeAutoAssigned();
        safe->metadataReplaced(saved);
        if (saved == nl2x::MetadataSaveResult::Saved)
            safe->setStatus("Removed the category Auto-tag gave " + countText(static_cast<std::size_t>(before), "sound", "sounds") + ".");
    });
}

void LibraryPanel::metadataReplaced(nl2x::MetadataSaveResult result)
{
    switch (result) {
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
    ++metadataChanges;
    findSuggestions();
    refreshCategoryChoices();
    rebuildTree();
}

namespace {

constexpr const char* kEntryDragPrefix = "nl2x-entry:";
constexpr const char* kBankFileDragPrefix = "nl2x-bank-file:";

} // namespace

juce::var LibraryPanel::dragDescriptionOf(const nl2x::LibraryEntry& entry) const
{
    for (std::size_t f = 0; f < files.size(); ++f)
        for (std::size_t e = 0; e < files[f].entries.size(); ++e)
            if (&files[f].entries[e] == &entry)
                return juce::String(kEntryDragPrefix) + juce::String(scanCount) + ":" + juce::String(f) + ":"
                       + juce::String(e);
    return {};
}

juce::var LibraryPanel::dragDescriptionOf(const nl2x::LibraryFile& file) const
{
    for (std::size_t f = 0; f < files.size(); ++f)
        if (&files[f] == &file)
            return juce::String(kBankFileDragPrefix) + juce::String(scanCount) + ":" + juce::String(f);
    return {};
}

const nl2x::LibraryEntry* LibraryPanel::draggedEntry(const juce::var& description) const
{
    const auto text = description.toString();
    if (! text.startsWith(kEntryDragPrefix))
        return nullptr;
    const auto parts = juce::StringArray::fromTokens(text.fromFirstOccurrenceOf(kEntryDragPrefix, false, false), ":", {});
    if (parts.size() != 3 || parts[0].getIntValue() != scanCount)
        return nullptr;
    const auto f = static_cast<std::size_t>(parts[1].getIntValue());
    const auto e = static_cast<std::size_t>(parts[2].getIntValue());
    if (f >= files.size() || e >= files[f].entries.size())
        return nullptr;
    return &files[f].entries[e];
}

const nl2x::LibraryFile* LibraryPanel::draggedBankFile(const juce::var& description) const
{
    const auto text = description.toString();
    if (! text.startsWith(kBankFileDragPrefix))
        return nullptr;
    const auto parts = juce::StringArray::fromTokens(text.fromFirstOccurrenceOf(kBankFileDragPrefix, false, false), ":", {});
    if (parts.size() != 2 || parts[0].getIntValue() != scanCount)
        return nullptr;
    const auto f = static_cast<std::size_t>(parts[1].getIntValue());
    return f < files.size() ? &files[f] : nullptr;
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
