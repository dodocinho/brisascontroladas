#pragma once
#include "IntegrationSupport.h"
struct SessionEvent {
    std::string type, sessionId, recordingId;
    bool alreadyRecorded = false;
    integration::Json session = nullptr;
};
// Network worker only. Persist identity changes before delivery to the main thread.
class SessionProtocol {
public:
    explicit SessionProtocol(const std::string &checkpoint);
    static SessionEvent parse(const std::string &payload);
    bool accept(SessionEvent &event);
private:
    std::string path, producerIdentity;
    integration::Json state;
};
