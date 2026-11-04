#include "nl2x/TagSuggestion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <variant>
#include <vector>

namespace nl2x {

namespace {

// ---------------------------------------------------------------------------------------------
// Names

// What a keyword says. The first kinds say what the sound is, and a name holds at most one of
// them (the last, see headKind); Voice is kept apart from Pad for "Lead Voice", which is a Lead.
// The kinds from Arpeggio on say how the sound plays, and every one found counts.
enum class SoundKind : std::uint8_t {
    Bass, Lead, Pad, Atmosphere, Voice, Strings, Brass, Woodwind, Keys, Organ, Bell, Pluck, Percussion, Fx,
    Arpeggio, Sequence, Sweep, Synth, Poly, Mono, Ambient,
};

bool saysHowItPlays(SoundKind kind)
{
    return kind >= SoundKind::Arpeggio;
}

std::vector<std::string_view> kindTags(SoundKind kind)
{
    switch (kind) {
    case SoundKind::Bass:
        return { "Bass" };
    case SoundKind::Lead:
        return { "Lead" };
    case SoundKind::Pad:
    case SoundKind::Voice:
        return { "Pad" };
    case SoundKind::Atmosphere:
        return { "Pad", "Ambient" };
    case SoundKind::Strings:
        return { "Strings" };
    case SoundKind::Brass:
        return { "Brass" };
    case SoundKind::Woodwind:
        // No default tag fits a flute or a clarinet: nothing rather than a wrong one.
        return {};
    case SoundKind::Keys:
        return { "Keys" };
    case SoundKind::Organ:
        return { "Keys", "Organ" };
    case SoundKind::Bell:
        return { "Bell" };
    case SoundKind::Pluck:
        return { "Pluck" };
    case SoundKind::Percussion:
        return { "Percussion" };
    case SoundKind::Fx:
        return { "FX" };
    case SoundKind::Arpeggio:
        return { "Arp" };
    case SoundKind::Sequence:
        return { "Rhythmic" };
    case SoundKind::Sweep:
        return { "Sweep" };
    case SoundKind::Synth:
        return { "Synth" };
    case SoundKind::Poly:
        return { "Poly" };
    case SoundKind::Mono:
        return { "Mono" };
    case SoundKind::Ambient:
        return { "Ambient" };
    }
    return {};
}

// A keyword is found anywhere in the upper-case name ("SAWBASS" holds BASS) unless it must start
// or end a word, and not where it continues with one of `notFollowedBy` ("BASSOON" is no Bass).
struct Keyword {
    std::string_view text;
    SoundKind kind;
    bool startsWord = false;
    bool endsWord = false;
    std::array<std::string_view, 4> notFollowedBy {};
};

constexpr bool kWord = true;

// Generic English words for sounds and instruments, as musicians name synth sounds.
const std::vector<Keyword>& keywords()
{
    using K = SoundKind;
    static const std::vector<Keyword> list {
        { "BASS", K::Bass, false, false, { "OON" } },
        { "303", K::Bass, kWord, kWord },
        { "TB", K::Bass, kWord, kWord },
        { "TAURUS", K::Bass },
        { "SUB", K::Bass, kWord, kWord, { "TONE", " TONE" } },

        { "LEAD", K::Lead },
        { "SOLO", K::Lead, kWord, kWord },
        { "MELODY", K::Lead },

        { "PAD", K::Pad },
        { "SWELL", K::Pad },
        { "ATMOS", K::Atmosphere },

        { "VOICE", K::Voice },
        { "VOCAL", K::Voice },
        { "CHOIR", K::Voice },
        { "VOX", K::Voice, kWord },
        { "AAH", K::Voice, kWord },
        { "OOH", K::Voice, kWord },

        { "STRING", K::Strings },
        { "VIOLIN", K::Strings },
        { "CELLO", K::Strings },
        { "VIOLA", K::Strings },
        { "ORCHEST", K::Strings },
        { "ENSEMB", K::Strings },
        { "ARCO", K::Strings, kWord, kWord },

        { "BRASS", K::Brass },
        { "HORN", K::Brass },
        { "TRUMPET", K::Brass },
        { "TROMBONE", K::Brass },
        { "TUBA", K::Brass, false, kWord },
        { "SAX", K::Brass, kWord },
        { "SAXOPHON", K::Brass },

        { "FLUTE", K::Woodwind },
        { "CLARINET", K::Woodwind },
        { "OBOE", K::Woodwind, kWord },
        { "BASSOON", K::Woodwind },
        { "PIPE", K::Woodwind, kWord, kWord },
        { "PIPES", K::Woodwind, kWord, kWord },
        { "RECORDER", K::Woodwind },
        { "WHISTLE", K::Woodwind },
        { "OCARINA", K::Woodwind },
        { "HARMONICA", K::Woodwind },
        { "DIDGERI", K::Woodwind },
        { "DIDJ", K::Woodwind },

        { "PIANO", K::Keys },
        { "EPIANO", K::Keys },
        { "E-PIANO", K::Keys },
        { "EP", K::Keys, kWord, kWord },
        { "RHODES", K::Keys },
        { "WURLI", K::Keys },
        { "CLAV", K::Keys },
        { "HARPSI", K::Keys },
        { "HARPSICHORD", K::Keys },
        { "KEYS", K::Keys, kWord, kWord },
        { "GRAND", K::Keys, false, kWord },
        { "ACCORDI", K::Keys },
        { "DX7", K::Keys, kWord, kWord },
        { "DX 7", K::Keys, kWord, kWord },
        { "MARIMBA", K::Keys },
        { "VIBES", K::Keys, kWord, kWord },
        { "VIBRAPHON", K::Keys },
        { "XYLO", K::Keys },
        { "KALIMBA", K::Keys },
        { "STEEL DRUM", K::Keys },
        { "STEELDRUM", K::Keys },

        { "ORGAN", K::Organ },
        { "FARFISA", K::Organ },
        { "DRAWBAR", K::Organ },
        { "HAMMOND", K::Organ },
        { "B3", K::Organ, kWord, kWord },

        { "BELL", K::Bell, false, false, { "Y" } },
        { "CHIME", K::Bell },
        { "GLOCK", K::Bell },
        { "CELESTA", K::Bell },
        { "CELESTE", K::Bell },
        { "TUBULAR", K::Bell },
        { "MUSIC BOX", K::Bell },
        { "MUSICBOX", K::Bell },

        { "PLUCK", K::Pluck },
        { "GUITAR", K::Pluck },
        { "GTR", K::Pluck, kWord, kWord },
        { "KOTO", K::Pluck },
        { "SITAR", K::Pluck },
        { "HARP", K::Pluck, false, false, { "SI" } },
        { "BANJO", K::Pluck },
        { "PIZZ", K::Pluck },
        { "MANDOLIN", K::Pluck },
        { "DULCIMER", K::Pluck },
        { "PICK", K::Pluck, kWord, kWord },

        { "DRUM", K::Percussion, false, false, { "BASS", " BASS", "SBASS", "S BASS" } },
        { "KICK", K::Percussion },
        { "SNARE", K::Percussion },
        { "HIHAT", K::Percussion },
        { "HI-HAT", K::Percussion },
        { "HAT", K::Percussion, kWord, kWord },
        { "TOM", K::Percussion, kWord, kWord },
        { "TOMS", K::Percussion, kWord, kWord },
        { "CLAP", K::Percussion, kWord },
        { "PERCUSS", K::Percussion },
        { "PERC", K::Percussion, kWord, kWord },
        { "COWBELL", K::Percussion },
        { "CONGA", K::Percussion },
        { "BONGO", K::Percussion },
        { "WOODBLOCK", K::Percussion },
        { "TIMPANI", K::Percussion },
        { "CYMBAL", K::Percussion },
        { "RIMSHOT", K::Percussion },

        { "FX", K::Fx, kWord, kWord },
        { "EFFECT", K::Fx },
        { "NOISE", K::Fx },
        { "WIND", K::Fx, kWord, kWord },
        { "RAIN", K::Fx, kWord, kWord },
        { "BIRD", K::Fx, kWord },
        { "LASER", K::Fx },
        { "ALARM", K::Fx },
        { "SIREN", K::Fx },
        { "ZAP", K::Fx, kWord },
        { "RISER", K::Fx },
        { "HELICOPTER", K::Fx },
        { "WATER DROP", K::Fx },
        { "WATERDROP", K::Fx },
        { "GEIGER", K::Fx },
        // Only as a word: inside "Saxophone" or "Vibraphone" it is no telephone.
        { "PHONE", K::Fx, kWord },
        { "TELEPHONE", K::Fx },
        { "UFO", K::Fx, kWord },
        { "ROBOT", K::Fx },
        { "EXPLOSION", K::Fx },
        { "THUNDER", K::Fx },
        { "OCEAN", K::Fx },
        { "SCI-FI", K::Fx },
        { "SCIFI", K::Fx },
        { "DRONE", K::Fx },
        { "SPACESHIP", K::Fx },
        { "BUBBLE", K::Fx },

        { "ARPEGG", K::Arpeggio },
        { "ARP", K::Arpeggio, kWord, kWord },
        { "ARPS", K::Arpeggio, kWord, kWord },
        { "SEQ", K::Sequence, kWord, kWord },
        { "SEQUENC", K::Sequence },
        { "GATE", K::Sequence, kWord, kWord },
        { "GATED", K::Sequence, kWord, kWord },
        { "SWEEP", K::Sweep },
        { "SYNTH", K::Synth },
        { "POLY", K::Poly, kWord },
        { "MONO", K::Mono, kWord },
        { "AMBIEN", K::Ambient },
    };
    return list;
}

// Letters and digits make words; any byte of a UTF-8 character counts as a letter.
bool isWordCharacter(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    return (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte >= 0x80;
}

struct Found {
    std::size_t start;
    std::size_t length;
    SoundKind kind;
};

std::vector<Found> findKeywords(const std::string& upper)
{
    std::vector<Found> found;
    for (const auto& keyword : keywords()) {
        for (auto at = upper.find(keyword.text); at != std::string::npos; at = upper.find(keyword.text, at + 1)) {
            const auto end = at + keyword.text.size();
            if (keyword.startsWord && at > 0 && isWordCharacter(upper[at - 1]))
                continue;
            if (keyword.endsWord && end < upper.size() && isWordCharacter(upper[end]))
                continue;
            const auto rest = std::string_view(upper).substr(end);
            if (std::any_of(keyword.notFollowedBy.begin(), keyword.notFollowedBy.end(),
                            [&](std::string_view after) { return ! after.empty() && rest.starts_with(after); }))
                continue;
            found.push_back({ at, keyword.text.size(), keyword.kind });
        }
    }
    return found;
}

// The kind of sound among `found` (keywords that say what a sound is).
std::optional<SoundKind> headKind(const std::vector<Found>& found)
{
    if (found.empty())
        return std::nullopt;
    // The last keyword names the sound; at the same place the longer one ("COWBELL", not "BELL").
    const auto head = *std::max_element(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.start != b.start ? a.start < b.start : a.length < b.length;
    });
    // A keyword inside a longer one that starts earlier is that longer one.
    for (const auto& outer : found)
        if (outer.start < head.start && outer.start + outer.length >= head.start + head.length && outer.length > head.length)
            return outer.kind;
    return head.kind;
}

void addOnce(std::vector<std::string>& tags, std::string_view tag)
{
    if (std::find(tags.begin(), tags.end(), tag) == tags.end())
        tags.emplace_back(tag);
}

// ---------------------------------------------------------------------------------------------
// Parameters

// Every kind of sound the rules score, so that Bass and Keys are suggested only when they clearly
// beat the others. Lead, Pad, Brass/Strings, FX, Percussion and arpeggios are never suggested.
enum class Scored : std::uint8_t { Bass, Lead, Pad, Keys, BrassStrings, Fx, Percussion, Arpeggio, Count };

// A suggestion needs at least this confidence (0 … 1, see confidence()). Chosen offline
// (2026-11-03) for at least 80 % right suggestions per kind; see TagSuggestion.h.
constexpr double kMinParameterConfidence = 0.7;

// The rules' reading of a Program: raw stored values, plus a few derived ones.
struct Reading {
    int attack, decay, sustain, release;  // Amp Env
    int filterAttack;
    int cutoff, resonance, filterEnvAmount;
    int fm, osc2Semitones;
    int portamento, gain;
    int modEnvAttack, modEnvAmount;
    int lfo1Amount, lfo2Amount;
    int filterKeyboardTrack;
    int octaveShift;  // -2 … +2
    double noiseLevel;  // 0 … 1: how much of the mix is Osc 2 set to noise
    bool mono, unison, arpeggiator, modEnvOn, sync, highOrBandPass;
};

Reading read(const Program& program)
{
    using P = ProgramParameter;
    const auto v = [&](P parameter) { return program.value(parameter); };
    Reading r {};
    r.attack = v(P::AmpEnvAttack);
    r.decay = v(P::AmpEnvDecay);
    r.sustain = v(P::AmpEnvSustain);
    r.release = v(P::AmpEnvRelease);
    r.filterAttack = v(P::FilterEnvAttack);
    r.cutoff = v(P::FilterCutoff);
    r.resonance = v(P::FilterResonance);
    r.filterEnvAmount = v(P::FilterEnvAmount);
    r.fm = v(P::FmDepth);
    r.osc2Semitones = std::abs(v(P::Osc2Semitones) - 60);
    r.portamento = v(P::PortamentoTime);
    r.gain = v(P::Gain);
    r.modEnvAttack = v(P::ModEnvAttack);
    r.modEnvAmount = std::abs(v(P::ModEnvAmount) - 64);
    r.lfo1Amount = v(P::Lfo1Amount);
    r.lfo2Amount = v(P::Lfo2Amount);
    r.filterKeyboardTrack = v(P::FilterKeyboardTrack);
    r.octaveShift = v(P::OctaveShift) - 2;
    // Osc 2's waveform 3 is noise; Osc Mix moves from Osc 1 (0) to Osc 2 (127).
    r.noiseLevel = v(P::Osc2Waveform) == 3 ? v(P::OscMix) / 127.0 : 0.0;
    // Stored values as measured on real sounds, not kVoiceModeChoices (whose order is unverified,
    // #52): pads, keys and strings almost all hold 2, basses and leads 0 or 1.
    r.mono = v(P::VoiceMode) == 0 || v(P::VoiceMode) == 1;
    r.unison = v(P::Unison) != 0;
    // kLfo2DestinationArpModeChoices: Arp Down, Up, Up/Down, Random, Echo.
    const auto lfo2 = v(P::Lfo2DestinationArpMode);
    r.arpeggiator = lfo2 == 0 || lfo2 == 1 || lfo2 == 2 || lfo2 == 5 || lfo2 == 6;
    const auto modEnvDestination = v(P::ModEnvDestination);
    r.modEnvOn = modEnvDestination >= 1 && modEnvDestination <= 3 && r.modEnvAmount > 4;
    r.sync = (v(P::SyncRingModDistortion) & 1) != 0;
    r.highOrBandPass = v(P::FilterType) == 2 || v(P::FilterType) == 3;
    return r;
}

// Points per kind of sound, in half points.
using Scores = std::array<int, static_cast<std::size_t>(Scored::Count)>;

Scores score(const Reading& r)
{
    Scores points {};
    const auto add = [&](Scored kind, int halfPoints) { points[static_cast<std::size_t>(kind)] += halfPoints; };
    const bool held = r.sustain >= 80;
    const bool fast = r.attack <= 12;
    const bool poly = ! r.mono;
    const bool shortHit = r.sustain <= 12 && r.decay <= 60;

    // Percussion: no sustain and a short decay, often noise or far-off pitches.
    if (shortHit && fast) {
        add(Scored::Percussion, 6);
        if (r.noiseLevel > 0.3)
            add(Scored::Percussion, 4);
        if (r.fm > 40 || r.osc2Semitones > 20)
            add(Scored::Percussion, 2);
        if (r.gain > 100)
            add(Scored::Percussion, 2);
    }

    // Bass: Octave Shift down is the strongest single clue.
    if (r.octaveShift <= -1)
        add(Scored::Bass, 6);
    if (r.mono && r.octaveShift <= 0)
        add(Scored::Bass, 2);
    if (fast && r.cutoff < 40 && r.filterEnvAmount > 40)
        add(Scored::Bass, 2);

    // Lead: mono, not shifted down, portamento, held notes.
    if (r.mono && r.octaveShift >= 0)
        add(Scored::Lead, 4);
    if (r.portamento > 0 && r.octaveShift >= 0)
        add(Scored::Lead, 2);
    if (held && r.octaveShift >= 0 && r.release < 60 && fast)
        add(Scored::Lead, 2);
    if (r.filterKeyboardTrack <= 1 && r.mono)
        add(Scored::Lead, 1);

    // Pad: slow attack or long release on a held note.
    if (r.attack >= 50)
        add(Scored::Pad, 6);
    else if (r.attack >= 25)
        add(Scored::Pad, 2);
    if (r.release >= 75 && poly && held)
        add(Scored::Pad, 4);
    if (held && poly && r.lfo1Amount > 50)
        add(Scored::Pad, 2);
    if (r.unison && poly && held)
        add(Scored::Pad, 1);

    // Keys (pianos, clavs, organs, bells, plucks): poly, fast attack, mostly decaying.
    if (poly && fast && r.sustain < 70) {
        add(Scored::Keys, 6);
        if (r.sustain < 30)
            add(Scored::Keys, 2);
    }
    if (poly && fast && held && r.filterEnvAmount < 30 && r.release < 60)
        add(Scored::Keys, 4);
    if (poly && r.octaveShift >= 1)
        add(Scored::Keys, 2);
    if (r.fm > 30 && poly && fast)
        add(Scored::Keys, 1);

    // Brass/Strings: held, a filter envelope with some attack, a moderate amp attack and release.
    if (poly && held && r.filterAttack >= 10 && r.filterAttack <= 90)
        add(Scored::BrassStrings, 4);
    if (poly && held && r.attack >= 8 && r.attack < 60)
        add(Scored::BrassStrings, 4);
    if (poly && held && r.release >= 25 && r.release < 75)
        add(Scored::BrassStrings, 2);

    // FX: noise, strong modulation, resonance, sync sweeps.
    if (r.noiseLevel > 0.3 && ! shortHit)
        add(Scored::Fx, 4);
    if (r.lfo2Amount > 40 && ! r.arpeggiator)
        add(Scored::Fx, 3);
    if (r.modEnvAttack > 40 && r.modEnvOn)
        add(Scored::Fx, 2);
    if (r.resonance > 70)
        add(Scored::Fx, 2);
    if (r.sync && r.modEnvAmount > 30)
        add(Scored::Fx, 1);
    if (r.highOrBandPass && r.resonance > 40)
        add(Scored::Fx, 1);

    // Arpeggios: the arpeggiator is on.
    if (r.arpeggiator) {
        add(Scored::Arpeggio, 4);
        if (r.lfo2Amount > 20)
            add(Scored::Arpeggio, 3);
    }
    return points;
}

// ---------------------------------------------------------------------------------------------
// Learning from the user's tags

constexpr std::size_t kFeatureCount = 25;

// What the neighbours are compared on: the rules' reading, every value scaled to 0 … 1 (Octave
// Shift to -1 … +1) so that no parameter outweighs another by its range alone, then weighted by
// how much it tells kinds of sound apart (the weight multiplies the squared difference). Chosen
// offline (2026-11-04) with the measurements in TagSuggestion.h: the amp envelope's attack,
// sustain and release, Octave Shift and mono/poly count most, as they do in the rules.
struct Feature {
    float value;
    float weight;
};

std::array<float, kFeatureCount> featuresOf(const Reading& r)
{
    const auto unit = [](int value, double range) {
        return static_cast<float>(std::clamp(static_cast<double>(value) / range, 0.0, 1.0));
    };
    const auto flag = [](bool on) { return on ? 1.0f : 0.0f; };
    const std::array<Feature, kFeatureCount> scaled {{
        { unit(r.attack, 127.0), 3.0f },
        { unit(r.decay, 127.0), 1.0f },
        { unit(r.sustain, 127.0), 3.0f },
        { unit(r.release, 127.0), 3.0f },
        { unit(r.filterAttack, 127.0), 1.0f },
        { unit(r.cutoff, 127.0), 1.0f },
        { unit(r.resonance, 127.0), 0.5f },
        { unit(r.filterEnvAmount, 127.0), 1.0f },
        { unit(r.fm, 127.0), 0.5f },
        { unit(r.osc2Semitones, 60.0), 0.5f },
        { unit(r.portamento, 127.0), 1.0f },
        { unit(r.gain, 127.0), 0.5f },
        { unit(r.modEnvAttack, 127.0), 0.5f },
        { unit(r.modEnvAmount, 64.0), 0.5f },
        { unit(r.lfo1Amount, 127.0), 0.5f },
        { unit(r.lfo2Amount, 127.0), 0.5f },
        { unit(r.filterKeyboardTrack, 3.0), 0.5f },
        { static_cast<float>(std::clamp(r.octaveShift, -2, 2)) / 2.0f, 3.0f },
        { static_cast<float>(r.noiseLevel), 1.0f },
        { flag(r.mono), 3.0f },
        { flag(r.unison), 0.5f },
        { flag(r.arpeggiator), 1.0f },
        { flag(r.modEnvOn), 0.5f },
        { flag(r.sync), 0.5f },
        { flag(r.highOrBandPass), 0.5f },
    }};
    // Each value times the square root of its weight: a plain squared distance is then weighted.
    std::array<float, kFeatureCount> features {};
    for (std::size_t i = 0; i < kFeatureCount; ++i)
        features[i] = scaled[i].value * std::sqrt(scaled[i].weight);
    return features;
}

// How sure the best kind is: how far it leads the second (60 %) and how many points it has
// (40 %, full at 4 points). A tie gives at most 0.4.
double confidence(int best, int second)
{
    const double share = static_cast<double>(best) / static_cast<double>(best + second);
    const double strength = std::min(1.0, static_cast<double>(best) / 8.0);
    return (share - 0.5) * 2.0 * 0.6 + strength * 0.4;
}

} // namespace

std::vector<std::string> tagsFromName(std::string_view name)
{
    std::string upper(name);
    for (auto& c : upper) {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        else if (c == '_')
            c = ' ';
    }
    const auto all = findKeywords(upper);
    std::vector<Found> kinds;
    std::vector<Found> ways;
    for (const auto& each : all)
        (saysHowItPlays(each.kind) ? ways : kinds).push_back(each);

    std::vector<std::string> tags;
    if (const auto head = headKind(kinds)) {
        auto kind = *head;
        // "Lead Voice" is a Lead with a vocal colour, not a choir.
        if (kind == SoundKind::Voice
            && std::any_of(kinds.begin(), kinds.end(), [](const Found& each) { return each.kind == SoundKind::Lead; }))
            kind = SoundKind::Lead;
        // "Bass/Lead": two kinds of sound joined, so neither is the sound.
        const bool joined = upper.find('/') != std::string::npos
                            && std::any_of(kinds.begin(), kinds.end(), [&](const Found& each) {
                                   return kindTags(each.kind) != kindTags(kinds.front().kind);
                               });
        if (! joined)
            for (const auto tag : kindTags(kind))
                addOnce(tags, tag);
    }
    // How it plays, in the order of the name.
    std::sort(ways.begin(), ways.end(), [](const Found& a, const Found& b) { return a.start < b.start; });
    for (const auto& way : ways)
        for (const auto tag : kindTags(way.kind))
            addOnce(tags, tag);
    return tags;
}

std::vector<std::string> tagsFromParameters(const Program& program)
{
    const auto points = score(read(program));
    std::array<std::size_t, static_cast<std::size_t>(Scored::Count)> order {};
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return points[a] > points[b]; });
    const auto best = points[order[0]];
    if (best <= 0 || confidence(best, points[order[1]]) < kMinParameterConfidence)
        return {};
    switch (static_cast<Scored>(order[0])) {
    case Scored::Bass:
        return { "Bass" };
    case Scored::Keys:
        return { "Keys" };
    default:
        return {};
    }
}

LearnedTags::LearnedTags(const std::vector<Example>& examples)
{
    for (const auto& example : examples) {
        std::vector<std::size_t> exampleLabels;
        for (const auto& tag : example.tags) {
            if (tag.empty())
                continue;
            const auto known = std::find(tags.begin(), tags.end(), tag);
            const auto label = static_cast<std::size_t>(known - tags.begin());
            if (known == tags.end())
                tags.push_back(tag);
            if (std::find(exampleLabels.begin(), exampleLabels.end(), label) == exampleLabels.end())
                exampleLabels.push_back(label);
        }
        if (exampleLabels.empty())
            continue;
        labels.push_back(std::move(exampleLabels));
        const auto each = featuresOf(read(example.program));
        features.insert(features.end(), each.begin(), each.end());
    }
}

LearnedTags LearnedTags::of(const LibraryMetadata& metadata)
{
    std::vector<Example> examples;
    for (const auto& [id, sound] : metadata.taggedSounds()) {
        if (sound.fingerprint.kind != LibraryEntryKind::Program)
            continue;
        auto own = sound.info.ownTags();
        if (own.empty())
            continue;
        std::vector<std::int8_t> values(sound.fingerprint.values.size());
        std::transform(sound.fingerprint.values.begin(), sound.fingerprint.values.end(), values.begin(),
                       [](std::uint8_t value) { return static_cast<std::int8_t>(value); });
        if (auto program = Program::fromValues(values))
            examples.push_back({ *program, std::move(own) });
    }
    return LearnedTags(examples);
}

std::vector<std::string> LearnedTags::tagsOf(const Program& program) const
{
    constexpr auto neighbours = static_cast<std::size_t>(kLearnedNeighbours);
    const auto count = labels.size();
    if (count < neighbours)
        return {};
    const auto query = featuresOf(read(program));
    // The closest examples as (squared distance, example), nearest first.
    std::vector<std::pair<float, std::size_t>> nearest;
    nearest.reserve(neighbours + 1);
    for (std::size_t example = 0; example < count; ++example) {
        const float* point = &features[example * kFeatureCount];
        float distance = 0.0f;
        for (std::size_t i = 0; i < kFeatureCount; ++i) {
            const float difference = point[i] - query[i];
            distance += difference * difference;
        }
        if (nearest.size() == neighbours && distance >= nearest.back().first)
            continue;
        const auto at = std::upper_bound(nearest.begin(), nearest.end(), distance,
                                         [](float value, const auto& other) { return value < other.first; });
        nearest.insert(at, { distance, example });
        if (nearest.size() > neighbours)
            nearest.pop_back();
    }
    if (std::sqrt(nearest.front().first) > kMaxLearnedDistance)
        return {};
    // Each tag against the rest: how many of the neighbours carry it.
    std::vector<int> votes(tags.size(), 0);
    for (const auto& [distance, example] : nearest)
        for (const auto label : labels[example])
            ++votes[label];
    std::vector<std::size_t> agreed;
    for (std::size_t label = 0; label < votes.size(); ++label)
        if (votes[label] >= kMinLearnedAgreement)
            agreed.push_back(label);
    std::stable_sort(agreed.begin(), agreed.end(), [&](std::size_t a, std::size_t b) { return votes[a] > votes[b]; });
    std::vector<std::string> result;
    for (const auto label : agreed)
        result.push_back(tags[label]);
    return result;
}

namespace {

void addSuggestions(std::vector<TagSuggestion>& suggestions, const std::vector<std::string>& tags,
                    TagSuggestionSource source)
{
    for (const auto& tag : tags)
        if (std::none_of(suggestions.begin(), suggestions.end(), [&](const auto& each) { return each.tag == tag; }))
            suggestions.push_back({ tag, source });
}

} // namespace

std::vector<TagSuggestion> suggestTags(const LibraryFile& file, const LibraryEntry& entry, const LearnedTags& learned)
{
    switch (entry.kind()) {
    case LibraryEntryKind::PercussionKit:
        return { { "Percussion", TagSuggestionSource::PercussionKit } };
    case LibraryEntryKind::Performance:
        return {};
    case LibraryEntryKind::Program:
        break;
    }
    std::vector<TagSuggestion> suggestions;
    if (file.entries.size() == 1)
        addSuggestions(suggestions, tagsFromName(entry.name), TagSuggestionSource::Name);
    const auto& program = std::get<ProgramDump>(entry.dump).program;
    addSuggestions(suggestions, learned.tagsOf(program), TagSuggestionSource::Learned);
    if (suggestions.empty())
        addSuggestions(suggestions, tagsFromParameters(program), TagSuggestionSource::Parameters);
    return suggestions;
}

std::vector<TagSuggestion> suggestTags(const Program& program, const LearnedTags& learned)
{
    std::vector<TagSuggestion> suggestions;
    addSuggestions(suggestions, learned.tagsOf(program), TagSuggestionSource::Learned);
    if (suggestions.empty())
        addSuggestions(suggestions, tagsFromParameters(program), TagSuggestionSource::Parameters);
    return suggestions;
}

} // namespace nl2x
