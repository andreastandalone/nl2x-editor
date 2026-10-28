#pragma once

#include <juce_core/juce_core.h>

// Only one plugin instance at a time controls the synth (spec #1, user story 11). Each processor
// owns one SynthControl and asks it for control before opening the MIDI ports; an instance that
// does not get control opens no port and so sends nothing to the synth.
//
// Two levels: within this process (the usual case, two instances in one DAW session) a
// process-wide owner decides; across processes (the standalone app next to a DAW) one
// juce::InterProcessLock is held by whichever instance controls the synth. The lock is a POSIX
// record lock, which is per process, so it is taken once per process and never per instance.
//
// Limit: the VST3 and AU bundles are separate binaries; if a host loaded both formats into one
// process, each would see the other as another process, and the lock would not separate them.
//
// Message thread only (instances are created and destroyed there).
class SynthControl {
public:
    enum class Refusal { None, AnotherInstanceHere, AnotherApplication };

    SynthControl() = default;
    ~SynthControl();

    // True when this instance controls the synth (now, or already).
    bool tryAcquire();
    void release();
    bool holds() const;
    // Why the last tryAcquire() failed.
    Refusal refusal() const { return lastRefusal; }

private:
    Refusal lastRefusal = Refusal::None;

    JUCE_DECLARE_NON_COPYABLE(SynthControl)
};
