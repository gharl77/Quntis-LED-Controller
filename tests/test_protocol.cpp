// Checks the packet code against packets captured from the real remote on 2026-10-31.
// Build and run: g++ -std=c++17 -I components tests/test_protocol.cpp -o test_protocol && ./test_protocol

#include <cstdio>
#include <cstring>

#include "quntis_0213/protocol.h"

using namespace esphome::quntis_0213::protocol;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void parse_hex(const char* hex, uint8_t* out, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned value;
        std::sscanf(hex + 2 * i, "%2x", &value);
        out[i] = (uint8_t) value;
    }
}

int main() {
    // Raw bytes after the sync, as captured. The 20th byte is noise after the trailer.
    const char* captured[] = {"042914ab2f7d134085221988ed6945b7fcacafda", "042914ab2f7d134085228c881269a54924c9afd0"};
    const uint8_t remote_id[4] = {0x83, 0x6F, 0x64, 0x11};

    for (const char* hex : captured) {
        uint8_t packet[PACKET_LENGTH];
        parse_hex(hex, packet, PACKET_LENGTH);
        uint8_t data[DATA_LENGTH];
        check(decode(packet, data), "a captured packet decodes with a valid CRC, trailer and sum");
        check(std::memcmp(data, remote_id, 4) == 0, "the remote ID is 83 6F 64 11");

        uint8_t rebuilt[PACKET_LENGTH];
        encode(packet, data, rebuilt);
        check(std::memcmp(rebuilt, packet, PACKET_LENGTH) == 0, "re-encoding gives the captured bytes");

        uint8_t corrupted[PACKET_LENGTH];
        std::memcpy(corrupted, packet, PACKET_LENGTH);
        corrupted[8] ^= 0x01;
        check(!decode(corrupted, data), "a changed bit is rejected");
    }

    const uint8_t address[5] = {0x04, 0x29, 0x14, 0xAB, 0x2F};
    const uint8_t data[10] = {0x83, 0x6F, 0x64, 0x11, 0x35, 0x80, 0x47, 0xFF, 0x00, 0x2A};
    uint8_t packet[PACKET_LENGTH];
    encode(address, data, packet);
    uint8_t decoded[DATA_LENGTH];
    check(decode(packet, decoded) && std::memcmp(decoded, data, 10) == 0, "an encoded packet decodes back");

    check(kelvin_to_byte(3000) == 0x00 && kelvin_to_byte(4000) == 0x47 && kelvin_to_byte(5000) == 0xA2 &&
              kelvin_to_byte(6500) == 0xFF,
          "the remote's color temperature presets map to 00, 47, A2 and FF");
    bool round_trip = true;
    for (int value = 0; value <= 255; value++) {
        round_trip &= kelvin_to_byte(byte_to_kelvin((uint8_t) value)) == value;
    }
    check(round_trip, "every color temperature byte survives a round trip through kelvin");
    check(kelvin_to_byte(2000) == 0x00 && kelvin_to_byte(9000) == 0xFF, "out-of-range temperatures are clamped");

    std::printf("%s\n", failures == 0 ? "All tests passed" : "Some tests failed");
    return failures == 0 ? 0 : 1;
}
