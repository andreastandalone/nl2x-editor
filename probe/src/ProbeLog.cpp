#include "nl2x/probe/ProbeLog.h"

#include <cctype>
#include <cstdio>

namespace nl2x::probe {

namespace {

std::string_view directionWord(Direction direction)
{
    switch (direction) {
    case Direction::In: return "IN  ";
    case Direction::Out: return "OUT ";
    case Direction::Note: return "NOTE";
    }
    return "?   ";
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Size of a channel message from its status byte.
std::size_t channelMessageSize(std::uint8_t status)
{
    const auto kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 2 : 3;
}

} // namespace

std::string toHex(std::span<const std::uint8_t> bytes)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * 3);
    for (auto byte : bytes) {
        if (! text.empty())
            text.push_back(' ');
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0F]);
    }
    return text;
}

RawMessage parseRawMessage(std::string_view text)
{
    RawMessage result;
    std::string digits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
            continue;
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;
            continue;
        }
        if (hexDigit(c) < 0) {
            result.error = "Not a hex digit: '" + std::string(1, c) + "'";
            return result;
        }
        digits.push_back(c);
    }

    if (digits.empty()) {
        result.error = "Nothing to send";
        return result;
    }
    if (digits.size() % 2 != 0) {
        result.error = "Odd number of hex digits";
        return result;
    }
    for (std::size_t i = 0; i < digits.size(); i += 2)
        result.bytes.push_back(static_cast<std::uint8_t>(hexDigit(digits[i]) * 16 + hexDigit(digits[i + 1])));

    const auto& bytes = result.bytes;
    const auto status = bytes.front();
    const auto dataBytesValid = [&](std::size_t first, std::size_t end) {
        for (auto i = first; i < end; ++i)
            if (bytes[i] >= 0x80)
                return false;
        return true;
    };

    if (status == 0xF0) {
        if (bytes.size() < 2 || bytes.back() != 0xF7)
            result.error = "A SysEx message must end with F7";
        else if (! dataBytesValid(1, bytes.size() - 1))
            result.error = "A SysEx message may contain only bytes below 80 between F0 and F7";
    } else if (status < 0x80 || status >= 0xF0) {
        result.error = "The first byte must be a channel message status (80–EF) or F0";
    } else if (bytes.size() != channelMessageSize(status)) {
        result.error = "This message needs " + std::to_string(channelMessageSize(status)) + " bytes";
    } else if (! dataBytesValid(1, bytes.size())) {
        result.error = "Data bytes must be below 80";
    }

    if (! result.error.empty())
        result.bytes.clear();
    return result;
}

void ProbeLog::clear(double nowMs)
{
    originMs = nowMs;
    entries.clear();
}

const std::string& ProbeLog::addMidi(double nowMs, Direction direction, std::span<const std::uint8_t> bytes)
{
    return add(nowMs, direction, toHex(bytes));
}

const std::string& ProbeLog::addNote(double nowMs, std::string_view text)
{
    return add(nowMs, Direction::Note, text);
}

const std::string& ProbeLog::add(double nowMs, Direction direction, std::string_view payload)
{
    char time[32];
    std::snprintf(time, sizeof time, "+%.1f ms", nowMs - originMs);
    std::string line = time;
    line += "  ";
    line += directionWord(direction);
    line += "  ";
    line += payload;
    entries.push_back(std::move(line));
    return entries.back();
}

std::string ProbeLog::text() const
{
    std::string all;
    for (const auto& line : entries) {
        all += line;
        all += '\n';
    }
    return all;
}

} // namespace nl2x::probe
