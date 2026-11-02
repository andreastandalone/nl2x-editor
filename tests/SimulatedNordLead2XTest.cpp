// The simulated synth itself: tests lean on it, so its basic behaviour is pinned down here.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("Simulated synth: type 20, the manual's All Controllers Request, reads a Program of the Bank shown as 5")
{
    SynthWithCables rig;

    rig.synth.receive(buildRequest(hardware::kFactoryGlobalChannel, hardware::kManualAllControllersRequestType, 0));

    // As on the hardware (2026-10-29): a 139-byte Program dump, type 6 spec 0, and no CC.
    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.kind == SysExKind::ProgramDump);
    CHECK(reply.type == 6);
    CHECK(reply.spec == 0);
    CHECK(reply.values == std::vector<std::int8_t>(rig.synth.bankProgram(5, 0).begin(), rig.synth.bankProgram(5, 0).end()));
}

TEST_CASE("Simulated synth: with the hardware's reply format, the active Slot decides Program or Kit")
{
    SynthWithCables rig;
    rig.synth.setKitReplyFormatFollowsActiveSlot(true);
    const auto before = programWithCutoff(21);
    rig.synth.setEditBuffer(Slot::C, before);
    SimulatedNordLead2X::PercussionKit kit {};
    kit.fill(4);
    rig.synth.playPercussionKit(Slot::C, kit);

    SECTION("a Program on the active Slot: the Kit Slot answers with the Program it held before")
    {
        rig.synth.setActiveSlot(Slot::A);
        rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::C));
        REQUIRE(rig.fromSynth.messages().size() == 1);
        const auto reply = parseSysEx(rig.fromSynth.messages().front());
        CHECK(reply.kind == SysExKind::ProgramDump);
        CHECK(reply.values == std::vector<std::int8_t>(before.begin(), before.end()));
    }

    SECTION("a Kit on the active Slot: every Slot answers with a Kit, zeros for a Program")
    {
        rig.synth.setActiveSlot(Slot::C);
        rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::C));
        rig.synth.receive(slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A));
        REQUIRE(rig.fromSynth.messages().size() == 2);
        const auto kitReply = parseSysEx(rig.fromSynth.messages()[0]);
        const auto programSlotReply = parseSysEx(rig.fromSynth.messages()[1]);
        CHECK(kitReply.kind == SysExKind::PercussionKitDump);
        CHECK(kitReply.values == std::vector<std::int8_t>(kit.begin(), kit.end()));
        CHECK(programSlotReply.kind == SysExKind::PercussionKitDump);
        CHECK(programSlotReply.spec == hardware::kSlotEditBufferKitReplySpecOfSlotA);
        CHECK(programSlotReply.values == std::vector<std::int8_t>(kPercussionKitParameterCount, 0));
    }
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

TEST_CASE("Simulated synth: a Performance dump to the Edit Buffer is stored but not applied")
{
    SynthWithCables rig;
    const auto slotABefore = rig.synth.editBuffer(Slot::A);
    const auto settingsBefore = rig.synth.performanceSettings();
    const auto slotCChannel = rig.synth.slotChannel(Slot::C);
    std::vector<std::int8_t> performance(kPerformanceParameterCount, 0);
    performance[3] = 120;
    performance[kPerformanceSlotChannelOffset + 2] = 9;
    performance[kPerformanceKeyboardSplitOffset] = 1;

    rig.synth.receive(performanceEditBufferDump(hardware::kFactoryGlobalChannel, performance));
    rig.synth.receive(performanceEditBufferRequest(hardware::kFactoryGlobalChannel));

    // As on the hardware (2026-10-29): read back as sent, but the synth plays as before.
    REQUIRE(rig.fromSynth.messages().size() == 1);
    CHECK(parseSysEx(rig.fromSynth.messages().front()).values == performance);
    CHECK(rig.synth.editBuffer(Slot::A) == slotABefore);
    CHECK(rig.synth.performanceSettings() == settingsBefore);
    CHECK(rig.synth.slotChannel(Slot::C) == slotCChannel);

    SECTION("selecting a Performance drops it")
    {
        rig.synth.selectPerformanceOnPanel(1, 0);
        CHECK(rig.synth.performanceEditBuffer()[kPerformanceKeyboardSplitOffset]
              == rig.synth.storedPerformance(1, 0)[kPerformanceKeyboardSplitOffset]);
        CHECK(rig.synth.performanceEditBuffer()[3] == rig.synth.storedPerformance(1, 0)[3]);
    }
}

TEST_CASE("Simulated synth: Performance Bank requests answer with type 31–34, a few values changing between reads")
{
    SynthWithCables rig;

    rig.synth.receive(performanceBankRequest(hardware::kFactoryGlobalChannel, 2, 7));
    rig.synth.receive(performanceBankRequest(hardware::kFactoryGlobalChannel, 2, 7));

    REQUIRE(rig.fromSynth.messages().size() == 2);
    const auto first = parseSysEx(rig.fromSynth.messages()[0]);
    const auto second = parseSysEx(rig.fromSynth.messages()[1]);
    CHECK(first.type == 32);
    CHECK(first.spec == 7);
    REQUIRE(first.values);
    REQUIRE(second.values);
    CHECK(*first.values != *second.values);
    for (std::size_t i = 0; i < first.values->size(); ++i)
        if (i < hardware::kFirstVolatilePerformanceValue || i > hardware::kLastVolatilePerformanceValue)
            CHECK((*first.values)[i] == (*second.values)[i]);
}

TEST_CASE("Simulated synth: a Performance Bank dump is ignored")
{
    SynthWithCables rig;
    const auto before = rig.synth.storedPerformance(1, 0);
    std::vector<std::int8_t> performance(kPerformanceParameterCount, 9);

    rig.synth.receive(buildSysEx(hardware::kFactoryGlobalChannel, hardware::kFirstPerformanceBankDumpType, 0, performance));

    CHECK(rig.synth.storedPerformance(1, 0) == before);
}

TEST_CASE("Simulated synth: Bank dumps are stored in the Bank selected on the panel, whatever their type")
{
    SynthWithCables rig;
    rig.synth.selectBank(3);
    const auto bank0Program99 = rig.synth.bankProgram(0, 98);

    rig.synth.receive(bankProgramDump(hardware::kFactoryGlobalChannel, 1, 98, programWithCutoff(12)));

    CHECK(rig.synth.bankProgram(3, 98) == programWithCutoff(12));
    CHECK(rig.synth.bankProgram(0, 98) == bank0Program99);
}

TEST_CASE("Simulated synth: Bank requests are type = dump type + 14, as on the hardware")
{
    SynthWithCables rig;
    rig.synth.setBankProgram(3, 5, programWithCutoff(44));

    // As printed (type 11–13) nothing answers.
    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 13, 5));
    CHECK(rig.fromSynth.messages().empty());

    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 18, 5));

    REQUIRE(rig.fromSynth.messages().size() == 1);
    const auto reply = parseSysEx(rig.fromSynth.messages().front());
    CHECK(reply.type == 4);
    CHECK(reply.spec == 5);
    CHECK((*reply.values)[3] == 44);
}

TEST_CASE("Simulated synth: Kit Bank locations answer with a Kit that changes from read to read")
{
    SynthWithCables rig;

    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 15, hardware::kFirstPercussionKitLocation));
    rig.synth.receive(bankLocationRequest(hardware::kFactoryGlobalChannel, 15, hardware::kFirstPercussionKitLocation));

    REQUIRE(rig.fromSynth.messages().size() == 2);
    const auto first = parseSysEx(rig.fromSynth.messages()[0]);
    const auto second = parseSysEx(rig.fromSynth.messages()[1]);
    CHECK(first.kind == SysExKind::PercussionKitDump);
    CHECK(first.type == 1);
    CHECK(first.spec == hardware::kFirstPercussionKitLocation);
    CHECK(first.values != second.values);
    const auto& stored = rig.synth.storedPercussionKit(0, 0);
    CHECK(first.values != std::vector<std::int8_t>(stored.begin(), stored.end()));
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

