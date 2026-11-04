#include "SessionProtocol.h"
#include <signal.h>
using namespace integration;
SessionProtocol::SessionProtocol(const std::string &checkpoint) : path(checkpoint) {
    state = {{"watermark", int64_t(0)}, {"seen", Json::array()}, {"desired", ""}, {"recordingId", ""}, {"closed", Json::array()}};
    if (fs::exists(path)) {
        state = Json::parse(read(path));
        if (!state.at("watermark").is_number_integer() || !state.at("seen").is_array() ||
            !state.at("desired").is_string()) throw std::runtime_error("Checkpoint inválido; restaure-o antes de conectar");
    }
}
SessionEvent SessionProtocol::parse(const std::string &payload) {
    if (payload.size() > 16384) throw std::runtime_error("Evento excede 16 KiB");
    auto j = Json::parse(payload);
    if (!j.is_object() || j.size() != 5 || !j.at("v").is_number_integer() || j.at("v") != 1)
        throw std::runtime_error("Envelope/versão inválido");
    SessionEvent e;
    e.type = j.at("type").get<std::string>(); e.eventId = j.at("eventId").get<std::string>();
    if (e.type != "sessao.iniciada" && e.type != "sessao.encerrada" && e.type != "sessao.estado")
        throw std::runtime_error("Tipo de evento desconhecido");
    if (!uuidValid(e.eventId)) throw std::runtime_error("eventId inválido");
    if (j.at("sessaoId").is_null()) {
        if (e.type != "sessao.estado") throw std::runtime_error("Sessão obrigatória");
    } else {
        e.sessionId = j.at("sessaoId").get<std::string>();
        if (!uuidValid(e.sessionId)) throw std::runtime_error("sessaoId inválido");
    }
    e.time = timestamp(j.at("timestamp").get<std::string>());
    return e;
}
bool SessionProtocol::accept(SessionEvent &e, bool initialSnapshot) {
    if (initialSnapshot && e.type != "sessao.estado") throw std::runtime_error("Primeiro evento deve ser sessao.estado");
    for (const auto &id : state["seen"]) if (id == e.eventId) return false;
    if (e.time < state.at("watermark").get<int64_t>()) return false;
    auto next = state;
    const auto process = processIdentity(getpid());
    if (!next.contains("closed")) next["closed"] = Json::array();
    bool closed = false;
    for (const auto &id : next["closed"]) if (id == e.sessionId) closed = true;
    if (e.type == "sessao.iniciada" && closed) return false;
    if (e.type == "sessao.encerrada" && !closed) next["closed"].push_back(e.sessionId);
    if (e.type != "sessao.encerrada" && !e.sessionId.empty()) {
        auto id = next.value("recordingId", std::string());
        if (next["desired"] != e.sessionId || !uuidValid(id)) id = uuid();
        const auto capture = fs::path(path).parent_path() / id / "session.json";
        e.alreadyRecorded = closed;
        if (!fs::exists(capture) && fs::exists(capture.parent_path()) && initialSnapshot &&
            next.value("producerIdentity", "") != process) id = uuid();
        if (fs::exists(capture)) {
            const auto local = Json::parse(read(capture));
            const auto status = local.value("state", "");
            if (status == "completed" && local.value("stop_reason", "") == "application_exit" &&
                initialSnapshot && local.value("owner_identity", "") != process && !closed) {
                id = uuid(); // resume the still-active web session as a separate capture
            } else if (status == "completed" || status == "error") e.alreadyRecorded = true;
            else if (initialSnapshot && local.value("owner_identity", "") != process) {
                if (producerAlive(local))
                    throw std::runtime_error("Outro processo ainda possui a captura");
                id = uuid(); // interrupted capture stays quarantined; new segment, never overwrite
            }
        }
        next["recordingId"] = id; e.recordingId = id;
    }
    if (e.type == "sessao.estado" && next["desired"] != e.sessionId && next["desired"] != "")
        next["closed"].push_back(next["desired"]);
    // A close for another session must not change the authoritative target.
    if (e.type != "sessao.encerrada") next["desired"] = e.sessionId;
    else if (next["desired"] == e.sessionId) next["desired"] = "";
    next["watermark"] = e.time;
    next["producerIdentity"] = process;
    next["seen"].push_back(e.eventId);
    if (next["seen"].size() > 256) next["seen"].erase(next["seen"].begin());
    fs::create_directories(fs::path(path).parent_path());
    atomicWrite(path, next.dump(2) + '\n');
    state = std::move(next);

    return true;
}
