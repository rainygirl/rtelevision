#include "Paths.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tv {
namespace {

const char* kAppFolder = "RTelevision";

std::string homeDir() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string(".");
}

}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (!a.empty() && a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string appDataDir() {
#if defined(__APPLE__)
    std::string base = joinPath(homeDir(), "Library/Application Support");
#elif defined(__HAIKU__)
    std::string base = joinPath(homeDir(), "config/settings");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base = xdg && *xdg ? std::string(xdg) : joinPath(homeDir(), ".local/share");
#endif
    std::string dir = joinPath(base, kAppFolder);
    ensureDirectory(base);
    ensureDirectory(dir);
    return dir;
}

bool ensureDirectory(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    // Create parents as needed.
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) ensureDirectory(path.substr(0, slash));
    return mkdir(path.c_str(), 0755) == 0;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            std::remove(tmp.c_str());
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace tv
