#pragma once

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <random>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace ccutest {

namespace fs = std::filesystem;

// RAII temporary directory for store test fixtures.
struct TempDir {
    fs::path path;

    TempDir() {
        auto base = fs::temp_directory_path();
        static std::atomic<unsigned long> counter{0};
        std::random_device rd;
        std::string name = "ccu-store-test-" + std::to_string(::getpid()) +
                           "-" + std::to_string(++counter) + "-" +
                           std::to_string(rd());
        path = base / name;
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        // Relax permissions before removal (some tests make dirs read-only).
        fs::permissions(path,
                        fs::perms::owner_read | fs::perms::owner_write |
                            fs::perms::owner_exec,
                        fs::perm_options::replace, ec);
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    fs::path mkdir(const std::string& rel) {
        fs::path p = path / rel;
        fs::create_directories(p);
        return p;
    }

    fs::path writeFile(const std::string& rel, const std::string& content) {
        fs::path p = path / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        if (!f) throw std::runtime_error("cannot write " + p.string());
        f << content;
        f.close();
        return p;
    }

    // Make a path read-only (for permission-failure tests).
    void makeReadOnly(const std::string& rel) {
        fs::path p = path / rel;
        fs::permissions(p, fs::perms::owner_read | fs::perms::owner_exec,
                        fs::perm_options::replace);
    }

    // Create a symlink `rel` -> `target`.
    fs::path makeSymlink(const std::string& rel, const fs::path& target) {
        fs::path p = path / rel;
        fs::create_directories(p.parent_path());
        fs::create_symlink(target, p);
        return p;
    }
};

} // namespace ccutest
