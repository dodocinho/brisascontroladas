#include "SessionProtocol.h"
using namespace integration;
SessionProtocol::SessionProtocol(const std::string &checkpoint)
    : path(checkpoint), producerIdentity(processIdentity(getpid())) {
    state = {{"desired", ""}, {"recordingId", ""}, {"closed", Json::array()}, {"producerIdentity", ""}};
    if (fs::exists(path)) {
        const auto saved = Json::parse(read(path));
        // Read only the current fields; existing checkpoints keep their recording IDs.
        for (auto it = state.begin(); it != state.end(); ++it)
            if (saved.contains(it.key())) it.value() = saved.at(it.key());
        if (!state.at("desired").is_string() || !state.at("recordingId").is_string() ||
            !state.at("producerIdentity").is_string() || !state.at("closed").is_array())
            throw std::runtime_error("Checkpoint inválido");
        for (const auto &id : state.at("closed"))
            if (!id.is_string() || !sessionIdValid(id.get<std::string>())) throw std::runtime_error("Checkpoint inválido");
    }
}
SessionEvent SessionProtocol::parse(const std::string &payload) {
    if (payload.size() > 16384) throw std::runtime_error("Evento excede 16 KiB");
    const auto j = Json::parse(payload);
    SessionEvent e;
    e.type = j.at("tipo").get<std::string>();
    if (e.type != "sessao.iniciada" && e.type != "sessao.estado" && e.type != "sessao.encerrada")
        throw std::runtime_error("Tipo de evento desconhecido");
    if ((e.type != "sessao.encerrada" || j.contains("versao")) &&
        (!j.at("versao").is_number_integer() || j.at("versao") != 1))
        throw std::runtime_error("Versão inválida");
    if (e.type == "sessao.encerrada") e.sessionId = j.at("sessaoId").get<std::string>();
    else {
        e.session = j.at("sessao");
        if (e.session.is_null() && e.type == "sessao.estado") return e;
        const auto &s = e.session;
        e.sessionId = s.at("id").get<std::string>();
        timestamp(s.at("iniciadaEm").get<std::string>());
        const auto &body = s.at("corpoPresente");
        if (!body.is_object() || (s.contains("corpoExpandido") &&
            !s.at("corpoExpandido").is_null() && !s.at("corpoExpandido").is_object()))
            throw std::runtime_error("Participante inválido");
        for (const auto *field : {"participanteId", "elemento", "corPrincipal"})
            if (body.contains(field) && !body.at(field).is_null() && !body.at(field).is_string())
                throw std::runtime_error("Campo do participante inválido");
        if (body.contains("paleta") && !body.at("paleta").is_null()) {
            if (!body.at("paleta").is_array()) throw std::runtime_error("Paleta inválida");
            for (const auto &color : body.at("paleta"))
                if (!color.is_string()) throw std::runtime_error("Cor inválida");
        }
    }
    if (!sessionIdValid(e.sessionId)) throw std::runtime_error("sessaoId inválido");
    return e;
}
bool SessionProtocol::accept(SessionEvent &e) {
    auto next = state;
    auto &closed = next["closed"];
    const auto isClosed = [&](const std::string &id) {
        return std::find(closed.begin(), closed.end(), id) != closed.end();
    };
    if (e.type == "sessao.iniciada" && isClosed(e.sessionId)) return false;
    if (e.type == "sessao.encerrada") {
        if (!isClosed(e.sessionId)) closed.push_back(e.sessionId);
        if (next["desired"] == e.sessionId) next["desired"] = "";
    } else {
        if (!e.sessionId.empty()) {
            auto id = next.at("recordingId").get<std::string>();
            if (next["desired"] != e.sessionId || !uuidValid(id)) id = uuid();
            const auto folder = fs::path(path).parent_path() / id;
            const auto capture = folder / "session.json";
            if (fs::exists(capture)) {
                const auto local = Json::parse(read(capture));
                if (local.value("owner_identity", "") != producerIdentity) {
                    if (producerAlive(local)) throw std::runtime_error("Outro processo ainda possui a captura");
                    id = uuid(); // Restart: new capture, preserving every previous segment.
                } else {
                    const auto status = local.value("state", "");
                    e.alreadyRecorded = status == "completed" || status == "error";
                }
            } else if (fs::exists(folder) && next["producerIdentity"] != producerIdentity) {
                id = uuid(); // Crash before session.json was committed.
            }
            next["recordingId"] = id;
            next["producerIdentity"] = producerIdentity;
            e.recordingId = id;
        }
        const auto previous = next.at("desired").get<std::string>();
        if (e.type == "sessao.estado" && !previous.empty() && previous != e.sessionId && !isClosed(previous))
            closed.push_back(previous);
        next["desired"] = e.sessionId;
    }
    // Repeated public/private snapshots and reconnects need no write/fsync.
    if (next != state) {
        fs::create_directories(fs::path(path).parent_path());
        atomicWrite(path, next.dump(2) + '\n');
        state = std::move(next);
    }
    return true;
}
