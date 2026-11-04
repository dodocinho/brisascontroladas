#pragma once
#include "IntegrationSupport.h"
#include <deque>
struct SessionEvent {
    std::string type, eventId, sessionId, recordingId;
    int64_t time = 0;
    bool alreadyRecorded = false;
};
// Used only by network worker. The checkpoint is committed before delivery.
class SessionProtocol {
public:
    explicit SessionProtocol(const std::string &checkpoint);
    static SessionEvent parse(const std::string &payload);
    bool accept(SessionEvent &event, bool initialSnapshot);
private:
    std::string path;
    integration::Json state;
};
