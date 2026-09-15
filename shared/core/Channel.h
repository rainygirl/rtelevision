// Portable channel model. No platform or UI dependencies.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tv {

struct Channel {
    std::string name;
    std::string url;
    std::string group;
    std::string logo;
    std::string tvgId;
    std::string country;  // derived from tvg-id suffix, e.g. "BBCNews.uk@SD" -> "uk"

    // Per-stream options harvested from #EXTVLCOPT lines and inline attributes
    // (http-user-agent, http-referrer, ...). Backend-agnostic key/value pairs.
    std::vector<std::pair<std::string, std::string>> options;

    bool valid() const { return !url.empty() && !name.empty(); }
};

using ChannelList = std::vector<Channel>;

}  // namespace tv
