#pragma once

// The LI-HY-0213-GY radio packet, without any ESPHome code, so it can be tested on a computer.
//
// After the XN297 sync 71 0F 55, a packet holds the 5-byte address, 11 scrambled data bytes,
// a 2-byte CRC and the trailer AF. The data bytes are:
// [remote id x4] [command] [brightness] [color temp] [backlight] 00 [counter] [sum]

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esphome {
    namespace quntis_0213 {
        namespace protocol {

            static const uint8_t SCRAMBLE[11] = {0xBC, 0xE5, 0x66, 0x0D, 0xAE, 0x8C, 0x88, 0x12, 0x69, 0xEE, 0x1F};
            static const uint8_t TRAILER = 0xAF;
            static const uint8_t PACKET_LENGTH = 19;
            static const uint8_t DATA_LENGTH = 11;

            // The remote's four color temperature presets.
            static const float PRESET_KELVIN[4] = {3000.0f, 4000.0f, 5000.0f, 6500.0f};
            static const float PRESET_BYTE[4] = {0.0f, 71.0f, 162.0f, 255.0f};

            inline uint8_t reverse_bits(uint8_t value) {
                uint8_t reversed = 0;
                for (uint8_t i = 0; i < 8; i++) {
                    reversed = (reversed << 1) | ((value >> i) & 1);
                }
                return reversed;
            }

            inline uint16_t packet_crc(const uint8_t* data, uint8_t length) {
                uint16_t crc = 0xB5D2;
                for (uint8_t i = 0; i < length; i++) {
                    crc ^= data[i] << 8;
                    for (uint8_t bit = 0; bit < 8; bit++) {
                        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
                    }
                }
                return crc ^ 0xF494;
            }

            inline uint8_t kelvin_to_byte(float kelvin) {
                kelvin = std::clamp(kelvin, PRESET_KELVIN[0], PRESET_KELVIN[3]);
                for (uint8_t i = 0; i < 3; i++) {
                    if (kelvin <= PRESET_KELVIN[i + 1]) {
                        float position = (kelvin - PRESET_KELVIN[i]) / (PRESET_KELVIN[i + 1] - PRESET_KELVIN[i]);
                        return (uint8_t) std::lround(PRESET_BYTE[i] + position * (PRESET_BYTE[i + 1] - PRESET_BYTE[i]));
                    }
                }
                return 0xFF;
            }

            inline float byte_to_kelvin(uint8_t value) {
                for (uint8_t i = 0; i < 3; i++) {
                    if (value <= PRESET_BYTE[i + 1]) {
                        float position = (value - PRESET_BYTE[i]) / (PRESET_BYTE[i + 1] - PRESET_BYTE[i]);
                        return PRESET_KELVIN[i] + position * (PRESET_KELVIN[i + 1] - PRESET_KELVIN[i]);
                    }
                }
                return PRESET_KELVIN[3];
            }

            // Builds a packet from the address and the first 10 data bytes. The sum is added here.
            inline void encode(const uint8_t* address, const uint8_t* data, uint8_t* packet) {
                uint8_t sum = 0;
                std::copy(address, address + 5, packet);
                for (uint8_t i = 0; i < DATA_LENGTH; i++) {
                    uint8_t value = i < 10 ? data[i] : sum;
                    sum += value;
                    packet[5 + i] = reverse_bits(value) ^ SCRAMBLE[i];
                }
                uint16_t crc = packet_crc(packet, 16);
                packet[16] = crc >> 8;
                packet[17] = crc & 0xFF;
                packet[18] = TRAILER;
            }

            // Reads the 11 data bytes of a packet. Returns false if the CRC, trailer or sum is wrong.
            inline bool decode(const uint8_t* packet, uint8_t* data) {
                uint16_t crc = packet_crc(packet, 16);
                if (packet[16] != (crc >> 8) || packet[17] != (crc & 0xFF) || packet[18] != TRAILER) {
                    return false;
                }
                uint8_t sum = 0;
                for (uint8_t i = 0; i < DATA_LENGTH; i++) {
                    data[i] = reverse_bits(packet[5 + i] ^ SCRAMBLE[i]);
                    if (i < 10) {
                        sum += data[i];
                    }
                }
                return sum == data[10];
            }

        }  // namespace protocol
    }  // namespace quntis_0213
}  // namespace esphome
