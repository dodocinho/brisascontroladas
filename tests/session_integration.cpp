#include "SessionWebSocketClient.h"
#include "SessionRecordingController.h"
#include "RecordingTransfer.h"
#include <cassert>
#include <thread>
#include <functional>
using namespace integration;
using Clock = std::chrono::steady_clock;
const std::string a = "33333333-3333-4333-8333-333333333333", b = "44444444-4444-4444-8444-444444444444";
SessionEvent event(const std::string &type, const std::string &id, int64_t ms = 2000000000000LL) {
    return SessionProtocol::parse(Json({{"v",1},{"type",type},{"eventId",uuid()},
        {"sessaoId",id.empty() ? Json(nullptr) : Json(id)},{"timestamp",utc(ms)}}).dump());
}
void until(const std::function<bool()> &fn, double seconds = 15) {
    const auto limit = Clock::now() + std::chrono::milliseconds(int(seconds*1000));
    while (!fn()) { assert(Clock::now() < limit); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
}
void unit(const fs::path &root) {
    fs::create_directories(root);
    SessionProtocol protocol((root / "session-control.json").string());
    auto e = event("sessao.estado", a);
    assert(protocol.accept(e, true)); const auto id = e.recordingId;
    assert(!protocol.accept(e, false));
    auto same = event("sessao.iniciada", a, e.time+1); assert(protocol.accept(same, false)); assert(same.recordingId == id);
    auto old = event("sessao.estado", "", e.time-1); assert(!protocol.accept(old, false));
    for (const auto &text : {"{}", "{", "[]", "null"}) {
        bool rejected = false; try { SessionProtocol::parse(text); } catch (...) { rejected = true; } assert(rejected);
    }
    auto valid = Json({{"v",1},{"type","sessao.iniciada"},{"eventId",uuid()},{"sessaoId",a},{"timestamp","2026-11-03T21:18:00Z"}});
    for (auto invalid : {Json({{"v",2}}), Json({{"type","unknown"}}), Json({{"sessaoId",nullptr}}),
            Json({{"sessaoId","123"}}), Json({{"timestamp","2026-02-30T21:18:00Z"}}),
            Json({{"timestamp","2026-11-03T21:18:00+00:00"}}), Json({{"eventId","bad"}}), Json({{"extra",1}})}) {
        auto j = valid; j.update(invalid); bool rejected = false;
        try { SessionProtocol::parse(j.dump()); } catch (...) { rejected = true; } assert(rejected);
    }
    assert(timestamp("2026-11-03T21:18:00.1Z") == timestamp("2026-11-03T21:18:00.100Z"));
    SessionProtocol restarted((root / "session-control.json").string());
    assert(!restarted.accept(old, false)); assert(!restarted.accept(same, false));
    // Persisted completed captures prevent new recording IDs even after restart.
    fs::create_directories(root / id);
    atomicWrite(root / id / "session.json", Json({{"state","completed"}}).dump());
    auto snapshot = event("sessao.estado",a,e.time+2); assert(restarted.accept(snapshot,true)); assert(snapshot.alreadyRecorded && snapshot.recordingId == id);
    auto stop = event("sessao.encerrada",a,e.time+3); assert(restarted.accept(stop,false));
    auto staleStart = event("sessao.iniciada",a,e.time+4); assert(!restarted.accept(staleStart,false));
    assert(!processIdentity(getpid()).empty());
    assert(producerAlive(Json({{"owner_pid",getpid()},{"owner_identity",processIdentity(getpid())}})));
    assert(!producerAlive(Json({{"owner_pid",getpid()},{"owner_identity","previous-boot-or-reused-pid"}})));
    // Recovery creates a fresh capture only for an interrupted producer or graceful app exit.
    for (const std::string status : {"recording", "completed"}) {
        const auto recovery = root / status; fs::create_directories(recovery);
        const auto prior = uuid();
        atomicWrite(recovery / "checkpoint.json", Json({{"watermark",0},{"seen",Json::array()},
            {"desired",a},{"recordingId",prior},{"closed",Json::array()}}).dump());
        fs::create_directories(recovery / prior);
        atomicWrite(recovery / prior / "session.json", Json({{"state",status},{"owner_pid",2147483647},
            {"stop_reason","application_exit"}}).dump());
        SessionProtocol recovering((recovery / "checkpoint.json").string());
        auto active = event("sessao.estado",a);
        assert(recovering.accept(active,true) && active.recordingId != prior && !active.alreadyRecorded);
    }
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
    std::cout << "PASS protocol, durable deduplication, manual conflict, automatic transitions\n";
}
int main(int argc, char **argv) {
    assert(argc >= 3); const std::string mode = argv[1]; const fs::path root = fs::absolute(argv[2]);
    if (mode == "unit") { unit(root); return 0; }
    if (mode == "ws") {
        assert(argc == 5); SessionWebSocketClient client; SessionWebSocketClient::Config c;
        c.enabled = true; c.url = argv[3]; client.start(c,root.string());
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
