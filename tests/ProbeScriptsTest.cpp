// The probe scripts of docs/hardware-discovery-plan.md, run against the simulated synth with a
// simulated clock: what they send, what they find, and the SUMMARY line they print.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/SysEx.h"
#include "nl2x/probe/ProbeSession.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

using namespace nl2x;
using namespace nl2x::probe;
using Catch::Matchers::ContainsSubstring;
using nl2x::test::MidiCable;
using nl2x::test::SimulatedNordLead2X;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kCutoff = 3;
constexpr std::size_t kCutoffMorph = 28;

SimulatedNordLead2X::Program someProgram(std::int8_t seed)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = static_cast<std::int8_t>((static_cast<int>(i) * 3 + seed) % 100);
    return program;
}

// The probe and the simulated synth, cabled both ways. Messages from the synth are queued and
// delivered on the next millisecond, as the standalone app delivers them from its timer.
struct ProbeRig {
    ProbeRig()
    {
        toSynth.plugInto([this](auto message) { synth.receive(message, now); });
        fromSynth.plugInto([this](auto message) { pending.emplace_back(message.begin(), message.end()); });
        synth.connectMidiOut(&fromSynth);
        synth.setEditBuffer(Slot::A, someProgram(1));
    }

    bool start(ScriptId id, const ScriptOptions& options = {}) { return session.start(id, options, now); }

    void tick()
    {
        ++now;
        while (! pending.empty()) {
            const auto message = pending.front();
            pending.pop_front();
            session.handleIncomingMidi(message, now);
        }
        session.advanceTime(now);
    }

    void runFor(TimeMs durationMs)
    {
        const auto end = now + durationMs;
        while (now < end)
            tick();
    }

    void runUntilDone()
    {
        for (int i = 0; i < 400000 && session.isRunning(); ++i)
            tick();
        REQUIRE_FALSE(session.isRunning());
    }

    std::string lastSummary() const
    {
        const auto found = std::find_if(reports.rbegin(), reports.rend(),
                                        [](const auto& line) { return line.starts_with("SUMMARY "); });
        return found != reports.rend() ? *found : std::string();
    }

    bool reported(std::string_view text) const
    {
        return std::any_of(reports.begin(), reports.end(),
                           [text](const auto& line) { return line.find(text) != std::string::npos; });
    }

    std::vector<Bytes> sysExSent() const
    {
        std::vector<Bytes> sent;
        for (const auto& message : toSynth.messages())
            if (! message.empty() && message[0] == 0xF0)
                sent.push_back(message);
        return sent;
    }

    MidiCable toSynth;
    MidiCable fromSynth;
    SimulatedNordLead2X synth;
    std::vector<std::string> reports;
    ProbeSession session { toSynth, [this](std::string_view line) { reports.emplace_back(line); } };
    std::deque<Bytes> pending;
    TimeMs now = 0;
};

ScriptOptions bankOptions(std::uint8_t requestType)
{
    ScriptOptions options;
    options.bankRequestType = requestType;
    options.bankDumpType = 4;
    return options;
}

// The simulated synth answers Bank requests from type 15 (SysEx Bank 1) to 18 (SysEx Bank 4 =
// the Bank shown as 3, the last RAM Bank), and has Bank 3 selected on its panel.
void prepareBanks(ProbeRig& rig)
{
    rig.synth.setBankRequestTypeOfSysExBank1(15);
    rig.synth.selectBank(3);
    for (int location = 0; location < SimulatedNordLead2X::kProgramsPerBank; ++location)
        rig.synth.setBankProgram(3, location, someProgram(static_cast<std::int8_t>(location)));
}

} // namespace

TEST_CASE("Probe: the script list matches the plan and marks the two memory writers")
{
    REQUIRE(scripts().size() == 21);
    for (const auto& info : scripts()) {
        CHECK(&scriptInfo(info.id) == &info);
        CHECK(info.writesToMemory == (info.id == ScriptId::WriteTestProgramToBank || info.id == ScriptId::RestoreBankBackup));
    }
    CHECK(scriptInfo(ScriptId::WriteTestProgramToBank).name == "Write test Program to Bank (writes to memory)");
}

TEST_CASE("Probe: every incoming SysEx is described in the log")
{
    ProbeRig rig;

    rig.session.handleIncomingMidi(buildRequest(15, 14, 0), 0);

    CHECK(rig.reported("SysEx in: 7 bytes, other Nord Lead message, dev 15, type 14, spec 0"));
}

TEST_CASE("Probe: All Controllers Request goes to every dev and credits the CCs to the one that answered")
{
    ProbeRig rig;

    REQUIRE(rig.start(ScriptId::AllControllersRequest));
    rig.runUntilDone();

    REQUIRE(rig.sysExSent().size() == 16);
    for (GlobalChannel dev = 0; dev < 16; ++dev)
        CHECK(rig.sysExSent()[dev] == allControllersRequest(dev, Slot::A));
    CHECK(rig.lastSummary() == "SUMMARY All Controllers Request A: 1 of 16 requests answered: dev 15 -> 1 CCs on channel 1");
}

TEST_CASE("Probe: Probe Global Channel adopts the only dev that answers")
{
    ProbeRig rig;
    rig.synth.setGlobalChannel(5);

    REQUIRE(rig.start(ScriptId::ProbeGlobalChannel));
    rig.runUntilDone();

    CHECK(rig.session.globalChannel() == 5);
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("1 of 16 requests answered: dev 5 -> 139 bytes, Program dump"));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("Adopted Global Channel 6 (dev 5)"));
    CHECK(rig.now < 16 * 300 + 50);
}

TEST_CASE("Probe: Probe Global Channel keeps the Global Channel when nothing answers")
{
    ProbeRig rig;
    rig.synth.setAnswersSysEx(false);
    rig.session.setGlobalChannel(2);

    REQUIRE(rig.start(ScriptId::ProbeGlobalChannel));
    rig.runUntilDone();

    CHECK(rig.session.globalChannel() == 2);
    CHECK(rig.lastSummary() == "SUMMARY Probe Global Channel: 0 of 16 requests answered. No dev answered: Global Channel unchanged");
}

TEST_CASE("Probe: Request Slot A–D lists the reply to each Slot request")
{
    ProbeRig rig;

    REQUIRE(rig.start(ScriptId::RequestSlotsType14));
    rig.runUntilDone();
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("4 of 4 requests answered: Slot A (type 14 spec 0) -> 139 bytes, "
                                                    "Program dump, dev 15, type 0, spec 0 | Slot B"));

    REQUIRE(rig.start(ScriptId::RequestSlotsType10));
    rig.runUntilDone();
    CHECK(rig.lastSummary() == "SUMMARY Request Slot A–D (type 10): 0 of 4 requests answered");
}

TEST_CASE("Probe: Kit and Performance requests are single requests")
{
    ProbeRig rig;

    REQUIRE(rig.start(ScriptId::RequestKitType10Spec16));
    rig.runUntilDone();
    CHECK(rig.sysExSent().back() == buildRequest(15, 10, 16));

    REQUIRE(rig.start(ScriptId::RequestPerformanceEditBuffer));
    rig.runUntilDone();
    CHECK(rig.sysExSent().back() == performanceEditBufferRequest(15));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("type 40 spec 0 -> 715 bytes, Performance dump"));

    REQUIRE(rig.start(ScriptId::RequestPerformanceBank1Location0));
    rig.runUntilDone();
    CHECK(rig.sysExSent().back() == performanceBankRequest(15, 1, 0));
}

TEST_CASE("Probe: Scan Bank requests sweeps types 11–24 and lists every hit")
{
    ProbeRig rig;
    prepareBanks(rig);

    REQUIRE(rig.start(ScriptId::ScanBankRequests));
    rig.runUntilDone();

    CHECK(rig.sysExSent().size() == 14 * 5);
    CHECK(rig.now >= 14 * 5 * 400);
    const auto summary = rig.lastSummary();
    CHECK_THAT(summary, ContainsSubstring("type 15 spec 0 -> 139 bytes, Program dump, dev 15, type 1, spec 0"));
    CHECK_THAT(summary, ContainsSubstring("type 18 spec 98 -> 139 bytes, Program dump, dev 15, type 4, spec 98"));
    CHECK_THAT(summary, ContainsSubstring("type 14 spec 0 -> 139 bytes"));
    CHECK_THAT(summary, ContainsSubstring("type 20 spec 0 -> 1 CCs on channel 1"));
    CHECK_THAT(summary, !ContainsSubstring("type 11 spec"));
    CHECK_THAT(summary, !ContainsSubstring("spec 99 ->"));
}

TEST_CASE("Probe: cutoff low/high sends Slot A's own Program with only the cutoff changed, then restores")
{
    ProbeRig rig;
    const auto original = rig.synth.editBuffer(Slot::A);

    REQUIRE(rig.start(ScriptId::SendProgramCutoffLowHigh));
    rig.runFor(100);
    auto expected = original;
    expected[kCutoff] = 0;
    CHECK(rig.synth.editBuffer(Slot::A) == expected);

    rig.runUntilDone();
    expected[kCutoff] = 127;
    CHECK(rig.synth.editBuffer(Slot::A) == expected);
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("Filter Cutoff 0, then Filter Cutoff 127 (original Filter Cutoff 1"));

    REQUIRE(rig.start(ScriptId::RestoreSlotAProgram));
    CHECK(rig.synth.editBuffer(Slot::A) == original);
}

TEST_CASE("Probe: Morph Amount on Cutoff starts from the original, even after another change")
{
    ProbeRig rig;
    const auto original = rig.synth.editBuffer(Slot::A);
    REQUIRE(rig.start(ScriptId::SendProgramCutoffLowHigh));
    rig.runUntilDone();

    REQUIRE(rig.start(ScriptId::SendProgramMorphOnCutoff));
    rig.runUntilDone();

    auto expected = original;
    expected[kCutoff] = 20;
    expected[kCutoffMorph] = 100;
    CHECK(rig.synth.editBuffer(Slot::A) == expected);

    REQUIRE(rig.start(ScriptId::RestoreSlotAProgram));
    CHECK(rig.synth.editBuffer(Slot::A) == original);
}

TEST_CASE("Probe: Restore Slot A Program refuses when the probe changed nothing")
{
    ProbeRig rig;

    CHECK_FALSE(rig.start(ScriptId::RestoreSlotAProgram));
    CHECK(rig.toSynth.messages().empty());
    CHECK(rig.reported("Not started: Restore Slot A Program"));
}

TEST_CASE("Probe: a script that needs Slot A fails clearly when the synth does not answer")
{
    ProbeRig rig;
    rig.synth.setAnswersSysEx(false);

    REQUIRE(rig.start(ScriptId::SendProgramCutoffLowHigh));
    rig.runUntilDone();

    CHECK_THAT(rig.lastSummary(), ContainsSubstring("FAILED: no reply to the Slot A request"));
    CHECK(rig.sysExSent().size() == 1);
}

TEST_CASE("Probe: Send Performance toggles the Keyboard Split in the Performance Edit Buffer")
{
    ProbeRig rig;

    REQUIRE(rig.start(ScriptId::SendPerformanceToggleSplit));
    rig.runUntilDone();
    const auto splitIndex = kPerformanceKeyboardSplitOffset - kPerformanceSlotChannelOffset;
    CHECK(rig.synth.performanceSettings()[splitIndex] == 1);
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("Keyboard Split was 0"));

    REQUIRE(rig.start(ScriptId::SendPerformanceToggleSplit));
    rig.runUntilDone();
    CHECK(rig.synth.performanceSettings()[splitIndex] == 0);
}

TEST_CASE("Probe: Watch CCs prints the values seen per CC when stopped")
{
    ProbeRig rig;
    REQUIRE(rig.start(ScriptId::WatchCcs));

    for (int value = 0; value <= 120; value += 2)
        rig.synth.turnPanelControl(Slot::A, 74, static_cast<std::uint8_t>(value));
    for (std::uint8_t value : std::array<std::uint8_t, 5> { 3, 0, 1, 2, 1 })
        rig.synth.turnPanelControl(Slot::B, 35, value);
    rig.runFor(10);
    rig.session.stop(rig.now);

    CHECK(rig.reported("CC range: CC74 on channel 1: 61 values from 0 to 120, steps 2"));
    CHECK(rig.reported("CC range: CC35 on channel 2: 4 values: 0 1 2 3"));
    CHECK(rig.lastSummary() == "SUMMARY Watch CCs: 2 controllers seen: CC74 ch 1, CC35 ch 2");
}

TEST_CASE("Probe: Send CC sweep sends every value of the chosen CC on the chosen channel")
{
    ProbeRig rig;
    ScriptOptions options;
    options.sweepCc = 78;
    options.sweepChannel = 2;

    REQUIRE(rig.start(ScriptId::SendCcSweep, options));
    rig.runUntilDone();

    REQUIRE(rig.toSynth.messages().size() == 128);
    for (std::size_t value = 0; value < 128; ++value)
        CHECK(rig.toSynth.messages()[value] == Bytes { 0xB2, 78, static_cast<std::uint8_t>(value) });
    CHECK(rig.lastSummary() == "SUMMARY Send CC sweep: sent CC78 values 0–127 on channel 3, 250 ms apart");
}

TEST_CASE("Probe: the pacing test passes every spacing on a synth that keeps up, and restores Slot A")
{
    ProbeRig rig;
    const auto original = rig.synth.editBuffer(Slot::A);

    REQUIRE(rig.start(ScriptId::PacingTest));
    rig.runUntilDone();

    CHECK(rig.reported("Pacing 0 ms (measured 0–0 ms): 20 of 20 read-backs match: PASS"));
    CHECK(rig.reported("Pacing 50 ms (measured 50–50 ms): 20 of 20 read-backs match: PASS"));
    CHECK(rig.lastSummary()
          == "SUMMARY Pacing test: 0 ms PASS, 5 ms PASS, 10 ms PASS, 20 ms PASS, 50 ms PASS; smallest spacing with "
             "every read-back matching: 0 ms; Slot A Program restored");
    CHECK(rig.synth.editBuffer(Slot::A) == original);
}

TEST_CASE("Probe: the pacing test finds the smallest spacing a slow synth can digest")
{
    ProbeRig rig;
    rig.synth.setSysExRecoveryMs(15);

    REQUIRE(rig.start(ScriptId::PacingTest));
    rig.runUntilDone();

    CHECK(rig.lastSummary()
          == "SUMMARY Pacing test: 0 ms FAIL, 5 ms FAIL, 10 ms FAIL, 20 ms PASS, 50 ms PASS; smallest spacing with "
             "every read-back matching: 20 ms; Slot A Program restored");
}

TEST_CASE("Probe: the stress test sends Slot A's Program every 50 ms for 30 s and checks it afterwards")
{
    ProbeRig rig;
    const auto original = rig.synth.editBuffer(Slot::A);

    REQUIRE(rig.start(ScriptId::StressProgramDumps));
    rig.runUntilDone();

    CHECK(rig.sysExSent().size() == 1 + 600 + 1);
    CHECK(rig.now >= 30000);
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("sent 600 Program dumps 50 ms apart"));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("0 malformed SysEx; Slot A Program read back afterwards: unchanged"));
    CHECK(rig.synth.editBuffer(Slot::A) == original);
}

TEST_CASE("Probe: Bank scripts refuse to run without the Bank request type from step 5")
{
    ProbeRig rig;

    for (auto id : { ScriptId::BackUpBank, ScriptId::WriteTestProgramToBank, ScriptId::RestoreBankBackup })
        CHECK_FALSE(rig.start(id));
    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("Probe: the Bank write refuses without a backup of that Bank from this session")
{
    ProbeRig rig;
    prepareBanks(rig);

    CHECK_FALSE(rig.start(ScriptId::WriteTestProgramToBank, bankOptions(18)));
    CHECK_FALSE(rig.start(ScriptId::RestoreBankBackup, bankOptions(18)));
    CHECK(rig.reported("no backup of the Bank read with request type 18 was captured in this session"));

    // A backup of another Bank does not count.
    REQUIRE(rig.start(ScriptId::BackUpBank, bankOptions(15)));
    rig.runUntilDone();
    CHECK_FALSE(rig.start(ScriptId::WriteTestProgramToBank, bankOptions(18)));

    CHECK(rig.synth.bankProgram(3, 98) == someProgram(98));
}

TEST_CASE("Probe: back up, write a test Program to Program 99, then restore the Bank")
{
    ProbeRig rig;
    prepareBanks(rig);
    std::optional<BankBackup> captured;
    rig.session.onBankBackupCaptured = [&](const BankBackup& backup) { captured = backup; };

    REQUIRE(rig.start(ScriptId::BackUpBank, bankOptions(18)));
    rig.runUntilDone();
    REQUIRE(captured);
    CHECK(captured->replies.size() == 99);
    CHECK(captured->replies[98] == bankProgramDump(15, 4, 98, someProgram(98)));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("captured all 99 Programs with request type 18; reply type bytes: 4; "
                                                    "reply spec equals the location for 99 of 99"));

    REQUIRE(rig.start(ScriptId::WriteTestProgramToBank, bankOptions(18)));
    CHECK(rig.reported("WRITES TO THE SYNTH'S MEMORY"));
    rig.runUntilDone();
    CHECK(rig.synth.bankProgram(3, 98) != someProgram(98));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("the test Program was stored at Program 99"));

    REQUIRE(rig.start(ScriptId::RestoreBankBackup, bankOptions(18)));
    rig.runUntilDone();
    for (int location = 0; location < 99; ++location)
        CHECK(rig.synth.bankProgram(3, location) == someProgram(static_cast<std::int8_t>(location)));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("wrote 99 Programs 100 ms apart (dump type 4); read-back: 99 of 99 "
                                                    "match the backup"));
}

TEST_CASE("Probe: the Bank write reports when the Program did not land where it was read")
{
    ProbeRig rig;
    prepareBanks(rig);
    REQUIRE(rig.start(ScriptId::BackUpBank, bankOptions(18)));
    rig.runUntilDone();

    // The user left Bank 0 selected on the panel: the synth stores there instead.
    rig.synth.selectBank(0);
    REQUIRE(rig.start(ScriptId::WriteTestProgramToBank, bankOptions(18)));
    rig.runUntilDone();

    CHECK_THAT(rig.lastSummary(), ContainsSubstring("Program 99 is unchanged"));
    CHECK(rig.synth.bankProgram(3, 98) == someProgram(98));
}

TEST_CASE("Probe: an incomplete Bank backup is not kept")
{
    ProbeRig rig;
    prepareBanks(rig);

    REQUIRE(rig.start(ScriptId::BackUpBank, bankOptions(19)));
    rig.runUntilDone();

    CHECK_FALSE(rig.session.bankBackup().has_value());
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("FAILED: Program 1 did not answer"));
}

TEST_CASE("Probe: one script at a time; Stop reports what was found so far")
{
    ProbeRig rig;
    REQUIRE(rig.start(ScriptId::ScanBankRequests));

    CHECK_FALSE(rig.start(ScriptId::PacingTest));
    CHECK(rig.reported("stop the running script first"));

    rig.runFor(1000);
    rig.session.stop(rig.now);

    CHECK_FALSE(rig.session.isRunning());
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("of 3 requests answered"));
    CHECK_THAT(rig.lastSummary(), ContainsSubstring("(stopped before the end)"));
}
