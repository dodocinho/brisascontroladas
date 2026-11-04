#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// Linux, single producer (FonteGlobalKinect/main thread), asynchronous consumer.
// No openFrameworks/GL dependency: usable with synthetic frames in headless tests.
class KinectRecorder
{
public:
    struct Config
    {
        std::string outputRoot = "recordings";
        // Empty for manual captures; immutable canonical identity for automatic captures.
        std::string recordingId, sessionId;
        std::string ffmpeg = "ffmpeg";
        std::string preset = "veryfast";
        int fps = 30;
        int rgbKbps = 2500, depthKbps = 1500;
        int depthNearMm = 500, depthFarMm = 4000;
        int segmentSeconds = 600;
        int queueFrames = 8;
        int maxDurationSeconds = 14400;
        uint64_t maxBytes = 10000000000ULL; // decimal GB, including metadata/logs
        uint64_t reserveBytes = 128000000;
    };
    struct Status
    {
        bool active = false, accepting = false;
        double seconds = 0;
        uint64_t bytes = 0, frames = 0;
        uint64_t droppedRgb = 0, droppedDepth = 0;
        uint64_t repeatedRgb = 0, repeatedDepth = 0;
        std::string directory, message, error;
    };
    static constexpr int width = 640, height = 480;
    KinectRecorder();
    ~KinectRecorder();
    KinectRecorder(const KinectRecorder &) = delete;
    KinectRecorder &operator=(const KinectRecorder &) = delete;

    // Start/stop only schedule work; disk, process management and encoding run off-thread.
    bool start(const Config &config);
    void stop(const std::string &reason = "user");
    void shutdown(); // joins only on application exit
    Status status() const;
    bool accepting() const;
    static int64_t monotonicNs();
    bool submitRgb(const uint8_t *pixels, std::size_t count, int64_t receivedNs);
    bool submitDepthMm(const uint16_t *pixels, std::size_t count, int64_t receivedNs);
    static uint8_t quantizeDepth(uint16_t mm, int nearMm, int farMm);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
