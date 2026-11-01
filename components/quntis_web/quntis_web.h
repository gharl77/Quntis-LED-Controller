#pragma once

#include <atomic>
#include <string>

#include "esphome/components/light/light_state.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
    namespace quntis_0213 {
        class Quntis0213Light;
    }

    namespace quntis_web {

        // Serves the lamp's own web page and the small API behind it. Requests arrive on the
        // web server task, so changes are deferred to the main loop and state is read from a
        // snapshot that the main loop refreshes.
        class QuntisWeb : public AsyncWebHandler, public Component {
           public:
            explicit QuntisWeb(web_server_base::WebServerBase* base) : base_(base) {}

            void setup() override;
            void loop() override;
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

            void set_light(light::LightState* light) { light_ = light; }

            bool canHandle(AsyncWebServerRequest* request) const override;
            void handleRequest(AsyncWebServerRequest* request) override;
            bool isRequestHandlerTrivial() const override { return false; }

           protected:
            quntis_0213::Quntis0213Light* output_() const;
            void refresh_state_();
            void start_scan_();
            void handle_light_(AsyncWebServerRequest* request);
            void handle_networks_(AsyncWebServerRequest* request);
            void handle_wifi_(AsyncWebServerRequest* request);

            web_server_base::WebServerBase* base_;
            light::LightState* light_{nullptr};

            Mutex state_lock_;
            std::string state_json_{"{}"};
            uint32_t last_refresh_ms_{0};

            std::atomic<bool> scanning_{false};
            bool scan_handler_registered_{false};
            uint32_t scan_started_ms_{0};
            uint32_t scan_done_ms_{0};
        };

    }  // namespace quntis_web
}  // namespace esphome
