#include "SessionWebSocketClient.h"
#include <Poco/URI.h>
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
struct SessionWebSocketClient::Impl {
    Config config;
    std::string root;
    std::thread worker;
    std::atomic<bool> stop{false};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::vector<SessionEvent> queue;
    std::string message = "WebSocket desativado";
    void log(const std::string &s) {
        std::lock_guard<std::mutex> lock(mutex); message = s;
        std::clog << "[SessionWebSocket] " << s << '\n';
    }
    void run() {
        int lockFd = -1;
        try {
            integration::fs::create_directories(root);
            lockFd = open((root + "/.session-control.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB)) throw std::runtime_error("Controle de sessão já em uso");
            SessionProtocol protocol(root + "/session-control.json");
            Poco::URI uri(config.url);
            // LAN endpoint or a local SSH tunnel. Never silently downgrade TLS.
            if (uri.getScheme() != "ws" || uri.getHost().empty() || !uri.getUserInfo().empty() ||
                !uri.getQuery().empty() || !uri.getFragment().empty())
                throw std::runtime_error("Use URL ws:// sem credenciais/query, na LAN ou túnel SSH");
            const char *env = std::getenv(config.tokenEnvironment.c_str());
            const std::string token = env ? env : "";
            if (token.empty() || token.find_first_of("\r\n") != std::string::npos)
                throw std::runtime_error("Token ausente/inválido na variável de ambiente configurada");
            const int delays[] = {1, 2, 5, 10, 30}; unsigned attempt = 0;
            while (!stop) {
                HTTPResponse response;
                try {
                    log("Conectando");
                    HTTPClientSession session(uri.getHost(), uri.getPort() ? uri.getPort() : 80);
                    session.setTimeout(Poco::Timespan(5, 0));
                    HTTPRequest request(HTTPRequest::HTTP_GET, uri.getPath().empty() ? "/" : uri.getPath(), HTTPMessage::HTTP_1_1);
                    request.set("Authorization", "Bearer " + token);
                    WebSocket ws(session, request, response);
                    ws.setMaxPayloadSize(16384);
                    ws.setReceiveTimeout(Poco::Timespan(3, 0)); ws.setSendTimeout(Poco::Timespan(3, 0));
                    log("Conectado; aguardando sessao.estado");
                    bool initial = true, fragmented = false;
                    std::string payload;
                    auto last = std::chrono::steady_clock::now();
                    const auto connectedAt = last;
                    while (!stop) {
                        if (!ws.poll(Poco::Timespan(0, 200000), Socket::SELECT_READ)) {
                            if (std::chrono::steady_clock::now() - last > std::chrono::seconds(75) ||
                                (initial && std::chrono::steady_clock::now() - connectedAt > std::chrono::seconds(10)))
                                throw std::runtime_error("Heartbeat/estado ausente");
                            continue;
                        }
                        char buffer[16384]; int flags = 0;
                        const int n = ws.receiveFrame(buffer, sizeof buffer, flags);
                        if ((!n && !flags) || (flags & 15) == WebSocket::FRAME_OP_CLOSE) break;
                        last = std::chrono::steady_clock::now();
                        const int op = flags & 15;
                        if (op == WebSocket::FRAME_OP_PING) { ws.sendFrame(buffer, n, WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_PONG); continue; }
                        if (op == WebSocket::FRAME_OP_PONG) continue;
                        if ((op != WebSocket::FRAME_OP_TEXT && op != WebSocket::FRAME_OP_CONT) ||
                            (op == WebSocket::FRAME_OP_CONT) != fragmented) throw std::runtime_error("Frame inválido");
                        payload.append(buffer, n);
                        if (payload.size() > 16384) throw std::runtime_error("Mensagem excede limite");
                        fragmented = !(flags & WebSocket::FRAME_FLAG_FIN);
                        if (fragmented) continue;
                        SessionEvent event;
                        try { event = SessionProtocol::parse(payload); }
                        catch (...) { log("Mensagem inválida rejeitada"); payload.clear(); continue; }
                        payload.clear();
                        // Persistence failures terminate the connection; never execute uncommitted events.
                        if (protocol.accept(event, initial)) {
                            initial = false; attempt = 0;
                            log("Sincronizado: " + event.type + " sessão=" + (event.sessionId.empty() ? "null" : event.sessionId));
                            std::lock_guard<std::mutex> lock(mutex);
                            if (queue.size() >= 256) throw std::runtime_error("Fila cheia; ressincronizar");
                            queue.push_back(std::move(event));
                        }
                    }
                } catch (...) {
                    // Do not log exception text: network errors may include HTTP data/credentials.
                    if (response.getStatus() == HTTPResponse::HTTP_UNAUTHORIZED || response.getStatus() == HTTPResponse::HTTP_FORBIDDEN)
                        log("Autenticação recusada (401/403); confira o token e reinicie após corrigir");
                    else if (response.getStatus() == HTTPResponse::HTTP_CONFLICT) log("Backend recusou conexão (409): múltiplas sessões ativas");
                    else log("Falha de conexão/protocolo/persistência; captura preservada");
                }
                if (stop) break;
                const int delay = delays[std::min(attempt++, 4u)];
                log(statusPrefix(response) + " Reconexão em " + std::to_string(delay) + "s; captura preservada");
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::seconds(delay), [&] { return stop.load(); });
            }
        } catch (...) { log("Cliente indisponível: confira URL ws://, variável do token, checkpoint e lock local"); }
        if (lockFd >= 0) close(lockFd);
    }
    static std::string statusPrefix(const HTTPResponse &r) {
        return r.getStatus() == HTTPResponse::HTTP_UNAUTHORIZED || r.getStatus() == HTTPResponse::HTTP_FORBIDDEN ? "Autenticação recusada." : "Desconectado.";
    }
};
SessionWebSocketClient::SessionWebSocketClient() : impl(new Impl) {}
SessionWebSocketClient::~SessionWebSocketClient() { shutdown(); }
void SessionWebSocketClient::start(const Config &c, const std::string &root) {
    shutdown(); impl->config = c; impl->root = root; impl->stop = false;
    if (c.enabled) impl->worker = std::thread(&Impl::run, impl.get());
}
void SessionWebSocketClient::shutdown() { impl->stop = true; impl->wake.notify_all(); if (impl->worker.joinable()) impl->worker.join(); }
std::vector<SessionEvent> SessionWebSocketClient::drain() {
    std::lock_guard<std::mutex> lock(impl->mutex); std::vector<SessionEvent> out; out.swap(impl->queue); return out;
}
std::string SessionWebSocketClient::status() const { std::lock_guard<std::mutex> lock(impl->mutex); return impl->message; }
