#include "quntis_0213.h"

#include "protocol.h"

#include <SPI.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
    namespace quntis_0213 {

        static const char* const TAG = "quntis_0213";

        static const uint8_t COMMAND_BRIGHTNESS_MODE = 0x30;
        static const uint8_t COMMAND_POWER = 0x31;
        static const uint8_t COMMAND_BACKLIGHT = 0x32;
        // The remote's brightness button also sends 33.
        static const uint8_t COMMAND_BRIGHTNESS_MODE_2 = 0x33;
        static const uint8_t COMMAND_TEMP_MODE = 0x34;
        static const uint8_t COMMAND_LEVEL_UP = 0x35;
        static const uint8_t COMMAND_LEVEL_DOWN = 0x36;
        // + held in brightness mode: the level rises step by step.
        static const uint8_t COMMAND_LEVEL_UP_HELD = 0x37;
        static const uint8_t COMMAND_TEMP_UP = 0x39;
        static const uint8_t COMMAND_TEMP_DOWN = 0x3A;
        static const uint8_t COMMAND_AUTO_DIM_ON = 0x40;
        static const uint8_t COMMAND_AUTO_DIM_OFF = 0x3E;

        // On air, the address written here becomes the XN297 sync 71 0F 55.
        static const uint8_t SYNC[3] = {0x55, 0x0F, 0x71};
        using protocol::byte_to_kelvin;
        using protocol::kelvin_to_byte;
        using protocol::PACKET_LENGTH;
        using protocol::PRESET_KELVIN;

        static const uint8_t TX_REPEATS = 20;
        // Steps of a fade are replaced by the next one soon, so fewer repeats are enough.
        static const uint8_t TX_REPEATS_FADE = 5;
        static const uint32_t TX_GAP_MS = 4;
        // Packets sent per loop pass, about 12 ms of the loop at most.
        static const uint8_t TX_PACKETS_PER_LOOP = 4;
        static const uint32_t MIN_SEND_INTERVAL_MS = 100;
        // The radio holds 3 packets. A limit keeps a radio that stopped answering from holding the loop.
        static const uint8_t RX_PACKETS_PER_LOOP = 6;
        // How often the radio is checked, and tried again while it does not answer.
        static const uint32_t RADIO_CHECK_MS = 10000;

        // The remote repeats each packet in a burst. A packet with the same counter within this
        // time belongs to the same press.
        static const uint32_t REMOTE_REPEAT_MS = 1000;
        static const uint32_t SYNC_WINDOW_MS = 300;
        static const uint32_t LEARNING_TIMEOUT_MS = 60000;

        struct RemoteSettings {
            uint8_t address[5];
            uint8_t id[4];
            uint8_t valid;
        };

        struct StateSettings {
            uint8_t backlight_level;
            uint8_t backlight_on_level;
            uint8_t auto_dim;
            uint8_t auto_temp;
        };

        // A press ends when its packets stop for this long.
        static const uint32_t PRESS_END_MS = 400;
        // A short press lasts about 0.7 s. A held button keeps sending, either with the same counter
        // or as new presses every 0.3 to 0.5 s.
        static const uint32_t LONG_PRESS_MS = 1500;
        static const uint32_t HOLD_GAP_MS = 600;
        static const uint32_t AUTO_TEMP_INTERVAL_MS = 10000;
        static const uint32_t AUTO_TEMP_FADE_MS = 5000;

        // Color temperature through the day: hour, kelvin.
        static const float AUTO_TEMP_CURVE[][2] = {{0, 3000},  {6, 3000},  {8, 4000},  {10, 5000}, {16, 5000},
                                                   {18, 4000}, {20, 3200}, {22, 3000}, {24, 3000}};

        static const char* button_name(uint8_t command) {
            switch (command) {
                case COMMAND_BRIGHTNESS_MODE:
                case COMMAND_BRIGHTNESS_MODE_2:
                    return "brightness";
                case COMMAND_POWER:
                    return "power";
                case COMMAND_BACKLIGHT:
                    return "backlight";
                case COMMAND_TEMP_MODE:
                    return "temperature";
                case COMMAND_LEVEL_UP:
                case COMMAND_LEVEL_UP_HELD:
                    return "brightness_up";
                case COMMAND_LEVEL_DOWN:
                    return "brightness_down";
                case COMMAND_TEMP_UP:
                    return "temperature_up";
                case COMMAND_TEMP_DOWN:
                    return "temperature_down";
                case COMMAND_AUTO_DIM_ON:
                    return "auto_dim";
                default:
                    return "other";
            }
        }

        void Quntis0213Light::setup() {
            // A remote learned on the device replaces the values from the YAML file.
            remote_pref_ = global_preferences->make_preference<RemoteSettings>(fnv1_hash("quntis_0213_remote"));
            RemoteSettings remote{};
            if (remote_pref_.load(&remote) && remote.valid == 1) {
                device_address_.assign(remote.address, remote.address + 5);
                device_id_.assign(remote.id, remote.id + 4);
            }
            state_pref_ = global_preferences->make_preference<StateSettings>(fnv1_hash("quntis_0213_state"));
            StateSettings saved{};
            if (state_pref_.load(&saved)) {
                std::memcpy(saved_settings_, &saved, sizeof(saved_settings_));
                backlight_level_ = saved.backlight_level;
                backlight_on_level_ = saved.backlight_on_level != 0 ? saved.backlight_on_level : 0xFF;
                auto_dim_on_ = saved.auto_dim == 1;
                auto_temp_on_ = saved.auto_temp == 1;
            }

            SPI.begin(clk_pin_, miso_pin_, mosi_pin_, -1);
            radio_ = new RF24(ce_pin_, cs_pin_);
            counter_ = (uint8_t) random_uint32();
            radio_ok_ = configure_radio_();
            if (!radio_ok_) {
                // Not marked as failed: the loop keeps trying, so a fixed wire needs no restart.
                ESP_LOGE(TAG, "nRF24L01 not responding, check the wiring");
                this->status_set_error();
            }
            if (!has_remote()) {
                ESP_LOGW(TAG, "No remote configured yet: start learning and press a button on the remote");
            }
        }

        bool Quntis0213Light::configure_radio_() {
            if (!radio_->begin()) {
                return false;
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
            radio_->openReadingPipe(1, SYNC);
            radio_->startListening();
            radio_->failureDetected = false;
            return true;
        }

        // A module that loses power forgets its settings, and one that stops answering makes every
        // send wait. This notices both and sets the radio up again when it is back.
        void Quntis0213Light::check_radio_() {
            uint32_t now = millis();
            if (now - radio_check_ms_ < RADIO_CHECK_MS) {
                return;
            }
            radio_check_ms_ = now;
            if (radio_ok_) {
                // Read twice, so one bad read does not interrupt a command.
                if (!radio_->failureDetected && (radio_->isChipConnected() || radio_->isChipConnected())) {
                    return;
                }
                radio_ok_ = false;
                tx_left_ = 0;
                tx_queued_ = 0;
                ESP_LOGE(TAG, "nRF24L01 stopped responding, check the wiring");
                this->status_set_error();
                return;
            }
            if (configure_radio_()) {
                radio_ok_ = true;
                tx_left_ = 0;
                tx_queued_ = 0;
                ESP_LOGI(TAG, "nRF24L01 responding again");
                this->status_clear_error();
            }
        }

        void Quntis0213Light::dump_config() {
            ESP_LOGCONFIG(TAG, "Quntis LI-HY-0213-GY:");
            ESP_LOGCONFIG(TAG, "  CE: %u, CS: %u, SCK: %u, MOSI: %u, MISO: %u", ce_pin_, cs_pin_, clk_pin_, mosi_pin_,
                          miso_pin_);
            ESP_LOGCONFIG(TAG, "  Channel: %u", channel_);
            ESP_LOGCONFIG(TAG, "  Minimum level: %02X", min_level_);
            ESP_LOGCONFIG(TAG, "  Remote: %s", has_remote() ? "configured" : "not configured");
            ESP_LOGCONFIG(TAG, "  Radio: %s", radio_ok_ ? "OK" : "not responding");
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
            target_level_ = (uint8_t) clamp(level, (int) min_level_, 255);
            target_temp_ = kelvin_to_byte(1000000.0f / state->current_values.get_color_temperature());

            // The value restored at boot, and the values taken from the remote, already match
            // what the lamp does, so they are not sent.
            bool syncing = syncing_until_ms_ != 0 && (int32_t) (millis() - syncing_until_ms_) < 0;
            bool automatic = auto_temp_until_ms_ != 0 && (int32_t) (millis() - auto_temp_until_ms_) < 0;
            // A color temperature chosen by hand stops following the time of day.
            if (!first_write_ && !syncing && !automatic && auto_temp_on_ && target_temp_ != sent_temp_) {
                auto_temp_on_ = false;
                save_state_();
            }
            if (first_write_ || syncing) {
                first_write_ = false;
                syncing_until_ms_ = 0;
                sent_on_ = target_on_;
                sent_level_ = target_level_;
                sent_temp_ = target_temp_;
                return;
            }
            pending_ = true;
        }

        void Quntis0213Light::loop() {
            check_radio_();
            transmit_();
            receive_();
            if (press_packets_ != 0 && millis() - last_remote_ms_ > PRESS_END_MS) {
                finish_press_();
            }
            check_timer_();
            apply_auto_temperature_();
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
                // A lamp just turned on takes the time-of-day temperature without waiting.
                auto_temp_check_ms_ = 0;
                send_(COMMAND_POWER);
                save_state_();
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
                save_state_();
            }
            if (target_temp_ != sent_temp_) {
                uint8_t command = target_temp_ > sent_temp_ ? COMMAND_TEMP_UP : COMMAND_TEMP_DOWN;
                sent_temp_ = target_temp_;
                send_(command);
            }
        }

        void Quntis0213Light::set_backlight(bool on) {
            // The remote's backlight button makes the lamp toggle, so nothing is sent when the state
            // already matches.
            if (on == get_backlight()) {
                return;
            }
            backlight_level_ = on ? backlight_on_level_ : 0x00;
            send_(COMMAND_BACKLIGHT);
            save_state_();
        }

        void Quntis0213Light::set_auto_dim(bool on) {
            // As on the remote, this mode needs the lamp on and also lights the backlight.
            if (on && !sent_on_) {
                return;
            }
            auto_dim_on_ = on;
            if (on) {
                backlight_level_ = backlight_on_level_;
            }
            send_(on ? COMMAND_AUTO_DIM_ON : COMMAND_AUTO_DIM_OFF);
            save_state_();
        }

        void Quntis0213Light::resync() {
            ESP_LOGI(TAG, "Sending the whole state to the lamp again");
            // The power command carries the level, color temperature and backlight, and the lamp
            // leaves automatic dimming on it, so that mode is asked for again after it.
            bool auto_dim = auto_dim_on_ && sent_on_;
            send_(COMMAND_POWER);
            if (auto_dim) {
                send_(COMMAND_AUTO_DIM_ON);
            }
            auto_dim_on_ = auto_dim;
            save_state_();
        }

        void Quntis0213Light::save_state_() {
            StateSettings settings{backlight_level_, backlight_on_level_, (uint8_t) (auto_dim_on_ ? 1 : 0),
                                   (uint8_t) (auto_temp_on_ ? 1 : 0)};
            static_assert(sizeof(settings) == sizeof(saved_settings_), "settings size");
            if (std::memcmp(&settings, saved_settings_, sizeof(settings)) == 0) {
                return;
            }
            std::memcpy(saved_settings_, &settings, sizeof(settings));
            state_pref_.save(&settings);
        }

        void Quntis0213Light::check_long_press_(uint8_t command, uint32_t now) {
            if (long_press_sent_ || now - hold_start_ms_ < LONG_PRESS_MS) {
                return;
            }
            long_press_sent_ = true;
            std::string button = std::string(button_name(command)) + "_long";
            ESP_LOGD(TAG, "Remote long press: %s", button.c_str());
            for (auto* trigger : remote_triggers_) {
                trigger->trigger(button);
            }
        }

        void Quntis0213Light::finish_press_() {
            last_press_ms_ = last_remote_ms_ - press_start_ms_;
            last_press_packets_ = press_packets_;
            press_packets_ = 0;
        }

        void Quntis0213Light::start_timer(uint32_t minutes) {
            if (minutes == 0) {
                timer_end_ms_ = 0;
                ESP_LOGI(TAG, "Timer cancelled");
                return;
            }
            // 0 means no timer, so a deadline that lands on 0 moves by 1 ms.
            timer_end_ms_ = millis() + minutes * 60000UL;
            if (timer_end_ms_ == 0) {
                timer_end_ms_ = 1;
            }
            ESP_LOGI(TAG, "Lamp turns off in %u min", (unsigned) minutes);
        }

        uint32_t Quntis0213Light::get_timer_minutes() const {
            if (timer_end_ms_ == 0) {
                return 0;
            }
            int32_t left = (int32_t) (timer_end_ms_ - millis());
            return left <= 0 ? 0 : (uint32_t) ((left + 59999) / 60000);
        }

        void Quntis0213Light::check_timer_() {
            if (timer_end_ms_ == 0 || (int32_t) (millis() - timer_end_ms_) < 0) {
                return;
            }
            timer_end_ms_ = 0;
            ESP_LOGI(TAG, "Timer done: turning the lamp off");
            if (get_backlight()) {
                set_backlight(false);
            }
            if (state_ != nullptr) {
                state_->make_call().set_state(false).perform();
            }
        }

        bool Quntis0213Light::has_clock() const {
#ifdef USE_TIME
            return clock_ != nullptr;
#else
            return false;
#endif
        }

        void Quntis0213Light::set_auto_temperature(bool on) {
            auto_temp_on_ = on && has_clock();
            auto_temp_check_ms_ = 0;
            save_state_();
        }

        void Quntis0213Light::apply_auto_temperature_() {
#ifdef USE_TIME
            uint32_t now_ms = millis();
            if (!auto_temp_on_ || clock_ == nullptr || state_ == nullptr || !sent_on_ ||
                (auto_temp_check_ms_ != 0 && now_ms - auto_temp_check_ms_ < AUTO_TEMP_INTERVAL_MS)) {
                return;
            }
            // A fade in progress finishes first, so it keeps its own length.
            if (state_->is_transformer_active()) {
                return;
            }
            auto_temp_check_ms_ = now_ms;
            ESPTime now = clock_->now();
            if (!now.is_valid()) {
                return;
            }
            float hour = now.hour + now.minute / 60.0f;
            float kelvin = AUTO_TEMP_CURVE[0][1];
            for (size_t i = 0; i + 1 < sizeof(AUTO_TEMP_CURVE) / sizeof(AUTO_TEMP_CURVE[0]); i++) {
                if (hour <= AUTO_TEMP_CURVE[i + 1][0]) {
                    float position = (hour - AUTO_TEMP_CURVE[i][0]) / (AUTO_TEMP_CURVE[i + 1][0] - AUTO_TEMP_CURVE[i][0]);
                    kelvin = AUTO_TEMP_CURVE[i][1] + position * (AUTO_TEMP_CURVE[i + 1][1] - AUTO_TEMP_CURVE[i][1]);
                    break;
                }
            }
            if (std::fabs(byte_to_kelvin(sent_temp_) - kelvin) < 50.0f) {
                return;
            }
            auto_temp_until_ms_ = now_ms + AUTO_TEMP_FADE_MS + 1000;
            auto call = state_->make_call();
            call.set_color_temperature(1000000.0f / kelvin);
            call.set_transition_length(AUTO_TEMP_FADE_MS);
            call.perform();
#endif
        }

        void Quntis0213Light::start_learning() {
            learning_ = true;
            learning_until_ms_ = millis() + LEARNING_TIMEOUT_MS;
            ESP_LOGI(TAG, "Learning: press a button on the remote near the module");
        }

        int Quntis0213Light::last_remote_age() const {
            if (last_remote_counter_ < 0) {
                return -1;
            }
            return (int) ((millis() - last_remote_ms_) / 1000);
        }

        void Quntis0213Light::receive_() {
            if (learning_ && (int32_t) (millis() - learning_until_ms_) >= 0) {
                learning_ = false;
                ESP_LOGW(TAG, "Learning stopped: no remote heard within %u s", (unsigned) (LEARNING_TIMEOUT_MS / 1000));
            }
            // Nothing arrives while the radio is sending.
            if (!radio_ok_ || tx_left_ != 0 || tx_queued_ != 0) {
                return;
            }
            for (uint8_t read = 0; read < RX_PACKETS_PER_LOOP && radio_->available(); read++) {
                uint8_t packet[PACKET_LENGTH];
                radio_->read(packet, PACKET_LENGTH);

                uint8_t data[protocol::DATA_LENGTH];
                if (!protocol::decode(packet, data)) {
                    continue;
                }
                packets_received_++;

                if (learning_) {
                    learning_ = false;
                    device_address_.assign(packet, packet + 5);
                    device_id_.assign(data, data + 4);
                    RemoteSettings remote{};
                    std::copy(packet, packet + 5, remote.address);
                    std::copy(data, data + 4, remote.id);
                    remote.valid = 1;
                    remote_pref_.save(&remote);
                    global_preferences->sync();
                    ESP_LOGI(TAG, "Remote learned: address %02X %02X %02X %02X %02X, ID %02X %02X %02X %02X", packet[0],
                             packet[1], packet[2], packet[3], packet[4], data[0], data[1], data[2], data[3]);
                }
                if (!has_remote() || !std::equal(device_address_.begin(), device_address_.end(), packet) ||
                    !std::equal(device_id_.begin(), device_id_.end(), data)) {
                    continue;
                }

                uint32_t now = millis();
                if (data[9] == last_remote_counter_ && now - last_remote_ms_ < REMOTE_REPEAT_MS) {
                    last_remote_ms_ = now;
                    press_packets_++;
                    check_long_press_(data[4], now);
                    continue;
                }
                // A new press of the same button right after the last packet continues a hold.
                if (data[4] != hold_command_ || now - last_remote_ms_ > HOLD_GAP_MS) {
                    hold_command_ = data[4];
                    hold_start_ms_ = now;
                    long_press_sent_ = false;
                }
                if (press_packets_ != 0) {
                    finish_press_();
                }
                press_start_ms_ = now;
                press_packets_ = 1;
                last_remote_counter_ = data[9];
                last_remote_ms_ = now;
                presses_received_++;
                handle_remote_(data);
                check_long_press_(data[4], now);
            }
        }

        void Quntis0213Light::handle_remote_(const uint8_t* data) {
            uint8_t command = data[4];
            uint8_t level = data[5];
            uint8_t temp = data[6];
            std::copy(data + 4, data + 8, last_remote_values_);
            ESP_LOGD(TAG, "Remote %02X: level %02X, temp %02X, backlight %02X", command, level, temp, data[7]);
            std::string button = button_name(command);
            for (auto* trigger : remote_triggers_) {
                trigger->trigger(button);
            }

            if (command == COMMAND_BACKLIGHT) {
                // The remote's backlight button always sends FF, and the lamp toggles its backlight.
                backlight_level_ = backlight_level_ != 0 ? 0x00 : backlight_on_level_;
            } else if (command == COMMAND_POWER || command == COMMAND_AUTO_DIM_ON) {
                // The remote does not follow the backlight's toggles, so its byte is only taken from
                // the buttons that set the backlight: power, and automatic dimming that turns it on.
                backlight_level_ = data[7];
                if (backlight_level_ != 0) {
                    backlight_on_level_ = backlight_level_;
                }
            }
            if (command == COMMAND_AUTO_DIM_ON) {
                auto_dim_on_ = true;
            } else if (command == COMMAND_AUTO_DIM_OFF || command == COMMAND_POWER || command == COMMAND_LEVEL_UP ||
                       command == COMMAND_LEVEL_DOWN || command == COMMAND_BRIGHTNESS_MODE ||
                       command == COMMAND_BRIGHTNESS_MODE_2 || command == COMMAND_LEVEL_UP_HELD) {
                auto_dim_on_ = false;
            }
            if (level == 0) {
                auto_dim_on_ = false;
            }
            if (command == COMMAND_TEMP_MODE || command == COMMAND_TEMP_UP || command == COMMAND_TEMP_DOWN) {
                // A color temperature chosen on the remote stops following the time of day.
                auto_temp_on_ = false;
            } else if (auto_temp_on_ && temp != sent_temp_) {
                // Other buttons resend the remote's own temperature: take it back right away.
                auto_temp_check_ms_ = 0;
            }
            save_state_();

            bool on = level != 0;
            sent_on_ = on;
            sent_temp_ = temp;
            if (on) {
                sent_level_ = level;
            }
            // The remote's values replace a command that was still waiting to be sent.
            pending_ = false;
            target_on_ = sent_on_;
            target_level_ = sent_level_;
            target_temp_ = sent_temp_;
            if (state_ == nullptr) {
                return;
            }
            const auto& current = state_->remote_values;
            float brightness = level / 255.0f;
            float mireds = 1000000.0f / byte_to_kelvin(temp);
            bool same = current.is_on() == on && (!on || std::fabs(current.get_brightness() - brightness) < 0.003f) &&
                        std::fabs(current.get_color_temperature() - mireds) < 0.5f;
            if (same) {
                return;
            }
            syncing_until_ms_ = millis() + SYNC_WINDOW_MS;
            auto call = state_->make_call();
            call.set_state(on);
            if (on) {
                call.set_brightness(brightness);
            }
            call.set_color_temperature(mireds);
            // The lamp already changed, so there is nothing to fade.
            call.set_transition_length(0);
            call.perform();
        }

        void Quntis0213Light::send_(uint8_t command) {
            if (!radio_ok_) {
                ESP_LOGW(TAG, "Command %02X not sent: the nRF24L01 does not respond", command);
                return;
            }
            if (!has_remote()) {
                ESP_LOGW(TAG, "No remote configured: learn the remote first");
                return;
            }
            // A brightness or color temperature step that is already waiting goes out with the latest
            // values anyway. Other commands are all kept: the lamp toggles its backlight on each 32.
            bool carries_state = command == COMMAND_LEVEL_UP || command == COMMAND_LEVEL_DOWN ||
                                 command == COMMAND_TEMP_UP || command == COMMAND_TEMP_DOWN;
            if (carries_state && std::find(tx_queue_, tx_queue_ + tx_queued_, command) != tx_queue_ + tx_queued_) {
                return;
            }
            if (tx_queued_ == TX_QUEUE_SIZE) {
                ESP_LOGW(TAG, "Too many commands at once, %02X dropped", command);
                return;
            }
            tx_queue_[tx_queued_++] = command;
        }

        void Quntis0213Light::build_packet_(uint8_t command) {
            uint8_t data[10];
            std::copy(device_id_.begin(), device_id_.end(), data);
            data[4] = command;
            // A level of 00 turns off the main light only: the backlight keeps its own byte.
            data[5] = sent_on_ ? sent_level_ : 0x00;
            data[6] = sent_temp_;
            data[7] = backlight_level_;
            data[8] = 0x00;
            data[9] = counter_++;
            protocol::encode(device_address_.data(), data, tx_packet_);
            bool fading = state_ != nullptr && state_->is_transformer_active();
            tx_left_ = fading ? TX_REPEATS_FADE : TX_REPEATS;
            ESP_LOGD(TAG, "Command %02X: level %02X, temp %02X, backlight %02X", command, data[5], data[6], data[7]);
        }

        void Quntis0213Light::transmit_() {
            if (!radio_ok_ || (tx_left_ == 0 && tx_queued_ == 0)) {
                return;
            }
            for (uint8_t sent = 0; sent < TX_PACKETS_PER_LOOP; sent++) {
                if (tx_left_ == 0) {
                    if (tx_queued_ == 0) {
                        break;
                    }
                    if (sent == 0) {
                        radio_->stopListening();
                    }
                    build_packet_(tx_queue_[0]);
                    std::copy(tx_queue_ + 1, tx_queue_ + tx_queued_, tx_queue_);
                    tx_queued_--;
                }
                if (sent > 0 || millis() - tx_last_ms_ < TX_GAP_MS) {
                    delay(TX_GAP_MS);
                }
                radio_->write(tx_packet_, PACKET_LENGTH);
                tx_last_ms_ = millis();
                tx_left_--;
                if (radio_->failureDetected) {
                    // The radio stopped answering: check it now instead of waiting on each packet.
                    radio_check_ms_ = tx_last_ms_ - RADIO_CHECK_MS;
                    check_radio_();
                    return;
                }
            }
            if (tx_left_ == 0 && tx_queued_ == 0) {
                // Back to listening for the remote once everything is out.
                radio_->startListening();
            }
        }

    }  // namespace quntis_0213
}  // namespace esphome
