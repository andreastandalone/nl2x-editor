#include "SynthControl.h"

#include <memory>
#include <mutex>

namespace {

struct ProcessWideControl {
    std::mutex mutex;
    const SynthControl* owner = nullptr;
    // Held while `owner` is set.
    std::unique_ptr<juce::InterProcessLock> lock;
};

ProcessWideControl& processWide()
{
    static ProcessWideControl control;
    return control;
}

constexpr const char* kLockName = "io.github.andreastandalone.nl2x-editor.synth-control";

} // namespace

SynthControl::~SynthControl()
{
    release();
}

bool SynthControl::tryAcquire()
{
    auto& control = processWide();
    const std::scoped_lock guard(control.mutex);
    if (control.owner == this)
        return true;
    if (control.owner != nullptr) {
        lastRefusal = Refusal::AnotherInstanceHere;
        return false;
    }

    if (control.lock == nullptr)
        control.lock = std::make_unique<juce::InterProcessLock>(kLockName);
    if (! control.lock->enter(0)) {
        lastRefusal = Refusal::AnotherApplication;
        return false;
    }

    control.owner = this;
    lastRefusal = Refusal::None;
    return true;
}

void SynthControl::release()
{
    auto& control = processWide();
    const std::scoped_lock guard(control.mutex);
    if (control.owner != this)
        return;
    control.owner = nullptr;
    control.lock->exit();
}

bool SynthControl::holds() const
{
    auto& control = processWide();
    const std::scoped_lock guard(control.mutex);
    return control.owner == this;
}
