#pragma once

#include <RF24.h>

#include <vector>

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome {
    namespace quntis_0213 {

        class RemoteTrigger;

        // Controls a Quntis LI-HY-0213-GY. Each packet carries the lamp's whole state,
        // so a command sets absolute values instead of stepping up or down. Between
        // commands, the radio listens to the remote and follows what it changes.
        class Quntis0213Light : public light::LightOutput, public Component {
           public:
            void setup() override;
            void loop() override;
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::HARDWARE; }

            light::LightTraits get_traits() override;
            void setup_state(light::LightState* state) override { state_ = state; }
            void write_state(light::LightState* state) override;
            void set_backlight(bool on);
            bool get_backlight() const { return backlight_level_ != 0; }
            void set_auto_dim(bool on);
            bool get_auto_dim() const { return auto_dim_on_; }

            // Waits for the next remote packet and keeps its address and ID.
            void start_learning();
            bool is_learning() const { return learning_; }
            bool has_remote() const { return device_address_.size() == 5 && device_id_.size() == 4; }
            // Seconds since the last packet from the remote, or -1 if none was heard.
            int last_remote_age() const;
            // Command, brightness, color temperature and backlight of the last remote packet.
            const uint8_t* last_remote_values() const { return last_remote_values_; }
            // Valid packets heard from any Quntis remote, and presses from this lamp's remote.
            uint32_t get_packets_received() const { return packets_received_; }
            uint32_t get_presses_received() const { return presses_received_; }
            // How long the last press lasted, and how many packets it sent, to tell long presses apart.
            uint32_t get_last_press_ms() const { return last_press_ms_; }
            uint32_t get_last_press_packets() const { return last_press_packets_; }

            // Turns the lamp off after this many minutes. 0 cancels.
            void start_timer(uint32_t minutes);
            uint32_t get_timer_minutes() const;

            // Follows the time of day: warm at night, cool in the middle of the day.
            void set_auto_temperature(bool on);
            bool get_auto_temperature() const { return auto_temp_on_; }
            bool has_clock() const;
#ifdef USE_TIME
            void set_time(time::RealTimeClock* clock) { clock_ = clock; }
#endif

            void set_pins(uint8_t ce, uint8_t cs, uint8_t clk, uint8_t mosi, uint8_t miso) {
                ce_pin_ = ce;
                cs_pin_ = cs;
                clk_pin_ = clk;
                mosi_pin_ = mosi;
                miso_pin_ = miso;
            }
            void set_channel(uint8_t channel) { channel_ = channel; }
            uint8_t get_channel() const { return channel_; }
            void set_device_address(const std::vector<uint8_t>& address) { device_address_ = address; }
            void set_device_id(const std::vector<uint8_t>& id) { device_id_ = id; }
            void set_min_level(uint8_t level) { min_level_ = level; }
            void add_remote_trigger(RemoteTrigger* trigger) { remote_triggers_.push_back(trigger); }
            uint8_t get_min_level() const { return min_level_; }

           protected:
            void apply_state_();
            void send_(uint8_t command);
            void receive_();
            void handle_remote_(const uint8_t* data);
            void save_state_();
            void finish_press_();
            void check_long_press_(uint8_t command, uint32_t now);
            void check_timer_();
            void apply_auto_temperature_();

            uint8_t ce_pin_{17};
            uint8_t cs_pin_{5};
            uint8_t clk_pin_{18};
            uint8_t mosi_pin_{23};
            uint8_t miso_pin_{19};
            uint8_t channel_{80};
            uint8_t min_level_{0x03};
            std::vector<uint8_t> device_address_;
            std::vector<uint8_t> device_id_;
            std::vector<RemoteTrigger*> remote_triggers_;
            RF24* radio_{nullptr};
            light::LightState* state_{nullptr};
            ESPPreferenceObject remote_pref_;
            ESPPreferenceObject state_pref_;

            // What the lamp was last told. It never reports its real state.
            bool sent_on_{false};
            uint8_t sent_level_{0xFF};
            uint8_t sent_temp_{0x00};
            uint8_t backlight_level_{0x00};
            uint8_t backlight_on_level_{0xFF};
            bool auto_dim_on_{false};

            // Latest values requested by Home Assistant.
            bool target_on_{false};
            uint8_t target_level_{0xFF};
            uint8_t target_temp_{0x00};

            uint8_t counter_{0};
            bool first_write_{true};
            bool pending_{false};
            uint32_t last_send_ms_{0};

            bool learning_{false};
            // Set while the light entity takes the remote's values, so they are not sent back.
            uint32_t syncing_until_ms_{0};
            int16_t last_remote_counter_{-1};
            uint32_t last_remote_ms_{0};
            uint32_t learning_until_ms_{0};
            uint8_t last_remote_values_[4]{};
            uint32_t packets_received_{0};
            uint32_t presses_received_{0};
            uint32_t press_start_ms_{0};
            uint32_t press_packets_{0};
            // A held button repeats its press, with a new counter each time.
            uint32_t hold_start_ms_{0};
            uint8_t hold_command_{0};
            bool long_press_sent_{false};
            uint32_t last_press_ms_{0};
            uint32_t last_press_packets_{0};

            uint32_t timer_end_ms_{0};
            bool auto_temp_on_{false};
            uint32_t auto_temp_check_ms_{0};
            // Set while the light entity takes the time-of-day temperature, so it is not mistaken for
            // a manual change.
            uint32_t auto_temp_until_ms_{0};
#ifdef USE_TIME
            time::RealTimeClock* clock_{nullptr};
#endif
        };

        // Fires once per press on the remote, with the button's name.
        class RemoteTrigger : public Trigger<std::string> {
           public:
            explicit RemoteTrigger(Quntis0213Light* parent) { parent->add_remote_trigger(this); }
        };

    }  // namespace quntis_0213
}  // namespace esphome
