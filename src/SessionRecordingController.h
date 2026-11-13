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
            if (targetSession != e.sessionId) {
                attemptedSession.clear(); captureRequested.clear(); streamAttempt.clear();
                message.clear();
            }
            targetSession = e.sessionId; targetId = e.recordingId;
            targetMetadata = e.session;
            if (e.alreadyRecorded) attemptedSession = e.sessionId;
        }
        if (!targetSession.empty()) lastSession = targetSession;
    }
    // One camera open request per session, never a retry loop that overrides UI Stop.
    bool takeCaptureRequest() {
        if (targetSession.empty() || captureRequested == targetSession) return false;
        captureRequested = targetSession; return true;
    }
    // Called on the main thread in the same update as recorder.start(). A stream
    // already requested elsewhere is borrowed and must survive session completion.
    template<class Stream>
    void updateStreaming(Stream &stream, bool connected, const std::string &host, int port, int latency) {
        if (targetSession.empty()) {
            if (ownsStream && !automatic) { stream.solicitarParada(); ownsStream = false; }
            return;
        }
        if (streamAttempt == targetSession || !connected) return;
        streamAttempt = targetSession;
        if (!stream.estaLigado()) {
            stream.solicitarInicio(host, port, latency);
            ownsStream = stream.estaLigado();
        }
    }
    void manualStreamingOverride() { ownsStream = false; streamAttempt = targetSession; }
    bool ownsStreaming() const { return ownsStream; }
    void update(KinectRecorder &recorder, const KinectRecorder::Config &base, bool connected) {
        const auto s = recorder.status();
        if (automatic && !s.active) {
            message = s.error.empty() ? "Captura automática finalizada: " + activeSession.substr(0, 5) : "Erro na captura automática: " + s.error;
            automatic = false; activeSession.clear();
        }
        if (automatic && targetSession != activeSession) {
            recorder.stop("session_transition"); message = "Finalizando sessão " + activeSession.substr(0, 5); return;
        }
        if (s.active) {
            if (!automatic && !targetSession.empty()) message = "Sessão aguardando: gravação manual em andamento";
            return;
        }
        if (targetSession.empty() || attemptedSession == targetSession) return;
        if (!connected) { message = "Sessão aguardando conexão do Kinect: " + targetSession.substr(0, 5); return; }
        auto config = base;
        config.sessionId = targetSession; config.recordingId = targetId;
        config.sessionMetadata = targetMetadata.is_null() ? "" : targetMetadata.dump();
        // <= 15 paired segments + optional JPEG fit the canonical 32-file limit.
        config.segmentSeconds = std::max(config.segmentSeconds, (config.maxDurationSeconds + 14) / 15);
        attemptedSession = targetSession;
        if (recorder.start(config)) {
            automatic = true; activeSession = targetSession;
            message = "Automática: sessão=" + activeSession.substr(0, 5) + " gravação=" + targetId;
            std::clog << "[SessionRecording] " << message << '\n';
        } else message = "Falha iniciando gravação automática";
    }
    bool isAutomatic() const { return automatic; }
    bool manualStartAllowed() const { return !automatic && targetSession.empty(); }
    const std::string &status() const { return message; }
    integration::Json deviceState(const KinectRecorder::Status &s, bool connected,
                                  bool transmitting, const std::string &streamError = "") const {
        const auto id = s.active && !automatic ? std::string() :
            (automatic ? activeSession : (!targetSession.empty() ? targetSession : lastSession));
        integration::Json state = {{"tipo", "dispositivo.estado"}, {"versao", 1}, {"cliente", "openframeworks"},
            {"sessaoId", id.empty() ? integration::Json(nullptr) : integration::Json(id)},
            {"gravando", s.accepting && s.frames > 0 && s.error.empty()},
            {"modo", automatic ? "automatico" : "manual"},
            {"transmitindo", transmitting}, {"finalizando", s.active && !s.accepting},
            {"mensagem", message}};
        std::string error;
        if ((automatic || attemptedSession == id) && !id.empty()) error = s.error;
        if (!targetSession.empty() && !connected) error += (error.empty() ? "" : "; ") + std::string("Kinect desconectado");
        if (!streamError.empty()) error += (error.empty() ? "" : "; ") + streamError;
        if (!error.empty()) state["erro"] = error;
        return state;
    }
private:
    bool automatic = false;
    bool ownsStream = false;
    integration::Json targetMetadata = nullptr;
    std::string targetSession, targetId, activeSession, attemptedSession, message;
    std::string captureRequested, streamAttempt, lastSession;
};
