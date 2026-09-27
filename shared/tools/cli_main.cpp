// Headless driver for the portable core. Exercises download, offline caching and
// parsing without any UI - the first thing to get running on a new platform.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "core/AppController.h"
#include "core/HlsRelay.h"
#include "core/Paths.h"

namespace {

// Mirrors the outline view: subcategories first, then the channels that stop
// at this level.
void printCategory(const tv::CategoryNode& node, const tv::ChannelIndex& index, int depth) {
    std::string indent(static_cast<size_t>(depth) * 2, ' ');
    std::printf("%s%s%s (%zu)\n", indent.c_str(), depth ? "- " : "== ", node.title.c_str(),
                node.totalChannels);
    for (const tv::CategoryNode& child : node.children) printCategory(child, index, depth + 1);

    size_t shown = 0;
    for (size_t i : node.channels) {
        const tv::Channel& ch = index.channelAt(i);
        std::printf("%s    %-44s %s\n", indent.c_str(), ch.name.c_str(), ch.url.c_str());
        if (++shown >= 8 && node.channels.size() > 8) {
            std::printf("%s    ... %zu more\n", indent.c_str(), node.channels.size() - shown);
            break;
        }
    }
}

// Percent-encoding for a channel URL carried in a /tune query.
std::string urlEncode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

void printUsage() {
    std::printf(
        "usage: rtv-cli [command]\n"
        "  list [country]    show cached/seeded channels, grouped by category\n"
        "                    or by country\n"
        "  refresh           download the playlist and update the offline cache\n"
        "  search <text>     filter channels by name, group or country\n"
        "  relay <url> [s]   run the HLS relay on one stream and print its address\n"
        "  serve [port]      serve every channel as plain MPEG-TS to players that\n"
        "                    cannot read HLS (default port 8090, all interfaces)\n"
        "  playlist <base> [sd]\n"
        "                    write an M3U whose entries point at a `serve` tuner;\n"
        "                    `sd` leaves out the channels that announce HD\n"
        "  where             print cache and data locations\n");
}

}  // namespace

int main(int argc, char** argv) {
    tv::AppPaths paths;
    paths.dataDir = tv::appDataDir();
    paths.seedPath = tv::joinPath(".", "resources/seed-playlist.m3u");

    tv::AppController app(paths);
    const std::string command = argc > 1 ? argv[1] : "list";

    if (command == "where") {
        std::printf("data dir : %s\n", paths.dataDir.c_str());
        std::printf("cache    : %s\n", app.store().cachePath().c_str());
        std::printf("backup   : %s\n", app.store().backupPath().c_str());
        std::printf("seed     : %s\n", paths.seedPath.c_str());
        std::printf("url      : %s\n", app.store().url().c_str());
        return 0;
    }

    if (command == "refresh") {
        std::printf("downloading %s ...\n", app.store().url().c_str());
        std::atomic<bool> done{false};
        tv::RefreshResult outcome;
        app.refreshAsync([&](const tv::RefreshResult& r) {
            outcome = r;
            done.store(true);
        });
        while (!done.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (outcome.updated)
            std::printf("updated: %zu channels cached\n", outcome.channelCount);
        else if (outcome.notModified)
            std::printf("already up to date (%zu channels)\n", outcome.channelCount);
        else
            std::printf("failed: %s (offline copy kept)\n", outcome.error.c_str());
        return outcome.succeeded ? 0 : 1;
    }

    if (command == "serve") {
        const int wanted = argc > 2 ? std::atoi(argv[2]) : 8090;
        tv::RelaySettings settings = tv::RelaySettings::fromEnvironment();
        settings.verbose = true;
        // A player handed one continuous stream needs far less of a head start
        // than one handed a playlist: writing blocks until the next segment is
        // ready, which paces it by itself. Waiting the playlist default here
        // just means the player sits without a byte until it times out.
        if (std::getenv("RTV_RELAY_BUFFER") == nullptr) settings.startBufferSeconds = 6;
        tv::HlsTuner tuner(std::shared_ptr<tv::HttpClient>(tv::makeHttpClient()), settings);
        std::string error;
        if (!tuner.start(wanted, true, &error)) {
            std::fprintf(stderr, "cannot listen on port %d: %s\n", wanted, error.c_str());
            return 1;
        }
        std::printf("tuner listening on port %d (every interface)\n", tuner.port());
        std::printf("a player opens http://<this machine>:%d/tune?u=<channel url>\n", tuner.port());
        std::fflush(stdout);
        tuner.run();
        return 0;
    }

    if (command == "relay" && argc > 2) {
        tv::RelaySettings settings = tv::RelaySettings::fromEnvironment();
        settings.verbose = true;
        tv::HlsRelay relay(std::shared_ptr<tv::HttpClient>(tv::makeHttpClient()), settings);
        relay.setProgressCallback(
            [](int percent) { std::fprintf(stderr, "[relay] start buffer %d%%\n", percent); });
        tv::Channel channel;
        channel.name = "cli";
        channel.url = argv[2];
        bool relayed = false;
        const std::string url = relay.open(channel, [] { return false; }, &relayed);
        std::printf("%s %s\n", relayed ? "relaying at" : "not relayed; play directly:", url.c_str());
        std::fflush(stdout);
        if (!relayed) return 1;
        const int seconds = argc > 3 ? std::atoi(argv[3]) : 0;
        for (int i = 0; seconds <= 0 || i < seconds; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    }

    if (!app.loadLocalPlaylist()) {
        std::printf("no offline playlist available; run `rtv-cli refresh` first\n");
        return 1;
    }

    if (command == "playlist" && argc > 2) {
        std::string base = argv[2];
        while (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
        const bool sdOnly = argc > 3 && std::string(argv[3]) == "sd";
        std::printf("#EXTM3U\n");
        const tv::ChannelList& all = app.index().channels();
        size_t written = 0;
        for (size_t i = 0; i < all.size(); ++i) {
            const tv::Channel& ch = all[i];
            if (sdOnly && tv::announcesHighDefinition(ch.name)) continue;
            std::printf("#EXTINF:-1 group-title=\"%s\",%s\n", ch.group.c_str(), ch.name.c_str());
            std::printf("%s/tune?u=%s\n", base.c_str(), urlEncode(ch.url).c_str());
            ++written;
        }
        std::fprintf(stderr, "%zu of %zu channels written\n", written, all.size());
        return 0;
    }

    if (argc > 2 && std::string(argv[2]) == "country") app.index().setGrouping(tv::Grouping::Country);

    if (command == "search" && argc > 2 && std::string(argv[2]) != "country") {
        app.index().setFilter(argv[2]);
    } else if (command != "list" && command != "search") {
        printUsage();
        return 2;
    }

    for (const tv::CategoryNode& root : app.index().roots()) printCategory(root, app.index(), 0);

    std::printf("\n%zu of %zu channels\n", app.index().visibleChannels(),
                app.index().totalChannels());
    return 0;
}
