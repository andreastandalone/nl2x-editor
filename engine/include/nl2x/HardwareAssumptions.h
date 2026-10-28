#pragma once

// Every guess about the real synth's behaviour lives here, and nowhere else (CODING_STANDARDS.md).
// Each entry names the research fact it rests on (docs/research/nl2x-midi-research.md) and its
// verification status. Hardware discovery (#3) changes this file; the engine and the simulated
// synth both follow it.
//
// Status words: "documented" = printed in the manual but not yet checked on hardware (the manual
// is known to be partly wrong, research §3); "secondary" = reported by other users of a 2X;
// "[INF]" = our own inference; "contradicted" = a source says the documented value is wrong.

#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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
// display presumably counts 1–16 (dev = shown value − 1).
// Status: secondary, not verified on this synth (plan step 1–2).
inline constexpr std::uint8_t kShownGlobalChannelOfDev0 = 1;

// Global Channel of a synth with factory settings: every factory Performance stores 15 at
// offset 315 (globalmidichan) and every factory file uses dev 0x0F [OFF-FB].
// Status: [INF] that a factory-reset synth uses it.
inline constexpr std::uint8_t kFactoryGlobalChannel = 15;

// ---------------------------------------------------------------------------------------------
// SysEx message types (the byte after the Model ID). Framing and sizes are verified against the
// factory files [OFF-FB] and live in SysEx.h; the meaning of each type code is not.

// Program dump into a Slot's Edit Buffer: type 0, spec 0–3 = Slot A–D [OFF-2X p.105].
// Status: documented. Whether it takes effect immediately and audibly is unverified (research §7.4,
// plan step 7).
inline constexpr std::uint8_t kProgramEditBufferDumpType = 0;

// Program dump into a Bank location: types 1–10 = "Bank 1–10", spec 0–98 = Program 1–99
// [OFF-2X p.105]. On receive the type byte is ignored and the Program is stored in the Bank
// currently selected on the synth [OFF-2X p.78]; the factory files use inconsistent type bytes
// [OFF-FB]. So the type byte is never a storage location (CODING_STANDARDS.md).
// Status: documented.
inline constexpr std::uint8_t kFirstProgramBankDumpType = 1;
inline constexpr std::uint8_t kLastProgramBankDumpType = 10;

// The manual's SysEx tables count Banks from 1 ("Bank 1–4" for the four RAM Banks); the synth's
// display counts Program Banks from 0, and Banks 0–3 are the RAM Banks [OFF-2X p.13]. So the
// manual's SysEx Bank 1 is presumably the Bank shown as 0, and SysEx Bank 4 the Bank shown as 3.
// Status: [INF], settled by plan step 5.
inline constexpr int kShownBankOfSysExBank1 = 0;

// Percussion Kit dumps: Edit Buffer spec 16–19 = Slot A–D; Bank locations 99–108 = P0–P9
// [OFF-2X p.105–106]. Status: documented; the Bank spec is consistent with [OFF-FB].
inline constexpr std::uint8_t kPercussionKitEditBufferSpecOfSlotA = 16;
inline constexpr std::uint8_t kFirstPercussionKitLocation = 99;

// The documented Program/Kit Edit Buffer request: type 10, spec = Slot [OFF-2X p.106].
// Status: contradicted, it gives no output on a 2X [SEC-1]; type 10 also collides with the
// "Bank 10" dump type (research §3).
inline constexpr std::uint8_t kDocumentedEditBufferRequestType = 10;

// The request that returns a Slot's Program Edit Buffer: type 14, spec 0–3 = Slot A–D.
// Reportedly it answers with an "empty" Program unless the active Slot plays a Program, and with a
// stored Percussion Kit when the active Slot plays a Kit [SEC-1].
// Status: secondary [SEC-1, SEC-2], not verified on this synth (plan step 3).
inline constexpr std::uint8_t kSlotEditBufferRequestType = 14;

// The reply to a Slot Edit Buffer request is assumed to be a Program dump with the Edit Buffer
// type and spec = Slot. Code must match a reply to its request by order and size, not by header.
// Status: [INF] (plan step 3 records the real header).
inline constexpr std::uint8_t kSlotEditBufferReplyType = kProgramEditBufferDumpType;

// How a reply to a Slot Edit Buffer request tells a Program from a Percussion Kit: by its size
// alone. 139 bytes (kProgramDumpSize) = the Slot plays a Program; 1063 bytes
// (kPercussionKitDumpSize) = the Slot plays a Percussion Kit, whatever the header says [SEC-1].
// Caveats [SEC-1]: the Kit may be the stored Kit rather than its Edit Buffer, and the reply may
// follow the *active* Slot rather than the requested one (see the next constant).
// Status: secondary, not verified on this synth (plan steps 3–4).
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
// with the active Slot's (false). [SEC-1] reports the reply depends on the active Slot: a Kit on
// the active Slot makes every request return that Kit, and a Program reply is "empty" unless the
// active Slot plays a Program. We assume the requested Slot answers, the only reading under
// which four separate reads are meaningful; if plan step 3 shows otherwise, reading the Slots
// needs the active Slot changed between requests, or the Performance dump instead (#5).
// Status: [INF], plan step 3 ("whether the reply follows the requested Slot or the active Slot").
inline constexpr bool kSlotEditBufferReplyFollowsRequestedSlot = true;

// Header of the Kit reply to a Slot Edit Buffer request: the Kit Edit Buffer dump of the manual,
// type 0, spec 16–19 = Slot A–D [OFF-2X p.105]. Only the simulated synth uses it: the engine
// matches replies by size. Status: [INF] (plan step 4 records the real header).
inline constexpr std::uint8_t kSlotEditBufferKitReplySpecOfSlotA = kPercussionKitEditBufferSpecOfSlotA;

// Bank location requests as printed: types 11–14 = Bank 1–4, spec 0–98 = Program, 99–108 = Kit
// [OFF-2X p.106]. Status: contradicted, type 14 is the Slot Edit Buffer request on a 2X [SEC-1].
inline constexpr std::uint8_t kPrintedFirstBankRequestType = 11;

// Candidate request types for Bank locations: as printed (11–14), or shifted like the 2X's
// extended dump types (15–24) [INF, research §3]. The probe scans this range (plan step 5).
inline constexpr std::uint8_t kFirstBankRequestTypeCandidate = 11;
inline constexpr std::uint8_t kLastBankRequestTypeCandidate = 24;

// Performance dumps: type 30 = Edit Buffer, 31–34 = Performance Bank 1–4, spec 0 or 0–99
// [OFF-2X p.107]. Status: documented; Bank dumps match [OFF-FB]; the Edit Buffer dump and whether
// it applies immediately are unverified (research §7.3, plan steps 6 and 8).
inline constexpr std::uint8_t kPerformanceEditBufferDumpType = 30;
inline constexpr std::uint8_t kFirstPerformanceBankDumpType = 31;

// Performance requests: type 40 = Edit Buffer, 41–44 = Performance Bank 1–4 [OFF-2X p.108; the
// body text says "41 or 42"]. Status: documented, not verified (plan step 6).
inline constexpr std::uint8_t kPerformanceEditBufferRequestType = 40;
inline constexpr std::uint8_t kFirstPerformanceBankRequestType = 41;

// All Controllers Request: type 20, spec = Slot; the synth answers with CCs for that Slot
// [OFF-2X p.109]. Status: secondary, works for Slot A with dev 0 on a 2X [SEC-3] (plan step 1).
inline constexpr std::uint8_t kAllControllersRequestType = 20;

// ---------------------------------------------------------------------------------------------
// The Connection check

// The C setting (hold SHIFT, press PROG/CTRL until the display shows C, UP/DOWN for on/oF)
// switches transmission *and* reception of CCs together; mod wheel and pedals are always
// transmitted [OFF-2X p.66–67, 77]. Whether C off also silences the CCs that answer an All
// Controllers Request is not documented. We assume it does, so no CC after the request points to
// C being off; the check then asks the user to turn a knob before concluding, so a wrong guess
// costs one extra step. Status: [INF] (plan step 1 with C off would settle it).
inline constexpr bool kAllControllersReplyNeedsControlChangeOn = true;

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
// so give it time. No official figure exists. Status: [INF], research §4.9 (plan steps 10–11).
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
// Status: [INF] (plan steps 6 and 8).
inline constexpr int kPerformanceDumpIntervalMs = 100;

} // namespace nl2x::hardware
