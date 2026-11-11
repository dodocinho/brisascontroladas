#pragma once
// Linux persistence and contract primitives shared by recorder, protocol and transfer.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <Poco/SHA2Engine.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <chrono>
#include <iomanip>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
namespace integration {
using Json = nlohmann::json;
namespace fs = std::filesystem;
// Session identifiers are opaque metadata, never paths or shell arguments.
inline bool sessionIdValid(const std::string &s) {
    return !s.empty() && s.size() <= 256 &&
        std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline bool uuidValid(const std::string &s) {
    static const std::regex p("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    return std::regex_match(s, p);
}
inline std::string uuid() {
    std::ifstream in("/proc/sys/kernel/random/uuid"); std::string id; in >> id;
    if (!uuidValid(id)) throw std::runtime_error("Falha gerando UUID");
    return id;
}
inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline std::string utc(int64_t ms) {
    time_t s = ms / 1000; std::tm t{}; gmtime_r(&s, &t);
    std::ostringstream o; o << std::put_time(&t, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms % 1000 << 'Z'; return o.str();
}
inline int64_t timestamp(const std::string &s) {
    static const std::regex p("([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(?:\\.([0-9]{1,3}))?Z");
    std::smatch m;
    if (!std::regex_match(s, m, p)) throw std::runtime_error("Timestamp UTC inválido");
    std::tm t{}; std::istringstream in(m[1]); in >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
    if (in.fail()) throw std::runtime_error("Calendário inválido");
    const auto secs = timegm(&t); char canonical[32]; std::strftime(canonical, sizeof canonical, "%Y-%m-%dT%H:%M:%S", &t);
    if (m[1] != canonical) throw std::runtime_error("Calendário inválido");
    auto fraction = m[2].str(); fraction.resize(3, '0');
    return int64_t(secs) * 1000 + std::stoi(fraction);
}
inline std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Arquivo indisponível: " + p.string());
    return {std::istreambuf_iterator<char>(in), {}};
}
// PID alone is not an identity: Linux may reuse it, especially across reboots.
inline std::string processIdentity(int pid) {
    if (pid <= 0) return {};
    std::ifstream bootFile("/proc/sys/kernel/random/boot_id");
    std::string boot; bootFile >> boot;
    std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
    std::string stat; std::getline(statFile, stat);
    const auto end = stat.rfind(')');
    if (boot.empty() || end == std::string::npos) return {};
    std::istringstream fields(stat.substr(end + 1));
    std::string start;
    for (int field = 3; field <= 22; ++field) if (!(fields >> start)) return {};
    return boot + ":" + std::to_string(pid) + ":" + start;
}
inline bool producerAlive(const Json &session) {
    const int owner = session.value("owner_pid", 0);
    if (session.contains("owner_identity")) {
        const auto identity = processIdentity(owner);
        return !identity.empty() && identity == session.at("owner_identity").get<std::string>();
    }
    return owner > 0 && (kill(owner, 0) == 0 || errno != ESRCH);
}
inline void syncFile(const fs::path &p) {
    const int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("Falha abrindo arquivo para fsync: " + p.string());
    const int rc = fsync(fd); close(fd);
    if (rc) throw std::runtime_error("Falha fsync mídia: " + p.string());
}
inline void atomicWrite(const fs::path &p, const std::string &bytes) {
    const auto tmp = p.string() + ".tmp";
    int fd = open(tmp.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::runtime_error("Falha abrindo persistência: " + p.string());
    size_t at = 0;
    while (at < bytes.size()) {
        const auto n = write(fd, bytes.data() + at, bytes.size() - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); throw std::runtime_error("Falha persistindo arquivo"); }
        at += n;
    }
    const int rc = fsync(fd); close(fd);
    if (rc) throw std::runtime_error("Falha fsync arquivo");
    fs::rename(tmp, p);
    fd = open(p.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Falha abrindo diretório");
    const int drc = fsync(fd); close(fd);
    if (drc) throw std::runtime_error("Falha fsync diretório");
}
inline std::string sha256(const std::string &data) {
    Poco::SHA2Engine engine; engine.update(data); return Poco::DigestEngine::digestToHex(engine.digest());
}
}
