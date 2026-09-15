#include "StringUtil.h"

#include <algorithm>
#include <cctype>

namespace tv {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

std::string toLower(const std::string& s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool containsFold(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    return toLower(haystack).find(needleLower) != std::string::npos;
}

}  // namespace tv
