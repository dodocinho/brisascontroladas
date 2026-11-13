#include "KinectRecorder.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <signal.h>

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
void waitFor(KinectRecorder &r, bool ready, int timeout = 25000)
{
    const auto end = Clock::now() + std::chrono::milliseconds(timeout);
    while (Clock::now() < end)
    {
        const auto s = r.status();
        if (ready ? s.accepting : !s.active) return;
        if (ready && !s.active) throw std::runtime_error(s.error);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Timeout waiting for recorder");
}
Json readSession(const KinectRecorder &r)
{
    std::ifstream in(fs::path(r.status().directory) / "session.json");
    Json result; in >> result; return result;
}
int main(int argc, char **argv)
{
    assert(argc >= 2);
    const fs::path root = fs::absolute(argv[1]);
    fs::create_directories(root);
    const double duration = argc > 2 ? std::stod(argv[2]) : 4.25;
    KinectRecorder::Config config;
    config.outputRoot = (root / "normal").string();
    config.segmentSeconds = duration > 600 ? 600 : 1;
    config.queueFrames = 2;
    assert(KinectRecorder::quantizeDepth(0, 500, 4000) == 0);
    assert(KinectRecorder::quantizeDepth(499, 500, 4000) == 0);
    assert(KinectRecorder::quantizeDepth(4001, 500, 4000) == 0);
    assert(KinectRecorder::quantizeDepth(500, 500, 4000) == 255);
    assert(KinectRecorder::quantizeDepth(4000, 500, 4000) == 1);
    assert(KinectRecorder::quantizeDepth(2250, 500, 4000) == 128);
    KinectRecorder r;
    assert(r.start(config));
    assert(!r.start(config));
    waitFor(r, true);
    constexpr auto count = KinectRecorder::width * KinectRecorder::height;
    std::vector<uint8_t> rgb(count * 3);
    std::vector<uint16_t> mm(count);
    for (int y = 0; y < 480; ++y)
        for (int x = 0; x < 640; ++x) mm[y * 640 + x] = x < 160 ? 0 : x < 320 ? 500 : x < 480 ? 2250 : 4000;
    // Saturate the bounded queues. Submissions must return without waiting for encoders.
    const auto burstStart = Clock::now();
    for (int i = 0; i < 100; ++i)
    {
        const auto ns = KinectRecorder::monotonicNs();
        r.submitRgb(rgb.data(), rgb.size(), ns);
        r.submitDepthMm(mm.data(), mm.size(), ns);
    }
    assert(Clock::now() - burstStart < std::chrono::seconds(1));
    const auto start = Clock::now();
    unsigned frame = 0;
    while (std::chrono::duration<double>(Clock::now() - start).count() < duration)
    {
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        const auto ns = KinectRecorder::monotonicNs();
        // Simulate dropped RGB and a separate period with no incoming data at all.
        if (!(elapsed > 2 && elapsed < 2.6))
        {
            if (!(elapsed > 0.5 && elapsed < 1.2))
            {
                for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = uint8_t((i / 3 + frame * 7) % 256);
                r.submitRgb(rgb.data(), rgb.size(), ns);
            }
            r.submitDepthMm(mm.data(), mm.size(), ns);
        }
        ++frame;
        std::this_thread::sleep_until(start + std::chrono::nanoseconds(frame * 1000000000LL / config.fps));
        if (!r.status().active) throw std::runtime_error(r.status().error);
    }
    const auto stopStart = Clock::now();
    r.stop();
    assert(Clock::now() - stopStart < std::chrono::milliseconds(100));
    waitFor(r, false);
    auto status = r.status();
    if (!status.error.empty()) throw std::runtime_error(status.error);
    assert(status.droppedRgb > 0 && status.droppedDepth > 0);
    assert(status.repeatedRgb >= 10 && status.repeatedDepth >= 10);
    assert(status.seconds >= duration && status.seconds < duration + 0.2);
    Json report = {{"normal", status.directory}};

    // An asymmetric image detects swapped axes and verifies RGB/Depth alignment
    // in the actual encoded files. Each capture must retain its initial settings.
    for (int orientation = 0; orientation < 4; ++orientation)
    {
        auto mirrored = config;
        const auto name = "mirror_" + std::to_string(orientation);
        mirrored.outputRoot = (root / name).string();
        mirrored.maxDurationSeconds = 1;
        mirrored.mirrorHorizontal = (orientation & 1) != 0;
        mirrored.mirrorVertical = (orientation & 2) != 0;
        assert(r.start(mirrored));
        mirrored.mirrorHorizontal = !mirrored.mirrorHorizontal;
        mirrored.mirrorVertical = !mirrored.mirrorVertical;
        waitFor(r, true);
        assert(r.status().mirrorHorizontal == ((orientation & 1) != 0));
        assert(r.status().mirrorVertical == ((orientation & 2) != 0));
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x)
            {
                const int quadrant = (y >= 240 ? 2 : 0) + (x >= 320 ? 1 : 0);
                const int i = y * 640 + x;
                rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = 40 + 50 * quadrant;
                mm[i] = 500 + 1000 * quadrant;
            }
        const auto ns = KinectRecorder::monotonicNs();
        assert(r.submitRgb(rgb.data(), rgb.size(), ns));
        assert(r.submitDepthMm(mm.data(), mm.size(), ns));
        waitFor(r, false);
        assert(r.status().error.empty());
        report[name] = r.status().directory;
    }

    config.maxDurationSeconds = 1;
    config.outputRoot = (root / "duration").string();
    assert(r.start(config));
    waitFor(r, true);
    waitFor(r, false);
    assert(r.status().error.empty());
    assert(r.status().frames == 30);
    assert(readSession(r)["stop_reason"] == "duration_limit");
    report["duration"] = r.status().directory;

    config.outputRoot = (root / "bad_executable").string();
    config.ffmpeg = "/nonexistent/brisas-ffmpeg";
    assert(r.start(config)); waitFor(r, false);
    assert(!r.status().error.empty());
    assert(readSession(r)["state"] == "error");

    config.outputRoot = (root / "bad_codec").string();
    config.ffmpeg = "ffmpeg";
    config.preset = "nonexistent-preset";
    assert(r.start(config)); waitFor(r, false);
    assert(!r.status().error.empty());

    config.preset = "veryfast";
    config.outputRoot = (root / "storage").string();
    config.maxDurationSeconds = 5;
    config.rgbKbps = 100; config.depthKbps = 100;
    config.reserveBytes = 16000000; config.maxBytes = 16500000;
    assert(r.start(config)); waitFor(r, true);
    while (r.status().active && r.status().frames < 2)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(r.status().frames >= 2);
    { // Simulate reaching the storage threshold without writing a large recording.
        std::ofstream filler(fs::path(r.status().directory) / "space-test.bin");
        filler.seekp(600000); filler.put('\0');
    }
    waitFor(r, false);
    assert(r.status().error.empty());
    assert(readSession(r)["stop_reason"] == "storage_limit");
    assert(r.status().bytes < config.maxBytes);
    report["storage"] = r.status().directory;

    config = KinectRecorder::Config{};
    config.outputRoot = (root / "killed").string();
    config.segmentSeconds = 1;
    assert(r.start(config)); waitFor(r, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    const auto child = readSession(r).at("rgb_pid").get<int>();
    assert(child > 0);
    kill(child, SIGKILL);
    waitFor(r, false);
    assert(!r.status().error.empty());
    assert(readSession(r)["state"] == "error");

    report["killed"] = r.status().directory;

    config.outputRoot = (root / "stalled").string();
    assert(r.start(config)); waitFor(r, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto stalledChild = readSession(r).at("rgb_pid").get<int>();
    assert(stalledChild > 0);
    kill(stalledChild, SIGSTOP);
    // A non-reading child must time out, terminate, and leave the UI callable.
    const auto stalledStop = Clock::now();
    r.stop();
    assert(Clock::now() - stalledStop < std::chrono::milliseconds(100));
    waitFor(r, false);
    assert(!r.status().error.empty());
    assert(readSession(r)["state"] == "error");
    assert(kill(stalledChild, 0) == -1);

    // Stop during startup and reuse the same recorder after failure.
    config.outputRoot = (root / "immediate_stop").string();
    assert(r.start(config)); r.stop(); waitFor(r, false);
    assert(!r.status().active);
    config.rgbKbps = 50000;
    assert(r.start(config)); waitFor(r, false);
    assert(r.status().error.find("orçamento") != std::string::npos);

    std::ofstream(root / "report.json") << report.dump(2);
    std::cout << "Recorder integration tests passed. Results: " << root << '\n';
}
