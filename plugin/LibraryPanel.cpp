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
// The suggested tags the user dismissed: a fingerprint key and a tag, tab-separated, one per line.
// The name is that of the editors that suggested one category per sound, whose lines (a key alone)
// still dismiss every suggestion for their sound.
constexpr const char* kDismissedSuggestionsKey = "dismissedCategorySuggestions";
constexpr int kRefreshIntervalMs = 200;
constexpr int kRowHeight = 26;
constexpr int kStarWidth = 16;
// The tree's indent per level: room for the open/close triangle and little more.
constexpr int kIndentSize = 12;
// A row's tags: the text keeps this much before they give way to "+N"; the add zone after them.
const TagChipRow::Layout kRowTags { 70, 18, 40, {} };
// The edit area's tags: no text beside them, and an add chip that says what it is.
const TagChipRow::Layout kEditTags { 0, 56, 56, "+ Tag", true };
constexpr int kAllTagsId = 1;
// Combo box item ids of the tags: kFirstTagId + index in `tagChoices`.
constexpr int kFirstTagId = 2;

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

// A sound: a star when it is a favourite, the name the Library gives it (file or Bank position),
// the user's own name beside it, and at the right end its tags (TagChipRow), laid out into `tags`
// for clicks. The add zone shows on the selected row.
void paintSoundRow(juce::Graphics& g, const LibraryPanel& panel, const nl2x::LibraryEntry& entry,
                   const juce::String& prefix, int width, int height, bool selected, TagChipRow& tags)
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

    tags.layout(panel.chipsFor(fingerprint), { kStarWidth, 0, std::max(0, width - kStarWidth - 2), height }, kRowTags);
    tags.paint(g, selected);

    const juce::Font font(NordFonts::body());
    g.setFont(font);
    auto text = tags.textArea();
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

// A click on a row's tags (not the context menu) acts on them, and does not audition the sound.
bool clickedTags(LibraryPanel& panel, const nl2x::LibraryEntry& entry, const TagChipRow& tags, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return false;
    const auto hit = tags.hitTest(e.position);
    if (hit.kind == TagChipRow::Hit::Kind::None)
        return false;
    panel.tagClicked(nl2x::SoundFingerprint::of(entry), hit);
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
        paintSoundRow(g, panel, entry, {}, width, height, isSelected(), tags);
    }
    void itemClicked(const juce::MouseEvent& e) override
    {
        if (clickedTags(panel, entry, tags, e))
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
    TagChipRow tags;
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
            paintSoundRow(g, panel, file.entries.front(), prefix, width, height, isSelected(), tags);
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
        if (file.entries.size() != 1 || clickedTags(panel, file.entries.front(), tags, e))
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
    TagChipRow tags;
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

namespace {

// The key a dismissed suggestion is remembered by.
std::string dismissalKey(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    return fingerprintKey(fingerprint) + "\t" + tag;
}

juce::String quoted(const std::string& tag)
{
    return juce::String::fromUTF8("\xE2\x80\x9C") + utf8(tag) + juce::String::fromUTF8("\xE2\x80\x9D");
}

const juce::String kEllipsis = juce::String::fromUTF8("\xE2\x80\xA6");

} // namespace

// The tags of the edited sound in the edit area, clicked as on a row; "+ Tag" adds one.
class LibraryPanel::TagStrip final : public juce::Component, public juce::TooltipClient {
public:
    explicit TagStrip(LibraryPanel& owner) : panel(owner) { setComponentID("editTags"); }

    void paint(juce::Graphics& g) override
    {
        const auto sound = panel.editedSound();
        if (! sound || ! isEnabled()) {
            g.setColour(NordPalette::textDim.withAlpha(0.6f));
            g.setFont(NordFonts::small());
            g.drawText("No tags", getLocalBounds(), juce::Justification::centredLeft, true);
            return;
        }
        chips.layout(panel.chipsFor(*sound), getLocalBounds(), kEditTags);
        chips.paint(g, true);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto sound = panel.editedSound();
        if (! sound || ! isEnabled())
            return;
        auto hit = chips.hitTest(e.position);
        // Anywhere but on a chip adds a tag.
        if (hit.kind == TagChipRow::Hit::Kind::None)
            hit.kind = TagChipRow::Hit::Kind::Add;
        panel.tagClicked(*sound, hit);
    }

    juce::String getTooltip() override
    {
        return "The tags of the sound being edited. Click a tag to keep, replace or remove it, a dashed tag to keep "
               "the suggestion, or \"+ Tag\" to add one.";
    }

private:
    LibraryPanel& panel;
    TagChipRow chips;
};

LibraryPanel::LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), selectedSlot(std::move(slotChoice)), tagStrip(std::make_unique<TagStrip>(*this))
{
    refreshButton.setTooltip("Read the Library folder again");
    refreshButton.onClick = [this] { rescan(); };
    autoTagButton.setTooltip("Auto-tag: give every sound the tags suggested for it at once (you confirm first), or "
                             "undo that");
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
                    "Librarian tab, drag a Program onto a Bank location, or a Bank file onto a Bank. Click a tag to "
                    "change it, or the end of a selected row to add one. A dashed tag such as \"Bass?\" is a "
                    "suggested tag: click it to keep it, or right-click the sound to dismiss it. A darker tag was "
                    "given by Auto-tag.");

    saveSlotButton.setTooltip("Save the selected Slot's Program as a new .syx file in the Library");
    saveSlotButton.onClick = [this] { saveSlot(); };
    savePerformanceButton.setTooltip("Save the current Performance as a new .syx file in the Library");
    savePerformanceButton.onClick = [this] { savePerformance(); };
    saveEntryButton.setTooltip("Save the selected Library entry (for example a Percussion Kit from a Bank file) "
                               "as its own .syx file");
    saveEntryButton.onClick = [this] { saveSelectedEntry(); };
    saveEntryButton.setEnabled(false);

    tagFilter.setTooltip("Show only the sounds with one tag");
    tagFilter.onChange = [this] { filterChanged(); };
    favouritesFilter.setTooltip("Show only the sounds marked as favourites");
    favouritesFilter.onClick = [this] { filterChanged(); };

    infoTitle.setFont(NordFonts::small());
    infoTitle.setColour(juce::Label::textColourId, NordPalette::textDim);
    infoTitle.setMinimumHorizontalScale(0.8f);
    nameEditor.setFont(NordFonts::body());
    nameEditor.setTooltip("Your name for the sound being edited, kept in library.json in the Library folder. Press "
                          "Return to keep it; leave it empty to use the file or Bank name.");
    nameEditor.onReturnKey = [this] { commitName(); };
    nameEditor.onFocusLost = [this] { commitName(); };
    nameEditor.onEscapeKey = [this] { showSelectedInfo(); };
    favouriteButton.setClickingTogglesState(true);
    favouriteButton.setTooltip("Mark the sound being edited as a favourite");
    favouriteButton.onClick = [this] {
        const auto sound = editedSound();
        if (! sound)
            return;
        auto info = libraryMetadata.info(*sound);
        info.favourite = favouriteButton.getToggleState();
        storeInfo(*sound, info);
    };

    statusLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setFont(NordFonts::small());
    slotLabel.setFont(NordFonts::small());
    slotLabel.setTooltip("The Slot that clicking a Program or Percussion Kit auditions it on: the Slot tab last shown");

    for (auto* child : std::initializer_list<juce::Component*> { &refreshButton, &autoTagButton, &finderButton,
                                                                 &slotLabel, &tagFilter, &favouritesFilter, &tree,
                                                                 &infoTitle, &nameEditor, tagStrip.get(),
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
    tagFilter.setBounds(filterRow);
    area.removeFromTop(6);

    statusLabel.setBounds(area.removeFromBottom(52));
    area.removeFromBottom(4);
    saveEntryButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    savePerformanceButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(4);
    saveSlotButton.setBounds(area.removeFromBottom(26));
    area.removeFromBottom(10);
    auto infoRow = area.removeFromBottom(26);
    favouriteButton.setBounds(infoRow.removeFromRight(80).withSizeKeepingCentre(80, 24));
    infoRow.removeFromRight(6);
    tagStrip->setBounds(infoRow);
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

    // Another sound at the edited location (dropped there, or read again): show that one, unless
    // a name is being typed for the one before.
    if (editingLocation && ! nameEditor.hasKeyboardFocus(true) && editedSound() != shownSound)
        showSelectedInfo();

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
    refreshTagChoices();
    rebuildTree();

    if (libraryMetadata.status() == nl2x::LibraryMetadataStatus::Unreadable)
        setStatus("library.json in the Library folder could not be read (or was written by a newer NL2X Editor), so "
                  "your names, tags and favourites are not shown, and it is left untouched. Fix or remove it, then "
                  "press Refresh.");
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

void LibraryPanel::refreshTagChoices()
{
    tagChoices = libraryMetadata.tagChoices();
    const auto chosen = filter.tag;
    tagFilter.clear(juce::dontSendNotification);
    tagFilter.addItem("All tags", kAllTagsId);
    for (std::size_t i = 0; i < tagChoices.size(); ++i)
        tagFilter.addItem(utf8(tagChoices[i]), kFirstTagId + static_cast<int>(i));
    const auto found = std::find(tagChoices.begin(), tagChoices.end(), chosen);
    if (found == tagChoices.end())
        filter.tag.clear();
    tagFilter.setSelectedId(found == tagChoices.end() ? kAllTagsId
                                                      : kFirstTagId + static_cast<int>(found - tagChoices.begin()),
                            juce::dontSendNotification);
}

void LibraryPanel::filterChanged()
{
    commitName();
    const auto index = tagFilter.getSelectedId() - kFirstTagId;
    filter.tag = index >= 0 && index < static_cast<int>(tagChoices.size()) ? tagChoices[static_cast<std::size_t>(index)]
                                                                          : std::string();
    filter.favouritesOnly = favouritesFilter.getToggleState();
    setStatus({});
    rebuildTree();
}

void LibraryPanel::editLocation(nl2x::BankLocation newLocation)
{
    if (editingLocation && location == newLocation)
        return;
    commitName();
    // The Library's selection no longer says what is edited.
    if (! editingLocation)
        tree.clearSelectedItems();
    location = newLocation;
    editingLocation = true;
    showSelectedInfo();
}

std::optional<nl2x::BankLocation> LibraryPanel::editedLocation() const
{
    return editingLocation ? std::optional(location) : std::nullopt;
}

std::optional<nl2x::SoundFingerprint> LibraryPanel::editedSound() const
{
    if (editingLocation) {
        const auto program = processor.librarian().program(location);
        return program ? std::optional(nl2x::SoundFingerprint::of(*program)) : std::nullopt;
    }
    return selectedEntry != nullptr ? std::optional(nl2x::SoundFingerprint::of(*selectedEntry)) : std::nullopt;
}

juce::String LibraryPanel::editedTitle() const
{
    const auto dot = juce::String::fromUTF8(" \xC2\xB7 ");
    if (editingLocation)
        return "Synth Bank " + juce::String(location.bank) + dot + juce::String(location.program)
               + (processor.librarian().isPending(location) ? " (to send)" : "");
    return selectedEntry != nullptr ? "Library" + dot + utf8(selectedEntry->name) : juce::String();
}

std::string LibraryPanel::editedDefaultName() const
{
    if (! editingLocation)
        return selectedEntry != nullptr ? selectedEntry->name : std::string();
    const auto sound = editedSound();
    return sound ? libraryNameOf(*sound) : std::string();
}

std::string LibraryPanel::libraryNameOf(const nl2x::SoundFingerprint& fingerprint) const
{
    // A file of its own names a sound better than a numbered entry of a Bank file.
    for (const bool singles : { true, false })
        for (const auto& file : files)
            if (! file.isBackup && (file.entries.size() == 1) == singles)
                for (const auto& entry : file.entries)
                    if (nl2x::SoundFingerprint::of(entry) == fingerprint)
                        return entry.name;
    return {};
}

void LibraryPanel::showSelectedInfo()
{
    const auto sound = editedSound();
    shownSound = sound;
    const bool editable = sound && libraryMetadata.status() == nl2x::LibraryMetadataStatus::Ready;
    for (auto* control : std::initializer_list<juce::Component*> { &nameEditor, tagStrip.get(), &favouriteButton })
        control->setEnabled(editable);
    tagStrip->repaint();

    if (! sound) {
        infoTitle.setText(editingLocation ? editedTitle() + " has not been read from the synth yet."
                                          : "Select a sound here or in the Synth Banks to name or tag it.",
                          juce::dontSendNotification);
        nameEditor.setText({}, false);
        nameEditor.setTextToShowWhenEmpty({}, NordPalette::textDim);
        favouriteButton.setToggleState(false, juce::dontSendNotification);
        return;
    }

    const auto info = libraryMetadata.info(*sound);
    infoTitle.setText("Editing: " + editedTitle(), juce::dontSendNotification);
    nameEditor.setText(utf8(info.name), false);
    const auto defaultName = editedDefaultName();
    nameEditor.setTextToShowWhenEmpty(defaultName.empty() ? juce::String("No name") : utf8(defaultName),
                                      NordPalette::textDim);
    favouriteButton.setToggleState(info.favourite, juce::dontSendNotification);
}

std::vector<TagChip> LibraryPanel::chipsFor(const nl2x::SoundFingerprint& fingerprint) const
{
    std::vector<TagChip> chips;
    const auto info = libraryMetadata.info(fingerprint);
    for (const auto& tag : info.tags)
        chips.push_back({ tag, info.isAutoTag(tag) ? TagChip::Style::Auto : TagChip::Style::Own });
    for (const auto& suggestion : suggestionsFor(fingerprint))
        chips.push_back({ suggestion.tag, TagChip::Style::Suggested });
    // Recognised by similarity: the sound was changed since it was tagged (on the synth panel, say).
    if (const auto match = libraryMetadata.match(fingerprint); match && match->differences > 0)
        chips.push_back({ "changed", TagChip::Style::Remark });
    return chips;
}

void LibraryPanel::tagClicked(const nl2x::SoundFingerprint& fingerprint, const TagChipRow::Hit& hit)
{
    if (libraryMetadata.status() != nl2x::LibraryMetadataStatus::Ready) {
        setStatus("library.json in the Library folder could not be read, so tags cannot be changed. Fix or remove it, "
                  "then press Refresh.");
        return;
    }
    switch (hit.kind) {
    case TagChipRow::Hit::Kind::None:
        break;
    case TagChipRow::Hit::Kind::Tag:
        showTagMenu(fingerprint, hit.tag);
        break;
    case TagChipRow::Hit::Kind::Suggested:
        acceptSuggestion(fingerprint, hit.tag);
        break;
    case TagChipRow::Hit::Kind::More:
    case TagChipRow::Hit::Kind::Add:
        showAddTagMenu(fingerprint);
        break;
    }
}

void LibraryPanel::addTagChoices(juce::PopupMenu& menu, const nl2x::SoundFingerprint& fingerprint,
                                 std::function<void(LibraryPanel&, const std::string&)> pick)
{
    const auto info = libraryMetadata.info(fingerprint);
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    for (std::size_t i = 0; i < tagChoices.size(); ++i) {
        const auto& tag = tagChoices[i];
        if (i == nl2x::kLibraryTags.size() && menu.getNumItems() > 0)
            menu.addSeparator();
        if (info.hasTag(tag))
            continue;
        menu.addItem(utf8(tag), [safe, pick, tag] {
            if (safe != nullptr)
                pick(*safe, tag);
        });
    }
    menu.addSeparator();
    menu.addItem("New tag" + kEllipsis, [safe, pick] {
        if (safe != nullptr)
            safe->askForNewTag("New tag", pick);
    });
}

void LibraryPanel::showTagMenu(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    const auto info = libraryMetadata.info(fingerprint);
    if (! info.hasTag(tag))
        return;
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    juce::PopupMenu menu;
    menu.addSectionHeader(utf8(tag) + (info.isAutoTag(tag) ? "  (given by Auto-tag)" : ""));
    if (info.isAutoTag(tag))
        menu.addItem("Keep " + quoted(tag), [safe, fingerprint, tag] {
            if (safe != nullptr)
                safe->confirmTags(fingerprint, tag);
        });
    juce::PopupMenu replacements;
    addTagChoices(replacements, fingerprint, [fingerprint, tag](LibraryPanel& panel, const std::string& to) {
        panel.replaceTag(fingerprint, tag, to);
    });
    menu.addSubMenu("Replace with", replacements);
    menu.addItem("Remove " + quoted(tag), [safe, fingerprint, tag] {
        if (safe != nullptr)
            safe->removeTag(fingerprint, tag);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void LibraryPanel::showAddTagMenu(const nl2x::SoundFingerprint& fingerprint)
{
    const auto info = libraryMetadata.info(fingerprint);
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    juce::PopupMenu menu;
    // The sound's tags and suggestions, also those with no room on its row.
    if (! info.tags.empty()) {
        juce::PopupMenu own;
        for (const auto& tag : info.tags) {
            juce::PopupMenu each;
            if (info.isAutoTag(tag))
                each.addItem("Keep (given by Auto-tag)", [safe, fingerprint, tag] {
                    if (safe != nullptr)
                        safe->confirmTags(fingerprint, tag);
                });
            juce::PopupMenu replacements;
            addTagChoices(replacements, fingerprint, [fingerprint, tag](LibraryPanel& panel, const std::string& to) {
                panel.replaceTag(fingerprint, tag, to);
            });
            each.addSubMenu("Replace with", replacements);
            each.addItem("Remove", [safe, fingerprint, tag] {
                if (safe != nullptr)
                    safe->removeTag(fingerprint, tag);
            });
            own.addSubMenu(utf8(tag) + (info.isAutoTag(tag) ? "  (Auto-tag)" : ""), each);
        }
        menu.addSubMenu("Tags of this sound", own);
    }
    const auto suggested = suggestionsFor(fingerprint);
    if (! suggested.empty()) {
        menu.addSectionHeader("Suggested");
        for (const auto& suggestion : suggested)
            menu.addItem(utf8(suggestion.tag) + "?", [safe, fingerprint, tag = suggestion.tag] {
                if (safe != nullptr)
                    safe->acceptSuggestion(fingerprint, tag);
            });
    }
    menu.addSectionHeader("Add a tag");
    addTagChoices(menu, fingerprint, [fingerprint](LibraryPanel& panel, const std::string& tag) {
        panel.addTag(fingerprint, tag);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void LibraryPanel::askForNewTag(const juce::String& title, std::function<void(LibraryPanel&, const std::string&)> pick)
{
    auto* window = new juce::AlertWindow(title, "A tag of your own, such as a style, a song or a set:",
                                         juce::MessageBoxIconType::NoIcon, isShowing() ? this : nullptr);
    window->addTextEditor("tag", {}, {});
    window->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto safe = juce::Component::SafePointer<LibraryPanel>(this);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe, window, onChosen = std::move(pick)](int result) {
                                // Called before the window is deleted.
                                if (result != 1 || safe == nullptr)
                                    return;
                                const auto typed = nl2x::normalisedTag(
                                    window->getTextEditorContents("tag").toStdString());
                                if (! typed.empty())
                                    onChosen(*safe, safe->choiceSpelling(typed));
                            }),
                            true);
}

std::string LibraryPanel::choiceSpelling(const std::string& typed) const
{
    const auto wanted = utf8(typed);
    for (const auto& choice : tagChoices)
        if (utf8(choice).equalsIgnoreCase(wanted))
            return choice;
    return typed;
}

void LibraryPanel::addTag(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    auto info = libraryMetadata.info(fingerprint);
    if (! info.hasTag(tag)) {
        info.addTag(tag);
    } else {
        // Picking a tag it has (from Auto-tag) keeps it.
        info.confirmTag(tag);
    }
    storeInfo(fingerprint, info);
}

void LibraryPanel::replaceTag(const nl2x::SoundFingerprint& fingerprint, const std::string& from, const std::string& to)
{
    auto info = libraryMetadata.info(fingerprint);
    if (info.replaceTag(from, to))
        storeInfo(fingerprint, info);
}

void LibraryPanel::removeTag(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    auto info = libraryMetadata.info(fingerprint);
    if (info.removeTag(tag))
        storeInfo(fingerprint, info);
}

void LibraryPanel::confirmTags(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    auto info = libraryMetadata.info(fingerprint);
    if (info.autoTags.empty())
        return;
    if (tag.empty())
        info.autoTags.clear();
    else
        info.confirmTag(tag);
    storeInfo(fingerprint, info);
}

void LibraryPanel::removeAutoAssigned(const nl2x::SoundFingerprint& fingerprint)
{
    auto info = libraryMetadata.info(fingerprint);
    if (info.autoTags.empty())
        return;
    for (const auto& tag : std::vector<std::string>(info.autoTags))
        info.removeTag(tag);
    storeInfo(fingerprint, info);
}

std::vector<nl2x::TagSuggestion> LibraryPanel::suggestionsFor(const nl2x::SoundFingerprint& fingerprint) const
{
    const auto key = fingerprintKey(fingerprint);
    // Dismissed by an editor that suggested one category per sound: none for this sound.
    if (dismissedSuggestions.contains(key))
        return {};
    const std::vector<nl2x::TagSuggestion>* found = nullptr;
    if (const auto known = suggestions.find(key); known != suggestions.end()) {
        found = &known->second;
    } else if (fingerprint.kind == nl2x::LibraryEntryKind::Program) {
        auto other = otherSuggestions.find(key);
        if (other == otherSuggestions.end()) {
            std::vector<std::int8_t> values(fingerprint.values.size());
            std::transform(fingerprint.values.begin(), fingerprint.values.end(), values.begin(),
                           [](std::uint8_t value) { return static_cast<std::int8_t>(value); });
            const auto program = nl2x::Program::fromValues(values);
            other = otherSuggestions.emplace(key, program ? nl2x::suggestTags(*program, learned)
                                                          : std::vector<nl2x::TagSuggestion> {})
                        .first;
        }
        found = &other->second;
    }
    if (found == nullptr || found->empty())
        return {};
    const auto info = libraryMetadata.info(fingerprint);
    std::vector<nl2x::TagSuggestion> shown;
    for (const auto& suggestion : *found)
        if (! info.hasTag(suggestion.tag) && ! dismissedSuggestions.contains(dismissalKey(fingerprint, suggestion.tag)))
            shown.push_back(suggestion);
    return shown;
}

void LibraryPanel::acceptSuggestion(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    const auto shown = suggestionsFor(fingerprint);
    if (std::none_of(shown.begin(), shown.end(), [&](const auto& suggestion) { return suggestion.tag == tag; }))
        return;
    auto info = libraryMetadata.info(fingerprint);
    info.addTag(tag);
    storeInfo(fingerprint, info);
}

void LibraryPanel::dismissSuggestion(const nl2x::SoundFingerprint& fingerprint, const std::string& tag)
{
    dismissedSuggestions.insert(dismissalKey(fingerprint, tag));
    juce::StringArray keys;
    for (const auto& key : dismissedSuggestions)
        keys.add(utf8(key));
    auto& settings = processor.midiPorts().userSettings();
    settings.setValue(kDismissedSuggestionsKey, keys.joinIntoString("\n"));
    settings.saveIfNeeded();
    ++metadataChanges;
    showSelectedInfo();
    tree.repaint();
}

void LibraryPanel::findSuggestions()
{
    learned = nl2x::LearnedTags::of(libraryMetadata);
    suggestions.clear();
    otherSuggestions.clear();
    for (const auto& file : files) {
        if (file.isBackup)
            continue;
        for (const auto& entry : file.entries) {
            auto found = nl2x::suggestTags(file, entry, learned);
            if (found.empty())
                continue;
            // The same sound in its own named file and in a Bank file: the name is more trusted.
            const auto [known, added] = suggestions.emplace(fingerprintKey(nl2x::SoundFingerprint::of(entry)), found);
            if (! added && found.front().source < known->second.front().source)
                known->second = std::move(found);
        }
    }
}

void LibraryPanel::commitName()
{
    const auto sound = editedSound();
    if (! sound || ! nameEditor.isEnabled() || shownSound != sound)
        return;
    auto info = libraryMetadata.info(*sound);
    auto name = nameEditor.getText().trim().toStdString();
    // Typing the name the Library already gives the sound is the same as no name of one's own.
    if (name == editedDefaultName())
        name.clear();
    if (name == info.name)
        return;
    info.name = name;
    storeInfo(*sound, info);
}

void LibraryPanel::storeInfo(const nl2x::SoundFingerprint& fingerprint, const nl2x::SoundInfo& info)
{
    ++metadataChanges;
    const auto before = libraryMetadata.info(fingerprint);
    const auto result = libraryMetadata.set(fingerprint, info);
    // A tag set or removed by hand teaches the suggestions something new.
    const auto after = libraryMetadata.info(fingerprint);
    if (before.ownTags() != after.ownTags())
        findSuggestions();
    // A tag of the user's own appears among the choices.
    if (std::any_of(after.tags.begin(), after.tags.end(), [this](const auto& tag) {
            return std::find(tagChoices.begin(), tagChoices.end(), tag) == tagChoices.end();
        }))
        refreshTagChoices();
    showSaveResult(result);
    // The sound stays in the list even if it no longer matches the filter, so that it does not
    // vanish under the pointer; changing the filter or Refresh applies it again.
    showSelectedInfo();
    tree.repaint();
}

void LibraryPanel::showSaveResult(nl2x::MetadataSaveResult result)
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
    const auto suggested = suggestionsFor(fingerprint);
    const auto info = libraryMetadata.info(fingerprint);
    if (entry.kind() != nl2x::LibraryEntryKind::Program && suggested.empty() && info.autoTags.empty())
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
        menu.addItem("Store to Bank/Program" + kEllipsis, static_cast<bool>(onStoreEntry), false, [safe, description] {
            if (safe != nullptr && safe->onStoreEntry)
                if (const auto* found = safe->draggedEntry(description))
                    safe->onStoreEntry(*found);
        });
    }
    if (! suggested.empty()) {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        for (const auto& suggestion : suggested)
            menu.addItem("Keep suggested tag " + quoted(suggestion.tag), [safe, fingerprint, tag = suggestion.tag] {
                if (safe != nullptr)
                    safe->acceptSuggestion(fingerprint, tag);
            });
        for (const auto& suggestion : suggested)
            menu.addItem("Dismiss suggested tag " + quoted(suggestion.tag), [safe, fingerprint, tag = suggestion.tag] {
                if (safe != nullptr)
                    safe->dismissSuggestion(fingerprint, tag);
            });
    }
    if (! info.autoTags.empty()) {
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addItem("Keep the tags given by Auto-tag", [safe, fingerprint] {
            if (safe != nullptr)
                safe->confirmTags(fingerprint);
        });
        menu.addItem("Remove the tags given by Auto-tag", [safe, fingerprint] {
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
    juce::PopupMenu menu;
    menu.addItem(plan.empty() ? juce::String("Auto-tag: no suggestion to keep")
                              : "Auto-tag " + countText(plan.size(), "sound", "sounds") + kEllipsis,
                 ! plan.empty(), false, [safe, plan] {
                     if (safe != nullptr)
                         safe->autoTag(plan);
                 });
    menu.addItem(assigned == 0 ? juce::String("Undo auto-tag")
                               : "Undo auto-tag (" + countText(static_cast<std::size_t>(assigned), "sound", "sounds") + ")" + kEllipsis,
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
            // Tags the sound has and dismissed suggestions are left out.
            std::vector<std::string> tags;
            for (const auto& suggestion : suggestionsFor(fingerprint))
                tags.push_back(suggestion.tag);
            if (! tags.empty())
                plan.push_back({ std::move(fingerprint), std::move(tags) });
        }
    }
    return plan;
}

void LibraryPanel::autoTag(const std::vector<nl2x::LibraryMetadata::AutoAssignment>& plan)
{
    // "Bass 312, Keys 240, Percussion 89": the most first.
    std::map<std::string, int> counts;
    for (const auto& assignment : plan)
        for (const auto& tag : assignment.tags)
            ++counts[tag];
    std::vector<std::pair<std::string, int>> byCount(counts.begin(), counts.end());
    std::stable_sort(byCount.begin(), byCount.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    juce::StringArray parts;
    for (const auto& [tag, count] : byCount)
        parts.add(utf8(tag) + " " + juce::String(count));

    const auto message = "Give " + countText(plan.size(), "sound", "sounds") + " the tags suggested for them:\n\n"
                         + parts.joinIntoString(", ")
                         + ".\n\nThe tags are added to those the sounds have, in library.json, marked as given by "
                           "Auto-tag (a darker tag). Keeping or replacing one by hand makes it yours; Undo auto-tag "
                           "removes the others. Dismissed suggestions are left out.";
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
        const auto saved = safe->libraryMetadata.autoAssign(plan);
        safe->metadataReplaced(saved);
        if (saved == nl2x::MetadataSaveResult::Saved)
            safe->setStatus("Auto-tag gave tags to " + countText(plan.size(), "sound", "sounds")
                            + ". Undo auto-tag, in the same menu, removes those you do not keep.");
    });
}

void LibraryPanel::undoAutoTag()
{
    const auto count = libraryMetadata.autoAssignedCount();
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Undo auto-tag")
                             .withMessage("Remove the tags Auto-tag gave " + countText(static_cast<std::size_t>(count), "sound", "sounds")
                                          + "? Tags you set or kept by hand stay.")
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
            safe->setStatus("Removed the tags Auto-tag gave " + countText(static_cast<std::size_t>(before), "sound", "sounds") + ".");
    });
}

void LibraryPanel::metadataReplaced(nl2x::MetadataSaveResult result)
{
    showSaveResult(result);
    ++metadataChanges;
    findSuggestions();
    refreshTagChoices();
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
    const bool wasEditingLocation = editingLocation;
    // A name being typed belongs to the sound it was typed for.
    if (entry != nullptr && (entry != previous || editingLocation))
        commitName();
    if (entry != nullptr) {
        selectedEntry = entry;
        // The edit area follows the sound selected last.
        editingLocation = false;
    } else if (tree.getNumSelectedItems() == 0) {
        selectedEntry = nullptr;
    }
    saveEntryButton.setEnabled(selectedEntry != nullptr);
    if (selectedEntry != previous || editingLocation != wasEditingLocation)
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
