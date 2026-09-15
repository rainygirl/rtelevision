// Favorite channels, persisted as one stream URL per line.
#pragma once

#include <set>
#include <string>

namespace tv {

class Favorites {
public:
    explicit Favorites(std::string dataDir);

    void load();
    bool save() const;

    bool contains(const std::string& url) const { return urls_.count(url) != 0; }
    void toggle(const std::string& url);
    const std::set<std::string>& urls() const { return urls_; }

private:
    std::string path_;
    std::set<std::string> urls_;
};

}  // namespace tv
