#include "quntis_0213.h"

#include <SPI.h>

#include <algorithm>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
    namespace quntis_0213 {

        static const char* const TAG = "quntis_0213";

        static const uint8_t COMMAND_POWER = 0x31;
        static const uint8_t COMMAND_BACKLIGHT = 0x32;
        static const uint8_t COMMAND_LEVEL_UP = 0x35;
        static const uint8_t COMMAND_LEVEL_DOWN = 0x36;
        static const uint8_t COMMAND_TEMP_UP = 0x39;
        static const uint8_t COMMAND_TEMP_DOWN = 0x3A;
        static const uint8_t COMMAND_AUTO_DIM_ON = 0x40;
        static const uint8_t COMMAND_AUTO_DIM_OFF = 0x3E;

        // On air, the address written here becomes the XN297 sync 71 0F 55.
        static const uint8_t SYNC[3] = {0x55, 0x0F, 0x71};
        static const uint8_t SCRAMBLE[11] = {0xBC, 0xE5, 0x66, 0x0D, 0xAE, 0x8C, 0x88, 0x12, 0x69, 0xEE, 0x1F};
        static const uint8_t TRAILER = 0xAF;
        static const uint8_t PACKET_LENGTH = 19;

        static const uint8_t TX_REPEATS = 20;
        static const uint32_t TX_GAP_MS = 4;
        static const uint32_t MIN_SEND_INTERVAL_MS = 150;

        // The remote's lowest setting is 0x1A. Lower values have not been tested on the lamp.
        static const uint8_t MIN_LEVEL = 0x1A;

        // The remote's four color temperature presets.
        static const float PRESET_KELVIN[4] = {3000.0f, 4000.0f, 5000.0f, 6500.0f};
        static const float PRESET_BYTE[4] = {0.0f, 71.0f, 162.0f, 255.0f};

        static uint8_t reverse_bits(uint8_t value) {
            uint8_t reversed = 0;
            for (uint8_t i = 0; i < 8; i++) {
                reversed = (reversed << 1) | ((value >> i) & 1);
            }
            return reversed;
        }

        static uint16_t packet_crc(const uint8_t* data, uint8_t length) {
            uint16_t crc = 0xB5D2;
            for (uint8_t i = 0; i < length; i++) {
                crc ^= data[i] << 8;
                for (uint8_t bit = 0; bit < 8; bit++) {
                    crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
                }
            }
            return crc ^ 0xF494;
        }

        static uint8_t kelvin_to_byte(float kelvin) {
            kelvin = clamp(kelvin, PRESET_KELVIN[0], PRESET_KELVIN[3]);
            for (uint8_t i = 0; i < 3; i++) {
                if (kelvin <= PRESET_KELVIN[i + 1]) {
                    float position = (kelvin - PRESET_KELVIN[i]) / (PRESET_KELVIN[i + 1] - PRESET_KELVIN[i]);
                    return (uint8_t) std::lround(PRESET_BYTE[i] + position * (PRESET_BYTE[i + 1] - PRESET_BYTE[i]));
                }
            }
            return 0xFF;
        }

        void Quntis0213Light::setup() {
            SPI.begin(clk_pin_, miso_pin_, mosi_pin_, -1);
            radio_ = new RF24(ce_pin_, cs_pin_);
            if (!radio_->begin()) {
                ESP_LOGE(TAG, "nRF24L01 not responding, check the wiring");
                this->mark_failed();
                return;
            }
            radio_->setAutoAck(false);
            radio_->disableCRC();
            radio_->setRetries(0, 0);
            radio_->setDataRate(RF24_1MBPS);
            radio_->setPALevel(RF24_PA_MAX);
            radio_->setChannel(channel_);
            radio_->setAddressWidth(3);
            radio_->setPayloadSize(PACKET_LENGTH);
            radio_->openWritingPipe(SYNC);
            radio_->stopListening();
            counter_ = (uint8_t) random_uint32();
        }

        void Quntis0213Light::dump_config() {
            ESP_LOGCONFIG(TAG, "Quntis LI-HY-0213-GY:");
            ESP_LOGCONFIG(TAG, "  CE: %u, CS: %u, SCK: %u, MOSI: %u, MISO: %u", ce_pin_, cs_pin_, clk_pin_, mosi_pin_,
                          miso_pin_);
            ESP_LOGCONFIG(TAG, "  Channel: %u", channel_);
        }

        light::LightTraits Quntis0213Light::get_traits() {
            auto traits = light::LightTraits();
            traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
            traits.set_min_mireds(1000000.0f / PRESET_KELVIN[3]);
            traits.set_max_mireds(1000000.0f / PRESET_KELVIN[0]);
            return traits;
        }

        void Quntis0213Light::write_state(light::LightState* state) {
            float brightness = 0.0f;
            state->current_values_as_brightness(&brightness);
            target_on_ = state->current_values.is_on();
            int level = (int) std::lround(brightness * 255.0f);
            target_level_ = (uint8_t) clamp(level, (int) MIN_LEVEL, 255);
            target_temp_ = kelvin_to_byte(1000000.0f / state->current_values.get_color_temperature());

            // The value restored at boot says nothing about the lamp, so it is not sent.
            if (first_write_) {
                first_write_ = false;
                sent_on_ = target_on_;
                sent_level_ = target_level_;
                sent_temp_ = target_temp_;
                return;
            }
            pending_ = true;
        }

        void Quntis0213Light::loop() {
            if (!pending_ || millis() - last_send_ms_ < MIN_SEND_INTERVAL_MS) {
                return;
            }
            pending_ = false;
            apply_state_();
            last_send_ms_ = millis();
        }

        void Quntis0213Light::apply_state_() {
            if (target_on_ != sent_on_) {
                sent_on_ = target_on_;
                sent_level_ = target_level_;
                sent_temp_ = target_temp_;
                auto_dim_on_ = false;
                send_(COMMAND_POWER);
                return;
            }
            if (!target_on_) {
                return;
            }
            if (target_level_ != sent_level_) {
                uint8_t command = target_level_ > sent_level_ ? COMMAND_LEVEL_UP : COMMAND_LEVEL_DOWN;
                sent_level_ = target_level_;
                // A manual brightness change leaves automatic dimming, as on the remote.
                auto_dim_on_ = false;
                send_(command);
            }
            if (target_temp_ != sent_temp_) {
                uint8_t command = target_temp_ > sent_temp_ ? COMMAND_TEMP_UP : COMMAND_TEMP_DOWN;
                sent_temp_ = target_temp_;
                send_(command);
            }
        }

        void Quntis0213Light::set_backlight(bool on) {
            backlight_on_ = on;
            send_(COMMAND_BACKLIGHT);
        }

        void Quntis0213Light::set_auto_dim(bool on) {
            // As on the remote, this mode needs the lamp on and also lights the backlight.
            if (on && !sent_on_) {
                return;
            }
            auto_dim_on_ = on;
            if (on) {
                backlight_on_ = true;
            }
            send_(on ? COMMAND_AUTO_DIM_ON : COMMAND_AUTO_DIM_OFF);
        }

        void Quntis0213Light::send_(uint8_t command) {
            if (radio_ == nullptr || device_address_.size() != 5 || device_id_.size() != 4) {
                return;
            }
            uint8_t data[11];
            std::copy(device_id_.begin(), device_id_.end(), data);
            data[4] = command;
            // A level of 00 turns off the main light only: the backlight keeps its own byte.
            data[5] = sent_on_ ? sent_level_ : 0x00;
            data[6] = sent_temp_;
            data[7] = backlight_byte_();
            data[8] = 0x00;
            data[9] = counter_++;
            uint8_t sum = 0;
            for (uint8_t i = 0; i < 10; i++) {
                sum += data[i];
            }
            data[10] = sum;

            uint8_t packet[PACKET_LENGTH];
            std::copy(device_address_.begin(), device_address_.end(), packet);
            for (uint8_t i = 0; i < 11; i++) {
                packet[5 + i] = reverse_bits(data[i]) ^ SCRAMBLE[i];
            }
            uint16_t crc = packet_crc(packet, 16);
            packet[16] = crc >> 8;
            packet[17] = crc & 0xFF;
            packet[18] = TRAILER;

            ESP_LOGD(TAG, "Command %02X: level %02X, temp %02X, backlight %02X", command, data[5], data[6], data[7]);
            for (uint8_t i = 0; i < TX_REPEATS; i++) {
                radio_->write(packet, PACKET_LENGTH);
                delay(TX_GAP_MS);
            }
        }

    }  // namespace quntis_0213
}  // namespace esphome
