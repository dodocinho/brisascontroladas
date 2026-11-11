#pragma once
#include "SessionProtocol.h"
#include <memory>
#include <vector>
class SessionWebSocketClient {
public:
    struct Config {
        bool enabled = false;
        std::string url, tokenEnvironment, tokenFile;
    };
    SessionWebSocketClient();
    ~SessionWebSocketClient();
    void start(const Config &, const std::string &root);
    void shutdown();
    std::vector<SessionEvent> drain();
    std::string status() const;
    // Bounded, coalesced snapshot; only the network worker writes to the socket.
    // Replayed after each registration, even if local state has not changed.
    void publishState(const integration::Json &state);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
