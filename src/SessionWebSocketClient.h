#pragma once
#include "SessionProtocol.h"
#include <memory>
#include <vector>
class SessionWebSocketClient {
public:
    struct Config {
        bool enabled = false;
        std::string url, tokenEnvironment = "BRISAS_GRAVADOR_TOKEN";
    };
    SessionWebSocketClient();
    ~SessionWebSocketClient();
    void start(const Config &, const std::string &root);
    void shutdown();
    std::vector<SessionEvent> drain();
    std::string status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
