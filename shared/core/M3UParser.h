// Extended M3U (#EXTM3U / #EXTINF) parser used for iptv-org playlists.
#pragma once

#include <string>

#include "Channel.h"

namespace tv {

struct ParseResult {
    ChannelList channels;
    size_t skippedEntries = 0;  // #EXTINF records without a usable URL
};

// Parses an in-memory playlist. Tolerant of CRLF, blank lines, comments and
// unknown directives; never throws.
ParseResult parseM3U(const std::string& text);

}  // namespace tv
