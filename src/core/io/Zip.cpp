#include "core/io/Zip.h"

#include <array>
#include <cstdint>
#include <fstream>

namespace lcad {

namespace {

std::uint32_t crc32(const std::string& data) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void putU16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

} // namespace

bool writeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    std::string body;
    std::string central;
    // A fixed, valid MS-DOS date/time (1980-01-01 00:00:00) -- transmittal
    // contents don't need real timestamps to be useful.
    constexpr std::uint16_t kDosTime = 0;
    constexpr std::uint16_t kDosDate = 0x21;

    for (const auto& [name, content] : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(body.size());
        const std::uint32_t crc = crc32(content);
        const auto size = static_cast<std::uint32_t>(content.size());

        putU32(body, 0x04034b50);
        putU16(body, 20); // version needed
        putU16(body, 0);  // flags
        putU16(body, 0);  // method: store
        putU16(body, kDosTime);
        putU16(body, kDosDate);
        putU32(body, crc);
        putU32(body, size);
        putU32(body, size);
        putU16(body, static_cast<std::uint16_t>(name.size()));
        putU16(body, 0); // extra field length
        body += name;
        body += content;

        putU32(central, 0x02014b50);
        putU16(central, 20); // version made by
        putU16(central, 20); // version needed
        putU16(central, 0);  // flags
        putU16(central, 0);  // method: store
        putU16(central, kDosTime);
        putU16(central, kDosDate);
        putU32(central, crc);
        putU32(central, size);
        putU32(central, size);
        putU16(central, static_cast<std::uint16_t>(name.size()));
        putU16(central, 0); // extra field length
        putU16(central, 0); // comment length
        putU16(central, 0); // disk number start
        putU16(central, 0); // internal attrs
        putU32(central, 0); // external attrs
        putU32(central, offset);
        central += name;
    }

    std::string end;
    putU32(end, 0x06054b50);
    putU16(end, 0); // disk number
    putU16(end, 0); // disk with central directory
    putU16(end, static_cast<std::uint16_t>(entries.size()));
    putU16(end, static_cast<std::uint16_t>(entries.size()));
    putU32(end, static_cast<std::uint32_t>(central.size()));
    putU32(end, static_cast<std::uint32_t>(body.size()));
    putU16(end, 0); // comment length

    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.write(central.data(), static_cast<std::streamsize>(central.size()));
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
    return out.good();
}

} // namespace lcad
