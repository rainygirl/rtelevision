#include "ChannelIndex.h"

#include "Strings.h"

#include <algorithm>
#include <map>
#include <utility>

#include "Country.h"
#include "StringUtil.h"

namespace tv {
namespace {

void sortByName(std::vector<size_t>& indices, const ChannelList& channels) {
    std::sort(indices.begin(), indices.end(), [&channels](size_t a, size_t b) {
        return toLower(channels[a].name) < toLower(channels[b].name);
    });
}

struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return toLower(a) < toLower(b);
    }
};

// Intermediate tree; std::map keeps siblings sorted as they are inserted.
struct Builder {
    std::map<std::string, Builder, CaseInsensitiveLess> children;
    std::vector<size_t> channels;
};

size_t convert(const std::string& title, const std::string& path, const Builder& builder,
               const ChannelList& channels, CategoryNode& out) {
    out.title = title;
    out.path = path;
    out.channels = builder.channels;
    std::sort(out.channels.begin(), out.channels.end(), [&channels](size_t a, size_t b) {
        return toLower(channels[a].name) < toLower(channels[b].name);
    });

    size_t total = out.channels.size();
    out.children.reserve(builder.children.size());
    for (const auto& entry : builder.children) {
        CategoryNode child;
        const std::string childPath = path.empty() ? entry.first : path + ";" + entry.first;
        total += convert(entry.first, childPath, entry.second, channels, child);
        out.children.push_back(std::move(child));
    }
    out.totalChannels = total;
    return total;
}

}  // namespace

namespace {
std::string toUpper(const std::string& s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}
}  // namespace

std::vector<std::string> splitCategoryPath(const std::string& groupTitle) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= groupTitle.size()) {
        size_t sep = groupTitle.find(';', start);
        std::string part = trim(groupTitle.substr(
            start, sep == std::string::npos ? std::string::npos : sep - start));
        if (!part.empty()) out.push_back(part);
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    if (out.empty()) out.push_back("Ungrouped");
    return out;
}

void ChannelIndex::setChannels(ChannelList channels) {
    channels_ = std::move(channels);
    rebuild();
}

void ChannelIndex::setFavorites(const std::set<std::string>& favoriteUrls) {
    favorites_ = favoriteUrls;
    rebuild();
}

void ChannelIndex::setFilter(const std::string& text) {
    std::string next = toLower(trim(text));
    if (next == filter_) return;
    filter_ = next;
    rebuild();
}

void ChannelIndex::setGrouping(Grouping grouping) {
    if (grouping == grouping_) return;
    grouping_ = grouping;
    rebuild();
}

size_t ChannelIndex::findByUrl(const std::string& url) const {
    for (size_t i = 0; i < channels_.size(); ++i)
        if (channels_[i].url == url) return i;
    return npos;
}

void ChannelIndex::rebuild() {
    roots_.clear();
    visibleChannels_ = 0;

    std::vector<size_t> matches;
    std::vector<size_t> favoriteChannels;
    matches.reserve(channels_.size());

    for (size_t i = 0; i < channels_.size(); ++i) {
        const Channel& ch = channels_[i];
        if (!filter_.empty()) {
            const bool hit = containsFold(ch.name, filter_) ||
                             containsFold(ch.group, filter_) ||
                             containsFold(ch.country, filter_);
            if (!hit) continue;
        }
        ++visibleChannels_;
        matches.push_back(i);
        if (favorites_.count(ch.url)) favoriteChannels.push_back(i);
    }

    if (!favoriteChannels.empty()) {
        CategoryNode favorites;
        favorites.title = str(Str::Favorites);
        favorites.path = "\x01" "favorites";  // never collides with a real group-title
        favorites.isFavorites = true;
        favorites.channels = std::move(favoriteChannels);
        sortByName(favorites.channels, channels_);
        favorites.totalChannels = favorites.channels.size();
        roots_.push_back(std::move(favorites));
    }

    if (grouping_ == Grouping::Country) buildByCountry(matches);
    else buildByCategory(matches);
}

void ChannelIndex::buildByCategory(const std::vector<size_t>& matches) {
    Builder root;
    for (size_t i : matches) {
        Builder* node = &root;
        for (const std::string& segment : splitCategoryPath(channels_[i].group))
            node = &node->children[segment];
        node->channels.push_back(i);
    }

    CategoryNode virtualRoot;
    convert(std::string(), std::string(), root, channels_, virtualRoot);
    for (CategoryNode& child : virtualRoot.children) roots_.push_back(std::move(child));
}

// One level: every channel sits directly under its country. Channels whose
// tvg-id carries no country code land in a trailing "Unknown" group.
void ChannelIndex::buildByCountry(const std::vector<size_t>& matches) {
    std::map<std::string, std::vector<size_t>> byCountry;
    std::vector<size_t> unknown;

    for (size_t i : matches) {
        const std::string& code = channels_[i].country;
        if (code.empty()) unknown.push_back(i);
        else byCountry[toUpper(code)].push_back(i);
    }

    std::vector<CategoryNode> countries;
    countries.reserve(byCountry.size());
    for (auto& entry : byCountry) {
        CategoryNode node;
        node.title = countryName(entry.first);
        node.path = "\x02" + entry.first;
        node.countryCode = entry.first;
        node.channels = std::move(entry.second);
        sortByName(node.channels, channels_);
        node.totalChannels = node.channels.size();
        countries.push_back(std::move(node));
    }
    // Alphabetical by the name the user actually reads, not by the code.
    std::sort(countries.begin(), countries.end(),
              [](const CategoryNode& a, const CategoryNode& b) {
                  return toLower(a.title) < toLower(b.title);
              });
    for (CategoryNode& node : countries) roots_.push_back(std::move(node));

    if (!unknown.empty()) {
        CategoryNode node;
        node.title = "Unknown";
        node.path = "\x02" "unknown";
        node.channels = std::move(unknown);
        sortByName(node.channels, channels_);
        node.totalChannels = node.channels.size();
        roots_.push_back(std::move(node));
    }
}

}  // namespace tv
