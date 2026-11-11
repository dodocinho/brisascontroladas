#include "RecordingTransfer.h"
#include "IntegrationSupport.h"
#include <sys/stat.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace {
int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string utc(int64_t ms) {
    const time_t seconds = ms / 1000;
    std::tm tm{}; gmtime_r(&seconds, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms % 1000 << 'Z';
    return out.str();
}
Json readJson(const fs::path &path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Não foi possível ler " + path.string());
    Json j; in >> j; return j;
}
// Shared atomic persistence, also used by capture and the protocol checkpoint.
void saveJson(const fs::path &path, const Json &j) {
    integration::atomicWrite(path, j.dump(2) + '\n');
}
std::string uuid() { return integration::uuid(); }
std::string quote(const std::string &value) {
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}
void validate(const RecordingTransfer::Config &c) {
    // Restrict remote shell/rsync syntax; no options, expansion, traversal or control chars.
    if (!std::regex_match(c.sshHost, std::regex("[A-Za-z0-9][A-Za-z0-9_.-]*")))
        throw std::runtime_error("transfer.sshHost deve ser um alias SSH seguro");
    if (!std::regex_match(c.remoteDirectory, std::regex("(/|~/)[A-Za-z0-9_./-]+")))
        throw std::runtime_error("transfer.remoteDirectory: use caminho absoluto ou ~/ sem espaços/metacaracteres");
    for (const auto &part : fs::path(c.remoteDirectory))
        if (part == ".." || part == ".") throw std::runtime_error("Diretório remoto não permite . ou ..");
    if (c.maxRetries < -1 || c.retryIntervalSeconds < 1 || c.retryIntervalSeconds > 86400 ||
        c.bandwidthKbps < 1 || c.thumbnailWidth < 2 || c.thumbnailWidth > 1920 ||
        !std::isfinite(c.thumbnailTimestampSeconds) || c.thumbnailTimestampSeconds < 0)
        throw std::runtime_error("Configuração de transferência/thumbnail inválida");
}
struct Segment { std::string name; double start = 0, end = 0; int index = 0; };
std::vector<Segment> segments(const fs::path &path, const std::string &stream, const std::string &id = "") {
    std::ifstream in(path);
    std::vector<Segment> result;
    std::string line;
    const auto prefix = id.empty() ? stream + "_" : id + "-" + stream;
    const std::regex pattern(prefix + "([0-9]+)\\.mp4,([0-9]+(?:\\.[0-9]+)?),([0-9]+(?:\\.[0-9]+)?)");
    while (std::getline(in, line)) {
        // An incomplete last CSV row is not a commit record.
        if (in.eof()) break;
        std::smatch match;
        if (!std::regex_match(line, match, pattern)) throw std::runtime_error("Linha inválida em " + path.string());
        Segment s;
        s.name = prefix + match[1].str() + ".mp4";
        s.index = std::stoi(match[1]); s.start = std::stod(match[2]); s.end = std::stod(match[3]);
        if (!std::isfinite(s.end) || s.end <= s.start) continue;
        result.push_back(s);
    }
    return result;
}
}

struct RecordingTransfer::Impl {
    Config config;
    fs::path root;
    std::string ffmpeg;
    std::thread worker;
    std::atomic<bool> stopping{false}, retry{false};
    mutable std::mutex mutex;
    std::condition_variable wake;
    Status state;
    int lockFd = -1;
    std::string lastScanError;

    void log(const std::string &text, bool error = false) {
        { std::lock_guard<std::mutex> guard(mutex); state.message = text; if (error) state.error = text; }
        std::clog << "[RecordingTransfer] " << text << '\n';
        std::ofstream out(root / "transfer.log", std::ios::app);
        out << utc(wallMs()) << ' ' << text << '\n';
    }
    // argv only locally. SSH's remote shell receives fixed templates with quoted data.
    void process(std::vector<std::string> args, const fs::path &output = "/dev/null", int timeout = 60) {
        if (stopping) throw std::runtime_error("Encerrando; trabalho preservado");
        std::vector<char *> argv;
        for (auto &arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const auto errors = root / "transfer-process.log";
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errors.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setpgroup(&attr, 0);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        pid_t pid = -1;
        const int rc = posix_spawnp(&pid, args[0].c_str(), &actions, &attr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr);
        if (rc) throw std::runtime_error(args[0] + ": " + std::strerror(rc));
        const auto deadline = Clock::now() + std::chrono::seconds(timeout);
        int status = 0;
        for (;;) {
            const auto done = waitpid(pid, &status, WNOHANG);
            if (done == pid) break;
            if (done < 0 && errno != EINTR) throw std::runtime_error("waitpid falhou");
            if (stopping || Clock::now() >= deadline) {
                // Includes rsync's SSH child; shutdown never waits on the network.
                kill(-pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                kill(-pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                throw std::runtime_error("Processo cancelado/timeout: " + args[0]);
            }
            std::unique_lock<std::mutex> guard(mutex);
            wake.wait_for(guard, std::chrono::milliseconds(50), [&] { return stopping.load(); });
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error(args[0] + " falhou (status " + std::to_string(status) + "); consulte transfer-process.log");
    }
    void ssh(const std::string &command, const fs::path &output = "/dev/null") {
        process({"ssh", "-T", "-oBatchMode=yes", "-oStrictHostKeyChecking=yes", "-oConnectTimeout=10",
            "-oServerAliveInterval=10", "-oServerAliveCountMax=2", "--", config.sshHost, command}, output);
    }
    void send(const fs::path &local, const std::string &remote) {
        const auto dir = config.remoteDirectory.rfind("~/", 0) == 0 ? config.remoteDirectory.substr(2) : config.remoteDirectory;
        process({"rsync", "-t", "--delay-updates", "--partial-dir=.rsync-partial", "--timeout=30",
            "--bwlimit=" + std::to_string(config.bandwidthKbps), "-e",
            "ssh -T -oBatchMode=yes -oStrictHostKeyChecking=yes -oConnectTimeout=10 -oServerAliveInterval=10 -oServerAliveCountMax=2",
            "--", fs::absolute(local).string(), config.sshHost + ":" + dir + "/" + remote}, "/dev/null", 86400);
    }
    std::pair<double, int64_t> inspectVideo(const fs::path &video, const fs::path &probeFile,
                       const Json &session, const Json &event, const Segment &segment) {
        // ffprobe from the same installation as the configured FFmpeg.
        const auto probe = (fs::path(ffmpeg).parent_path() / "ffprobe").string();
        process({probe, "-v", "error", "-select_streams", "v:0", "-show_entries",
            "stream=codec_name,duration,nb_frames:format=duration", "-of", "json", video.string()}, probeFile);
        const auto details = readJson(probeFile);
        fs::remove(probeFile);
        const auto &v = details.at("streams").at(0);
        const double duration = std::stod(v.at("duration").get<std::string>());
        const auto count = std::stoll(v.at("nb_frames").get<std::string>());
        const double fps = session.at("fps").get<double>();
        const auto expected = event.at("frames").get<int64_t>();
        if (v.at("codec_name") != "h264" || !std::isfinite(duration) || duration <= 0 || count != expected ||
            std::abs(duration - count / fps) > 0.01 || std::abs(duration - (segment.end - segment.start)) > 1 / fps + .01)
            throw std::runtime_error("Container/duração/frames não correspondem à captura: " + video.string());
        return {duration, count};
    }
    void thumbnail(const fs::path &video, const fs::path &folder, const std::string &id, int64_t count, double fps) {
        const auto temp = folder / (id + ".tmp.jpg");
        const auto jpg = folder / (id + ".jpg");
        const double at = std::min(config.thumbnailTimestampSeconds, std::max(0.0, (count - 1) / fps - 0.001));
        process({ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-threads", "1",
            "-ss", std::to_string(at), "-i", video.string(), "-frames:v", "1", "-an",
            "-vf", "scale=" + std::to_string(config.thumbnailWidth) + ":-2", "-filter_threads", "1",
            "-threads", "1", "-q:v", "3", temp.string()});
        if (!fs::exists(temp) || !fs::file_size(temp)) throw std::runtime_error("FFmpeg não gerou a thumbnail");
        fs::rename(temp, jpg);
        log("Thumbnail gerada: " + id);
    }
    void prepare(Json &job, const fs::path &jobPath, const Json &session, const Json &event,
                 const fs::path &video, const Segment &segment, const std::string &stream) {
        const auto id = job.at("id").get<std::string>();
        const auto folder = jobPath.parent_path();
        const auto metadataPath = folder / (id + ".json");
        if (job.value("prepared", false)) {
            if (!fs::exists(metadataPath) || (job.value("hasThumbnail", false) && !fs::exists(folder / (id + ".jpg"))))
                throw std::runtime_error("Artefato preparado ausente; restaure os arquivos locais");
            return;
        }
        const auto details = inspectVideo(video, folder / (id + ".probe.tmp"), session, event, segment);
        const auto duration = details.first;
        const auto size = fs::file_size(video);
        if (!size) throw std::runtime_error("Vídeo vazio");
        if (config.thumbnailEnabled) thumbnail(video, folder, id, details.second, session.at("fps").get<double>());
        Json metadata = {{"id", id}, {"source", "kinect"}, {"device", session.at("device")},
            {"startedAt", utc(event.at("startedUtcMs"))}, {"endedAt", utc(event.at("endedUtcMs"))},
            {"durationMs", std::llround(duration * 1000)}, {"video", id + ".mp4"},
            {"thumbnail", config.thumbnailEnabled ? Json(id + ".jpg") : Json(nullptr)}, {"sizeBytes", size},
            {"sessionId", session.at("session_id")}, {"stream", stream}, {"segmentIndex", segment.index},
            {"captureDurationMs", event.at("elapsedMs")}, {"timestampBasis", "capture UTC anchor + monotonic CFR offsets; stop event; not sensor exposure"}};
        saveJson(metadataPath, metadata);
        job["prepared"] = true;
        job["hasThumbnail"] = config.thumbnailEnabled;
        saveJson(jobPath, job);
    }
    void transfer(const Json &job, const fs::path &folder, const fs::path &video) {
        const auto id = job.at("id").get<std::string>();
        const auto remoteDir = config.remoteDirectory.rfind("~/", 0) == 0 ?
            "\"$HOME\"/" + quote(config.remoteDirectory.substr(2)) : quote(config.remoteDirectory);
        log("Transferência iniciada: " + id);
        const auto metadata = readJson(folder / (id + ".json"));
        const auto remoteMarker = folder / (id + ".remote.tmp");
        ssh("mkdir -p " + remoteDir + " && cd " + remoteDir + " && if test -e " + quote(id + ".json") +
            "; then cat " + quote(id + ".json") + "; fi", remoteMarker);
        if (fs::file_size(remoteMarker)) {
            const auto published = readJson(remoteMarker);
            fs::remove(remoteMarker);
            if (published != metadata) throw std::runtime_error("Conflito: UUID já publicado com outros metadados");
            log("Transferência concluída (publicação já confirmada): " + id);
            return;
        }
        fs::remove(remoteMarker);
        if (fs::file_size(video) != metadata.at("sizeBytes").get<uint64_t>())
            throw std::runtime_error("Tamanho do vídeo mudou após finalização");
        send(video, id + ".mp4");
        if (job.at("hasThumbnail").get<bool>()) send(folder / (id + ".jpg"), id + ".jpg");
        // Remote temp stays outside the backend's *.json discovery until both media succeeded.
        send(folder / (id + ".json"), "." + id + ".json.pending");
        const auto pending = quote("." + id + ".json.pending"), final = quote(id + ".json");
        ssh("cd " + remoteDir + " && if test -e " + final + "; then cmp -s " + pending + " " + final +
            " && rm -f " + pending + "; else mv -f " + pending + " " + final + "; fi");
        log("Transferência concluída: " + id);
    }
    // Uses the same serial worker, process runner, ffprobe, thumbnail and rsync as manual jobs.
    void prepareAutomatic(Json &job, const fs::path &jobPath, const Json &session) {
        const auto folder = jobPath.parent_path().parent_path();
        const auto id = session.at("gravacaoId").get<std::string>();
        if (job.value("prepared", false)) return;
        Json files = Json::array();
        int64_t begin = 0, end = 0;
        fs::path firstVideo;
        int64_t firstCount = 0;
        for (const std::string stream : {"rgb", "depth"}) {
            const auto list = segments(folder / stream / "segments.csv", stream, id);
            if (list.empty()) throw std::runtime_error("Captura sem segmentos finalizados");
            int expectedIndex = 0;
            int64_t totalFrames = 0;
            for (const auto &s : list) {
                if (s.index != expectedIndex++) throw std::runtime_error("Sequência de segmentos incompleta");
                const auto event = readJson(folder / "capture" / (std::to_string(s.index) + ".json"));
                if (event.value("error", true)) throw std::runtime_error("Evento de captura inválido");
                const auto video = folder / s.name;
                regularSize(video);
                const auto details = inspectVideo(video, folder / "publication" / "probe.tmp", session, event, s);
                totalFrames += details.second;
                if (firstVideo.empty()) { firstVideo = video; firstCount = details.second; }
                const int64_t start = event.at("startedUtcMs"), finish = event.at("endedUtcMs");
                if (finish <= start) throw std::runtime_error("Intervalo UTC inválido");
                begin = begin ? std::min(begin, start) : start; end = std::max(end, finish);
                files.push_back({{"nome", s.name}, {"tipo", "video"}, {"tamanhoBytes", regularSize(video)}});
            }
            if (totalFrames != session.at("frames_written_pairs").get<int64_t>())
                throw std::runtime_error("Lista de segmentos não cobre toda a captura");
        }
        if (config.thumbnailEnabled) {
            thumbnail(firstVideo, folder, id, firstCount, session.at("fps").get<double>());
            integration::syncFile(folder / (id + ".jpg"));
            files.push_back({{"nome", id + ".jpg"}, {"tipo", "thumbnail"}, {"tamanhoBytes", regularSize(folder / (id + ".jpg"))}});
        }
        if (files.size() > 32) throw std::runtime_error("Captura excede 32 arquivos do contrato v1");
        Json manifest = {{"v", 1}, {"gravacaoId", id}, {"sessaoId", session.at("sessaoId")},
            {"iniciadaEm", utc(begin)}, {"encerradaEm", utc(end)}, {"arquivos", files}};
        saveJson(folder / (id + ".json"), manifest);
        job["sha256"] = integration::sha256(integration::read(folder / (id + ".json")));
        job["prepared"] = true;
        saveJson(jobPath, job);
        log("Manifesto v1 finalizado: sessão=" + session.at("sessaoId").get<std::string>() + " gravação=" + id + " pasta=" + folder.string());
    }
    static uint64_t regularSize(const fs::path &path) {
        struct stat info{};
        if (lstat(path.c_str(), &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
            info.st_size <= 0 || uint64_t(info.st_size) > 9007199254740991ULL)
            throw std::runtime_error("Arquivo inválido/vazio/com links: " + path.string());
        return info.st_size;
    }
    void transferAutomatic(Json &job, const fs::path &jobPath) {
        const auto folder = jobPath.parent_path().parent_path();
        const auto id = job.at("id").get<std::string>();
        const auto manifestPath = folder / (id + ".json");
        regularSize(manifestPath);
        const auto bytes = integration::read(manifestPath);
        const auto digest = integration::sha256(bytes);
        if (bytes.size() > 65536 || digest != job.at("sha256").get<std::string>()) throw std::runtime_error("Manifesto preparado foi alterado");
        const auto manifest = Json::parse(bytes);
        if (manifest.at("gravacaoId") != id || !integration::sessionIdValid(manifest.at("sessaoId")))
            throw std::runtime_error("Identidade do manifesto inválida");
        const std::regex namePattern(id + "(?:-[a-z0-9]{1,16})?\\.(?:mp4|jpg)");
        for (const auto &f : manifest.at("arquivos")) {
            const auto name = f.at("nome").get<std::string>();
            if (!std::regex_match(name, namePattern) || regularSize(folder / name) != f.at("tamanhoBytes").get<uint64_t>())
                throw std::runtime_error("Arquivo mudou após finalização");
        }
        const auto remoteDir = config.remoteDirectory.rfind("~/", 0) == 0 ?
            "\"$HOME\"/" + quote(config.remoteDirectory.substr(2)) : quote(config.remoteDirectory);
        const auto remoteMarker = jobPath.parent_path() / "remote-marker.tmp";
        ssh("mkdir -p " + remoteDir + " && cd " + remoteDir + " && if test -e " + quote(id + ".ready") +
            "; then cat " + quote(id + ".ready") + "; fi", remoteMarker);
        auto existing = integration::read(remoteMarker); fs::remove(remoteMarker);
        if (!existing.empty()) {
            if (existing != digest && existing != digest + '\n') throw std::runtime_error("Colisão: marcador remoto diferente");
            log("Publicação já confirmada: " + id); return;
        }
        // Always replay the immutable payload after an interrupted publication. This also
        // repairs a destination restored from backup between attempts; ID never changes.
        job["state"] = "transferring"; saveJson(jobPath, job);
        log("Transferência v1 iniciada: " + id);
        for (const auto &f : manifest.at("arquivos")) {
            const auto name = f.at("nome").get<std::string>(); send(folder / name, name);
        }
        send(manifestPath, id + ".json");
        job["state"] = "publishing"; saveJson(jobPath, job);
        // The marker is separate and can never enter the media batch.
        const auto marker = jobPath.parent_path() / (id + ".ready");
        integration::atomicWrite(marker, digest + '\n');
        send(marker, id + ".ready");
        log("Transferência v1 publicada: " + id);
    }
    void automatic(const fs::path &folder, const Json &session, bool reset,
                   uint64_t &pending, uint64_t &completed, uint64_t &failed) {
        const auto id = session.at("gravacaoId").get<std::string>();
        if (!integration::uuidValid(id) || !integration::sessionIdValid(session.at("sessaoId")) || folder.filename() != id)
            throw std::runtime_error("Identidade automática inválida");
        const auto state = session.value("state", "");
        if (state != "completed" || session.value("rgb_exit_code", -1) != 0 || session.value("depth_exit_code", -1) != 0) {
            if (state == "error") ++failed;
            // A dead producer is not proof that encoders finalized their last MP4.
            if (state != "error" && !integration::producerAlive(session)) {
                ++failed;
                const auto interrupted = folder / "interrupted.json";
                if (!fs::exists(interrupted)) {
                    saveJson(interrupted, {{"state", "error"}, {"gravacaoId", id}, {"reason", "producer_interrupted; manual validation required"}});
                    log("Captura interrompida preservada; não publicar MP4 sem fechamento confirmado: " + id, true);
                }
            }
            return;
        }
        fs::create_directories(folder / "publication");
        const auto jobPath = folder / "publication" / "automatic.job.json";
        Json job;
        if (fs::exists(jobPath)) job = readJson(jobPath);
        else {
            job = {{"id", id}, {"attempts", 0}, {"nextAttemptMs", 0}, {"state", "pending"}, {"prepared", false}};
            saveJson(jobPath, job);
        }
        if (job.at("id") != id) throw std::runtime_error("Identidade divergente na fila");
        if (job.value("state", "") == "completed") { ++completed; return; }
        if (reset) { job["attempts"] = 0; job["nextAttemptMs"] = 0; job["state"] = "pending"; saveJson(jobPath, job); }
        if (job.value("state", "") == "failed") { ++failed; return; }
        ++pending;
        if (job.value("nextAttemptMs", int64_t(0)) > wallMs()) return;
        try {
            prepareAutomatic(job, jobPath, session);
            if (!config.enabled) return;
            transferAutomatic(job, jobPath);
            job["state"] = "completed"; job["completedAt"] = utc(wallMs()); job["error"] = "";
            saveJson(jobPath, job); --pending; ++completed;
        } catch (const std::exception &e) {
            if (stopping) return;
            const int attempts = job.value("attempts", 0) + 1;
            job["attempts"] = attempts; job["error"] = e.what();
            job["nextAttemptMs"] = wallMs() + int64_t(config.retryIntervalSeconds) * 1000;
            const bool exhausted = config.maxRetries >= 0 && attempts > config.maxRetries;
            job["state"] = exhausted ? "failed" : "pending";
            saveJson(jobPath, job);
            if (exhausted) { --pending; ++failed; }
            log("Gravação " + id + ": " + e.what(), true);
        }
    }
    void scan(bool reset) {
        uint64_t pending = 0, completed = 0, failed = 0;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (stopping) break;
            if (!entry.is_directory() || !fs::exists(entry.path() / "session.json")) continue;
            try {
                const auto session = readJson(entry.path() / "session.json");
                if (session.value("mode", "") == "automatic") {
                    automatic(entry.path(), session, reset, pending, completed, failed); continue;
                }
                // Legacy sessions lack event times. Never fabricate them at recovery time.
                if (!session.contains("session_id") || !session.contains("device")) continue;
                for (const std::string stream : {"rgb", "depth"}) {
                    const auto list = segments(entry.path() / stream / "segments.csv", stream);
                    for (const auto &s : list) {
                        if (stopping) break;
                        const auto eventPath = entry.path() / "capture" / (std::to_string(s.index) + ".json");
                        if (!fs::exists(eventPath)) continue;
                        const auto event = readJson(eventPath);
                        if (event.value("error", false)) continue;
                        std::ostringstream next;
                        next << stream << '_' << std::setw(3) << std::setfill('0') << s.index + 1 << ".mp4";
                        // CSV is flushed before close. Next-file open proves previous close returned;
                        // last file additionally requires waitpid success persisted by the recorder.
                        const bool closed = fs::exists(entry.path() / stream / next.str()) ||
                            (session.value("state", "") == "completed" && session.value(stream + "_exit_code", -1) == 0);
                        if (!closed) continue;
                        const auto folder = entry.path() / "publication";
                        fs::create_directories(folder);
                        const auto jobPath = folder / (s.name + ".job.json");
                        Json job;
                        if (fs::exists(jobPath)) job = readJson(jobPath);
                        else {
                            job = {{"id", uuid()}, {"attempts", 0}, {"nextAttemptMs", 0}, {"state", "pending"}, {"prepared", false}};
                            saveJson(jobPath, job);
                            log("Gravação finalizada: " + s.name + " -> " + job.at("id").get<std::string>());
                        }
                        if (!std::regex_match(job.at("id").get<std::string>(),
                            std::regex("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}")))
                            throw std::runtime_error("UUID inválido na fila");
                        if (job.value("state", "") == "completed") { ++completed; continue; }
                        if (reset) {
                            job["attempts"] = 0; job["nextAttemptMs"] = 0; job["state"] = "pending";
                            saveJson(jobPath, job);
                        }
                        if (job.value("state", "") == "failed") { ++failed; continue; }
                        ++pending;
                        if (job.value("nextAttemptMs", int64_t(0)) > wallMs()) continue;
                        const auto video = entry.path() / stream / s.name;
                        try {
                            prepare(job, jobPath, session, event, video, s, stream);
                            if (!config.enabled) continue;
                            transfer(job, folder, video);
                            job["state"] = "completed"; job["completedAt"] = utc(wallMs()); job["error"] = "";
                            saveJson(jobPath, job);
                            --pending; ++completed;
                        } catch (const std::exception &e) {
                            if (stopping) break; // cancellation does not consume a retry
                            const int attempts = job.value("attempts", 0) + 1;
                            job["attempts"] = attempts; job["error"] = e.what();
                            job["nextAttemptMs"] = wallMs() + int64_t(config.retryIntervalSeconds) * 1000;
                            const bool exhausted = config.maxRetries >= 0 && attempts > config.maxRetries;
                            job["state"] = exhausted ? "failed" : "pending";
                            saveJson(jobPath, job);
                            if (exhausted) { --pending; ++failed; }
                            log(std::string(e.what()) + (exhausted ? "; tentativas esgotadas: " : "; nova tentativa agendada: ") +
                                job.at("id").get<std::string>(), true);
                        }
                    }
                }
            } catch (const std::exception &e) {
                ++failed;
                if (lastScanError != e.what()) { lastScanError = e.what(); log(e.what(), true); }
            }
        }
        std::lock_guard<std::mutex> guard(mutex);
        state.pending = pending; state.completed = completed; state.failed = failed;
    }
    void run() {
        // Linux scheduling applies to this TID and is inherited by children, not the capture/SRT threads.
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#ifdef SYS_ioprio_set
        syscall(SYS_ioprio_set, 1, 0, (3 << 13)); // idle I/O class
#endif
        try {
            validate(config);
            fs::create_directories(root);
            lockFd = open((root / ".transfer.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB)) throw std::runtime_error("Outra fila usa este output_root (ou lock indisponível)");
            log(config.enabled ? "Fila iniciada; recuperando pendências" : "Transferência desativada; preparando arquivos localmente");
            while (!stopping) {
                scan(retry.exchange(false));
                std::unique_lock<std::mutex> guard(mutex);
                wake.wait_for(guard, std::chrono::seconds(1), [&] { return stopping.load() || retry.load(); });
            }
        } catch (const std::exception &e) { log(e.what(), true); }
        if (lockFd >= 0) { close(lockFd); lockFd = -1; }
    }
};
RecordingTransfer::RecordingTransfer() : impl(new Impl) {}
RecordingTransfer::~RecordingTransfer() { shutdown(); }
void RecordingTransfer::start(const std::string &root, const std::string &ffmpeg, const Config &config) {
    shutdown();
    impl->root = fs::absolute(root); impl->ffmpeg = ffmpeg; impl->config = config;
    impl->stopping = false; impl->retry = false;
    { std::lock_guard<std::mutex> guard(impl->mutex); impl->state = {}; }
    impl->worker = std::thread(&Impl::run, impl.get());
}
void RecordingTransfer::shutdown() {
    impl->stopping = true; impl->wake.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
}
void RecordingTransfer::retryFailed() { impl->retry = true; impl->wake.notify_all(); }
RecordingTransfer::Status RecordingTransfer::status() const {
    std::lock_guard<std::mutex> guard(impl->mutex); return impl->state;
}
