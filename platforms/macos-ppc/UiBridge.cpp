#include "UiBridge.h"

#include <sys/sysctl.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "core/AppController.h"
#include "core/Country.h"
#include "core/Paths.h"
#include "core/Strings.h"

namespace {

// One flattened line of the channel tree.
struct Row {
    std::string title;
    int depth = 0;
    bool isChannel = false;
    size_t channelIndex = 0;
    size_t count = 0;
};

// The picture the decoder writes into and the view reads out of. RV24, so the
// bytes are plain R, G, B in that order on this big-endian machine and an
// NSBitmapImageRep can take the buffer as it stands.
class FrameBuffer : public tv::VideoFrameSink {
public:
    void setCallback(RtvFrameCallback callback, void* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = callback;
        context_ = context;
    }

    // vmem is configured when the instance is created, so the backend decides
    // the size and this allocates exactly that.
    unsigned setupFormat(unsigned& width, unsigned& height) override {
        std::lock_guard<std::mutex> lock(mutex_);
        width_ = static_cast<int>(width);
        height_ = static_cast<int>(height);
        pitch_ = width_ * 3;
        pixels_.assign(static_cast<size_t>(pitch_) * height_, 0);
        ready_ = false;
        return static_cast<unsigned>(pitch_);
    }

    void* lockFrame() override {
        mutex_.lock();
        return pixels_.empty() ? NULL : &pixels_[0];
    }

    void unlockFrame() override {
        // VLC's RV24 output leaves the bytes in B, G, R order here, and an
        // NSBitmapImageRep can only be told R, G, B. Swapping once per frame
        // beats converting on every redraw.
        const size_t count = pixels_.size();
        for (size_t i = 0; i + 2 < count; i += 3) {
            const unsigned char blue = pixels_[i];
            pixels_[i] = pixels_[i + 2];
            pixels_[i + 2] = blue;
        }
        ready_ = true;
        mutex_.unlock();
    }

    void displayFrame() override {
        RtvFrameCallback callback;
        void* context;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
            context = context_;
        }
        if (callback) callback(context);
    }

    void cleanupFormat() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
    }

    const void* lockForDrawing(int* width, int* height, int* pitch) {
        mutex_.lock();
        if (!ready_ || pixels_.empty()) {
            mutex_.unlock();
            return NULL;
        }
        *width = width_;
        *height = height_;
        *pitch = pitch_;
        return &pixels_[0];
    }

    void unlockAfterDrawing() { mutex_.unlock(); }

private:
    std::mutex mutex_;
    std::vector<unsigned char> pixels_;
    int width_ = 0;
    int height_ = 0;
    int pitch_ = 0;
    bool ready_ = false;
    RtvFrameCallback callback_ = NULL;
    void* context_ = NULL;
};

void copyInto(char* buffer, size_t size, const std::string& text) {
    if (!buffer || size == 0) return;
    const size_t n = text.size() < size - 1 ? text.size() : size - 1;
    std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
}

}  // namespace

struct RtvApp {
    tv::AppPaths paths;
    tv::AppController* controller = nullptr;
    std::vector<Row> rows;
    RtvStateCallback stateCallback = NULL;
    void* stateContext = NULL;
    FrameBuffer frames;
    bool standardDefinitionOnly = false;

    // Returns how many channels ended up under this node. A category left with
    // none of them - everything filtered away - takes its heading with it.
    size_t flatten(const tv::CategoryNode& node, int depth) {
        const size_t headingAt = rows.size();
        Row heading;
        heading.depth = depth;
        if (!node.countryCode.empty())
            heading.title = tv::countryFlagEmoji(node.countryCode) + " " + node.title;
        else
            heading.title = node.title;
        rows.push_back(heading);

        size_t channels = 0;
        for (size_t i = 0; i < node.children.size(); ++i)
            channels += flatten(node.children[i], depth + 1);

        const tv::ChannelIndex& index = controller->index();
        for (size_t i = 0; i < node.channels.size(); ++i) {
            const tv::Channel& channel = index.channelAt(node.channels[i]);
            if (standardDefinitionOnly && tv::announcesHighDefinition(channel.name)) continue;
            Row row;
            row.depth = depth + 1;
            row.isChannel = true;
            row.channelIndex = node.channels[i];
            row.title = channel.name;
            rows.push_back(row);
            ++channels;
        }

        if (channels == 0) {
            rows.resize(headingAt);
            return 0;
        }
        rows[headingAt].count = channels;
        return channels;
    }

    void rebuild() {
        rows.clear();
        const std::vector<tv::CategoryNode>& roots = controller->index().roots();
        for (size_t i = 0; i < roots.size(); ++i) flatten(roots[i], 0);
    }
};

extern "C" {

int rtv_prefers_standard_definition(void) {
    // CPU_SUBTYPE_POWERPC_970 is the G5, which can manage 720p. Everything
    // older on this platform cannot, and an unreadable answer errs towards SD.
    int subtype = 0;
    size_t size = sizeof subtype;
    if (sysctlbyname("hw.cpusubtype", &subtype, &size, NULL, 0) != 0) return 1;
    return subtype == 100 ? 0 : 1;
}

RtvApp* rtv_create(const char* seedPath, const char* pluginPath, char* errorOut, size_t errorSize) {
    RtvApp* app = new RtvApp();
    app->paths.dataDir = tv::appDataDir();
    if (seedPath) app->paths.seedPath = seedPath;
    if (pluginPath) app->paths.pluginPath = pluginPath;
    app->controller = new tv::AppController(app->paths);
    app->standardDefinitionOnly = rtv_prefers_standard_definition() != 0;

    std::string error;
    if (!app->controller->startPlayer(&error)) copyInto(errorOut, errorSize, error);
    else copyInto(errorOut, errorSize, std::string());
    return app;
}

void rtv_destroy(RtvApp* app) {
    if (!app) return;
    delete app->controller;
    delete app;
}

int rtv_load_local(RtvApp* app) {
    if (!app) return 0;
    const bool ok = app->controller->loadLocalPlaylist();
    app->rebuild();
    return ok ? 1 : 0;
}

void rtv_refresh(RtvApp* app, RtvRefreshCallback done, void* context) {
    if (!app) return;
    app->controller->refreshAsync([done, context](const tv::RefreshResult& result) {
        if (done) done(result.updated ? 1 : 0, result.error.c_str(), context);
    });
}

void rtv_set_filter(RtvApp* app, const char* text) {
    if (!app) return;
    app->controller->index().setFilter(text ? text : "");
    app->rebuild();
}

void rtv_set_standard_definition_only(RtvApp* app, int on) {
    if (!app) return;
    app->standardDefinitionOnly = on != 0;
    app->rebuild();
}

int rtv_standard_definition_only(RtvApp* app) {
    return app && app->standardDefinitionOnly ? 1 : 0;
}

void rtv_set_grouping_by_country(RtvApp* app, int byCountry) {
    if (!app) return;
    app->controller->index().setGrouping(byCountry ? tv::Grouping::Country
                                                   : tv::Grouping::Category);
    app->rebuild();
}

size_t rtv_row_count(RtvApp* app) { return app ? app->rows.size() : 0; }

int rtv_row(RtvApp* app, size_t index, RtvRow* out) {
    if (!app || !out || index >= app->rows.size()) return 0;
    const Row& row = app->rows[index];
    out->title = row.title.c_str();
    out->depth = row.depth;
    out->isChannel = row.isChannel ? 1 : 0;
    out->channelIndex = row.channelIndex;
    out->count = row.count;
    return 1;
}

void rtv_status(RtvApp* app, char* buffer, size_t size) {
    if (!app) return;
    const char* origin = tv::str(tv::Str::NoPlaylist);
    switch (app->controller->playlistSource()) {
        case tv::PlaylistSource::Network: origin = tv::str(tv::Str::Online); break;
        case tv::PlaylistSource::Cache: origin = tv::str(tv::Str::OfflineCache); break;
        case tv::PlaylistSource::Seed: origin = tv::str(tv::Str::BundledSnapshot); break;
        case tv::PlaylistSource::None: break;
    }
    size_t listed = 0;
    for (size_t i = 0; i < app->rows.size(); ++i)
        if (app->rows[i].isChannel) ++listed;
    char shown[32];
    char total[32];
    std::snprintf(shown, sizeof shown, "%lu", static_cast<unsigned long>(listed));
    std::snprintf(total, sizeof total, "%lu",
                  static_cast<unsigned long>(app->controller->index().totalChannels()));
    copyInto(buffer, size, tv::format(tv::Str::ChannelCount, shown, total) + " - " + origin);
}

void rtv_attach_video(RtvApp* app, void* nsView) {
    if (!app || !app->controller->player()) return;
    // The OpenGL output cannot start on this hardware, so the picture comes
    // back through the sink instead and the front end draws it.
    if (!app->controller->player()->attachVideoSink(&app->frames))
        app->controller->player()->attachVideoView(nsView);
}

void rtv_set_frame_callback(RtvApp* app, RtvFrameCallback callback, void* context) {
    if (app) app->frames.setCallback(callback, context);
}

const void* rtv_video_lock(RtvApp* app, int* width, int* height, int* pitch) {
    return app ? app->frames.lockForDrawing(width, height, pitch) : NULL;
}

void rtv_video_unlock(RtvApp* app) {
    if (app) app->frames.unlockAfterDrawing();
}

int rtv_play_row(RtvApp* app, size_t index) {
    if (!app || index >= app->rows.size() || !app->rows[index].isChannel) return 0;
    tv::MediaPlayer* player = app->controller->player();
    if (!player) return 0;
    return player->play(app->controller->index().channelAt(app->rows[index].channelIndex)) ? 1 : 0;
}

void rtv_stop(RtvApp* app) {
    if (app && app->controller->player()) app->controller->player()->stop();
}

void rtv_toggle_pause(RtvApp* app) {
    if (!app || !app->controller->player()) return;
    tv::MediaPlayer* player = app->controller->player();
    player->setPaused(!player->isPaused());
}

int rtv_is_paused(RtvApp* app) {
    return app && app->controller->player() && app->controller->player()->isPaused() ? 1 : 0;
}

void rtv_set_volume(RtvApp* app, int percent) {
    if (app && app->controller->player()) app->controller->player()->setVolume(percent);
}

void rtv_set_state_callback(RtvApp* app, RtvStateCallback callback, void* context) {
    if (!app || !app->controller->player()) return;
    app->stateCallback = callback;
    app->stateContext = context;
    RtvApp* self = app;
    app->controller->player()->setStateCallback(
        [self](tv::PlaybackState state, const std::string& message) {
            if (self->stateCallback)
                self->stateCallback(static_cast<int>(state), message.c_str(), self->stateContext);
        });
}

void rtv_toggle_favorite(RtvApp* app, size_t index) {
    if (!app || index >= app->rows.size() || !app->rows[index].isChannel) return;
    app->controller->toggleFavorite(
        app->controller->index().channelAt(app->rows[index].channelIndex).url);
    app->rebuild();
}

int rtv_is_favorite(RtvApp* app, size_t index) {
    if (!app || index >= app->rows.size() || !app->rows[index].isChannel) return 0;
    const std::string& url = app->controller->index().channelAt(app->rows[index].channelIndex).url;
    return app->controller->favorites().contains(url) ? 1 : 0;
}

const char* rtv_text_select_channel(void) { return tv::str(tv::Str::SelectChannel); }
const char* rtv_text_search(void) { return tv::str(tv::Str::SearchPlaceholder); }
const char* rtv_text_category(void) { return tv::str(tv::Str::TabCategory); }
const char* rtv_text_country(void) { return tv::str(tv::Str::TabCountry); }
const char* rtv_text_sd_only(void) { return tv::str(tv::Str::StandardDefinitionOnly); }

}  // extern "C"
