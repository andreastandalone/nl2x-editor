// nl2x_render_editor: paints the plugin's editor into a PNG, offscreen (README, "Building").
//
// It builds the real processor and editor on a SynthLink to the simulated Nord Lead 2X
// (tests/sim), so the editor shows Slots and a Performance read the way the plugin reads them.
// Nothing touches the user's settings, MIDI ports or Library: everything goes to --work.
//
//   nl2x_render_editor --out <file.png> [--view A|B|C|D|performance|librarian] [--morph]
//                      [--settings] [--about] [--check] [--kit A|B|C|D] [--offline] [--edge-cases]
//                      [--open-library] [--favourites] [--width <px>] [--scale <n>] [--work <dir>]
//
// --kit makes that Slot play a Percussion Kit; --offline lets the synth answer nothing;
// --edge-cases puts Slots A and B on one channel, and values outside the documented ranges and
// Morph Amounts past the ends of the ranges into Slot A. --width resizes the editor window as
// dragging its corner does: within its limits, the aspect ratio kept. --open-library opens every
// group (subfolder) of the Library; --favourites turns on its "Favourites only" filter.
// --view librarian shows the Librarian tab once the simulated synth's Banks are read: Bank 1 holds
// the Slots' sounds at Programs 1–4 (so the Library's names and tags show), and a few locations
// are changed ("to send") by putting Library Programs there and copying one location to another.
// --bank <0–3> shows another Bank.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "SimulatedNordLead2X.h"

#include "nl2x/CcParameters.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/MorphAmounts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <CoreFoundation/CoreFoundation.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace {

using Sim = nl2x::test::SimulatedNordLead2X;

struct Options {
    juce::File out;
    juce::String view = "A";
    bool morph = false;
    bool settings = false;
    bool about = false;
    bool check = false;
    bool offline = false;
    bool edgeCases = false;
    bool openLibrary = false;
    bool favourites = false;
    std::optional<nl2x::Slot> kit;
    float scale = 1.0f;
    int width = 0;
    int bank = 1;
    juce::File work;
};

std::optional<nl2x::Slot> slotOf(const juce::String& letter)
{
    for (auto slot : nl2x::kAllSlots)
        if (letter.equalsIgnoreCase(juce::String::charToString(nl2x::slotLetter(slot))))
            return slot;
    return std::nullopt;
}

std::optional<Options> parse(const juce::StringArray& args)
{
    Options options;
    options.work = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("nl2x-render-editor");
    for (int i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto next = [&]() -> juce::String { return i + 1 < args.size() ? args[++i] : juce::String(); };
        if (arg == "--out")
            options.out = juce::File::getCurrentWorkingDirectory().getChildFile(next());
        else if (arg == "--view")
            options.view = next();
        else if (arg == "--morph")
            options.morph = true;
        else if (arg == "--settings")
            options.settings = true;
        else if (arg == "--about")
            options.about = true;
        else if (arg == "--check")
            options.check = true;
        else if (arg == "--offline")
            options.offline = true;
        else if (arg == "--edge-cases")
            options.edgeCases = true;
        else if (arg == "--open-library")
            options.openLibrary = true;
        else if (arg == "--favourites")
            options.favourites = true;
        else if (arg == "--kit") {
            options.kit = slotOf(next());
            if (! options.kit)
                return std::nullopt;
        } else if (arg == "--scale")
            options.scale = juce::jlimit(1.0f, 4.0f, next().getFloatValue());
        else if (arg == "--bank")
            options.bank = juce::jlimit(0, 3, next().getIntValue());
        else if (arg == "--width")
            options.width = next().getIntValue();
        else if (arg == "--work")
            options.work = juce::File::getCurrentWorkingDirectory().getChildFile(next());
        else
            return std::nullopt;
    }
    if (options.out == juce::File())
        return std::nullopt;
    return options;
}

// A Program that looks like a sound: every CC parameter inside its range, some Morph Amounts set.
Sim::Program testProgram(int seed)
{
    nl2x::Program program;
    int i = 0;
    for (const auto& info : nl2x::ccParameters()) {
        const int span = info.maxValue - info.minValue + 1;
        nl2x::setCcParameterValue(program, info.id, info.minValue + (i * 37 + seed * 23 + 11) % span);
        ++i;
    }
    i = 0;
    for (auto parameter : nl2x::morphableParameters()) {
        if ((i + seed) % 3 == 0)
            program.setValue(*nl2x::morphAmountOf(parameter), ((i * 29 + seed * 13) % 120) - 60);
        ++i;
    }
    return program.bytes();
}

// Runs the message loop (timers, async messages) for `ms` milliseconds, or until `done`.
void pump(int ms, const std::function<bool()>& done = {})
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (juce::Time::getMillisecondCounter() < end && ! (done && done()))
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
}

juce::Component* findById(juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

void click(juce::Component& editor, const juce::String& id)
{
    if (auto* button = dynamic_cast<juce::Button*>(findById(editor, id)))
        button->triggerClick();
    else
        std::cerr << "No button " << id << "\n";
}

// Opens the Library's groups (the items LibraryPanel names "Folder …"), nested ones included.
void openGroups(juce::TreeViewItem& item)
{
    for (int i = 0; i < item.getNumSubItems(); ++i)
        if (auto* sub = item.getSubItem(i); sub->getUniqueName().startsWith("Folder ")) {
            sub->setOpen(true);
            openGroups(*sub);
        }
}

// The cable from the processor to the simulated synth, and back on the next timer tick.
class SimulatedLink final : public NL2XEditorProcessor::SynthLink, public nl2x::MidiOutput {
public:
    SimulatedLink() { synth.connectMidiOut(this); }

    void toSynth(std::span<const std::uint8_t> message) override
    {
        if (! offline)
            synth.receive(message);
    }
    void send(std::span<const std::uint8_t> message) override
    {
        if (processor != nullptr)
            processor->fromSynth(message);
        else
            early.emplace_back(message.begin(), message.end());
    }

    // The processor connects while it is being built: replies until then wait here.
    void plugInto(NL2XEditorProcessor& owner)
    {
        processor = &owner;
        for (const auto& message : early)
            processor->fromSynth(message);
        early.clear();
    }

    Sim synth;
    NL2XEditorProcessor* processor = nullptr;
    bool offline = false;
    std::vector<std::vector<std::uint8_t>> early;
};

int render(const Options& options)
{
    options.work.createDirectory();
    const auto settingsFile = options.work.getChildFile("NL2X Editor.settings");
    const auto library = options.work.getChildFile("Library");
    {
        // Fresh settings: the Connection check counts as shown (--check opens it) and the Library
        // is the work folder's.
        juce::PropertiesFile settings(settingsFile, {});
        settings.clear();
        settings.setValue("connectionCheckShown", ! options.check);
        settings.setValue("libraryFolder", library.getFullPathName());
        // Found earlier on the synth's factory Global Channel, so connecting asks that one first.
        settings.setValue("globalChannel", static_cast<int>(nl2x::hardware::kFactoryGlobalChannel));
        settings.saveIfNeeded();
    }

    SimulatedLink link;
    link.offline = options.offline;
    for (auto slot : nl2x::kAllSlots)
        link.synth.setEditBuffer(slot, testProgram(static_cast<int>(nl2x::slotIndex(slot)) + 1));
    if (options.kit)
        link.synth.playPercussionKit(*options.kit, {});
    // Bank 1 holds the Slots' sounds at Programs 1–4, and Program 10 an edited Slot B sound, so
    // that the Librarian shows names, tags and "changed".
    for (auto slot : nl2x::kAllSlots)
        link.synth.setBankProgram(1, static_cast<int>(nl2x::slotIndex(slot)),
                                  testProgram(static_cast<int>(nl2x::slotIndex(slot)) + 1));
    {
        nl2x::Program edited(testProgram(2));
        edited.setValue(nl2x::ProgramParameter::FilterCutoff, edited.value(nl2x::ProgramParameter::FilterCutoff) / 2);
        link.synth.setBankProgram(1, 9, edited.bytes());
    }
    if (options.edgeCases) {
        // Slots A and B on one channel (a Slot Channel warning), and values outside the documented
        // ranges in Slot A, as a dump from the synth may hold them.
        link.synth.setSlotChannel(nl2x::Slot::B, nl2x::MidiChannel { 0 });
        nl2x::Program program(link.synth.editBuffer(nl2x::Slot::A));
        program.setValue(nl2x::ProgramParameter::Osc2Semitones, 125);
        program.setValue(nl2x::ProgramParameter::FilterType, 7);
        // Morph Amounts that go past the end of the range, and one at the edge pointing out of it.
        program.setValue(nl2x::ProgramParameter::AmpEnvAttack, 0);
        program.setValue(*nl2x::morphAmountOf(nl2x::ProgramParameter::AmpEnvAttack), -40);
        program.setValue(nl2x::ProgramParameter::FilterCutoff, 100);
        program.setValue(*nl2x::morphAmountOf(nl2x::ProgramParameter::FilterCutoff), 60);
        program.setValue(nl2x::ProgramParameter::FilterResonance, 30);
        program.setValue(*nl2x::morphAmountOf(nl2x::ProgramParameter::FilterResonance), -90);
        link.synth.setEditBuffer(nl2x::Slot::A, program.bytes());
    }

    NL2XEditorProcessor processor(link, settingsFile);
    link.plugInto(processor);
    const auto& synth = processor.synth();
    pump(options.offline ? 300 : 15000, [&] {
        return synth.connectionStatus() == nl2x::ConnectionStatus::Connected
               && synth.performanceStatus() == nl2x::PerformanceStatus::Read;
    });

    // A Library with the Slots' Programs and the Performance, as "Save … as .syx" writes them.
    // Slot A's sound and the Performance at the top, the rest in subfolders (shown as groups): "My
    // sounds" with a nested "Live", and "Factory" with two Bank files made of the same Programs (our
    // own, never Clavia's). A Bank backup is listed under the Backups heading, two sounds carry
    // the user's own information, and Slot C's sound is also saved as "Glass Bells", whose name
    // suggests a category.
    library.deleteRecursively();
    const auto save = [](const juce::File& file, const std::vector<std::uint8_t>& bytes) {
        nl2x::saveLibraryFile(file.getFullPathName().toStdString(), bytes);
    };
    const auto mySounds = library.getChildFile("My sounds");
    std::vector<std::uint8_t> bank;
    for (auto slot : nl2x::kAllSlots)
        if (const auto file = nl2x::slotProgramFile(synth, slot)) {
            const auto name = juce::String("Slot ") + nl2x::slotLetter(slot) + " sound.syx";
            save(slot == nl2x::Slot::A ? library.getChildFile(name) : mySounds.getChildFile(name), *file);
            bank.insert(bank.end(), file->begin(), file->end());
            // A sound with no category whose file name says one: the Library suggests it.
            if (slot == nl2x::Slot::C)
                save(library.getChildFile("Glass Bells.syx"), *file);
        }
    if (const auto file = nl2x::performanceFile(synth)) {
        save(library.getChildFile("Test Performance.syx"), *file);
        save(mySounds.getChildFile("Live").getChildFile("Encore.syx"), *file);
    }
    save(library.getChildFile("Factory").getChildFile("bank1.syx"), bank);
    save(library.getChildFile("Factory").getChildFile("bank2.syx"), bank);
    save(library.getChildFile("Backups").getChildFile("Bank 3 backup.syx"), bank);
    {
        const auto libraryPath = std::filesystem::path(library.getFullPathName().toStdString());
        auto metadata = nl2x::LibraryMetadata::load(libraryPath);
        for (const auto& file : nl2x::scanLibrary(libraryPath)) {
            if (file.name == "Slot A sound")
                metadata.set(file.entries.front(), { "Warm lead", "Lead", true });
            else if (file.name == "Slot B sound")
                metadata.set(file.entries.front(), { "Fat bass", "Bass", false });
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    editor->setVisible(true);
    if (auto* constrainer = editor->getConstrainer(); options.width > 0 && constrainer != nullptr)
        constrainer->setBoundsForComponent(editor.get(), editor->getBounds().withWidth(options.width), false, false,
                                           false, true);

    int tab = -1;
    if (const auto slot = slotOf(options.view))
        tab = static_cast<int>(nl2x::slotIndex(*slot));
    else if (options.view.equalsIgnoreCase("performance"))
        tab = 4;
    else if (options.view.equalsIgnoreCase("librarian") || options.view.equalsIgnoreCase("memory"))
        tab = 5;
    if (auto* tabs = dynamic_cast<juce::TabbedButtonBar*>(findById(*editor, "viewTabs")); tabs != nullptr && tab >= 0)
        tabs->setCurrentTabIndex(tab);
    if (tab == 5) {
        // The tab reads the synth's Banks when first shown; then a few changes to send.
        auto& librarian = processor.librarian();
        pump(30000, [&] { return librarian.synthBanksStatus().stage != nl2x::SynthBanksStage::Reading
                                 && librarian.synthBanksStatus().stage != nl2x::SynthBanksStage::NotRead; });
        const auto files = nl2x::scanLibrary(library.getFullPathName().toStdString());
        const auto put = [&](const std::string& fileName, nl2x::BankLocation location) {
            for (const auto& file : files)
                if (file.name == fileName && ! file.entries.empty())
                    librarian.put(location, file.entries.front());
        };
        put("Slot B sound", { 1, 12 });
        put("Slot A sound", { 1, 23 });
        put("Slot D sound", { 1, 41 });
        librarian.copy({ 1, 3 }, { 1, 60 });
        put("Slot C sound", { 2, 5 });
        click(*editor, "bankTab" + juce::String(options.bank));
    }
    if (options.morph)
        click(*editor, "morph");
    if (options.settings)
        click(*editor, "settings");
    if (options.about)
        click(*editor, "about");
    if (options.check)
        processor.openConnectionCheck();
    if (options.favourites)
        click(*editor, "favouritesOnly");
    if (auto* tree = dynamic_cast<juce::TreeView*>(findById(*editor, "libraryTree")); tree != nullptr && options.openLibrary)
        openGroups(*tree->getRootItem());
    pump(600);

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, options.scale);
    options.out.getParentDirectory().createDirectory();
    options.out.deleteFile();
    juce::FileOutputStream stream(options.out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream(image, stream)) {
        std::cerr << "Cannot write " << options.out.getFullPathName() << "\n";
        return 1;
    }
    std::cout << options.out.getFullPathName() << " (" << editor->getWidth() << " x " << editor->getHeight()
              << ")\n";
    processor.editorBeingDeleted(editor.get());
    editor.reset();
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));
    const auto options = parse(args);
    if (! options) {
        std::cerr << "Usage: nl2x_render_editor --out <file.png> [--view A|B|C|D|performance|librarian] [--morph]\n"
                     "                          [--settings] [--about] [--check] [--kit A|B|C|D] [--offline]\n"
                     "                          [--edge-cases] [--open-library] [--favourites] [--width <px>]\n"
                     "                          [--scale <n>] [--bank <0-3>] [--work <dir>]\n";
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    return render(*options);
}
