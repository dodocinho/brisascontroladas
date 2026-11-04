#pragma once
#include <memory>
#include <string>
#include <cstdint>

// Independent, serial post-processing worker. Never called from frame submission.
class RecordingTransfer
{
public:
    struct Config {
        bool enabled = true;
        std::string sshHost = "mephistopheles";
        std::string remoteDirectory = "~/Documents/dev/corpo-expandido-web-front/public/gravacoes_kinect";
        int maxRetries = 10; // retries after first attempt; -1 = unlimited
        int retryIntervalSeconds = 30;
        int bandwidthKbps = 2048; // rsync KiB/s; reserve network capacity for SRT
        bool thumbnailEnabled = true;
        int thumbnailWidth = 320;
        double thumbnailTimestampSeconds = 20;
    };
    struct Status {
        uint64_t pending = 0, completed = 0, failed = 0;
        std::string message, error;
    };
    RecordingTransfer();
    ~RecordingTransfer();
    RecordingTransfer(const RecordingTransfer &) = delete;
    RecordingTransfer &operator=(const RecordingTransfer &) = delete;
    void start(const std::string &root, const std::string &ffmpeg, const Config &config);
    void shutdown(); // cancels subprocess group; pending work survives
    void retryFailed(); // schedules retries, including exhausted jobs
    Status status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
