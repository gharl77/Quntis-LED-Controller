#include "quntis_web.h"

#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esphome/components/quntis_0213/quntis_0213.h"
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif
#include "esphome/components/wifi/scan_list.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

// The compressed page, generated at build time from page.h.
extern const uint8_t QUNTIS_PAGE_GZ[];
extern const size_t QUNTIS_PAGE_GZ_SIZE;

namespace esphome {
    namespace quntis_web {

        static const char* const TAG = "quntis_web";

        static const uint32_t STATE_REFRESH_MS = 500;
        static const uint32_t SCAN_TIMEOUT_MS = 20000;
        // The WiFi component stores the results from its own loop, just after the driver's event.
        static const uint32_t SCAN_SETTLE_MS = 300;

        static std::atomic<bool> scan_event_received{false};

        struct PasswordSettings {
            char password[65];
            // The YAML password this one replaced. A new YAML password takes over again.
            uint32_t yaml_hash;
        };

#ifdef USE_UPDATE
        // The firmware update entity, when the configuration has one.
        static update::UpdateEntity* firmware_update() {
            for (auto* entity : App.get_updates()) {
                return entity;
            }
            return nullptr;
        }
#endif

        static void on_scan_done(void*, esp_event_base_t, int32_t, void*) { scan_event_received = true; }

        void QuntisWeb::setup() {
#ifdef USE_WEBSERVER_AUTH
            if (yaml_password_ != nullptr) {
                password_pref_ = global_preferences->make_preference<PasswordSettings>(fnv1_hash("quntis_web_password"));
                PasswordSettings saved{};
                if (password_pref_.load(&saved) && saved.yaml_hash == fnv1_hash(yaml_password_) && saved.password[0] != 0) {
                    std::memcpy(password_, saved.password, sizeof(password_));
                    password_[sizeof(password_) - 1] = 0;
                    base_->set_auth_password(password_);
                }
            }
#endif
            base_->init();
            base_->add_handler_without_auth(&captive_forwarder_);
            base_->add_handler(this);
        }

        void QuntisWeb::dump_config() { ESP_LOGCONFIG(TAG, "Quntis web page on port %u", base_->get_port()); }

        void QuntisWeb::loop() {
            uint32_t now = millis();
            if (scanning_) {
                if (scan_event_received.exchange(false)) {
                    scan_done_ms_ = now;
                }
                bool settled = scan_done_ms_ != 0 && now - scan_done_ms_ >= SCAN_SETTLE_MS;
                if (settled || now - scan_started_ms_ >= SCAN_TIMEOUT_MS) {
                    scanning_ = false;
                }
            }
            if (now - last_refresh_ms_ >= STATE_REFRESH_MS) {
                last_refresh_ms_ = now;
                refresh_state_();
            }
        }

        quntis_0213::Quntis0213Light* QuntisWeb::output_() const {
            return static_cast<quntis_0213::Quntis0213Light*>(light_->get_output());
        }

        void QuntisWeb::refresh_state_() {
            const auto& values = light_->remote_values;
            // The color temperature is 0 until the light has one, right after the first boot.
            float mireds = values.get_color_temperature();
            int kelvin = mireds > 0.0f ? (int) std::lround(1000000.0f / mireds) : 4000;
            auto* wifi_component = wifi::global_wifi_component;

            char ssid[wifi::SSID_BUFFER_SIZE];
            char escaped_ssid[32 * JSON_ESCAPE_MAX_EXPANSION + 1];
            json_escape_into_buffer(escaped_ssid, StringRef(wifi_component->wifi_ssid_to(ssid)));

            char ip[network::IP_ADDRESS_BUFFER_SIZE] = "";
            for (const auto& address : wifi_component->wifi_sta_ip_addresses()) {
                if (address.is_set()) {
                    address.str_to(ip);
                    break;
                }
            }

            char escaped_name[64 * JSON_ESCAPE_MAX_EXPANSION + 1];
            json_escape_into_buffer(escaped_name, StringRef(App.get_friendly_name()));

            char mac[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
            get_mac_address_pretty_into_buffer(mac);

            // -1: no update entity. 0: up to date or not checked. 1: available. 2: installing.
            int update_state = -1;
            char escaped_latest[32 * JSON_ESCAPE_MAX_EXPANSION + 1] = "";
#ifdef USE_UPDATE
            if (auto* entity = firmware_update()) {
                update_state = entity->state == update::UPDATE_STATE_AVAILABLE    ? 1
                               : entity->state == update::UPDATE_STATE_INSTALLING ? 2
                                                                                  : 0;
                const std::string& latest = entity->update_info.latest_version;
                json_escape_into_buffer(escaped_latest, StringRef(latest.c_str(), std::min<size_t>(latest.size(), 32)));
            }
#endif

            char buffer[1408];
            snprintf(buffer, sizeof(buffer),
                     "{\"on\":%d,\"brightness\":%d,\"kelvin\":%d,\"backlight\":%d,\"auto_dim\":%d,"
                     "\"radio\":%d,\"channel\":%u,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mac\":\"%s\","
                     "\"uptime\":%u,\"heap\":%u,\"name\":\"%s\",\"version\":\"%s\",\"scanning\":%d,"
                     "\"remote\":%d,\"learning\":%d,\"remote_age\":%d,\"min\":%d,\"remote_last\":\"%02X %02X %02X %02X\",\"auth\":%d,\"timer\":%u,\"auto_temp\":%d,\"clock\":%d,"
                     "\"fw\":\"%s\",\"repo\":\"%s\",\"update\":%d,\"latest\":\"%s\"}",
                     values.is_on(), (int) std::lround(values.get_brightness() * 100.0f),
                     kelvin, output_()->get_backlight(),
                     output_()->get_auto_dim(), output_()->is_radio_ok(), output_()->get_channel(), escaped_ssid,
                     ip, wifi_component->wifi_rssi(), mac, (unsigned) (millis() / 1000),
                     (unsigned) esp_get_free_heap_size(), escaped_name, ESPHOME_VERSION,
                     scanning_.load(), output_()->has_remote(), output_()->is_learning(),
                     output_()->last_remote_age(), std::max(1, (int) std::lround(output_()->get_min_level() * 100.0f / 255.0f)), output_()->last_remote_values()[0],
                     output_()->last_remote_values()[1], output_()->last_remote_values()[2],
                     output_()->last_remote_values()[3], yaml_password_ != nullptr,
                     (unsigned) output_()->get_timer_minutes(), output_()->get_auto_temperature(),
                     output_()->has_clock(), QUNTIS_VERSION, QUNTIS_REPO_URL, update_state, escaped_latest);

            LockGuard guard(state_lock_);
            state_json_ = buffer;
        }

        void QuntisWeb::start_scan_() {
            if (scanning_ || wifi::global_wifi_component->is_roaming_scan_active()) {
                return;
            }
            if (!scan_handler_registered_) {
                esp_event_handler_instance_t instance;
                esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, on_scan_done, nullptr, &instance);
                scan_handler_registered_ = true;
            }

            wifi_scan_config_t config{};
            config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            config.scan_time.active.min = 100;
            config.scan_time.active.max = 300;
            // Returns to the home channel between channels, so the connection stays up.
            config.coex_background_scan = true;

            scan_event_received = false;
            scan_done_ms_ = 0;
            esp_err_t err = esp_wifi_scan_start(&config, false);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "WiFi scan failed to start: %s", esp_err_to_name(err));
                return;
            }
            scan_started_ms_ = millis();
            scanning_ = true;
        }

        bool QuntisWeb::CaptiveForwarder::canHandle(AsyncWebServerRequest* request) const {
#ifdef USE_CAPTIVE_PORTAL
            auto* portal = captive_portal::global_captive_portal;
            return !wifi::global_wifi_component->is_connected() && portal != nullptr && portal->is_active() &&
                   request->method() == HTTP_GET;
#else
            return false;
#endif
        }

        void QuntisWeb::CaptiveForwarder::handleRequest(AsyncWebServerRequest* request) {
#ifdef USE_CAPTIVE_PORTAL
            captive_portal::global_captive_portal->handleRequest(request);
#endif
        }

        bool QuntisWeb::canHandle(AsyncWebServerRequest* request) const {
            // Connected, this page answers everything, so web_server's own API stays closed.
            return wifi::global_wifi_component->is_connected();
        }

        void QuntisWeb::handleRequest(AsyncWebServerRequest* request) {

            char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
            StringRef url = request->url_to(buffer);

            if (request->method() == HTTP_GET) {
                if (url == "/") {
                    auto* response = request->beginResponse(200, "text/html; charset=utf-8", QUNTIS_PAGE_GZ,
                                                            QUNTIS_PAGE_GZ_SIZE);
                    response->addHeader("Content-Encoding", "gzip");
                    // Keeps a browser from showing the old page after an update.
                    response->addHeader("Cache-Control", "no-cache");
                    request->send(response);
                } else if (url == "/api/state") {
                    std::string json;
                    {
                        LockGuard guard(state_lock_);
                        json = state_json_;
                    }
                    request->send(200, "application/json", json.c_str());
                } else if (url == "/api/networks") {
                    handle_networks_(request);
                } else if (url == "/api/presets") {
                    handle_presets_(request);
                } else {
                    request->send(404);
                }
                return;
            }

            // Browsers refuse to add this header to a request sent from another site, which
            // stops a web page from changing the lamp or its WiFi behind the user's back.
            if (request->method() != HTTP_POST || !request->hasHeader("X-Quntis")) {
                request->send(401);
                return;
            }

            if (url == "/api/light") {
                handle_light_(request);
            } else if (url == "/api/backlight") {
                bool on = request->arg("on") == "1";
                defer([this, on]() { output_()->set_backlight(on); });
                request->send(200);
            } else if (url == "/api/auto_dim") {
                bool on = request->arg("on") == "1";
                defer([this, on]() { output_()->set_auto_dim(on); });
                request->send(200);
            } else if (url == "/api/preset") {
                auto key = parse_number<uint32_t>(request->arg("key"));
                if (!key.has_value()) {
                    request->send(400);
                    return;
                }
                defer([key]() {
#ifdef USE_BUTTON
                    for (auto* button : App.get_buttons()) {
                        if (button->get_object_id_hash() == *key &&
                            button->get_entity_category() == ENTITY_CATEGORY_NONE) {
                            button->press();
                        }
                    }
#endif
                });
                request->send(200);
            } else if (url == "/api/timer") {
                uint32_t minutes = parse_number<uint32_t>(request->arg("minutes")).value_or(0);
                defer([this, minutes]() { output_()->start_timer(std::min<uint32_t>(minutes, 24 * 60)); });
                request->send(200);
            } else if (url == "/api/auto_temp") {
                bool on = request->arg("on") == "1";
                defer([this, on]() { output_()->set_auto_temperature(on); });
                request->send(200);
            } else if (url == "/api/resync") {
                defer([this]() { output_()->resync(); });
                request->send(200);
#ifdef USE_UPDATE
            } else if (url == "/api/update_check" || url == "/api/update") {
                bool install = url == "/api/update";
                defer([install]() {
                    if (auto* entity = firmware_update()) {
                        if (install) {
                            entity->perform();
                        } else {
                            entity->check();
                        }
                    }
                });
                request->send(200);
#endif
            } else if (url == "/api/learn") {
                defer([this]() { output_()->start_learning(); });
                request->send(200);
            } else if (url == "/api/scan") {
                defer([this]() { start_scan_(); });
                request->send(200);
            } else if (url == "/api/wifi") {
                handle_wifi_(request);
            } else if (url == "/api/password") {
                handle_password_(request);
            } else if (url == "/api/restart") {
                request->send(200);
                set_timeout(500, []() { App.safe_reboot(); });
            } else {
                request->send(404);
            }
        }

        void QuntisWeb::handle_light_(AsyncWebServerRequest* request) {
            auto on = request->arg("on");
            auto brightness = parse_number<float>(request->arg("brightness"));
            auto kelvin = parse_number<float>(request->arg("kelvin"));
            defer([this, on, brightness, kelvin]() {
                auto call = light_->make_call();
                if (!on.empty()) {
                    call.set_state(on == "1");
                }
                if (brightness.has_value() && std::isfinite(*brightness)) {
                    call.set_brightness(clamp(*brightness, 0.0f, 100.0f) / 100.0f);
                }
                if (kelvin.has_value() && std::isfinite(*kelvin) && *kelvin > 0.0f) {
                    call.set_color_temperature(1000000.0f / *kelvin);
                }
                call.perform();
            });
            request->send(200);
        }

        void QuntisWeb::handle_networks_(AsyncWebServerRequest* request) {
            AsyncResponseStream* stream = request->beginResponseStream("application/json");
            stream->print("[");
            {
                char escaped_ssid[32 * JSON_ESCAPE_MAX_EXPANSION + 1];
                wifi::ScanResultsLock lock(wifi::global_wifi_component);
                const auto& results = wifi::global_wifi_component->get_scan_result();
                bool first = true;
                for (const auto& scan : results) {
                    bool with_auth = false;
                    if (!wifi::should_show_scan_entry(results, scan, with_auth)) {
                        continue;
                    }
                    json_escape_into_buffer(escaped_ssid, scan.get_ssid());
                    stream->printf(R"(%s{"ssid":"%s","rssi":%d,"lock":%d})", first ? "" : ",", escaped_ssid,
                                   scan.get_rssi(), with_auth);
                    first = false;
                }
            }
            stream->print("]");
            request->send(stream);
        }

        // Buttons without a category, such as "Lecture" or "Soir", are shown as presets.
        void QuntisWeb::handle_presets_(AsyncWebServerRequest* request) {
            AsyncResponseStream* stream = request->beginResponseStream("application/json");
            stream->print("[");
#ifdef USE_BUTTON
            char escaped[64 * JSON_ESCAPE_MAX_EXPANSION + 1];
            bool first = true;
            for (auto* button : App.get_buttons()) {
                if (button->get_entity_category() != ENTITY_CATEGORY_NONE) {
                    continue;
                }
                json_escape_into_buffer(escaped, button->get_name());
                stream->printf(R"(%s{"key":%u,"name":"%s"})", first ? "" : ",",
                               (unsigned) button->get_object_id_hash(), escaped);
                first = false;
            }
#endif
            stream->print("]");
            request->send(stream);
        }

        void QuntisWeb::handle_wifi_(AsyncWebServerRequest* request) {
            std::string ssid = request->arg("ssid");
            std::string password = request->arg("password");
            bool password_valid = password.empty() || (password.size() >= 8 && password.size() <= 64);
            if (ssid.empty() || ssid.size() > 32 || !password_valid) {
                request->send(400);
                return;
            }
            ESP_LOGI(TAG, "New WiFi network requested from the web page");
            request->send(200);
            // Lets the answer reach the browser before the connection changes.
            set_timeout(500, [ssid, password]() { wifi::global_wifi_component->save_wifi_sta(ssid, password); });
        }

        void QuntisWeb::handle_password_(AsyncWebServerRequest* request) {
#ifdef USE_WEBSERVER_AUTH
            std::string password = request->arg("password");
            if (yaml_password_ == nullptr || password.size() < 8 || password.size() > 64) {
                request->send(400);
                return;
            }
            request->send(200);
            defer([this, password]() {
                PasswordSettings settings{};
                std::strncpy(settings.password, password.c_str(), sizeof(settings.password) - 1);
                settings.yaml_hash = fnv1_hash(yaml_password_);
                password_pref_.save(&settings);
                global_preferences->sync();
                std::memcpy(password_, settings.password, sizeof(password_));
                base_->set_auth_password(password_);
                ESP_LOGI(TAG, "Web page password changed");
            });
#else
            request->send(404);
#endif
        }

    }  // namespace quntis_web
}  // namespace esphome
