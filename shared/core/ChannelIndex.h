// Category tree + filtering shared by every UI front end. Keeping this in the
// portable core means a new platform only has to render rows.
//
// iptv-org encodes a hierarchy in group-title with ';' separators, e.g.
// "Animation;Kids;Movies" -> Animation > Kids > Movies. Each channel lands on
// the leaf of its own path; parents carry the aggregate count.
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "Channel.h"

namespace tv {

// How the tree is organised. The UI switches between these; everything else -
// filtering, favorites, counts - behaves the same either way.
enum class Grouping { Category, Country };

struct CategoryNode {
    std::string title;                  // this segment only, e.g. "Kids"
    std::string path;                   // full path, e.g. "Animation;Kids"
    std::vector<CategoryNode> children;
    std::vector<size_t> channels;       // channels whose full path ends here
    size_t totalChannels = 0;           // channels here plus every descendant
    bool isFavorites = false;
    // Set when grouping by country, so a front end can draw the flag.
    std::string countryCode;
};

class ChannelIndex {
public:
    void setChannels(ChannelList channels);
    void setFavorites(const std::set<std::string>& favoriteUrls);
    // Case-insensitive match against channel name, group and country code.
    void setFilter(const std::string& text);
    const std::string& filter() const { return filter_; }

    void setGrouping(Grouping grouping);
    Grouping grouping() const { return grouping_; }

    const ChannelList& channels() const { return channels_; }
    const std::vector<CategoryNode>& roots() const { return roots_; }
    const Channel& channelAt(size_t index) const { return channels_[index]; }

    size_t totalChannels() const { return channels_.size(); }
    size_t visibleChannels() const { return visibleChannels_; }
    size_t findByUrl(const std::string& url) const;

    static const size_t npos = static_cast<size_t>(-1);

private:
    void rebuild();
    void buildByCategory(const std::vector<size_t>& matches);
    void buildByCountry(const std::vector<size_t>& matches);

    ChannelList channels_;
    std::set<std::string> favorites_;
    std::string filter_;
    Grouping grouping_ = Grouping::Category;
    std::vector<CategoryNode> roots_;
    size_t visibleChannels_ = 0;
};

// Splits a group-title into its hierarchy segments.
std::vector<std::string> splitCategoryPath(const std::string& groupTitle);

}  // namespace tv
