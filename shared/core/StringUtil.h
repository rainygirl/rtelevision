#pragma once

#include <string>

namespace tv {

std::string trim(const std::string& s);
std::string toLower(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool containsFold(const std::string& haystack, const std::string& needleLower);

}  // namespace tv
