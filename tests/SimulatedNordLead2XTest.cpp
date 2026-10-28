// The simulated synth itself: tests lean on it, so its basic behaviour is pinned down here.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/SysEx.h"

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

// ---------------------------------------------------------------------------------------------
// SysEx, following HardwareAssumptions.h.

namespace {

struct SynthWithCables {
    SynthWithCables() { synth.connectMidiOut(&fromSynth); }

    SimulatedNordLead2X synth;
    MidiCable fromSynth;
};

SimulatedNordLead2X::Program programWithCutoff(std::int8_t cutoff)
{
    SimulatedNordLead2X::Program program {};
    program[3] = cutoff;
    program[28] = -128; // a Morph Amount at the end of its range
    program[59] = 7;    // beyond the documented maximum, as in the factory files [OFF-FB]
    return program;
}

} // namespace

TEST_CASE("Simulated synth: the Slot Edit Buffer request returns that Slot's Program")
{
    SynthWithCables rig;
    rig.synth.setEditBuffer(Slot::B, programWithCutoff(77));

    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::B));

    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.kind == SysExKind::ProgramDump);
    CHECK(reply.type == hardware::kSlotEditBufferReplyType);
    CHECK(reply.spec == 1);
    const auto expected = programWithCutoff(77);
    CHECK(reply.values == std::vector<std::int8_t>(expected.begin(), expected.end()));
}

TEST_CASE("Simulated synth: requests on another Global Channel are ignored")
{
    SynthWithCables rig;
    rig.synth.setGlobalChannel(3);

    rig.synth.receive(slotEditBufferRequest(4, Slot::A));
    CHECK(rig.fromSynth.messages().empty());

    rig.synth.receive(slotEditBufferRequest(3, Slot::A));
    CHECK(rig.fromSynth.messages().size() == 1);
}

TEST_CASE("Simulated synth: a Program dump of type 0 goes into that Slot's Edit Buffer")
{
    SynthWithCables rig;
    const auto program = programWithCutoff(-5);

    rig.synth.receive(programEditBufferDump(hardware::kFactoryGlobalChannel, Slot::C, program));

    CHECK(rig.synth.editBuffer(Slot::C) == program);
    CHECK(rig.synth.editBuffer(Slot::A) == SimulatedNordLead2X::Program {});
}

TEST_CASE("Simulated synth: a Slot playing a Percussion Kit answers the Slot Edit Buffer request with its Kit")
{
    SynthWithCables rig;
    SimulatedNordLead2X::PercussionKit kit {};
    kit[kProgramParameterCount + 3] = -7; // second percussion sound's cutoff
    rig.synth.playPercussionKit(Slot::D, kit);

    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::D));

    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.kind == SysExKind::PercussionKitDump);
    CHECK(hardware::classifySlotEditBufferReply(reply.size) == hardware::SlotEditBufferReply::PercussionKit);
    CHECK(reply.spec == hardware::kSlotEditBufferKitReplySpecOfSlotA + 3);
    CHECK(reply.values == std::vector<std::int8_t>(kit.begin(), kit.end()));
}

TEST_CASE("Simulated synth: a Program dump is ignored by a Slot playing a Percussion Kit, and back")
{
    SynthWithCables rig;
    rig.synth.playPercussionKit(Slot::A, {});

    rig.synth.receive(programEditBufferDump(hardware::kFactoryGlobalChannel, Slot::A, programWithCutoff(50)));
    CHECK(rig.synth.editBuffer(Slot::A)[3] == 0);
    CHECK(rig.synth.playsPercussionKit(Slot::A));

    rig.synth.setEditBuffer(Slot::A, programWithCutoff(51));
    CHECK_FALSE(rig.synth.playsPercussionKit(Slot::A));
}

TEST_CASE("Simulated synth: the All Controllers Request answers with the Slot's CCs on its channel")
{
    SynthWithCables rig;
    rig.synth.setEditBuffer(Slot::A, programWithCutoff(90));

    rig.synth.receive(allControllersRequest(hardware::kFactoryGlobalChannel, Slot::A));

    const std::vector<std::vector<std::uint8_t>> expected {
        { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[0]), 74, 90 }
    };
    CHECK(rig.fromSynth.messages() == expected);
}

TEST_CASE("Simulated synth: the Performance Edit Buffer request returns the four Slots and the settings")
{
    SynthWithCables rig;
    rig.synth.setEditBuffer(Slot::D, programWithCutoff(33));
    rig.synth.setSlotChannel(Slot::B, std::nullopt);

    rig.synth.receive(performanceEditBufferRequest(hardware::kFactoryGlobalChannel));

    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.kind == SysExKind::PerformanceDump);
    CHECK(reply.type == hardware::kPerformanceEditBufferDumpType);
    REQUIRE(reply.values);
    CHECK((*reply.values)[3 * kProgramParameterCount + 3] == 33);
    CHECK((*reply.values)[kPerformanceSlotChannelOffset] == 0);
    CHECK((*reply.values)[kPerformanceSlotChannelOffset + 1] == -1);
}

TEST_CASE("Simulated synth: a Performance dump to the Edit Buffer replaces the Slots and settings")
{
    SynthWithCables rig;
    std::vector<std::int8_t> performance(kPerformanceParameterCount, 0);
    performance[3] = 120;
    performance[kPerformanceSlotChannelOffset + 2] = 9;
    performance[kPerformanceKeyboardSplitOffset] = 1;

    rig.synth.receive(performanceEditBufferDump(hardware::kFactoryGlobalChannel, performance));
    rig.synth.receive(performanceEditBufferRequest(hardware::kFactoryGlobalChannel));

    CHECK(rig.synth.editBuffer(Slot::A)[3] == 120);
    CHECK(rig.synth.performanceSettings()[kPerformanceKeyboardSplitOffset - kPerformanceSlotChannelOffset] == 1);
    REQUIRE(rig.fromSynth.messages().size() == 1);
    CHECK(parseSysEx(rig.fromSynth.messages().front()).values == performance);
}

TEST_CASE("Simulated synth: Bank dumps are stored in the Bank selected on the panel, whatever their type")
{
    SynthWithCables rig;
    rig.synth.selectBank(3);

    rig.synth.receive(bankProgramDump(hardware::kFactoryGlobalChannel, 1, 98, programWithCutoff(12)));

    CHECK(rig.synth.bankProgram(3, 98) == programWithCutoff(12));
    CHECK(rig.synth.bankProgram(0, 98) == SimulatedNordLead2X::Program {});
}

TEST_CASE("Simulated synth: Bank requests are answered only once a request type is chosen")
{
    SynthWithCables rig;
    rig.synth.setBankProgram(3, 5, programWithCutoff(44));

    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 18, 5));
    CHECK(rig.fromSynth.messages().empty());

    rig.synth.setBankRequestTypeOfSysExBank1(15);
    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 18, 5));

    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.type == 4);
    CHECK(reply.spec == 5);
    CHECK((*reply.values)[3] == 44);
}

TEST_CASE("Simulated synth misbehaving: SysEx arriving too soon after the previous one is lost")
{
    SynthWithCables rig;
    rig.synth.setSysExRecoveryMs(15);

    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A), 100);
    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A), 110);
    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A), 130);

    CHECK(rig.fromSynth.messages().size() == 2);
    CHECK(rig.synth.lostSysExCount() == 1);
}

TEST_CASE("Simulated synth misbehaving: no SysEx answers at all")
{
    SynthWithCables rig;
    rig.synth.setAnswersSysEx(false);

    rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A));

    CHECK(rig.fromSynth.messages().empty());
}

TEST_CASE("Simulated synth: a Performance dump ignores the settings the synth does not receive")
{
    SimulatedNordLead2X synth;
    auto before = synth.performanceSettings();
    before[318 - kPerformanceSlotChannelOffset] = -12; // Master Tune
    synth.setPerformanceSettings(before);

    std::vector<std::int8_t> performance(kPerformanceParameterCount, 0);
    for (std::size_t offset = 315; offset <= 323; ++offset)
        performance[offset] = 3;
    performance[kPerformanceKeyboardSplitOffset] = 1;
    synth.receive(performanceEditBufferDump(hardware::kFactoryGlobalChannel, performance));

    const auto& after = synth.performanceSettings();
    for (std::size_t offset : { 315u, 316u, 317u, 318u, 319u, 320u, 321u, 323u })
        CHECK(after[offset - kPerformanceSlotChannelOffset] == before[offset - kPerformanceSlotChannelOffset]);
    CHECK(after[322 - kPerformanceSlotChannelOffset] == 3); // Selected Slot is received
    CHECK(after[kPerformanceKeyboardSplitOffset - kPerformanceSlotChannelOffset] == 1);
}

TEST_CASE("Simulated synth: after a Performance dump the Slots listen on its Slot Channels only")
{
    SimulatedNordLead2X synth;
    std::vector<std::int8_t> performance(kPerformanceParameterCount, 0);
    performance[kPerformanceSlotChannelOffset + 0] = 7;
    performance[kPerformanceSlotChannelOffset + 1] = 7;
    performance[kPerformanceSlotChannelOffset + 2] = 9;
    performance[kPerformanceSlotChannelOffset + 3] = hardware::kPerformanceSlotChannelOff;
    synth.receive(performanceEditBufferDump(hardware::kFactoryGlobalChannel, performance));

    synth.receive(std::array<std::uint8_t, 3> { 0xB0, 74, 11 }); // channel 1: nobody now
    synth.receive(std::array<std::uint8_t, 3> { 0xB7, 74, 22 }); // channel 8: A and B
    synth.receive(std::array<std::uint8_t, 3> { 0xB3, 74, 33 }); // channel 4: D used to be here

    CHECK(synth.editBuffer(Slot::A)[3] == 22);
    CHECK(synth.editBuffer(Slot::B)[3] == 22);
    CHECK(synth.editBuffer(Slot::C)[3] == 0);
    CHECK(synth.editBuffer(Slot::D)[3] == 0);
    CHECK_FALSE(synth.slotChannel(Slot::D));
}

TEST_CASE("Simulated synth: layered Slots also receive on the leftmost active Slot's channel")
{
    SimulatedNordLead2X synth;
    synth.setLayered(Slot::B, true);
    synth.setLayered(Slot::D, true);

    // Slot B (channel 2) is the leftmost active Slot.
    synth.receive(std::array<std::uint8_t, 3> { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[1]), 74, 44 });

    CHECK(synth.editBuffer(Slot::A)[3] == 0);
    CHECK(synth.editBuffer(Slot::B)[3] == 44);
    CHECK(synth.editBuffer(Slot::C)[3] == 0);
    CHECK(synth.editBuffer(Slot::D)[3] == 44);
}

TEST_CASE("Simulated synth: a Performance dump leaves a Slot playing a Percussion Kit alone")
{
    SimulatedNordLead2X synth;
    synth.playPercussionKit(Slot::B, {});
    std::vector<std::int8_t> performance(kPerformanceParameterCount, 5);
    performance[kPerformanceSlotChannelOffset + 1] = 1;

    synth.receive(performanceEditBufferDump(hardware::kFactoryGlobalChannel, performance));

    CHECK(synth.playsPercussionKit(Slot::B));
    CHECK(synth.editBuffer(Slot::B)[3] == 0);
    CHECK(synth.editBuffer(Slot::A)[3] == 5);
}
