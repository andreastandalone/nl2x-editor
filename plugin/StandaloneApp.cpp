// The standalone app, in place of JUCE's StandaloneFilterApp (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP,
// set in plugin/CMakeLists.txt for the Standalone target only). JUCE's window draws its own title
// bar with an "Options" button for the audio/MIDI device dialog; this window uses the native macOS
// title bar instead. The dialog is not needed: the editor makes no sound and chooses its MIDI
// ports in its own Settings panel. JUCE's StandalonePluginHolder still creates the processor and
// keeps its state in the same settings file as before, so existing users keep their state.

#include "NordLookAndFeel.h"

// The modules juce_StandaloneFilterWindow.h needs, included first as JUCE's own wrapper does.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace {

// The keys JUCE's own window used, so the window opens where it was left before this change.
constexpr auto kWindowXKey = "windowX";
constexpr auto kWindowYKey = "windowY";

class StandaloneWindow final : public juce::DocumentWindow {
public:
    StandaloneWindow(const juce::String& title, juce::StandalonePluginHolder& holderToUse)
        : DocumentWindow(title, NordPalette::background,
                         juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
        , holder(holderToUse)
        , editor(holder.processor->createEditorAndMakeActive())
    {
        setUsingNativeTitleBar(true);
        // The window fits the editor exactly and resizes under the editor's own limits and aspect
        // ratio (PluginEditor.h), from the window's edges as well as the editor's corner.
        setContentNonOwned(editor.get(), true);
        setResizable(true, false);
        setConstrainer(editor->getConstrainer());
        placeOnScreen();
    }

    ~StandaloneWindow() override
    {
        if (auto* props = holder.settings.get()) {
            props->setValue(kWindowXKey, getX());
            props->setValue(kWindowYKey, getY());
        }
        clearContentComponent();
        holder.processor->editorBeingDeleted(editor.get());
    }

    void closeButtonPressed() override
    {
        holder.savePluginState();
        juce::JUCEApplicationBase::quit();
    }

private:
    // Back where it was left, kept inside the screen it is on; centred the first time.
    void placeOnScreen()
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (auto* props = holder.settings.get(); props != nullptr && props->containsKey(kWindowXKey)
                                                 && props->containsKey(kWindowYKey)) {
            const juce::Rectangle<int> saved { props->getIntValue(kWindowXKey), props->getIntValue(kWindowYKey),
                                               getWidth(), getHeight() };
            if (const auto* display = displays.getDisplayForRect(saved)) {
                setBounds(saved.constrainedWithin(display->userBounds.toNearestInt()));
                return;
            }
        }
        centreWithSize(getWidth(), getHeight());
    }

    juce::StandalonePluginHolder& holder;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

class StandaloneApp final : public juce::JUCEApplication {
public:
    StandaloneApp()
    {
        // The same settings file as JUCE's StandaloneFilterApp.
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "";
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override
    {
        holder = std::make_unique<juce::StandalonePluginHolder>(appProperties.getUserSettings(), false);
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            return;
        window = std::make_unique<StandaloneWindow>(getApplicationName(), *holder);
        window->setVisible(true);
    }

    void shutdown() override
    {
        // As JUCE's own window does: the audio stops before the editor goes.
        if (holder != nullptr)
            holder->stopPlaying();
        window = nullptr;
        holder = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (holder != nullptr)
            holder->savePluginState();

        // A modal dialog (a file chooser, an alert) must close before the app quits.
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
            return;
        }
        quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<StandaloneWindow> window;
};

} // namespace

JUCE_CREATE_APPLICATION_DEFINE(StandaloneApp)
