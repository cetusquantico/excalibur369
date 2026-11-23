#include "run_files.h"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ex369 {
namespace {
bool error(std::string& err, const std::string& what) {
    err = what + ": " + std::strerror(errno);
    return false;
}
bool sync_fd(int fd) {
    int rc;
    do { rc = fsync(fd); } while (rc < 0 && errno == EINTR);
    return rc == 0;
}
std::string quote(const std::string& s) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
}

bool canonical_run_path(const std::string& path, std::string& result, std::string& err) {
    if (path.empty()) { result.clear(); return true; }
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) { err = "invalid file path: " + ec.message(); return false; }
    // Resolve parent aliases but leave the final component for O_NOFOLLOW.
    auto parent = std::filesystem::weakly_canonical(absolute.parent_path(), ec);
    if (ec) { err = "invalid parent directory: " + ec.message(); return false; }
    result = (parent / absolute.filename()).lexically_normal().string();
    return true;
}

bool run_paths_distinct(const std::string& checkpoint, const std::string& output,
                        const std::string& input, std::string& err) {
    auto same = [](const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return false;
        std::error_code ec;
        return a == b || std::filesystem::equivalent(a, b, ec);
    };
    if (same(checkpoint, output) || (!output.empty() && same(checkpoint, output + ".lock")) ||
        (!checkpoint.empty() && same(output, checkpoint + ".lock")) ||
        same(checkpoint, input) || same(output, input) ||
        (!checkpoint.empty() && same(checkpoint + ".lock", input)) ||
        (!output.empty() && same(output + ".lock", input))) {
        err = "checkpoint, output, lock files and target inputs must use distinct paths";
        return false;
    }
    return true;
}

PathLock::~PathLock() { if (fd_ >= 0) close(fd_); }
bool PathLock::acquire(const std::string& path, std::string& err) {
    if (path.empty()) return true;
    if (fd_ >= 0) { err = "lock already acquired"; return false; }
    struct stat data{};
    if (lstat(path.c_str(), &data) == 0 && (!S_ISREG(data.st_mode) || data.st_nlink != 1)) {
        err = "run file must be a regular file without symlinks or hard links: " + path;
        return false;
    }
    int fd = ::open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return error(err, "cannot open run lock");
    struct stat st{};
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
        close(fd); err = "lock must be a regular file without hard links"; return false;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        error(err, "run file is busy or cannot be locked: " + path); close(fd); return false;
    }
    fd_ = fd;
    return true;
}

ResultJournal::~ResultJournal() { if (fd_ >= 0) close(fd_); }
bool ResultJournal::open(const std::string& path, std::string& err) {
    if (path.empty()) return true;
    if (fd_ >= 0) { err = "output already open"; return false; }
    int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return error(err, "cannot open result output");
    struct stat st{};
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
        close(fd); err = "result output must be a regular file without hard links"; return false;
    }
    if ((st.st_mode & 077) != 0) {
        close(fd); err = "existing result output must have private permissions (0600)"; return false;
    }
    char last;
    if (st.st_size > 0 && (pread(fd, &last, 1, st.st_size - 1) != 1 || last != '\n')) {
        close(fd); err = "result output has an incomplete final line; preserve it and use a new path"; return false;
    }
    // Make a newly created directory entry durable before starting any work.
    auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) parent = ".";
    int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) { error(err, "cannot open result directory"); close(fd); return false; }
    bool ok = sync_fd(fd) && sync_fd(dir);
    if (!ok) error(err, "cannot synchronize result output");
    close(dir);
    if (!ok) { close(fd); return false; }
    fd_ = fd;
    return true;
}

bool ResultJournal::append(const VerifyResult& r, const std::string& fingerprint, std::string& err) {
    if (fd_ < 0) return true; // output is optional
    if (!r.ok) { err = "refusing an unverified result"; return false; }
    std::string record = "{\"schema\":1,\"tool\":\"EXCALIBUR369\",\"operation\":" +
        quote(fingerprint.empty() ? "verify" : "confirmed_match") + ",\"config_hash\":" + quote(fingerprint) +
        ",\"private\":" + quote(r.priv_hex) + ",\"wif\":" + quote(r.wif) +
        ",\"pubkey\":" + quote(r.pub_compressed_hex) + ",\"hash160\":" + quote(r.hash160_hex) +
        ",\"address\":" + quote(r.address) + ",\"verified\":true}\n";
    size_t done = 0;
    while (done < record.size()) {
        ssize_t n = write(fd_, record.data() + done, record.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return error(err, "cannot write result output");
        done += static_cast<size_t>(n);
    }
    if (!sync_fd(fd_)) return error(err, "cannot synchronize confirmed result");
    return true;
}
} // namespace ex369
