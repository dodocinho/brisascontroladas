#include "RecordingTransfer.h"
#include "KinectRecorder.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
int main(int argc, char **argv) {
    assert(argc == 5);
    const std::string mode = argv[1];
    const fs::path root = fs::absolute(argv[2]);
    const auto seconds = std::stod(argv[4]);
    std::ifstream input(argv[3]); Json settings; input >> settings;
    RecordingTransfer::Config tc;
    tc.enabled = settings.value("enabled", true);
    tc.sshHost = settings.value("sshHost", std::string("test-mac"));
    tc.remoteDirectory = settings.value("remoteDirectory", std::string("~/recordings"));
    tc.maxRetries = settings.value("maxRetries", 1);
    tc.retryIntervalSeconds = 1;
    tc.thumbnailEnabled = settings.value("thumbnailEnabled", true);
    tc.thumbnailTimestampSeconds = 20;
    RecordingTransfer transfer;
    transfer.start(root.string(), settings.value("ffmpeg", std::string("ffmpeg")), tc);
    uint64_t expected = 0;
    if (mode == "drain" || mode == "retry" || mode == "failures" || mode == "exhausted") {
        for (const auto &session : fs::directory_iterator(root)) {
            if (!session.is_directory()) continue;
            for (const std::string stream : {"rgb", "depth"}) {
                std::ifstream csv(session.path() / stream / "segments.csv");
                std::string row;
                while (std::getline(csv, row)) if (!row.empty()) ++expected;
            }
        }
    }
    if (mode == "retry") transfer.retryFailed();
    KinectRecorder recorder;
    if (mode == "live" || mode == "record") {
        KinectRecorder::Config rc;
        rc.outputRoot = root.string(); rc.segmentSeconds = 1;
        rc.ffmpeg = std::getenv("TEST_CAPTURE_FFMPEG");
        assert(recorder.start(rc));
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!recorder.accepting()) {
            assert(recorder.status().active && Clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::atomic<bool> churnDone{false};
    std::thread churn;
    if (mode == "live") {
        const auto folder = fs::path(recorder.status().directory) / "publication";
        fs::create_directories(folder);
        churn = std::thread([&churnDone, folder] {
            // Exercise capture's storage scan while asynchronous temporary sidecars disappear.
            const auto file = folder / ".storage-scan-test.tmp";
            while (!churnDone) {
                { std::ofstream out(file); out << "temporary sidecar"; }
                fs::remove(file);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    std::vector<uint8_t> rgb(640 * 480 * 3, 80);
    std::vector<uint16_t> depth(640 * 480, 2000);
    const auto deadline = Clock::now() + std::chrono::milliseconds(int(seconds * 1000));
    bool livePublication = false;
    while (Clock::now() < deadline) {
        if ((mode == "drain" || mode == "retry") && transfer.status().completed == expected) break;
        if (mode == "exhausted") {
            const auto state = transfer.status();
            if (state.pending == 0 && state.completed + state.failed == expected) break;
        }
        if (mode == "failures") {
            uint64_t attempted = 0;
            for (const auto &session : fs::directory_iterator(root)) {
                const auto publication = session.path() / "publication";
                if (!fs::is_directory(publication)) continue;
                for (const auto &entry : fs::directory_iterator(publication)) {
                    if (entry.path().filename().string().find(".job.json") == std::string::npos || entry.path().extension() != ".json") continue;
                    std::ifstream in(entry.path()); Json job; in >> job;
                    if (job.value("state", "") == "completed" || job.value("attempts", 0) > 0) ++attempted;
                }
            }
            if (attempted == expected) break;
        }
        if (mode == "live" || mode == "record") {
            assert(recorder.status().active);
            const auto ns = KinectRecorder::monotonicNs();
            recorder.submitRgb(rgb.data(), rgb.size(), ns);
            recorder.submitDepthMm(depth.data(), depth.size(), ns);
            if (transfer.status().completed > 0) livePublication = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    if (mode == "live") assert(livePublication);
    if (mode == "live" || mode == "record") {
        recorder.shutdown();
        assert(recorder.status().error.empty());
        std::cout << recorder.status().directory << '\n';
    }
    churnDone = true;
    if (churn.joinable()) churn.join();
    if (mode == "drain" || mode == "retry") assert(transfer.status().completed == expected);
    const auto before = Clock::now();
    transfer.shutdown();
    assert(Clock::now() - before < std::chrono::seconds(2));
    const auto s = transfer.status();
    Json report = {{"pending", s.pending}, {"completed", s.completed}, {"failed", s.failed}, {"error", s.error}};
    std::cout << report.dump() << '\n';
}
