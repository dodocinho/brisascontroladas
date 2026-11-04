#include "KinectRecorder.h"
#include "IntegrationSupport.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

namespace {
constexpr std::size_t pixels = KinectRecorder::width * KinectRecorder::height;
constexpr int64_t second = 1000000000LL;
int64_t wallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr int64_t playoutDelay = 100000000; // allow independently arriving RGB/depth frames

std::string wallTime(bool filename = false)
{
    const auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char value[64];
    std::strftime(value, sizeof(value), filename ? "%Y-%m-%d_%H-%M-%S" : "%Y-%m-%dT%H:%M:%SZ", &tm);
    return value;
}

// Preallocated SPSC ring: producer never waits, allocates or touches the filesystem.
// release/acquire ownership includes the slot vector swapped by the consumer.
template<typename T> struct FrameQueue
{
    struct Frame { std::vector<T> data; int64_t ns = -1; uint64_t id = 0; };
    std::vector<Frame> slots;
    std::atomic<uint64_t> head{0}, tail{0}, rejected{0};
    uint64_t sequence = 0; // producer only
    std::size_t frameSize = 0;
    void reset(int capacity, std::size_t count)
    {
        frameSize = count;
        slots.resize(capacity);
        for (auto &slot : slots) slot.data.resize(count);
        head = 0; tail = 0; rejected = 0; sequence = 0;
    }
    bool push(const T *data, std::size_t count, int64_t ns)
    {
        const auto id = ++sequence;
        const auto h = head.load(std::memory_order_relaxed);
        if (!data || count != frameSize ||
            h - tail.load(std::memory_order_acquire) >= slots.size())
        { ++rejected; return false; }
        auto &slot = slots[h % slots.size()];
        std::memcpy(slot.data.data(), data, count * sizeof(T));
        slot.ns = ns; slot.id = id;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    unsigned select(int64_t ns, Frame &current)
    {
        unsigned selected = 0;
        auto t = tail.load(std::memory_order_relaxed);
        // Snapshot avoids chasing the producer indefinitely during catch-up.
        const auto h = head.load(std::memory_order_acquire);
        while (t < h)
        {
            auto &slot = slots[t % slots.size()];
            if (slot.ns > ns) break;
            current.data.swap(slot.data);
            current.ns = slot.ns; current.id = slot.id;
            tail.store(++t, std::memory_order_release);
            ++selected;
        }
        return selected;
    }
};

struct Encoder
{
    pid_t pid = -1;
    int fd = -1;
    int exitCode = -1;
    std::vector<std::string> args;
    bool running()
    {
        if (pid <= 0) return false;
        int status = 0;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == 0 || (result < 0 && errno == EINTR)) return true;
        exitCode = result > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid = -1;
        return false;
    }
    void open(const KinectRecorder::Config &c, const fs::path &dir, bool depth)
    {
        const std::string name = depth ? "depth" : "rgb";
        const std::string rate = std::to_string(depth ? c.depthKbps : c.rgbKbps) + "k";
        args = {c.ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
            "-f", "rawvideo", "-pixel_format", depth ? "gray" : "rgb24",
            "-video_size", "640x480", "-framerate", std::to_string(c.fps), "-i", "pipe:0",
            "-an", "-c:v", "libx264", "-preset", c.preset, "-threads", "2",
            "-pix_fmt", "yuv420p", "-b:v", rate, "-maxrate", rate,
            "-bufsize", std::to_string(2 * (depth ? c.depthKbps : c.rgbKbps)) + "k",
            "-bf", "0", "-g", std::to_string(c.fps * 2), "-sc_threshold", "0",
            "-flags", "+cgop", "-force_key_frames", "expr:gte(t,n_forced*" + std::to_string(c.segmentSeconds) + ")",
            "-f", "segment", "-segment_time", std::to_string(c.segmentSeconds),
            "-segment_time_delta", std::to_string(0.5 / c.fps),
            "-reset_timestamps", "1", "-segment_format", "mp4",
            "-segment_format_options", "movflags=+faststart",
            "-segment_list_type", "csv", "-segment_list", (dir / name / "segments.csv").string(),
            (c.recordingId.empty() ? dir / name / (name + "_%03d.mp4") :
                dir / (c.recordingId + "-" + name + "%03d.mp4")).string()};
        int pipeFds[2];
        if (pipe2(pipeFds, O_CLOEXEC) != 0) throw std::runtime_error("Falha criando pipe FFmpeg");
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFds[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
        posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
        const auto log = (dir / (name + "-ffmpeg.log")).string();
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        std::vector<char *> argv;
        for (auto &arg : args) argv.push_back(&arg[0]);
        argv.push_back(nullptr);
        // SIGPIPE is blocked only in our worker, not inherited by the encoders.
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        sigset_t empty;
        sigemptyset(&empty);
        posix_spawnattr_setsigmask(&attr, &empty);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
        const int result = posix_spawnp(&pid, c.ffmpeg.c_str(), &actions, &attr, argv.data(), environ);
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&actions);
        close(pipeFds[0]);
        if (result != 0)
        {
            close(pipeFds[1]); pid = -1;
            throw std::runtime_error("Não foi possível iniciar FFmpeg: " + std::string(std::strerror(result)));
        }
        fd = pipeFds[1];
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0)
            throw std::runtime_error("Falha configurando pipe não bloqueante");
    }
    void eof() { if (fd >= 0) { close(fd); fd = -1; } }
    bool finish()
    {
        eof();
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (running() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (pid > 0)
        {
            kill(pid, SIGTERM);
            deadline = Clock::now() + std::chrono::seconds(2);
            while (running() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pid > 0)
        {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            pid = -1; exitCode = -1;
        }
        return exitCode == 0;
    }
    ~Encoder() { if (pid > 0 || fd >= 0) finish(); }
};

// Advance both pipes together. A stalled/dead encoder cannot freeze capture or shutdown.
void writePair(Encoder &rgb, Encoder &depth, const uint8_t *color, const uint8_t *gray)
{
    Encoder *encoders[] = {&rgb, &depth};
    const uint8_t *data[] = {color, gray};
    std::size_t remaining[] = {pixels * 3, pixels};
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (remaining[0] || remaining[1])
    {
        if (!rgb.running() || !depth.running()) throw std::runtime_error("FFmpeg encerrou inesperadamente; consulte os logs da sessão");
        if (Clock::now() > deadline) throw std::runtime_error("FFmpeg não consumiu o frame em 5 segundos");
        pollfd descriptors[2] = {{rgb.fd, static_cast<short>(remaining[0] ? POLLOUT : 0), 0},
                                 {depth.fd, static_cast<short>(remaining[1] ? POLLOUT : 0), 0}};
        const int result = poll(descriptors, 2, 50);
        if (result < 0 && errno != EINTR) throw std::runtime_error("Falha no poll dos pipes");
        for (int i = 0; i < 2; ++i)
        {
            if (descriptors[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("Pipe FFmpeg fechado; consulte os logs da sessão");
            if (!remaining[i] || !(descriptors[i].revents & POLLOUT)) continue;
            const auto n = write(encoders[i]->fd, data[i], remaining[i]);
            if (n > 0) { data[i] += n; remaining[i] -= n; }
            else if (n < 0 && errno != EAGAIN && errno != EINTR)
                throw std::runtime_error("Erro escrevendo no FFmpeg: " + std::string(std::strerror(errno)));
        }
    }
}
uint64_t directoryBytes(const fs::path &dir)
{
    uint64_t result = 0;
    for (const auto &entry : fs::recursive_directory_iterator(dir))
    {
        // The publication worker atomically renames/removes temporary sidecars.
        // A disappearing temporary file must never interrupt capture's storage check.
        std::error_code error;
        if (entry.is_regular_file(error))
        {
            const auto size = entry.file_size(error);
            if (!error) result += size;
        }
        if (error && error != std::errc::no_such_file_or_directory)
            throw fs::filesystem_error("Falha verificando espaço da sessão", entry.path(), error);
    }
    return result;
}
}

struct KinectRecorder::Impl
{
    Config config;
    FrameQueue<uint8_t> rgb;
    FrameQueue<uint16_t> depth;
    std::thread worker;
    std::atomic<bool> active{false}, accepting{false};
    std::atomic<int64_t> stopNs{0}, stopUtcMs{0};
    std::atomic<uint64_t> frames{0}, bytes{0}, repeatedRgb{0}, repeatedDepth{0}, skippedRgb{0}, skippedDepth{0};
    mutable std::mutex infoMutex;
    std::string directory, message, error, stopReason;
    void info(const std::string &text) { std::lock_guard<std::mutex> lock(infoMutex); message = text; }
    void fail(const std::string &text) { std::lock_guard<std::mutex> lock(infoMutex); error = text; message = "Erro na gravação"; }
    void run();
};

int64_t KinectRecorder::monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

uint8_t KinectRecorder::quantizeDepth(uint16_t mm, int nearMm, int farMm)
{
    // Registered libfreenect depth is millimeters; 0 means invalid. Outside ROI = black.
    // TODO: support lossless 16-bit depth (FFV1 or equivalent), preserving original
    // Kinect data for 3D reconstruction/spatial processing. This H.264 path is lossy.
    if (mm == 0 || mm < nearMm || mm > farMm || farMm <= nearMm) return 0;
    return static_cast<uint8_t>(1 + (254LL * (farMm - mm)) / (farMm - nearMm));
}

void KinectRecorder::Impl::run()
{
    sigset_t blocked;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    Encoder rgbEncoder, depthEncoder;
    fs::path dir;
    Json metadata;
    std::ofstream csv;
    int64_t epoch = 0;
    std::string reason = "user";
    int64_t segmentStartMs = 0, segmentStartNs = 0, captureEpochUtcMs = 0;
    uint64_t segmentFirstFrame = 0;
    auto finishCaptureSegment = [&](bool failed, bool final = false) {
        const auto count = frames.load();
        if (!segmentStartMs || count == segmentFirstFrame) return;
        // Record event clocks before waiting for encoders or transferring anything.
        const auto endNs = final ? (stopNs.load() ? stopNs.load() :
            std::min(KinectRecorder::monotonicNs(), epoch + config.maxDurationSeconds * second)) :
            epoch + static_cast<int64_t>(count) * second / config.fps;
        const auto endMs = final && stopNs.load() ? stopUtcMs.load() :
            captureEpochUtcMs + (endNs - epoch) / 1000000;
        const auto index = segmentFirstFrame / (config.segmentSeconds * config.fps);
        Json event = {{"startedUtcMs", segmentStartMs}, {"endedUtcMs", endMs},
            {"elapsedMs", (endNs - segmentStartNs) / 1000000}, {"frames", count - segmentFirstFrame},
            {"error", failed || endNs < segmentStartNs || endMs < segmentStartMs}};
        const auto path = dir / "capture" / (std::to_string(index) + ".json");
        integration::atomicWrite(path, event.dump(2) + '\n');
        segmentStartMs = 0;
    };
    auto saveMetadata = [&](const std::string &state) {
        metadata["state"] = state;
        metadata["updated_utc"] = wallTime();
        metadata["frames_written_pairs"] = frames.load();
        metadata["video_duration_seconds"] = double(frames.load()) / config.fps;
        metadata["session_bytes_at_last_scan"] = bytes.load();
        metadata["queue_dropped_rgb"] = rgb.rejected.load();
        metadata["queue_dropped_depth"] = depth.rejected.load();
        metadata["resampled_away_rgb"] = skippedRgb.load();
        metadata["resampled_away_depth"] = skippedDepth.load();
        metadata["repeated_rgb"] = repeatedRgb.load();
        metadata["repeated_depth"] = repeatedDepth.load();
        integration::atomicWrite(dir / "session.json", metadata.dump(2) + '\n');
    };
    try
    {
        const auto &c = config;
        if (c.fps < 1 || c.fps > 30 || c.queueFrames < 2 || c.queueFrames > 60 ||
            c.depthNearMm < 1 || c.depthFarMm > 10000 || c.depthFarMm <= c.depthNearMm ||
            c.rgbKbps < 100 || c.depthKbps < 100 || c.rgbKbps > 50000 || c.depthKbps > 50000 ||
            c.segmentSeconds < 1 || c.segmentSeconds > (c.recordingId.empty() ? 600 : 14400) || c.maxDurationSeconds < 1 || c.maxDurationSeconds > 14400 ||
            c.maxBytes > 10000000000ULL || c.reserveBytes < 16000000 || c.reserveBytes >= c.maxBytes)
            throw std::runtime_error("Configuração de gravação inválida");
        // 5% container/VBV allowance plus a conservative 256 bytes/CSV row.
        const double projected = (c.rgbKbps + c.depthKbps) * 1000.0 / 8 * c.maxDurationSeconds * 1.05
                               + c.fps * 256.0 * c.maxDurationSeconds;
        if (projected > c.maxBytes - c.reserveBytes)
            throw std::runtime_error("Bitrates excedem o orçamento de armazenamento para a duração configurada");
        if ((!c.recordingId.empty() || !c.sessionId.empty()) &&
            (!integration::uuidValid(c.recordingId) || !integration::uuidValid(c.sessionId)))
            throw std::runtime_error("Identidade automática inválida");
        fs::create_directories(c.outputRoot);
        const auto base = fs::absolute(fs::path(c.outputRoot) / (c.recordingId.empty() ? wallTime(true) : c.recordingId));
        dir = base;
        if (!c.recordingId.empty()) {
            if (!fs::create_directory(dir)) { dir.clear(); throw std::runtime_error("UUID já existe; captura não sobrescrita"); }
        } else {
            for (unsigned suffix = 1; !fs::create_directory(dir); ++suffix) dir = base.string() + "_" + std::to_string(suffix);
        }
        fs::create_directory(dir / "rgb"); fs::create_directory(dir / "depth");
        fs::create_directory(dir / "capture");
        { std::lock_guard<std::mutex> lock(infoMutex); directory = dir.string(); }
        if (fs::space(dir).available < c.reserveBytes * 2)
            throw std::runtime_error("Espaço livre insuficiente para iniciar gravação");
        rgb.reset(c.queueFrames, pixels * 3); depth.reset(c.queueFrames, pixels);
        FrameQueue<uint8_t>::Frame color;
        FrameQueue<uint16_t>::Frame distance;
        color.data.resize(pixels * 3, 0); distance.data.resize(pixels, 0);
        std::vector<uint8_t> gray(pixels, 0);
        epoch = KinectRecorder::monotonicNs();
        metadata = {{"schema_version", 1}, {"started_utc", wallTime()}, {"directory_timezone", "UTC"},
            {"monotonic_epoch_ns", epoch}, {"capture_nominal_fps", 30}, {"width", width}, {"height", height}, {"fps", c.fps},
            {"codec", "libx264"}, {"container", "mp4"}, {"pixel_format", "yuv420p"}, {"preset", c.preset},
            {"rgb_kbps", c.rgbKbps}, {"depth_kbps", c.depthKbps}, {"rate_control", "ABR with maxrate=bitrate, VBV=2 seconds"},
            {"segment_seconds", c.segmentSeconds}, {"queue_frames_per_stream", c.queueFrames},
            {"max_bytes", c.maxBytes}, {"reserve_bytes", c.reserveBytes}, {"max_duration_seconds", c.maxDurationSeconds},
            {"projected_bytes_with_margin", projected},
            {"depth", {{"source", "libfreenect FREENECT_DEPTH_REGISTERED"}, {"source_units", "millimeters"},
                {"registered_to_rgb", true}, {"quantized_bits", 8}, {"lossless", false}, {"near_mm", c.depthNearMm}, {"far_mm", c.depthFarMm},
                {"mapping", "in range: 1 + floor(254*(far-mm)/(far-near)); near=255, far=1"},
                {"invalid_and_outside_range", 0}, {"compression_note", "H.264 is lossy; decoded black boundaries/values may change"}}},
            {"synchronization", {{"clock", "steady_clock"}, {"timestamp_source", "host receipt after ofxKinect::update, NOT hardware exposure"},
                {"hardware_drops", "unknown: addon overwrites frames and discards device timestamps"},
                {"strategy", "common CFR grid, latest received frame at or before PTS; repeat previous; black before first"},
                {"playout_delay_ns", playoutDelay}, {"segment_pts", "reset to zero; global frame = segment*segment_seconds*fps + local_frame"}}}};
        // Preserve the existing unique session directory as the session identifier.
        metadata["session_id"] = dir.filename().string();
        metadata["owner_pid"] = getpid();
        metadata["owner_identity"] = integration::processIdentity(getpid());
        if (!c.recordingId.empty()) {
            metadata["gravacaoId"] = c.recordingId; metadata["sessaoId"] = c.sessionId;
            metadata["mode"] = "automatic";
        } else metadata["mode"] = "manual";
        char hostname[256]{};
        if (gethostname(hostname, sizeof(hostname) - 1)) throw std::runtime_error("Falha lendo hostname");
        metadata["device"] = hostname;
        saveMetadata("starting");
        csv.open(dir / "timestamps.csv");
        csv << "frame,pts_ns,segment,segment_frame,rgb_id,depth_id,rgb_received_ns,depth_received_ns,rgb_minus_depth_ns,rgb_repeat,depth_repeat,rgb_missing,depth_missing,rgb_queue_dropped,depth_queue_dropped,rgb_resampled_away,depth_resampled_away\n";
        if (!csv) throw std::runtime_error("Falha abrindo timestamps.csv");
        rgbEncoder.open(c, dir, false); depthEncoder.open(c, dir, true);
        metadata["rgb_pid"] = rgbEncoder.pid;
        metadata["depth_pid"] = depthEncoder.pid;
        metadata["rgb_ffmpeg_argv"] = rgbEncoder.args;
        metadata["depth_ffmpeg_argv"] = depthEncoder.args;
        // Start clock only when both subprocesses have been spawned. Initial missing frames
        // remain black, so even delayed sensor start preserves the session timeline.
        epoch = KinectRecorder::monotonicNs();
        captureEpochUtcMs = wallMs();
        metadata["capture_started_utc_ms"] = captureEpochUtcMs;
        metadata["monotonic_epoch_ns"] = epoch;
        metadata["started_utc"] = wallTime();
        accepting.store(true, std::memory_order_release);
        info("Gravando RGB + Depth");
        std::clog << "[KinectRecorder] Captura iniciada: " << dir.string() << '\n';
        auto nextCheck = Clock::now();
        while (true)
        {
            const auto now = KinectRecorder::monotonicNs();
            const auto requestedStop = stopNs.load();
            const int64_t end = requestedStop ? std::min(requestedStop, epoch + c.maxDurationSeconds * second)
                                               : epoch + c.maxDurationSeconds * second;
            const auto index = frames.load();
            const int64_t pts = index * second / c.fps;
            if (epoch + pts >= end)
            {
                if (!requestedStop || requestedStop >= epoch + c.maxDurationSeconds * second) reason = "duration_limit";
                else { std::lock_guard<std::mutex> lock(infoMutex); reason = stopReason; }
                break;
            }
            if (Clock::now() >= nextCheck)
            {
                if (!rgbEncoder.running() || !depthEncoder.running()) throw std::runtime_error("FFmpeg encerrou; consulte rgb-ffmpeg.log/depth-ffmpeg.log");
                csv.flush();
                if (!csv) throw std::runtime_error("Falha escrevendo timestamps.csv");
                bytes = directoryBytes(dir);
                if (bytes >= c.maxBytes - c.reserveBytes) { reason = "storage_limit"; break; }
                if (fs::space(dir).available <= c.reserveBytes) { reason = "disk_space_limit"; break; }
                saveMetadata(requestedStop ? "stopping" : "recording");
                nextCheck = Clock::now() + std::chrono::seconds(1);
            }
            if (!requestedStop && now < epoch + pts + playoutDelay)
            { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
            if (std::min(now, end) - (epoch + pts) > 5 * second)
                throw std::runtime_error("Codificação atrasou mais de 5 segundos; sessão encerrada no último par completo");
            const auto previousRgb = color.id, previousDepth = distance.id;
            const auto nRgb = rgb.select(epoch + pts, color);
            const auto nDepth = depth.select(epoch + pts, distance);
            if (nRgb > 1) skippedRgb += nRgb - 1;
            if (nDepth > 1) skippedDepth += nDepth - 1;
            const bool repeatRgb = color.id != 0 && previousRgb == color.id;
            const bool repeatDepth = distance.id != 0 && previousDepth == distance.id;
            if (nDepth)
                for (std::size_t i = 0; i < pixels; ++i) gray[i] = KinectRecorder::quantizeDepth(distance.data[i], c.depthNearMm, c.depthFarMm);
            if (index % (c.segmentSeconds * c.fps) == 0)
            {
                // Capture intervals follow the already sampled UTC/steady-clock epoch,
                // never the delayed encoder write time (including stop during playout).
                finishCaptureSegment(false);
                segmentFirstFrame = index;
                segmentStartMs = captureEpochUtcMs + pts / 1000000;
                segmentStartNs = epoch + pts;
            }
            writePair(rgbEncoder, depthEncoder, color.data.data(), gray.data());
            ++frames;
            if (repeatRgb) ++repeatedRgb;
            if (repeatDepth) ++repeatedDepth;
            const auto segmentFrames = c.segmentSeconds * c.fps;
            csv << index << ',' << pts << ',' << index / segmentFrames << ',' << index % segmentFrames << ','
                << color.id << ',' << distance.id << ',' << (color.id ? color.ns - epoch : -1) << ','
                << (distance.id ? distance.ns - epoch : -1) << ',' << (color.id && distance.id ? color.ns - distance.ns : 0) << ','
                << repeatRgb << ',' << repeatDepth << ',' << (color.id == 0) << ',' << (distance.id == 0) << ','
                << rgb.rejected << ',' << depth.rejected << ',' << skippedRgb << ',' << skippedDepth << '\n';
        }
    }
    catch (const std::exception &e) { fail(e.what()); reason = "error"; }
    accepting = false;
    try { finishCaptureSegment(reason == "error", true); }
    catch (const std::exception &e) { fail(e.what()); reason = "error"; }
    info("Finalizando arquivos...");
    // EOF on BOTH pipes before waiting, allowing MP4 trailers/faststart to finish normally.
    rgbEncoder.eof(); depthEncoder.eof();
    const bool rgbOk = rgbEncoder.finish(), depthOk = depthEncoder.finish();
    if ((!rgbOk || !depthOk) && reason != "error")
    { fail("FFmpeg falhou ao finalizar; o último segmento pode estar incompleto. Consulte os logs."); reason = "error"; }
    csv.close();
    try
    {
        if (!dir.empty() && fs::exists(dir))
        {
            if (csv.fail() && frames > 0) { fail("Falha finalizando timestamps.csv"); reason = "error"; }
            // A durable completed receipt must never precede durable automatic media.
            // Keep this off the rendering thread, after both encoders have closed.
            if (!config.recordingId.empty() && reason != "error") {
                for (const auto &file : fs::directory_iterator(dir))
                    if (file.path().extension() == ".mp4") integration::syncFile(file.path());
            }
            bytes = directoryBytes(dir);
            metadata["ended_utc"] = wallTime();
            metadata["capture_ended_utc_ms"] = stopUtcMs.load() ? stopUtcMs.load() : wallMs();
            metadata["stop_reason"] = reason;
            metadata["rgb_exit_code"] = rgbEncoder.exitCode;
            metadata["depth_exit_code"] = depthEncoder.exitCode;
            metadata["stop_requested_elapsed_ns"] = stopNs.load() ? std::max<int64_t>(0, stopNs.load() - epoch) : 0;
            { std::lock_guard<std::mutex> lock(infoMutex); metadata["error"] = error; }
            saveMetadata(reason == "error" ? "error" : "completed");
            bytes = directoryBytes(dir);
        }
    }
    catch (const std::exception &e) { fail(e.what()); reason = "error"; }
    info(reason == "error" ? "Erro na gravação" : "Gravação finalizada: " + reason);
    std::clog << "[KinectRecorder] Captura encerrada: " << dir.string() << " motivo=" << reason << '\n';
    active.store(false, std::memory_order_release);
}

KinectRecorder::KinectRecorder() : impl(new Impl) {}
KinectRecorder::~KinectRecorder() { shutdown(); }
bool KinectRecorder::start(const Config &config)
{
    if (impl->active.load(std::memory_order_acquire)) return false;
    if (impl->worker.joinable()) impl->worker.join();
    impl->config = config;
    impl->stopNs = 0; impl->stopUtcMs = 0; impl->frames = 0; impl->bytes = 0;
    impl->repeatedRgb = 0; impl->repeatedDepth = 0; impl->skippedRgb = 0; impl->skippedDepth = 0;
    impl->rgb.rejected = 0; impl->depth.rejected = 0;
    { std::lock_guard<std::mutex> lock(impl->infoMutex);
      impl->directory.clear(); impl->error.clear(); impl->stopReason = "user"; impl->message = "Preparando gravação..."; }
    impl->active = true;
    try { impl->worker = std::thread(&Impl::run, impl.get()); }
    catch (const std::exception &e) { impl->active = false; impl->fail(e.what()); return false; }
    return true;
}
void KinectRecorder::stop(const std::string &reason)
{
    if (!impl->active) return;
    std::lock_guard<std::mutex> lock(impl->infoMutex);
    if (!impl->stopNs) { impl->stopReason = reason; impl->stopUtcMs = wallMs(); impl->stopNs = monotonicNs(); impl->message = "Finalizando arquivos..."; }
}
void KinectRecorder::shutdown()
{
    stop("application_exit");
    if (impl->worker.joinable()) impl->worker.join();
}
bool KinectRecorder::accepting() const { return impl->accepting.load(std::memory_order_acquire) && impl->stopNs == 0; }
bool KinectRecorder::submitRgb(const uint8_t *data, std::size_t count, int64_t ns)
{
    return accepting() && impl->rgb.push(data, count, ns);
}
bool KinectRecorder::submitDepthMm(const uint16_t *data, std::size_t count, int64_t ns)
{
    return accepting() && impl->depth.push(data, count, ns);
}
KinectRecorder::Status KinectRecorder::status() const
{
    Status s;
    s.active = impl->active; s.accepting = accepting();
    s.frames = impl->frames; s.seconds = double(s.frames) / std::max(1, impl->config.fps); s.bytes = impl->bytes;
    s.droppedRgb = impl->rgb.rejected + impl->skippedRgb; s.droppedDepth = impl->depth.rejected + impl->skippedDepth;
    s.repeatedRgb = impl->repeatedRgb; s.repeatedDepth = impl->repeatedDepth;
    std::lock_guard<std::mutex> lock(impl->infoMutex);
    s.directory = impl->directory; s.message = impl->message; s.error = impl->error;
    return s;
}
