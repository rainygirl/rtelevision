// Per-platform user data locations. Adding a platform means adding one branch.
#pragma once

#include <string>

namespace tv {

// Directory for the offline playlist cache and settings. Created on demand.
std::string appDataDir();

// Joins with the platform separator ('/' on macOS, Haiku and Linux).
std::string joinPath(const std::string& a, const std::string& b);

bool ensureDirectory(const std::string& path);
bool fileExists(const std::string& path);

bool readFile(const std::string& path, std::string& out);
// Writes via a temporary file + rename so a crash never leaves a half-written cache.
bool writeFileAtomic(const std::string& path, const std::string& data);

}  // namespace tv
