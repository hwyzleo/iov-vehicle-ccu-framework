#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace ccutest {

namespace fs = std::filesystem;

// RAII temporary directory for config test fixtures.
struct TempDir {
    fs::path path;

    TempDir() {
        auto base = fs::temp_directory_path();
        std::string name = "ccu-cfg-test-";
        for (int i = 0; i < 8; ++i) name.push_back('a' + (std::rand() % 26));
        path = base / name;
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    // Create a subdirectory (e.g. "conf.d"), returns full path.
    fs::path mkdir(const std::string& rel) {
        fs::path p = path / rel;
        fs::create_directories(p);
        return p;
    }

    // Write content to a relative path under the temp dir.
    fs::path writeFile(const std::string& rel, const std::string& content) {
        fs::path p = path / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        if (!f) throw std::runtime_error("cannot write " + p.string());
        f << content;
        f.close();
        return p;
    }

    // Create a directory entry (non-regular file) at rel, to test fail-fast.
    fs::path makeDir(const std::string& rel) {
        fs::path p = path / rel;
        fs::create_directories(p);
        return p;
    }
};

} // namespace ccutest
