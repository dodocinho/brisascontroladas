#pragma once
#include "KinectRecorder.h"
#include "SessionProtocol.h"
#include <iostream>
#include <algorithm>
// Main-thread reducer: no filesystem/network/GL, exclusive ownership of the recorder.
// A manual capture is never adopted or stopped by remote events.
class SessionRecordingController {
public:
    void receive(const SessionEvent &e) {
        if (e.type == "sessao.encerrada") {
            if (targetSession == e.sessionId) { targetSession.clear(); targetId.clear(); }
        } else {
            if (targetSession != e.sessionId) attemptedSession.clear();
            targetSession = e.sessionId; targetId = e.recordingId;
            if (e.alreadyRecorded) attemptedSession = e.sessionId;
        }
    }
    void update(KinectRecorder &recorder, const KinectRecorder::Config &base, bool connected) {
        const auto s = recorder.status();
        if (automatic && !s.active) {
            message = s.error.empty() ? "Captura automática finalizada: " + activeSession : "Erro na captura automática: " + s.error;
            automatic = false; activeSession.clear();
        }
        if (automatic && targetSession != activeSession) {
            recorder.stop("session_transition"); message = "Finalizando sessão " + activeSession; return;
        }
        if (s.active) {
            if (!automatic && !targetSession.empty()) message = "Sessão aguardando: gravação manual em andamento";
            return;
        }
        if (targetSession.empty() || attemptedSession == targetSession) return;
        if (!connected) { message = "Sessão aguardando conexão do Kinect: " + targetSession; return; }
        auto config = base;
        config.sessionId = targetSession; config.recordingId = targetId;
        // <= 15 paired segments + optional JPEG fit the canonical 32-file limit.
        config.segmentSeconds = std::max(config.segmentSeconds, (config.maxDurationSeconds + 14) / 15);
        attemptedSession = targetSession;
        if (recorder.start(config)) {
            automatic = true; activeSession = targetSession;
            message = "Automática: sessão=" + activeSession + " gravação=" + targetId;
            std::clog << "[SessionRecording] " << message << '\n';
        } else message = "Falha iniciando gravação automática";
    }
    bool isAutomatic() const { return automatic; }
    bool manualStartAllowed() const { return !automatic && targetSession.empty(); }
    const std::string &status() const { return message; }
private:
    bool automatic = false;
    std::string targetSession, targetId, activeSession, attemptedSession, message;
};
