#pragma once

// Every guess about the real synth's behaviour lives here, and nowhere else (CODING_STANDARDS.md).
// Each entry names the research fact it rests on (docs/research/nl2x-midi-research.md) and its
// verification status. Hardware discovery (#3) changes this file; the engine and the simulated
// synth both follow it.
//
// Status words: "documented" = printed in the manual but not yet checked on hardware (the manual
// is known to be partly wrong, research §3); "secondary" = reported by other users of a 2X;
// "[INF]" = our own inference; "contradicted" = a source says the documented value is wrong;
// "verified [HW date]" / "refuted [HW date]" = settled on Andrea's Nord Lead 2X during hardware
// discovery (#3), recorded with its log in docs/research/hardware-session-2026-10-29.md (the
// session ran on 2026-10-29 and 2026-11-02).

#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace nl2x::hardware {

// Slot Channels assumed until the Performance has been read from the synth (#5).
// Facts: each Slot has its own MIDI channel, 1–16 or off, stored in the Performance
// [OFF-2X p.67, 75–76]. The manual does not state a default. In the 100 factory Performances
// [OFF-FB] Slot A is always on channel 1; B, C and D vary (most often 1, 3, 4).
// Choice: channels 1, 2, 3, 4, so that the four Slots can be addressed independently.
// Status: [INF], not verified on hardware.
inline constexpr std::array<MidiChannel, 4> kDefaultSlotChannels { 0, 1, 2, 3 };

// A Slot Channel as stored in the Performance (decoded offsets 264–267): 0–15 = channels 1–16
// [OFF-2X p.112]. The manual documents no value for "oFF", but [OFF-FB] contains −1 there, so −1
// is taken to mean off. Any other value outside 0–15 cannot address a channel and is also treated
// as off (the byte itself is kept unchanged).
// Status: [INF] (research §3, Performance layout), not verified on hardware.
inline constexpr std::int8_t kPerformanceSlotChannelOff = -1;

constexpr std::optional<MidiChannel> slotChannelFromPerformance(std::int8_t value)
{
    if (value >= 0 && value <= 15)
        return static_cast<MidiChannel>(value);
    return std::nullopt;
}

// All Slots in a Layer (all activated Slots, Performance offsets 324–327) also receive on the MIDI
// channel of the leftmost active Slot, as well as on their own [OFF-2X p.76–77]. So with Slots A,
// B and D layered and Slot A on channel 1, a CC on channel 1 reaches B and D too.
// Status: documented, not verified on hardware.
inline constexpr bool kLayeredSlotsAlsoReceiveOnLeftmostActiveSlotChannel = true;

// ---------------------------------------------------------------------------------------------
// Global Channel and SysEx Device ID

// The SysEx Device ID byte equals the Global Channel, counted 0–15 [OFF-2X p.105]. The synth
// shows it as "G.x"; [SEC-3] reports dev 0 answering with the Global Channel set to 1, so the
// display counts 1–16 (dev = shown value − 1).
// Status: verified [HW 2026-10-29, step 2]: with the synth showing G.16 only dev 15 answered.
inline constexpr std::uint8_t kShownGlobalChannelOfDev0 = 1;

// Global Channel of a synth with factory settings: every factory Performance stores 15 at
// offset 315 (globalmidichan) and every factory file uses dev 0x0F [OFF-FB].
// Status: [INF] that a factory-reset synth uses it.
inline constexpr std::uint8_t kFactoryGlobalChannel = 15;

// ---------------------------------------------------------------------------------------------
// SysEx message types (the byte after the Model ID). Framing and sizes are verified against the
// factory files [OFF-FB] and live in SysEx.h; the meaning of each type code is not.

// Program dump into a Slot's Edit Buffer: type 0, spec 0–3 = Slot A–D [OFF-2X p.105].
// Status: verified [HW 2026-10-29, step 7]: the synth applies it at once, audibly, even during a
// held note (the display does not change); a Morph Amount sent this way is heard too.
inline constexpr std::uint8_t kProgramEditBufferDumpType = 0;

// Program dump into a Bank location: types 1–10 = "Bank 1–10", spec 0–98 = Program 1–99
// [OFF-2X p.105]. The manual says that on receive the type byte is ignored and the Program is
// stored in the Bank currently selected on the synth [OFF-2X p.78]; the factory files carry type
// bytes 2, 3, 4, 2 for Banks 0–3 [OFF-FB, checked HW 2026-11-02]. So the type byte is never a
// storage location (CODING_STANDARDS.md).
// Status: storing verified [HW 2026-11-02, step 11]: a type-4 dump to spec 98 was stored at
// Program 99 of the selected Bank (shown as 3). Open: every write so far had the type byte naming
// the selected Bank, so whether the type or the panel decides is not known.
inline constexpr std::uint8_t kFirstProgramBankDumpType = 1;
inline constexpr std::uint8_t kLastProgramBankDumpType = 10;

// The manual's SysEx tables count Banks from 1 ("Bank 1–4" for the four RAM Banks); the synth's
// display counts Program Banks from 0, and Banks 0–3 are the RAM Banks [OFF-2X p.13]. So the
// manual's SysEx Bank 1 is the Bank shown as 0, and SysEx Bank 4 the Bank shown as 3.
// Status: verified [HW 2026-10-29, step 5]: the replies of dump type 1–4 equal the factory Banks
// 0–3 byte for byte; types 5–10 are the ROM Banks shown as 4–9.
inline constexpr int kShownBankOfSysExBank1 = 0;

// Percussion Kit dumps: Edit Buffer spec 16–19 = Slot A–D; Bank locations 99–108 = P0–P9
// [OFF-2X p.105–106]. Status: documented; the Bank spec is consistent with [OFF-FB]. The Bank
// locations could not be read reliably (kPercussionKitBankReadingVerified below).
inline constexpr std::uint8_t kPercussionKitEditBufferSpecOfSlotA = 16;
inline constexpr std::uint8_t kFirstPercussionKitLocation = 99;

// The documented Program/Kit Edit Buffer request: type 10, spec = Slot [OFF-2X p.106].
// Status: refuted [HW 2026-10-29, steps 3–4]: type 10 gets no answer on a 2X, as [SEC-1] said;
// type 10 also collides with the "Bank 10" dump type (research §3).
inline constexpr std::uint8_t kDocumentedEditBufferRequestType = 10;

// The request that returns a Slot's Program Edit Buffer: type 14, spec 0–3 = Slot A–D [SEC-1].
// It reads the Edit Buffer, not the stored Program: unstored panel edits are in the reply.
// Status: verified [HW 2026-10-29, step 3]; only the dev of the Global Channel answers (step 2).
inline constexpr std::uint8_t kSlotEditBufferRequestType = 14;

// The reply to a Slot Edit Buffer request is a Program dump with the Edit Buffer type and
// spec = the requested Slot. Code still matches a reply to its request by order and size, not by
// header. Status: verified [HW 2026-10-29, step 3].
inline constexpr std::uint8_t kSlotEditBufferReplyType = kProgramEditBufferDumpType;

// How a reply to a Slot Edit Buffer request tells a Program from a Percussion Kit: by its size
// alone. 139 bytes (kProgramDumpSize) = a Program; 1063 bytes (kPercussionKitDumpSize) = a
// Percussion Kit, whatever the header says.
// Status: sizes verified [HW 2026-10-29, steps 3–4]; but the *format* of the reply follows the
// active Slot, not the requested one, so for a Slot holding a Kit the size is unreliable (see
// kSlotEditBufferReplyFormatFollowsActiveSlot). The Kit reply reads the Kit's Edit Buffer (panel
// edits are in it), contrary to [SEC-1].
enum class SlotEditBufferReply : std::uint8_t { Program, PercussionKit, NotAReply };

constexpr SlotEditBufferReply classifySlotEditBufferReply(std::size_t messageSize)
{
    if (messageSize == kProgramDumpSize)
        return SlotEditBufferReply::Program;
    if (messageSize == kPercussionKitDumpSize)
        return SlotEditBufferReply::PercussionKit;
    return SlotEditBufferReply::NotAReply;
}

// Whether a Slot Edit Buffer request is answered with the requested Slot's content (true) or
// with the active Slot's (false). [SEC-1] reported that the reply depends on the active Slot.
// Status: verified true for Programs [HW 2026-10-29, step 3]: with Programs on every Slot, each
// request returned that Slot's own Edit Buffer (A and B on the same Program gave identical
// dumps, C and D differed), whichever Slot was active.
inline constexpr bool kSlotEditBufferReplyFollowsRequestedSlot = true;

// What the synth does when a Slot holds a Percussion Kit [HW 2026-10-29, steps 3–4]: the reply's
// *format* follows the active Slot. With a Program active on Slot A and a Kit on Slot C, every
// Slot answered with a 139-byte Program, C with the Program it held before the Kit; with the
// active Slot on a Kit (or, once, on an empty Program), every Slot answered with a 1063-byte Kit,
// all zeros for Slots playing Programs. So a type 14 read of a Slot holding a Kit is unreliable,
// and the reply's size alone cannot tell whether a non-active Slot plays a Kit.
// The engine still classifies replies by size (classifySlotEditBufferReply), which is right for
// the active Slot only; telling Kits apart on the other Slots needs another source (the
// Performance's pgmselect, [INF]) and is a follow-up. The simulated synth models this behaviour
// on request (SimulatedNordLead2X::setKitReplyFormatFollowsActiveSlot).
// Status: verified [HW 2026-10-29].
inline constexpr bool kSlotEditBufferReplyFormatFollowsActiveSlot = true;

// Header of the Kit reply to a Slot Edit Buffer request: the Kit Edit Buffer dump of the manual,
// type 0, spec 16–19 = Slot A–D [OFF-2X p.105]. Only the simulated synth uses it: the engine
// matches replies by size. Status: verified for Slot A [HW 2026-10-29, step 4]: type 0, spec 16.
// Requests with spec 16 (type 10 or 14) get no answer.
inline constexpr std::uint8_t kSlotEditBufferKitReplySpecOfSlotA = kPercussionKitEditBufferSpecOfSlotA;

// Bank location requests as printed: types 11–14 = Bank 1–4, spec 0–98 = Program, 99–108 = Kit
// [OFF-2X p.106]. Status: refuted [HW 2026-10-29, step 5]: types 11–13 get no answer and 14 is
// the Slot Edit Buffer request.
inline constexpr std::uint8_t kPrintedFirstBankRequestType = 11;

// On a 2X a Bank location is read with request type = dump type + 14: types 15–24 read the
// Program Banks of dump types 1–10 (shown as 0–9); the reply is a Bank dump of that type with
// spec = location. Type 20, the manual's All Controllers Request, is one of them (Bank shown as
// 5). Status: verified [HW 2026-10-29, step 5; HW 2026-11-02, step 11].
inline constexpr std::uint8_t kBankRequestTypeOffset = 14;

constexpr std::uint8_t bankRequestTypeOfDumpType(std::uint8_t bankDumpType)
{
    return static_cast<std::uint8_t>(bankDumpType + kBankRequestTypeOffset);
}

// The request types the probe's "Scan Bank requests" sweeps: as printed (11–14) and shifted
// (15–24). Kept for the probe; the answer is bankRequestTypeOfDumpType above.
inline constexpr std::uint8_t kFirstBankRequestTypeCandidate = 11;
inline constexpr std::uint8_t kLastBankRequestTypeCandidate = 24;

// Percussion Kit Bank locations (spec 99–108 of request types 15–18) answer with 1063-byte Kit
// dumps, but their contents are not trustworthy: none matched a factory Kit (380+ bytes differ),
// two reads in a row differed at the same bytes of every Kit, and a factory Kit sent as type 1
// spec 99 did not read back. So the editor neither reads nor writes Kit Bank locations.
// Status: verified unreliable [HW 2026-11-02]; how to read and write Kits is open.
inline constexpr bool kPercussionKitBankReadingVerified = false;

// ---------------------------------------------------------------------------------------------
// Writing to the synth's memory (#12, MemoryWriter.h)

// THE LOCK. True once hardware discovery step 5 confirmed the request type that reads a Bank
// location (kBankRequestTypeOfSysExBank1 below), and step 11 showed that a Bank read that way
// restores correctly. While false, the editor cannot back up a Bank: sending a whole Bank or
// storing a Program then needs the user's explicit "no backup possible" confirmation, and reading
// a Bank is offered as experimental only. While true, every overwrite is preceded by an automatic
// backup of the target Bank, and every write is read back.
// Status: verified [HW 2026-11-02, step 11]: Bank shown as 3 read with request type 18 (99 of 99,
// reply type 4, spec = location), written back as 99 dumps of type 4 100 ms apart, read back
// equal 99 of 99.
inline constexpr bool kBankReadingVerified = true;

// The request type that reads SysEx Bank 1 (the Bank shown as 0); SysEx Banks 2–4 follow.
// Status: verified [HW 2026-10-29, step 5]: 15 = dump type 1 + kBankRequestTypeOffset.
inline constexpr std::uint8_t kBankRequestTypeOfSysExBank1 = bankRequestTypeOfDumpType(kFirstProgramBankDumpType);

// The writable Program Banks, as the synth shows them: Banks 0–3 are RAM, 4–9 ROM [OFF-2X p.13].
// Status: documented.
inline constexpr int kFirstRamProgramBank = 0;
inline constexpr int kLastRamProgramBank = 3;
inline constexpr int kProgramsPerRamBank = 99;

// The manual's SysEx Bank number (1–4) of a Bank shown as `shownBank`, following
// kShownBankOfSysExBank1. Program Bank dumps use it as their type byte (which the synth ignores
// on receive, [OFF-2X p.78]); Bank requests add it to the request type of SysEx Bank 1.
constexpr int sysExBankOfShownBank(int shownBank)
{
    return shownBank - kShownBankOfSysExBank1 + 1;
}

// Whether a type-40 read of the Performance Edit Buffer names the Bank a received Bank dump is
// stored in ("the Bank currently selected", [OFF-2X p.78]): the active Slot's (offset 322,
// selected_channel) bankselect (offset 332 + Slot) [OFF-2X p.112]. It does not, so the editor
// cannot read which Bank is selected on the panel. The memory writer relies on the user selecting
// the target Bank, as the confirmation asks first, and reads the written locations back from the
// target Bank to catch a write that went elsewhere. Nothing reads this constant; it records why.
// Status: refuted [HW 2026-11-05]: with Bank 3 selected on the panel on Slot B, the type-40 read
// still named Bank 0. (Offset 322 also lies in the bytes that vary between reads,
// kFirstVolatilePerformanceValue; and a Performance dump the editor sent is read back instead of
// what the synth plays, kPerformanceEditBufferDumpIsApplied [HW 2026-11-04].)
inline constexpr bool kPerformanceNamesSelectedBank = false;

// Performance dumps: type 30 = Edit Buffer, 31–34 = Performance Bank 1–4, spec 0 or 0–99
// [OFF-2X p.107]. Status: documented; Bank dumps match [OFF-FB].
inline constexpr std::uint8_t kPerformanceEditBufferDumpType = 30;
inline constexpr std::uint8_t kFirstPerformanceBankDumpType = 31;

// Whether the synth *applies* a Performance dump received into its Edit Buffer (type 30).
// It stores it: a type 40 read-back equals the dump sent. But it does not apply it: with Keyboard
// Split 1 in the Edit Buffer the split LED stayed off and the keyboard was not split, and leaving
// and re-entering Performance mode reloaded the stored Performance instead. So editing
// Performance settings by type 30 dumps (#5) does not change what the synth plays. While false the
// engine refuses Performance edits (PerformanceEditResult::RefusedNotApplied) and the Performance
// view shows the settings read-only, until a way to apply them is found (#31).
// Status: verified [HW 2026-10-29, step 8].
inline constexpr bool kPerformanceEditBufferDumpIsApplied = false;

// Whether a Performance Bank dump (type 31 spec 0) is stored. A factory Performance sent as type
// 31 spec 0, with the synth in Program mode and Bank 3 selected, changed nothing.
// Status: verified not accepted in that state [HW 2026-11-02]; Performance mode is untested.
inline constexpr bool kPerformanceBankDumpStoredInProgramMode = false;

// Performance requests: type 40 = Edit Buffer, 41–44 = Performance Bank 1–4 [OFF-2X p.108; the
// body text says "41 or 42"]. Status: verified [HW 2026-10-29, step 6; HW 2026-11-02]: type 40
// answers with a 715-byte type-30 dump of the Edit Buffer (unstored panel changes are in it);
// type 41 spec 0–99 answers with type 31, spec = location.
inline constexpr std::uint8_t kPerformanceEditBufferRequestType = 40;
inline constexpr std::uint8_t kFirstPerformanceBankRequestType = 41;

// Bytes 648–653 of a 715-byte Performance dump change between two reads of the same stored
// Performance (type 41); the rest is stable. Counting F0 as byte 1 they hold decoded values
// 320–323 (counting from 0, 321–323): Local On/Off, Keyboard Octave Shift, selected_channel (the
// active Slot, 322) and Arp MIDI Out, three of them marked "not received" [OFF-2X p.112]. Two
// Performances must be compared without these values; and the active Slot read from a dump may
// be stale (kPerformanceNamesSelectedBank). Status: verified [HW 2026-11-02]; which of the four
// values really vary is not known.
inline constexpr std::size_t kFirstVolatilePerformanceValue = 320;
inline constexpr std::size_t kLastVolatilePerformanceValue = 323;

// The manual's All Controllers Request: type 20, spec = Slot; the synth would answer with CCs for
// that Slot [OFF-2X p.109; SEC-3]. Status: refuted [HW 2026-10-29, step 1]: on a 2X type 20 is a
// Bank request (dump type 6 + kBankRequestTypeOffset) and answers with a Program of the Bank shown
// as 5, never with CCs. Nothing in the editor sends it; the probe keeps it as a raw type.
inline constexpr std::uint8_t kManualAllControllersRequestType = 20;

// ---------------------------------------------------------------------------------------------
// CC values of the panel controls (research §2). The CC numbers are printed in the manual's CC
// chart [OFF-2X p.103–104] and live in the engine's table (CcParameters.cpp); pairing each CC with
// its SysEx offset is [INF], but the names match one to one. How a CC value maps to the stored
// byte is mostly undocumented; every rule below is used by the engine *and* the simulated synth.

// Knobs: the CC value is the SysEx value, 0–127 both ways. Status: [INF] (research §2 pairing);
// the CC numbers of every panel control seen in step 9 match CcParameters.cpp [HW 2026-10-29].

// On/off buttons: "0" = off and "a value of 'on'" = on [OFF-2X p.103], the on value is not
// printed. The panel sends 1 for on; the synth takes any non-zero value as on (0 turned Unison
// off, 1, 64 and 127 turned it on). The editor sends 127 and reads any value from 1 up as on.
// The stored byte is 0 or 1 (research §3).
// Status: verified [HW 2026-10-29 and 2026-11-02, step 9].
inline constexpr std::uint8_t kSwitchOnCcValue = 127;
inline constexpr std::uint8_t kSwitchOnCcThreshold = 1;

// Stepped buttons send 0, 1, 2, … "from the lowest setting" [OFF-2X p.103]: the CC value is the
// step number and the SysEx value (CC24 0–7 read back as the same number at offset 65), not a
// value spread over 0–127. A step number read from the synth beyond the list is kept as it is
// (the factory data has such values, research §3).
// Status: verified [HW 2026-10-29 and 2026-11-02, step 9]. The order of each choice list below:
// see its own comment.

// Osc2 Semitones (CC78): SysEx 0–120 with 60 = centre. The panel sends 0–120, the SysEx value
// unscaled, so 121–127 never come from the panel and are kept as they are if they do.
// Status: verified [HW 2026-10-29, step 9].

// The RING MOD/SYNC button (CC35) cycles off, sync, ring mod, both [OFF-2X p.42–43]; byte 52 holds
// bit 0 = sync, bit 1 = ring mod [OFF-2X p.111]. One 4-state control whose CC value 0–3 is bits
// 0–1 of byte 52 (1 = Sync, 2 = Ring Mod, 3 = both). A CC35 value above 3 cannot be stored in
// those bits and is ignored.
// Status: verified [HW 2026-10-29, step 9]: the panel sends 0 → 1 → 2 → 3 → 0, shown as off →
// Sync → Ring Mod → both.
inline constexpr std::uint8_t kRingModSyncCc = 35;
inline constexpr std::uint8_t kRingModSyncBits = 0x03;
inline constexpr std::array<std::string_view, 4> kRingModSyncChoices { "Off", "Sync", "Ring Mod", "Sync + Ring Mod" };

// Whether CC35 reaches the ring-mod bit (bit 1 of byte 52) at all. The CC chart names CC35
// "Oscillator Sync" only [OFF-2X p.103], so research §2 listed Ring Mod as not reachable by CC.
// If it were false, CC35 would carry the sync bit as 0/1 and a change of the ring-mod bit would go
// to the synth as a throttled Program dump to the Slot's Edit Buffer instead (#7); the code keeps
// that path, which tests cover.
// Status: verified true [HW 2026-10-29, step 9].
inline constexpr bool kRingModReachableByCc35 = true;

// The bits of byte 52 that CC35 carries, under either reading of the switch above.
constexpr std::uint8_t ringModSyncCcBits(bool ringModReachableByCc35)
{
    return ringModReachableByCc35 ? kRingModSyncBits : std::uint8_t { 0x01 };
}

// Switches that tests flip to cover both readings (and the locked memory writer). Production code
// always uses the defaults, which are the constants; only tests construct an Engine or the
// simulated synth with other values.
struct Switches {
    bool ringModReachableByCc35 = kRingModReachableByCc35;
    // The memory writer's lock and Bank request type (#12).
    bool bankReadingVerified = kBankReadingVerified;
    std::uint8_t bankRequestTypeOfSysExBank1 = kBankRequestTypeOfSysExBank1;
    // Whether the engine accepts Performance edits (#5, #31).
    bool performanceEditBufferDumpIsApplied = kPerformanceEditBufferDumpIsApplied;
};

// Filter Distortion (CC80, an on/off button) is bit 4 of byte 52 [OFF-2X p.111]; the other bits
// of byte 52 are kept when it changes. Status: the bit is documented; that CC80 drives it is [INF].
inline constexpr std::uint8_t kFilterDistortionBit = 0x10;

// Named steps, lowest first. Names are the manual's panel terms [OFF-2X p.37–61]; the order is
// the order the manual describes them in, except where the patch layout fixes one value
// [OFF-2X p.111]. Step 9 [HW 2026-10-29] showed that each of these buttons (but Filter Type and
// LFO2 Dest/Arp, pressed irregularly) steps its CC by one per press and wraps, so the panel order
// is the value order; which name value 0 has was checked on the LEDs only where a list says so.
// Status: [INF] order of names unless stated otherwise.

// Osc1: "3 = sine"; Osc 2 shares the first three waveforms with Osc 1 and has Noise instead of
// Sine, "3 = noise" [OFF-2X p.39, 111]. CC30/31 send 0–3 [HW 2026-10-29].
inline constexpr std::array<std::string_view, 4> kOsc1WaveformChoices { "Triangle", "Sawtooth", "Pulse", "Sine" };
inline constexpr std::array<std::string_view, 4> kOsc2WaveformChoices { "Triangle", "Sawtooth", "Pulse", "Noise" };
inline constexpr std::array<std::string_view, 5> kLfo1WaveformChoices { "Soft Random", "Square", "Triangle",
                                                                        "Sawtooth", "Random" };
inline constexpr std::array<std::string_view, 5> kLfo1DestinationChoices { "FM", "Osc 1+2", "Osc 2", "Filter", "PW" };
// The LFO 2 destinations and the Arpeggio modes share one button and one byte (offset 65). The
// manual's order (LFO destinations, then Arp modes, "8 = off" [OFF-2X p.111]) is wrong. Each value
// sent as CC24 on Slot B's channel and read on the LEDs: 0–7 as below; 8 and 9 act as 7 (the synth
// clamps them), so there is no "off" value; and Slot B's dump held the same number at offset 65.
// Status: verified [HW 2026-11-02, step 9].
inline constexpr std::array<std::string_view, 8> kLfo2DestinationArpModeChoices {
    "Arp Down", "Arp Up", "Arp Up/Down", "LFO Amp", "LFO Osc 1+2", "Arp Random", "Arp Echo", "LFO Filter"
};
inline constexpr std::array<std::string_view, 4> kModEnvDestinationChoices { "None", "FM Amt", "PW", "Osc 2" };
inline constexpr std::array<std::string_view, 5> kModWheelDestinationChoices { "Morph", "LFO 1", "Osc 2", "FM",
                                                                               "Filter" };
inline constexpr std::array<std::string_view, 3> kVoiceModeChoices { "Poly", "Legato", "Mono" };
// Each value sent as CC44 and read on the LEDs. Status: verified [HW 2026-11-02, step 9].
inline constexpr std::array<std::string_view, 5> kFilterTypeChoices { "LP 12dB", "LP 24dB", "HP 24dB", "BP",
                                                                      "Notch+LP" };
inline constexpr std::array<std::string_view, 4> kFilterKeyboardTrackChoices { "Off", "1/3", "2/3", "Full" };
// CC17 sends 0–4 with 2 at the centre [HW 2026-10-29, step 9].
inline constexpr std::array<std::string_view, 5> kOctaveShiftChoices { "-2", "-1", "0", "+1", "+2" };

// The CC value sent for a control's value: 0–127 always (a CC data byte has 7 bits).
constexpr std::uint8_t ccDataFromValue(std::uint8_t cc, bool isSwitch, int value)
{
    if (isSwitch)
        return value != 0 ? kSwitchOnCcValue : std::uint8_t { 0 };
    if (cc == kRingModSyncCc)
        return static_cast<std::uint8_t>(value & kRingModSyncBits);
    return static_cast<std::uint8_t>(value < 0 ? 0 : value > 127 ? 127 : value);
}

// The control's value for a CC value received from the synth; empty when the value cannot be
// stored and is ignored.
constexpr std::optional<int> valueFromCcData(std::uint8_t cc, bool isSwitch, std::uint8_t data)
{
    if (isSwitch)
        return data >= kSwitchOnCcThreshold ? 1 : 0;
    if (cc == kRingModSyncCc && data > kRingModSyncBits)
        return std::nullopt;
    return data;
}

// ---------------------------------------------------------------------------------------------
// Morph Amounts

// Where a Morph Amount takes its parameter: the base value plus the amount, in the units of the
// knob's value, so a knob at 40 with a Morph Amount of +30 reaches 70 at full velocity or Mod
// Wheel. Facts: the 25 Morph Amounts are signed bytes, −128…127, 0 = off [OFF-2X p.111]; the
// manual says how far the parameter moves "away from its base value" increases with velocity or
// the Mod Wheel [OFF-2X p.26–27] but prints no scale. The editor's Morph ring draws the arc from
// the value to this target (plugin/ArcKnob.cpp); nothing else depends on it.
// Status: [INF], not verified on hardware. To check: set a knob near the middle, give it a large
// Morph Amount, and hear or read where full Mod Wheel takes it.
constexpr int morphTarget(int value, int morphAmount)
{
    return value + morphAmount;
}

// ---------------------------------------------------------------------------------------------
// The Connection check

// The C setting (hold SHIFT, press PROG/CTRL until the display shows C, UP/DOWN for on/oF)
// switches transmission *and* reception of CCs together; mod wheel and pedals are always
// transmitted [OFF-2X p.66–67, 77]. No request makes the 2X send its CCs (type 20 is a Bank
// request, kManualAllControllersRequestType), so the check asks the user to turn a knob to test
// CC transmission. Status: documented; type 20 refuted [HW 2026-10-29, step 1].

// Controllers sent and received whatever the C setting says: mod wheel (CC1), expression pedal
// (CC11) and sustain pedal (CC64) [OFF-2X p.74, 77, 104]. Their arrival proves nothing about C.
// Status: documented.
inline constexpr std::array<std::uint8_t, 3> kControllersIgnoringControlChangeSetting { 1, 11, 64 };

// The Connection check tests CC reception by sending Filter Cutoff to a Slot and reading the
// Slot's Edit Buffer back. Time allowed between the CC and the read, so that the synth has
// applied the value. A CC is 1 ms on the wire; the synth's own reaction time is unknown.
// Status: [INF], research §4.9.
inline constexpr int kControlChangeSettleMs = 50;

// That test assumes the Edit Buffer reply carries the CC's value unchanged at the parameter's
// offset (Filter Cutoff, CC74 = offset 3, both 0–127). The check only asks "did it change?", so
// a different scaling would not break it. Status: [INF] (research §2: CC/offset pairing).

// How the synth shows its Global Channel: SHIFT + MIDI CH, then MIDI CH again; the display shows
// the Global MIDI Channel [OFF-2X p.67], as "G.x" (research §4.4; the "G." prefix is not in the
// manual's text). Counting: kShownGlobalChannelOfDev0 above. Status: documented / [INF] prefix.

// ---------------------------------------------------------------------------------------------
// Timing

// Spacing between consecutive Program dumps written to Bank locations: the synth stores each one,
// so give it time. No official figure exists. Status: verified enough [HW 2026-11-02, step 11]: a
// whole Bank written 100 ms apart read back equal 99 of 99; shorter spacings were not tried for
// Bank writes.
inline constexpr int kBankWriteSpacingMs = 100;

// How long the editor waits for the answer to a request before giving up on it. A Kit reply is
// 1063 bytes, about 340 ms on the wire at 31 250 baud; a Program reply about 45 ms. No official
// figure for the synth's own reaction time exists. Probing all 16 Global Channels with no synth
// attached takes 16 times this. Status: [INF], research §4.9 (plan steps 1 and 10 measure it).
inline constexpr int kRequestReplyTimeoutMs = 1000;

// The smallest gap between the end of one request (answered or timed out) and the next one, so
// that the synth is never handed a request while still sending. Research §4.9 only says "wait for
// each reply before sending the next request". Status: [INF] (plan step 10).
inline constexpr int kRequestSpacingMs = 20;

// Edits of Performance settings are sent as whole Performance dumps (715 bytes, about 230 ms on
// the wire at 31 250 baud) to the Performance Edit Buffer. Rapid edits (a slider being dragged)
// are coalesced into at most one dump per this interval. Whether the synth applies a type-30 dump
// at once, and how long it needs to digest one, is unknown (research §7.3).
// Status: [INF]. The synth stores such a dump but does not apply it (kPerformanceEditBufferDumpIsApplied,
// [HW 2026-10-29, step 8]), so these edits do not reach the sound yet (#5 follow-up).
inline constexpr int kPerformanceDumpIntervalMs = 100;

// Parameters without a CC (the Morph Amounts, and the ring-mod bit if CC35 does not reach it) are
// sent as whole Program dumps (139 bytes, about 45 ms on the wire) to the Slot's Edit Buffer
// (type 0, spec = Slot). Rapid edits are coalesced into at most one dump per Slot per this
// interval, about 10 per second (spec #1, "Sync"): the first edit goes out at once, later ones
// within the interval are sent together when it has passed. A type-0 dump is applied at once and
// audibly [HW 2026-10-29, step 7]. Pacing: dumps 0, 10, 20 and 50 ms apart, each followed by a
// read-back, all read back right (step 10), and 600 dumps 50 ms apart while Live played through
// the same MIDI output caused no errors, stuck notes or wrong sounds (step 12)
// [HW 2026-11-02]. So 100 ms is conservative.
// Status: verified safe [HW 2026-11-02]; the synth is not the bottleneck.
inline constexpr int kProgramDumpIntervalMs = 100;

// ---------------------------------------------------------------------------------------------
// Project recall (#8)

// A Performance holds full copies of the four Programs [OFF-2X p.31–33], so a Performance dump to
// the Performance Edit Buffer (type 30) should by itself put the project's Programs into the
// Slots. But the synth stores a type-30 dump without applying it
// (kPerformanceEditBufferDumpIsApplied, [HW 2026-10-29, step 8]). So project recall also sends
// each Slot playing a Program its own Program dump (type 0) after the Performance dump, which the
// synth does apply (step 7): the Slots' sounds come back; the Performance settings do not (#5
// follow-up). Status: verified necessary [HW 2026-10-29].
inline constexpr bool kProjectRecallSendsSlotDumpsAfterPerformance = true;

// ---------------------------------------------------------------------------------------------
// Selecting stored Programs and Performances (#10)

// Program Change and Bank Select (CC32) are transmitted and received on a Slot's channel; the
// synth transmits them on the active Slot's channel [OFF-2X p.76, 104]. Both need the P setting
// (Shift + PROG/CTRL) on, which covers transmission and reception [OFF-2X p.66–67]. The editor
// cannot tell whether P is on: with P off a selection simply changes nothing.
// Status: transmission seen [HW 2026-10-29, step 5]: selecting Programs on the panel sent
// CC32 (B0 20 00) then Program Change on Slot A's channel, and the editor re-read the Slot and the
// Performance as #10 intends. Reception not tested.
inline constexpr std::uint8_t kBankSelectController = 32;

// Whether CC32 is governed by the P setting (Program Change) rather than by C (Control Change).
// The manual lists CC32 with the controllers [OFF-2X p.104] but describes Bank Select together
// with Program Change [OFF-2X p.76]. We assume P governs it, so the simulated synth takes CC32
// even with C off. Status: [INF] (plan: select a Bank from the editor with C off).
inline constexpr bool kBankSelectFollowsProgramChangeSetting = true;

// The CC32 value for a Program Bank: the Bank as the synth shows it, 0–9 [OFF-2X p.13 shows
// Banks 0–9; the value mapping is not printed]. Status: [INF].
inline constexpr int kBankSelectOfProgramBank0 = 0;
inline constexpr int kProgramBankCount = 10;
inline constexpr int kProgramsPerProgramBank = 99;

// The Program Change number of Program 1 of a Bank; Programs 1–99 = 0–98. Consistent with the
// Bank dump spec 0–98 [OFF-2X p.105] and the Performance's pgmselect 0–98 [OFF-2X p.112].
// Status: [INF].
inline constexpr int kProgramChangeOfProgram1 = 0;

// Percussion Kits P0–P9 sit "above" Program 99 in Banks 0–3 [OFF-2X p.13, 28], and their Bank
// locations are specs 99–108 [OFF-2X p.105–106]. So we assume Program Change 99–108 selects
// P0–P9 in the selected Bank. Status: [INF] (no source says how a Kit is selected by MIDI).
inline constexpr int kProgramChangeOfPercussionKitP0 = 99;
inline constexpr int kPercussionKitBankCount = 4;
inline constexpr int kPercussionKitsPerBank = 10;

// Performances: Banks 1–4 × 100, named A0–L9 without the letters I and K [OFF-2X p.16, 32].
// They are selected by Bank Select and Program Change on the Global Channel [OFF-2X p.67, 76];
// when the Global Channel equals a Slot Channel, the Performance selection wins [OFF-2X p.76].
// Assumed numbering: CC32 = Bank − 1 (0–3) and Program Change 0–99 in name order (A0 = 0,
// A9 = 9, B0 = 10, …, L9 = 99). Status: documented channel; [INF] numbering.
inline constexpr int kPerformanceBankCount = 4;
inline constexpr int kPerformancesPerBank = 100;
inline constexpr int kBankSelectOfPerformanceBank1 = 0;

// When the user turns the dial in Performance mode, the synth transmits CC32 and Program Change
// on the Global Channel, as it receives them [OFF-2X p.76 describes reception only].
// Status: [INF]; the simulated synth does so.

// A Program Change on a channel switches every Slot that receives on it: Slots sharing the
// channel [OFF-2X p.77, CCs], and Slots in a Layer on the leftmost active Slot's channel
// [OFF-2X p.76–77, notes]. The manual says this for notes and CCs; we assume Program Change
// follows the same reception rule. Status: [INF].
inline constexpr bool kProgramChangeReachesEverySlotReceivingOnItsChannel = true;

// The Performance's pgmselect and bankselect bytes (decoded offsets 328–335) name the stored
// Program each Slot was loaded from [OFF-2X p.112]: pgmselect 0–98 = Program 1–99, bankselect
// 0–3 (the manual's range; we accept 0–9 as a Bank shown the same way). We read pgmselect 99–108
// as Kits P0–P9, like Program Change. Status: [INF].

// Time between sending (or receiving) a selection and reading the Edit Buffers again, so that
// the synth has loaded the new Program or Performance. Bursts (CC32 then Program Change, or the
// dial turned quickly) within this time are coalesced into one re-read after the last message.
// No official figure exists. Status: [INF], research §4.9.
inline constexpr int kSelectionSettleMs = 150;

} // namespace nl2x::hardware
