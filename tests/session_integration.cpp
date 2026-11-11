#include "SessionWebSocketClient.h"
#include "SessionRecordingController.h"
#include "RecordingTransfer.h"
#include <cassert>
#include <thread>
#include <functional>
using namespace integration;
using Clock = std::chrono::steady_clock;
const std::string a = "33333333-3333-4333-8333-333333333333", b = "44444444-4444-4444-8444-444444444444";
void until(const std::function<bool()> &fn, double seconds = 15) {
    const auto limit = Clock::now() + std::chrono::milliseconds(int(seconds*1000));
    while (!fn()) { assert(Clock::now() < limit); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
}
Json webSession(const std::string &id = a) {
    return {{"id",id}, {"iniciadaEm","2026-11-11T18:00:00.123Z"},
        {"corpoPresente", {{"participanteId","p-123"}, {"elemento","ar"},
            {"corPrincipal","#00aaff"}, {"paleta",Json::array({"#00aaff"})}}}};
}
SessionEvent webEvent(const std::string &type, Json session) {
    return SessionProtocol::parse(Json({{"tipo",type},{"versao",1},{"sessao",session}}).dump());
}
struct FakeStream {
    bool on = false, fail = false;
    int starts = 0, stops = 0;
    bool estaLigado() const { return on; }
    void solicitarInicio(const std::string &, int, int) { ++starts; on = !fail; }
    void solicitarParada() { ++stops; on = false; }
};
void modern(const fs::path &root) {
    fs::create_directories(root);
    SessionProtocol protocol((root / "session-control.json").string());
    auto start = webEvent("sessao.iniciada",webSession("session-opaque-123"));
    assert(protocol.accept(start));
    const auto checkpoint = root/"session-control.json";
    const auto writtenAt = fs::last_write_time(checkpoint);
    auto duplicate = start; assert(protocol.accept(duplicate));
    assert(fs::last_write_time(checkpoint) == writtenAt);
    assert(start.recordingId == duplicate.recordingId);
    auto snapshot = webEvent("sessao.estado",start.session);
    assert(protocol.accept(snapshot) && snapshot.recordingId == start.recordingId);
    assert(fs::last_write_time(checkpoint) == writtenAt);
    const auto stop = SessionProtocol::parse(R"({"tipo":"sessao.encerrada","sessaoId":"session-opaque-123"})");
    for (auto invalid : {Json({{"versao",2}}), Json({{"versao",1.0}}), Json({{"sessao",nullptr}}),
            Json({{"sessao",{{"id",a}}}}), Json({{"sessao",{{"id",a},{"iniciadaEm","bad"},{"corpoPresente",Json::object()}}}})}) {
        Json j = {{"tipo","sessao.iniciada"},{"versao",1},{"sessao",webSession()}};
        j.update(invalid); bool rejected = false;
        try { SessionProtocol::parse(j.dump()); } catch (...) { rejected = true; }
        assert(rejected);
    }
    KinectRecorder recorder; KinectRecorder::Config config;
    config.outputRoot = root.string(); config.maxDurationSeconds = 15; config.segmentSeconds = 1;
    SessionRecordingController controller; FakeStream stream;
    auto tick = [&](bool connected = true) {
        controller.update(recorder,config,connected);
        controller.updateStreaming(stream,connected,"127.0.0.1",9494,20);
    };
    controller.receive(start);
    assert(controller.takeCaptureRequest() && !controller.takeCaptureRequest());
    tick(false); assert(!recorder.status().active && stream.starts == 0);
    assert(controller.deviceState(recorder.status(),false,false).contains("erro"));
    tick(); assert(stream.starts == 1 && controller.ownsStreaming());
    until([&] { return recorder.status().frames > 0; });
    const auto folder = recorder.status().directory;
    assert(controller.deviceState(recorder.status(),true,false).at("gravando") == true);
    assert(controller.deviceState(recorder.status(),true,false).at("transmitindo") == false);
    controller.receive(duplicate); tick(); controller.receive(snapshot); tick();
    assert(stream.starts == 1 && recorder.status().directory == folder);
    controller.receive(SessionProtocol::parse(R"({"tipo":"sessao.encerrada","sessaoId":"other"})")); tick();
    assert(recorder.accepting() && stream.stops == 0);
    controller.receive(stop); tick();
    assert(!recorder.accepting() && stream.stops == 0); // wait for file finalization
    until([&] { tick(); return !recorder.status().active && !stream.on; });
    assert(stream.stops == 1 && controller.manualStartAllowed());
    auto metadata = Json::parse(read(fs::path(folder)/"session.json"));
    assert(metadata.at("sessaoId") == start.sessionId && metadata.at("participanteId") == "p-123");
    assert(metadata.at("sessao") == start.session && metadata.at("state") == "completed");
    auto segment = Json::parse(read(fs::path(folder)/"capture/0.json"));
    assert(segment.at("sessaoId") == start.sessionId && segment.at("fps") == 30);
    assert(fs::exists(fs::path(folder)/segment.at("arquivos").at("rgb").get<std::string>()));
    assert(!controller.deviceState(recorder.status(),true,false).at("gravando").get<bool>());
    // Borrow an existing/manual stream; neither recording failure nor session end owns it.
    SessionRecordingController borrowed; FakeStream manual; manual.on = true;
    borrowed.receive(start); borrowed.updateStreaming(manual,true,"127.0.0.1",9494,20);
    borrowed.receive(stop); borrowed.updateStreaming(manual,true,"127.0.0.1",9494,20);
    assert(manual.on && manual.starts == 0 && manual.stops == 0);
    SessionRecordingController retained; FakeStream keep;
    retained.receive(start); retained.updateStreaming(keep,true,"127.0.0.1",9494,20);
    retained.manualStreamingOverride(); retained.receive(stop);
    retained.updateStreaming(keep,true,"127.0.0.1",9494,20); assert(keep.on && keep.stops == 0);
    // Manual stop suppresses restart even on a duplicate snapshot.
    retained.receive(start); retained.manualStreamingOverride(); keep.solicitarParada();
    retained.receive(snapshot); retained.updateStreaming(keep,true,"127.0.0.1",9494,20);
    assert(!keep.on && keep.starts == 1);
    SessionRecordingController streamFailed; FakeStream unavailable; unavailable.fail = true;
    streamFailed.receive(start); streamFailed.updateStreaming(unavailable,true,"127.0.0.1",9494,20);
    streamFailed.receive(duplicate); streamFailed.updateStreaming(unavailable,true,"127.0.0.1",9494,20);
    assert(unavailable.starts == 1 && !unavailable.on && !streamFailed.ownsStreaming());
    // Physical disconnect uses the existing source's stop path; no crash/restart loop.
    SessionRecordingController disconnected; auto fresh = start; fresh.recordingId = uuid();
    disconnected.receive(fresh); disconnected.update(recorder,config,true);
    until([&] { return recorder.status().frames > 0; });
    recorder.stop("kinect_disconnected");
    until([&] { return !recorder.status().active; }); disconnected.update(recorder,config,false);
    assert(disconnected.deviceState(recorder.status(),false,false).contains("erro"));
    disconnected.receive(fresh); disconnected.update(recorder,config,true);
    assert(!recorder.status().active);
    // Failed asynchronous encoder spawn never claims recording and never retries on duplicates.
    SessionRecordingController failed; auto bad = start; bad.recordingId = uuid();
    config.ffmpeg = "/does-not-exist/brisas-ffmpeg";
    failed.receive(bad); failed.update(recorder,config,true);
    until([&] { return !recorder.status().active; }); failed.update(recorder,config,true);
    auto state = failed.deviceState(recorder.status(),true,false,"SRT indisponível");
    assert(state.at("gravando") == false && state.contains("erro"));
    failed.receive(bad); failed.update(recorder,config,true); assert(!recorder.status().active);
    // An authoritative active snapshot after a dead process creates a new UUID.
    metadata["owner_pid"] = 2147483647; metadata["owner_identity"] = "previous-process";
    atomicWrite(fs::path(folder)/"session.json",metadata.dump());
    SessionProtocol restarted((root/"session-control.json").string());
    auto recovered = webEvent("sessao.estado",start.session);
    assert(restarted.accept(recovered) && !recovered.alreadyRecorded && recovered.recordingId != start.recordingId);
    assert(fs::exists(fs::path(folder)/"session.json"));
    auto closed = stop; assert(protocol.accept(closed));
    assert(!protocol.accept(duplicate));
    // Manual captures report their actual state without attributing them to a web session.
    KinectRecorder::Status manualStatus; manualStatus.active = true;
    manualStatus.accepting = true; manualStatus.frames = 1;
    const auto manualState = borrowed.deviceState(manualStatus,true,true);
    assert(manualState.at("gravando") == true && manualState.at("sessaoId").is_null());
    assert(manualState.at("versao") == 1);
    std::cout << "PASS events v1, metadata, SRT ownership, duplicate starts, capture/encoder failure, recovery\n";
}
SessionEvent event(const std::string &type, const std::string &id) {
    if (type == "sessao.encerrada")
        return SessionProtocol::parse(Json({{"tipo",type},{"sessaoId",id}}).dump());
    return webEvent(type,id.empty() ? Json(nullptr) : webSession(id));
}
void unit(const fs::path &root) {
    modern(root / "modern");
    // Reject the removed envelope; only the current event contract remains.
    for (const auto &text : {"{}", "{", "[]", "null", R"({"v":1,"type":"sessao.estado","sessaoId":null})"}) {
        bool rejected = false; try { SessionProtocol::parse(text); } catch (...) { rejected = true; }
        assert(rejected);
    }
    assert(timestamp("2026-11-03T21:18:00.1Z") == timestamp("2026-11-03T21:18:00.100Z"));
    assert(!processIdentity(getpid()).empty());
    assert(producerAlive(Json({{"owner_pid",getpid()},{"owner_identity",processIdentity(getpid())}})));
    assert(!producerAlive(Json({{"owner_pid",getpid()},{"owner_identity","previous-boot-or-reused-pid"}})));
    // Recording transitions use the real encoder but require no Kinect or GL.
    KinectRecorder recorder; KinectRecorder::Config rc; rc.outputRoot = (root / "captures").string(); rc.maxDurationSeconds = 15; rc.segmentSeconds = 1;
    SessionRecordingController controller;
    auto start = event("sessao.iniciada",a); start.recordingId = uuid();
    controller.receive(start); controller.update(recorder,rc,false); assert(!recorder.status().active);
    assert(recorder.start(rc)); until([&] { return recorder.accepting(); });
    controller.update(recorder,rc,true); assert(!controller.isAutomatic());
    controller.receive(event("sessao.estado","")); controller.update(recorder,rc,true); assert(recorder.accepting());
    recorder.shutdown();
    controller.receive(start); controller.update(recorder,rc,true); until([&] { return recorder.accepting(); });
    const auto folder = recorder.status().directory; assert(fs::path(folder).filename() == start.recordingId);
    assert(!controller.manualStartAllowed());
    controller.receive(event("sessao.encerrada",b)); controller.update(recorder,rc,true); assert(recorder.accepting());
    controller.receive(start); controller.update(recorder,rc,true); assert(recorder.status().directory == folder);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto change = event("sessao.estado",b); change.recordingId = uuid(); controller.receive(change);
    controller.update(recorder,rc,true); assert(!recorder.accepting());
    until([&] { return !recorder.status().active; }); controller.update(recorder,rc,true);
    until([&] { return recorder.accepting(); }); assert(recorder.status().directory != folder);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    controller.receive(event("sessao.estado","")); controller.update(recorder,rc,true);
    until([&] { return !recorder.status().active; }); controller.update(recorder,rc,true);
    assert(controller.manualStartAllowed()); assert(recorder.status().error.empty());
    std::cout << "PASS current protocol, manual conflict, automatic transitions\n";
}
int main(int argc, char **argv) {
    assert(argc >= 3); const std::string mode = argv[1]; const fs::path root = fs::absolute(argv[2]);
    if (mode == "unit") { unit(root); return 0; }
    if (mode == "ws") {
        assert(argc == 5); SessionWebSocketClient client; SessionWebSocketClient::Config c;
        c.enabled = true; c.url = argv[3];
        if (const auto *file = std::getenv("BRISAS_TEST_TOKEN_FILE")) c.tokenFile = file;
        else c.tokenEnvironment = "CORPO_EXPANDIDO_TOKEN";
        client.start(c,root.string());
        Json events = Json::array(); std::string diagnostic;
        KinectRecorder recorder; KinectRecorder::Config rc; rc.outputRoot = root.string(); rc.maxDurationSeconds = 15;
        SessionRecordingController controller;
        bool started = false, stopReceived = false, prematurelyStopped = false;
        const auto end = Clock::now()+std::chrono::seconds(std::stoi(argv[4]));
        while (Clock::now()<end) {
            for (const auto &e : client.drain()) {
                events.push_back({{"type",e.type},{"session",e.sessionId},{"recording",e.recordingId}});
                if (e.type == "sessao.encerrada") stopReceived = true;
                controller.receive(e); controller.update(recorder,rc,true);
                if (e.type == "sessao.iniciada") started = true;
            }
            controller.update(recorder,rc,true);
            if (started && !stopReceived && !recorder.status().active) prematurelyStopped = true;
            client.publishState(controller.deviceState(recorder.status(),true,false));
            diagnostic = client.status(); std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        client.shutdown(); recorder.shutdown();
        std::cout << Json({{"events",events},{"status",diagnostic},{"prematurelyStopped",prematurelyStopped}}).dump() << '\n'; return 0;
    }
    assert(argc == 5); const auto settings = Json::parse(read(argv[3])); const double seconds = std::stod(argv[4]);
    fs::create_directories(root);
    RecordingTransfer transfer; RecordingTransfer::Config tc;
    tc.sshHost = "test-mac"; tc.remoteDirectory = "~/recordings"; tc.retryIntervalSeconds = 1;
    tc.maxRetries = settings.value("maxRetries",0); tc.thumbnailEnabled = settings.value("thumbnail",true);
    tc.enabled = settings.value("enabled",true);
    transfer.start(root.string(),"ffmpeg",tc); if (mode == "retry") transfer.retryFailed();
    KinectRecorder recorder; KinectRecorder::Config rc; rc.outputRoot = root.string();
    rc.segmentSeconds = 1; rc.maxDurationSeconds = 15; rc.sessionId = a; rc.recordingId = uuid();
    if (mode == "record") { assert(recorder.start(rc)); until([&] { return recorder.accepting(); }); }
    std::vector<uint8_t> rgb(640*480*3,80); std::vector<uint16_t> depth(640*480,2000);
    const auto end = Clock::now()+std::chrono::milliseconds(int(seconds*1000));
    while (Clock::now()<end) {
        if (mode == "record") {
            assert(recorder.status().active); const auto ns=KinectRecorder::monotonicNs();
            recorder.submitRgb(rgb.data(),rgb.size(),ns); recorder.submitDepthMm(depth.data(),depth.size(),ns);
            // Auto jobs must never publish even already closed segments during capture.
            assert(transfer.status().completed == 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    recorder.shutdown(); if (mode=="record") assert(recorder.status().error.empty());
    transfer.shutdown(); const auto s=transfer.status();
    std::cout<<Json({{"id",rc.recordingId},{"pending",s.pending},{"completed",s.completed},{"failed",s.failed},{"error",s.error}}).dump()<<'\n';
}
