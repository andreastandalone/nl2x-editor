// nl2x_render_editor: paints the plugin's editor into a PNG, offscreen (README, "Building").
//
// It builds the real processor and editor on a SynthLink to the simulated Nord Lead 2X
// (tests/sim), so the editor shows Slots and a Performance read the way the plugin reads them.
// Nothing touches the user's settings, MIDI ports or Library: everything goes to --work.
//
//   nl2x_render_editor --out <file.png> [--view A|B|C|D|performance|memory] [--morph]
//                      [--settings] [--check] [--kit A|B|C|D] [--offline] [--edge-cases]
//                      [--scale <n>] [--work <dir>]
//
// --kit makes that Slot play a Percussion Kit; --offline lets the synth answer nothing;
// --edge-cases puts Slots A and B on one channel and values outside the documented ranges into
// Slot A.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "SimulatedNordLead2X.h"

#include "nl2x/CcParameters.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/MorphAmounts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <CoreFoundation/CoreFoundation.h>

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
    bool check = false;
    bool offline = false;
    bool edgeCases = false;
    std::optional<nl2x::Slot> kit;
    float scale = 1.0f;
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
        else if (arg == "--check")
            options.check = true;
        else if (arg == "--offline")
            options.offline = true;
        else if (arg == "--edge-cases")
            options.edgeCases = true;
        else if (arg == "--kit") {
            options.kit = slotOf(next());
            if (! options.kit)
                return std::nullopt;
        } else if (arg == "--scale")
            options.scale = juce::jlimit(1.0f, 4.0f, next().getFloatValue());
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
    if (options.edgeCases) {
        // Slots A and B on one channel (a Slot Channel warning), and values outside the documented
        // ranges in Slot A, as a dump from the synth may hold them.
        link.synth.setSlotChannel(nl2x::Slot::B, nl2x::MidiChannel { 0 });
        nl2x::Program program(link.synth.editBuffer(nl2x::Slot::A));
        program.setValue(nl2x::ProgramParameter::Osc2Semitones, 125);
        program.setValue(nl2x::ProgramParameter::FilterType, 7);
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
    library.deleteRecursively();
    for (auto slot : nl2x::kAllSlots)
        if (const auto file = nl2x::slotProgramFile(synth, slot))
            nl2x::saveLibraryFile(library.getChildFile(juce::String("Slot ") + nl2x::slotLetter(slot) + " sound.syx")
                                      .getFullPathName()
                                      .toStdString(),
                                  *file);
    if (const auto file = nl2x::performanceFile(synth))
        nl2x::saveLibraryFile(library.getChildFile("Test Performance.syx").getFullPathName().toStdString(), *file);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    editor->setVisible(true);

    int tab = -1;
    if (const auto slot = slotOf(options.view))
        tab = static_cast<int>(nl2x::slotIndex(*slot));
    else if (options.view.equalsIgnoreCase("performance"))
        tab = 4;
    else if (options.view.equalsIgnoreCase("memory"))
        tab = 5;
    if (auto* tabs = dynamic_cast<juce::TabbedButtonBar*>(findById(*editor, "viewTabs")); tabs != nullptr && tab >= 0)
        tabs->setCurrentTabIndex(tab);
    if (options.morph)
        click(*editor, "morph");
    if (options.settings)
        click(*editor, "settings");
    if (options.check)
        processor.openConnectionCheck();
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
        std::cerr << "Usage: nl2x_render_editor --out <file.png> [--view A|B|C|D|performance|memory] [--morph]\n"
                     "                          [--settings] [--check] [--kit A|B|C|D] [--offline]\n"
                     "                          [--edge-cases] [--scale <n>] [--work <dir>]\n";
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    return render(*options);
}
