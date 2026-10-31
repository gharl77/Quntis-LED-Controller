#pragma once

#include <RF24.h>

#include <vector>

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"

namespace esphome {
    namespace quntis_0213 {

        // Controls a Quntis LI-HY-0213-GY. Each packet carries the lamp's whole state,
        // so a command sets absolute values instead of stepping up or down.
        class Quntis0213Light : public light::LightOutput, public Component {
           public:
            void setup() override;
            void loop() override;
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::HARDWARE; }

            light::LightTraits get_traits() override;
            void write_state(light::LightState* state) override;
            void set_backlight(bool on);
            bool get_backlight() const { return backlight_on_; }
            void set_auto_dim(bool on);
            bool get_auto_dim() const { return auto_dim_on_; }

            void set_pins(uint8_t ce, uint8_t cs, uint8_t clk, uint8_t mosi, uint8_t miso) {
                ce_pin_ = ce;
                cs_pin_ = cs;
                clk_pin_ = clk;
                mosi_pin_ = mosi;
                miso_pin_ = miso;
            }
            void set_channel(uint8_t channel) { channel_ = channel; }
            void set_device_address(const std::vector<uint8_t>& address) { device_address_ = address; }
            void set_device_id(const std::vector<uint8_t>& id) { device_id_ = id; }

           protected:
            void apply_state_();
            void send_(uint8_t command);
            uint8_t backlight_byte_() const { return backlight_on_ ? 0xFF : 0x00; }

            uint8_t ce_pin_{17};
            uint8_t cs_pin_{5};
            uint8_t clk_pin_{18};
            uint8_t mosi_pin_{23};
            uint8_t miso_pin_{19};
            uint8_t channel_{80};
            std::vector<uint8_t> device_address_;
            std::vector<uint8_t> device_id_;
            RF24* radio_{nullptr};

            // What the lamp was last told. It never reports its real state.
            bool sent_on_{false};
            uint8_t sent_level_{0xFF};
            uint8_t sent_temp_{0x00};
            bool backlight_on_{false};
            bool auto_dim_on_{false};

            // Latest values requested by Home Assistant.
            bool target_on_{false};
            uint8_t target_level_{0xFF};
            uint8_t target_temp_{0x00};

            uint8_t counter_{0};
            bool first_write_{true};
            bool pending_{false};
            uint32_t last_send_ms_{0};
        };

    }  // namespace quntis_0213
}  // namespace esphome
