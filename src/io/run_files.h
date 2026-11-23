#pragma once
#include <string>
#include "verify/verify.h"

namespace ex369 {

// CLI ownership for an entire invocation, acquired before loading a checkpoint.
// Lock files deliberately persist: unlinking a live lock permits an inode race.
class PathLock {
public:
    PathLock() = default;
    ~PathLock();
    PathLock(const PathLock&) = delete;
    PathLock& operator=(const PathLock&) = delete;
    bool acquire(const std::string& path, std::string& err);
private:
    int fd_ = -1;
};

// Caller must hold the corresponding PathLock. Append only verified records.
class ResultJournal {
public:
    ResultJournal() = default;
    ~ResultJournal();
    ResultJournal(const ResultJournal&) = delete;
    ResultJournal& operator=(const ResultJournal&) = delete;
    bool open(const std::string& path, std::string& err);
    bool append(const VerifyResult& result, const std::string& fingerprint, std::string& err);
private:
    int fd_ = -1;
};

// Empty paths remain empty; normalizes existing parent directory aliases.
bool canonical_run_path(const std::string& path, std::string& result, std::string& err);
bool run_paths_distinct(const std::string& checkpoint, const std::string& output,
                        const std::string& input, std::string& err);
} // namespace ex369
