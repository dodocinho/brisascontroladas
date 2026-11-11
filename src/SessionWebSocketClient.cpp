#include "SessionWebSocketClient.h"
#include <Poco/URI.h>
#include <Poco/Exception.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/WebSocket.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <sys/file.h>
using namespace Poco::Net;
using integration::Json;
using Clock = std::chrono::steady_clock;
namespace {
std::string safeDiagnostic(std::string value, const std::string &token) {
    if (!token.empty()) {
        for (auto at = value.find(token); at != std::string::npos; at = value.find(token, at + 8))
            value.replace(at, token.size(), "[secret]");
    }
    for (char &c : value) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    return value.substr(0, 180);
}
std::string readToken(const SessionWebSocketClient::Config &config) {
    if (!config.tokenEnvironment.empty() && !config.tokenFile.empty())
        throw std::runtime_error("Configure somente tokenEnvironment ou tokenFile");
    std::string token;
    if (!config.tokenEnvironment.empty()) {
        const auto *env = std::getenv(config.tokenEnvironment.c_str());
        if (env) token = env;
    } else if (!config.tokenFile.empty()) {
        // Limit memory and never print file contents or include them in exceptions.
        std::ifstream input(config.tokenFile, std::ios::binary);
        char buffer[4097]; input.read(buffer, sizeof buffer);
        token.assign(buffer, static_cast<size_t>(input.gcount()));
        while (!token.empty() && (token.back() == '\r' || token.back() == '\n')) token.pop_back();
    }
    if (token.empty() || token.size() > 4096 ||
        std::any_of(token.begin(), token.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw std::runtime_error("Token ausente/inválido; configure a credencial do dispositivo");
    return token;
}
}
struct SessionWebSocketClient::Impl {
    Config config;
    std::string root;
    std::thread worker;
    std::atomic<bool> stop{false};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::vector<SessionEvent> queue;
    std::string message = "WebSocket desativado";
    Json deviceState = nullptr;
    uint64_t stateRevision = 0;
    void log(const std::string &s) {
        { std::lock_guard<std::mutex> lock(mutex); message = s; }
        std::clog << "[SessionWebSocket] " << s << '\n';
        // Small persistent diagnostics, bounded independently of recordings.
        try {
            const auto path = integration::fs::path(root) / "session-websocket.log";
            if (integration::fs::exists(path) && integration::fs::file_size(path) > 256 * 1024)
                integration::fs::rename(path, path.string() + ".previous");
            std::ofstream out(path, std::ios::app);
            out << integration::utc(integration::nowMs()) << ' ' << s << '\n';
        } catch (...) { /* Diagnostics must never stop capture/control. */ }
    }
    void run() {
        int lockFd = -1;
        try {
            integration::fs::create_directories(root);
            lockFd = open((root + "/.session-control.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB)) throw std::runtime_error("Controle de sessão já em uso");
            SessionProtocol protocol(root + "/session-control.json");
            Poco::URI uri(config.url);
            if (uri.getScheme() != "ws" || uri.getHost().empty() || !uri.getUserInfo().empty() ||
                !uri.getQuery().empty() || !uri.getFragment().empty())
                throw std::runtime_error("Use URL ws:// sem credenciais/query, na LAN ou túnel SSH");
            const int delays[] = {1, 2, 5, 10, 30}; unsigned attempt = 0;
            std::string serverSession, endedSession;
            while (!stop) {
                HTTPResponse response;
                std::string token, diagnostic;
                bool credentialFailure = false, registered = false;
                auto connectedAt = Clock::time_point{};
                try {
                    try { token = readToken(config); }
                    catch (...) { credentialFailure = true; throw; }
                    log("Conectando");
                    HTTPClientSession session(uri.getHost(), uri.getPort() ? uri.getPort() : 80);
                    session.setTimeout(Poco::Timespan(5, 0));
                    HTTPRequest request(HTTPRequest::HTTP_GET, uri.getPath().empty() ? "/" : uri.getPath(), HTTPMessage::HTTP_1_1);
                    request.set("Authorization", "Bearer " + token);
                    WebSocket ws(session, request, response);
                    ws.setMaxPayloadSize(16384);
                    ws.setReceiveTimeout(Poco::Timespan(3, 0)); ws.setSendTimeout(Poco::Timespan(3, 0));
                    const std::string registration = R"({"tipo":"cliente.registrar","cliente":"openframeworks","versao":1})";
                    ws.sendFrame(registration.data(), int(registration.size()));
                    log("Conectado; registro enviado / aguardando confirmação");
                    bool fragmented = false, publicSnapshot = false;
                    std::string payload;
                    connectedAt = Clock::now();
                    auto last = connectedAt, lastPing = connectedAt;
                    uint64_t sentRevision = 0;
                    while (!stop) {
                        const auto now = Clock::now();
                        if (now - last > std::chrono::seconds(75)) throw std::runtime_error("Heartbeat ausente por 75s");
                        if (!registered && now - connectedAt > std::chrono::seconds(10))
                            throw std::runtime_error("Registro não confirmado pelo backend em 10s");
                        Json pending; uint64_t revision;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            revision = stateRevision;
                            if (registered && revision != sentRevision) pending = deviceState;
                        }
                        if (pending.is_object()) {
                            const auto id = pending.value("sessaoId", Json(nullptr));
                            // The backend accepts session-scoped telemetry, never idle/manual null IDs
                            // nor a delayed status from the session preceding its current snapshot.
                            if (id.is_string() && ((!serverSession.empty() && id == serverSession) ||
                                (!endedSession.empty() && id == endedSession && !pending.value("gravando", false)))) {
                                const auto bytes = pending.dump();
                                ws.sendFrame(bytes.data(), int(bytes.size()));
                            }
                            sentRevision = revision;
                        }
                        if (now - lastPing >= std::chrono::seconds(20)) {
                            ws.sendFrame("", 0, WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_PING);
                            lastPing = now;
                        }
                        if (!ws.poll(Poco::Timespan(0, 200000), Socket::SELECT_READ)) continue;
                        char buffer[16384]; int flags = 0;
                        const int n = ws.receiveFrame(buffer, sizeof buffer, flags);
                        if (!n && !flags) { diagnostic = "Conexão encerrada sem frame de fechamento (EOF)"; break; }
                        last = Clock::now();
                        const int op = flags & 15;
                        if (op == WebSocket::FRAME_OP_CLOSE) {
                            const unsigned code = n >= 2 ? (static_cast<unsigned char>(buffer[0]) << 8) |
                                static_cast<unsigned char>(buffer[1]) : 1005;
                            diagnostic = "Backend fechou WebSocket: código=" + std::to_string(code);
                            if (n > 2) diagnostic += " motivo=" + safeDiagnostic(std::string(buffer + 2, n - 2), token);
                            credentialFailure = code == 1008;
                            try { ws.sendFrame(buffer, n, WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_CLOSE); } catch (...) {}
                            break;
                        }
                        if (op == WebSocket::FRAME_OP_PING) { ws.sendFrame(buffer, n, WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_PONG); continue; }
                        if (op == WebSocket::FRAME_OP_PONG) continue;
                        if ((op != WebSocket::FRAME_OP_TEXT && op != WebSocket::FRAME_OP_CONT) ||
                            (op == WebSocket::FRAME_OP_CONT) != fragmented) throw std::runtime_error("Frame inválido");
                        payload.append(buffer, n);
                        if (payload.size() > 16384) throw std::runtime_error("Mensagem excede limite");
                        fragmented = !(flags & WebSocket::FRAME_FLAG_FIN);
                        if (fragmented) continue;
                        SessionEvent event;
                        try {
                            const auto json = Json::parse(payload);
                            const auto type = json.at("tipo").get<std::string>();
                            if (type == "erro") {
                                log("Backend rejeitou mensagem: " + safeDiagnostic(json.value("codigo", "desconhecido"), token));
                                payload.clear(); continue;
                            }
                            // Shared bus: catalog, people and echoed telemetry are not session commands.
                            if (type != "sessao.estado" && type != "sessao.iniciada" && type != "sessao.encerrada") {
                                payload.clear(); continue;
                            }
                            event = SessionProtocol::parse(payload);
                        } catch (...) { log("Mensagem de sessão inválida rejeitada"); payload.clear(); continue; }
                        payload.clear();
                        if (!registered) {
                            // This backend sends a public snapshot before handling registration,
                            // then a second snapshot on successful authenticated registration.
                            // Never start a recording from the public snapshot preceding a 1008.
                            if (event.type != "sessao.estado") continue;
                            if (!publicSnapshot) { publicSnapshot = true; continue; }
                            registered = true;
                            log("Registro confirmado; controle de sessão sincronizado");
                        }
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (queue.size() >= 256) throw std::runtime_error("Fila cheia; ressincronizar");
                        }
                        if (protocol.accept(event)) {
                            if (event.type != "sessao.encerrada") {
                                if (event.sessionId.empty()) { if (!serverSession.empty()) endedSession = serverSession; }
                                else endedSession.clear();
                                serverSession = event.sessionId;
                            } else if (event.sessionId == serverSession) {
                                endedSession = serverSession; serverSession.clear();
                            }
                            sentRevision = 0; // re-evaluate the latest state against the authoritative session
                            log("Sincronizado: " + event.type + " sessão=" + (event.sessionId.empty() ? "null" : event.sessionId));
                            std::lock_guard<std::mutex> lock(mutex);
                            queue.push_back(std::move(event));
                        }
                    }
                    if (stop) { try { ws.shutdown(); } catch (...) {} }
                } catch (const Poco::Exception &e) {
                    // Exception names are useful without exposing HTTP headers/credentials.
                    diagnostic = std::string("Falha de rede: ") + e.name();
                } catch (const std::exception &e) { diagnostic = safeDiagnostic(e.what(), token); }
                if (stop) break;
                if (response.getStatus() == HTTPResponse::HTTP_UNAUTHORIZED || response.getStatus() == HTTPResponse::HTTP_FORBIDDEN) {
                    diagnostic = "Autenticação recusada (401/403); confira a credencial"; credentialFailure = true;
                }
                // A valid snapshot alone cannot reset backoff: rapid closes must back off too.
                if (registered && Clock::now() - connectedAt >= std::chrono::seconds(30)) attempt = 0;
                if (credentialFailure) attempt = 4;
                const int delay = delays[attempt];
                attempt = std::min(attempt + 1, 4u);
                log(diagnostic + "; reconexão em " + std::to_string(delay) + "s; captura preservada");
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::seconds(delay), [&] { return stop.load(); });
            }
        } catch (const std::exception &e) { log(std::string("Cliente indisponível: ") + e.what()); }
        if (lockFd >= 0) close(lockFd);
    }
};
SessionWebSocketClient::SessionWebSocketClient() : impl(new Impl) {}
SessionWebSocketClient::~SessionWebSocketClient() { shutdown(); }
void SessionWebSocketClient::start(const Config &c, const std::string &root) {
    shutdown(); impl->config = c; impl->root = root; impl->stop = false;
    { std::lock_guard<std::mutex> lock(impl->mutex); impl->queue.clear();
      impl->message = c.enabled ? "Conectando" : "WebSocket desativado"; }
    if (c.enabled) impl->worker = std::thread(&Impl::run, impl.get());
}
void SessionWebSocketClient::shutdown() { impl->stop = true; impl->wake.notify_all(); if (impl->worker.joinable()) impl->worker.join(); }
std::vector<SessionEvent> SessionWebSocketClient::drain() {
    std::lock_guard<std::mutex> lock(impl->mutex); std::vector<SessionEvent> out; out.swap(impl->queue); return out;
}
std::string SessionWebSocketClient::status() const { std::lock_guard<std::mutex> lock(impl->mutex); return impl->message; }
void SessionWebSocketClient::publishState(const Json &state) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->deviceState != state) { impl->deviceState = state; ++impl->stateRevision; }
}
