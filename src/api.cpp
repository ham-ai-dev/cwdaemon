#include "api.hpp"
#include "config.hpp"
#include <drogon/WebSocketController.h>
#include <drogon/HttpController.h>
#include <nlohmann/json.hpp>

using namespace drogon;

std::shared_ptr<AudioCapture> ApiServer::audio_sys = nullptr;
std::shared_ptr<RigCtl> ApiServer::rigctl_sys = nullptr;
std::atomic<float> ApiServer::current_wpm(0);
std::atomic<float> ApiServer::current_snr(0);
std::atomic<float> ApiServer::current_freq(0);

// Global WebSocket connection tracking
static std::mutex ws_mutex;
static std::vector<WebSocketConnectionPtr> decoded_clients;
static std::vector<WebSocketConnectionPtr> metrics_clients;

class DecodedWsController : public drogon::WebSocketController<DecodedWsController> {
public:
    void handleNewMessage(const WebSocketConnectionPtr&, std::string&&, const WebSocketMessageType&) override {}
    void handleNewConnection(const HttpRequestPtr&, const WebSocketConnectionPtr& conn) override {
        std::lock_guard<std::mutex> lock(ws_mutex);
        decoded_clients.push_back(conn);
    }
    void handleConnectionClosed(const WebSocketConnectionPtr& conn) override {
        std::lock_guard<std::mutex> lock(ws_mutex);
        decoded_clients.erase(std::remove(decoded_clients.begin(), decoded_clients.end(), conn), decoded_clients.end());
    }
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/decoded");
    WS_PATH_LIST_END
};

class MetricsWsController : public drogon::WebSocketController<MetricsWsController> {
public:
    void handleNewMessage(const WebSocketConnectionPtr&, std::string&&, const WebSocketMessageType&) override {}
    void handleNewConnection(const HttpRequestPtr&, const WebSocketConnectionPtr& conn) override {
        std::lock_guard<std::mutex> lock(ws_mutex);
        metrics_clients.push_back(conn);
    }
    void handleConnectionClosed(const WebSocketConnectionPtr& conn) override {
        std::lock_guard<std::mutex> lock(ws_mutex);
        metrics_clients.erase(std::remove(metrics_clients.begin(), metrics_clients.end(), conn), metrics_clients.end());
    }
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/metrics");
    WS_PATH_LIST_END
};

ApiServer::ApiServer(std::shared_ptr<AudioCapture> audio, std::shared_ptr<RigCtl> rigctl) {
    audio_sys = audio;
    rigctl_sys = rigctl;
    is_running_ = false;
}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::start(int port) {
    if (is_running_) return;

    drogon::app().addListener("0.0.0.0", port);
    
    // GET /api/config
    drogon::app().registerHandler("/api/config",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto conf = ConfigManager::instance().get_config();
            nlohmann::json j;
            j["audio_device"] = conf.audio_device;
            j["sample_rate"] = conf.sample_rate;
            j["buffer_size"] = conf.buffer_size;
            j["tone_frequency"] = conf.tone_frequency;
            j["agc_decay"] = conf.agc_decay;
            j["wpm_min"] = conf.wpm_min;
            j["wpm_max"] = conf.wpm_max;
            j["api_port"] = conf.api_port;
            j["log_level"] = conf.log_level;
            j["rigctl_enabled"] = conf.rigctl_enabled;
            j["rigctl_host"] = conf.rigctl_host;
            j["rigctl_port"] = conf.rigctl_port;
            j["rigctl_poll_interval_ms"] = conf.rigctl_poll_interval_ms;

            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(j.dump());
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            callback(resp);
        }, {Get});

    // POST /api/config
    drogon::app().registerHandler("/api/config",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto body = req->getBody();
            try {
                auto j = nlohmann::json::parse(body.data(), body.data() + body.length());
                auto conf = ConfigManager::instance().get_config();
                
                if (j.contains("audio_device")) conf.audio_device = j["audio_device"];
                if (j.contains("sample_rate")) conf.sample_rate = j["sample_rate"];
                if (j.contains("tone_frequency")) conf.tone_frequency = j["tone_frequency"];
                // and so on for all fields...
                
                ConfigManager::instance().update_config(conf);
                
                auto resp = HttpResponse::newHttpResponse();
                resp->setBody("{\"status\":\"success\"}");
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                callback(resp);
            } catch (...) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setBody("{\"status\":\"error\"}");
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                callback(resp);
            }
        }, {Post});

    // GET /api/status
    drogon::app().registerHandler("/api/status",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            nlohmann::json j;
            j["wpm"] = current_wpm.load();
            j["snr"] = current_snr.load();
            j["tone_freq"] = current_freq.load();
            if (rigctl_sys) {
                j["rig_freq"] = rigctl_sys->get_frequency();
                j["rig_mode"] = rigctl_sys->get_mode();
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(j.dump());
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            callback(resp);
        }, {Get});

    // GET /api/devices
    drogon::app().registerHandler("/api/devices",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            auto devices = AudioCapture::get_devices();
            nlohmann::json j = devices;
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(j.dump());
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            callback(resp);
        }, {Get});

    // POST /api/decoder/start
    drogon::app().registerHandler("/api/decoder/start",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            if (audio_sys) audio_sys->start();
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("{\"status\":\"started\"}");
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            callback(resp);
        }, {Post});

    // POST /api/decoder/stop
    drogon::app().registerHandler("/api/decoder/stop",
        [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
            if (audio_sys) audio_sys->stop();
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("{\"status\":\"stopped\"}");
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            callback(resp);
        }, {Post});

    is_running_ = true;
    thread_ = std::thread([]() {
        drogon::app().run();
    });
}

void ApiServer::stop() {
    if (!is_running_) return;
    drogon::app().quit();
    if (thread_.joinable()) thread_.join();
    is_running_ = false;
}

void ApiServer::broadcast_decoded_char(char c) {
    std::string s(1, c);
    std::lock_guard<std::mutex> lock(ws_mutex);
    for (auto& client : decoded_clients) {
        client->send(s);
    }
}

void ApiServer::broadcast_metrics(float snr, float wpm, float freq, float agc) {
    nlohmann::json j;
    j["snr"] = snr;
    j["wpm"] = wpm;
    j["freq"] = freq;
    j["agc"] = agc;
    
    std::string s = j.dump();
    std::lock_guard<std::mutex> lock(ws_mutex);
    for (auto& client : metrics_clients) {
        client->send(s);
    }
}
