// Headless driver for the portable core. Exercises download, offline caching and
// parsing without any UI - the first thing to get running on a new platform.
#include <atomic>
#include <chrono>
#include <cstdio>
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

void printUsage() {
    std::printf(
        "usage: rtv-cli [command]\n"
        "  list [country]    show cached/seeded channels, grouped by category\n"
        "                    or by country\n"
        "  refresh           download the playlist and update the offline cache\n"
        "  search <text>     filter channels by name, group or country\n"
        "  relay <url> [s]   run the HLS relay on one stream and print its address\n"
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
