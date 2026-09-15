#include "M3UParser.h"

#include "StringUtil.h"

namespace tv {
namespace {

// Reads key="value" pairs out of the #EXTINF attribute section.
void parseAttributes(const std::string& s, Channel& ch) {
    size_t i = 0;
    while (i < s.size()) {
        size_t eq = s.find('=', i);
        if (eq == std::string::npos) return;
        size_t keyStart = eq;
        while (keyStart > i && static_cast<unsigned char>(s[keyStart - 1]) > ' ') --keyStart;
        std::string key = s.substr(keyStart, eq - keyStart);

        size_t q1 = s.find('"', eq);
        if (q1 == std::string::npos) return;
        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) return;
        std::string value = s.substr(q1 + 1, q2 - q1 - 1);
        i = q2 + 1;

        if (key == "tvg-id") {
            ch.tvgId = value;
            // "BBCNews.uk@SD" -> country "uk"
            size_t dot = value.find('.');
            if (dot != std::string::npos) {
                size_t end = value.find_first_of("@.", dot + 1);
                ch.country = value.substr(dot + 1, end == std::string::npos ? std::string::npos
                                                                            : end - dot - 1);
            }
        } else if (key == "tvg-logo") {
            ch.logo = value;
        } else if (key == "group-title") {
            ch.group = value;
        } else if (key == "tvg-name") {
            if (ch.name.empty()) ch.name = value;
        } else if (key == "http-user-agent" || key == "user-agent") {
            ch.options.emplace_back("http-user-agent", value);
        } else if (key == "http-referrer" || key == "http-referer") {
            ch.options.emplace_back("http-referrer", value);
        }
    }
}

void parseExtInf(const std::string& line, Channel& ch) {
    // #EXTINF:-1 key="value" ...,Display Name
    size_t colon = line.find(':');
    std::string rest = colon == std::string::npos ? std::string() : line.substr(colon + 1);

    // The display name follows the last comma that is not inside quotes.
    size_t comma = std::string::npos;
    bool inQuotes = false;
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '"') inQuotes = !inQuotes;
        else if (rest[i] == ',' && !inQuotes) comma = i;
    }

    std::string attrs = comma == std::string::npos ? rest : rest.substr(0, comma);
    if (comma != std::string::npos) ch.name = trim(rest.substr(comma + 1));
    parseAttributes(attrs, ch);
}

void parseVlcOpt(const std::string& line, Channel& ch) {
    // #EXTVLCOPT:http-user-agent=Mozilla/5.0 ...
    size_t colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string body = line.substr(colon + 1);
    size_t eq = body.find('=');
    if (eq == std::string::npos) return;
    std::string key = trim(body.substr(0, eq));
    std::string value = trim(body.substr(eq + 1));
    if (key.empty()) return;
    for (auto& kv : ch.options) {
        if (kv.first == key) {  // EXTVLCOPT wins over the inline attribute
            kv.second = value;
            return;
        }
    }
    ch.options.emplace_back(key, value);
}

}  // namespace

ParseResult parseM3U(const std::string& text) {
    ParseResult result;
    result.channels.reserve(text.size() / 200 + 16);

    Channel pending;
    bool havePending = false;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

        line = trim(line);
        if (line.empty()) continue;

        if (startsWith(line, "#EXTINF")) {
            if (havePending) ++result.skippedEntries;
            pending = Channel();
            parseExtInf(line, pending);
            havePending = true;
        } else if (startsWith(line, "#EXTVLCOPT")) {
            if (havePending) parseVlcOpt(line, pending);
        } else if (line[0] == '#') {
            continue;  // #EXTM3U, #EXTGRP and friends
        } else {
            if (!havePending) continue;  // bare URL without metadata
            pending.url = line;
            if (pending.group.empty()) pending.group = "Ungrouped";
            if (pending.name.empty()) pending.name = pending.url;
            if (pending.valid()) result.channels.push_back(pending);
            else ++result.skippedEntries;
            havePending = false;
        }
    }
    if (havePending) ++result.skippedEntries;
    return result;
}

}  // namespace tv
