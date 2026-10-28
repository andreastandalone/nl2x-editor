// The simulated synth itself: tests lean on it, so its basic behaviour is pinned down here.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::MidiCable;
using nl2x::test::SimulatedNordLead2X;

TEST_CASE("Simulated synth: every Slot on the CC's channel reacts")
{
    SimulatedNordLead2X synth;
    synth.setSlotChannel(Slot::B, hardware::kDefaultSlotChannels[slotIndex(Slot::A)]);

    const std::array<std::uint8_t, 3> cutoff { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[0]), 74, 90 };
    synth.receive(cutoff);

    CHECK(synth.editBuffer(Slot::A)[3] == 90);
    CHECK(synth.editBuffer(Slot::B)[3] == 90);
    CHECK(synth.editBuffer(Slot::C)[3] == 0);
}

TEST_CASE("Simulated synth: a Slot whose channel is off ignores CCs")
{
    SimulatedNordLead2X synth;
    synth.setSlotChannel(Slot::A, std::nullopt);

    const std::array<std::uint8_t, 3> cutoff { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[0]), 74, 90 };
    synth.receive(cutoff);

    CHECK(synth.editBuffer(Slot::A)[3] == 0);
}

TEST_CASE("Simulated synth: turning a panel knob transmits its CC on the active Slot's channel")
{
    SimulatedNordLead2X synth;
    MidiCable synthToEditor;
    synth.connectMidiOut(&synthToEditor);

    synth.turnPanelControl(Slot::C, 74, 12);

    const std::vector<std::vector<std::uint8_t>> expected {
        { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[slotIndex(Slot::C)]), 74, 12 }
    };
    CHECK(synthToEditor.messages() == expected);
    CHECK(synth.editBuffer(Slot::C)[3] == 12);
}
