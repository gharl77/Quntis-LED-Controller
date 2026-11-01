#include "quntis_web.h"

#include <esp_event.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <cmath>
#include <cstring>

#include "esphome/components/quntis_0213/quntis_0213.h"
#include "esphome/components/wifi/scan_list.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "page.h"

namespace esphome {
    namespace quntis_web {

        static const char* const TAG = "quntis_web";

        static const uint32_t STATE_REFRESH_MS = 500;
        static const uint32_t SCAN_TIMEOUT_MS = 20000;
        // The WiFi component stores the results from its own loop, just after the driver's event.
        static const uint32_t SCAN_SETTLE_MS = 300;

        static std::atomic<bool> scan_event_received{false};

        static void on_scan_done(void*, esp_event_base_t, int32_t, void*) { scan_event_received = true; }

        void QuntisWeb::setup() {
            base_->init();
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

            char mac[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
            get_mac_address_pretty_into_buffer(mac);

            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                     "{\"on\":%d,\"brightness\":%d,\"kelvin\":%d,\"backlight\":%d,\"auto_dim\":%d,"
                     "\"radio\":%d,\"channel\":%u,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mac\":\"%s\","
                     "\"uptime\":%u,\"heap\":%u,\"name\":\"%s\",\"version\":\"%s\",\"scanning\":%d}",
                     values.is_on(), (int) std::lround(values.get_brightness() * 100.0f),
                     (int) std::lround(1000000.0f / values.get_color_temperature()), output_()->get_backlight(),
                     output_()->get_auto_dim(), !output_()->is_failed(), output_()->get_channel(), escaped_ssid,
                     ip, wifi_component->wifi_rssi(), mac, (unsigned) (millis() / 1000),
                     (unsigned) esp_get_free_heap_size(), App.get_friendly_name().c_str(), ESPHOME_VERSION,
                     scanning_.load());

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

        bool QuntisWeb::canHandle(AsyncWebServerRequest* request) const {
            // Without a connection, the captive portal serves its own WiFi page instead.
            if (!wifi::global_wifi_component->is_connected()) {
                return false;
            }
            char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
            StringRef url = request->url_to(buffer);
            return url == "/" || std::strncmp(url.c_str(), "/api/", 5) == 0;
        }

        void QuntisWeb::handleRequest(AsyncWebServerRequest* request) {
            char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
            StringRef url = request->url_to(buffer);

            if (request->method() == HTTP_GET) {
                if (url == "/") {
                    request->send(200, "text/html", PAGE);
                } else if (url == "/api/state") {
                    std::string json;
                    {
                        LockGuard guard(state_lock_);
                        json = state_json_;
                    }
                    request->send(200, "application/json", json.c_str());
                } else if (url == "/api/networks") {
                    handle_networks_(request);
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
            } else if (url == "/api/scan") {
                defer([this]() { start_scan_(); });
                request->send(200);
            } else if (url == "/api/wifi") {
                handle_wifi_(request);
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
                if (brightness.has_value()) {
                    call.set_brightness(clamp(*brightness, 0.0f, 100.0f) / 100.0f);
                }
                if (kelvin.has_value() && *kelvin > 0.0f) {
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

    }  // namespace quntis_web
}  // namespace esphome
