#include "MainWindow.h"

#include "core/Strings.h"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>

// Xlib defines these as macros and they collide with ordinary C++ identifiers
// (PlaylistSource::None, status_t-style names). Nothing below needs them.
#undef None
#undef Status
#undef Bool
#undef Success

#include <ctime>
#include <utility>

#include "core/Country.h"

namespace tv {
namespace linux_ui {
namespace {

// Tree store layout.
enum {
    COL_LABEL,     // display text
    COL_INDEX,     // channel index, or -1 for a category
    COL_IS_CHANNEL,
    COL_WEIGHT,    // categories are drawn semibold
    N_COLUMNS
};

// Hops a lambda onto the GTK main loop. Worker threads must never touch widgets.
template <typename Fn>
void RunOnMainLoop(Fn fn) {
    auto* boxed = new Fn(std::move(fn));
    g_idle_add(
        [](gpointer data) -> gboolean {
            auto* f = static_cast<Fn*>(data);
            (*f)();
            delete f;
            return G_SOURCE_REMOVE;
        },
        boxed);
}

std::string formatCount(size_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    int count = 0;
    for (size_t i = digits.size(); i-- > 0;) {
        out.insert(out.begin(), digits[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return out;
}

}  // namespace

MainWindow::MainWindow(std::shared_ptr<AppController> controller)
    : controller_(std::move(controller)) {
    BuildInterface();
    ReloadChannelTree();

    MediaPlayer* player = controller_->player();
    if (player != nullptr) {
        player->setStateCallback([this](PlaybackState state, const std::string& message) {
            std::string copy = message;
            RunOnMainLoop([this, state, copy]() { OnPlayerState(state, copy); });
        });
    }
}

MainWindow::~MainWindow() {
    MediaPlayer* player = controller_->player();
    if (player != nullptr) player->setStateCallback(nullptr);
}

void MainWindow::Show() {
    gtk_widget_show_all(window_);
    gtk_widget_hide(spinner_);
    RefreshPlaylist();

    // Debug aid, also useful when bringing up a new platform.
    const char* autoplay = g_getenv("RTV_AUTOPLAY");
    if (autoplay != nullptr && *autoplay != '\0') {
        gtk_entry_set_text(GTK_ENTRY(searchEntry_), autoplay);
        controller_->index().setFilter(autoplay);
        ReloadChannelTree();
        StepChannel(1);
    }
}


void MainWindow::BuildInterface() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "R Television");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 760);
    g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(&MainWindow::OnKeyPress), this);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    sidebar_ = BuildSidebar();
    gtk_paned_pack1(GTK_PANED(paned), sidebar_, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), BuildPlayerPane(), TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 300);
    gtk_container_add(GTK_CONTAINER(window_), paned);
}

GtkWidget* MainWindow::BuildSidebar() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(box, 240, -1);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);

    searchEntry_ = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry_), str(Str::SearchPlaceholder));
    g_signal_connect(searchEntry_, "search-changed", G_CALLBACK(&MainWindow::OnSearchChanged),
                     this);
    gtk_box_pack_start(GTK_BOX(box), searchEntry_, FALSE, FALSE, 0);

    // Category / country switch, drawn as a pair of linked buttons.
    categoryButton_ = gtk_radio_button_new_with_label(nullptr, str(Str::TabCategory));
    countryButton_ = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(categoryButton_), str(Str::TabCountry));
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(categoryButton_), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(countryButton_), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(categoryButton_), TRUE);
    g_signal_connect(countryButton_, "toggled", G_CALLBACK(&MainWindow::OnGroupingToggled), this);

    GtkWidget* switcher = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(switcher), GTK_STYLE_CLASS_LINKED);
    gtk_box_pack_start(GTK_BOX(switcher), categoryButton_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(switcher), countryButton_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), switcher, FALSE, FALSE, 0);

    store_ = gtk_tree_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_INT64, G_TYPE_BOOLEAN,
                                G_TYPE_INT);
    treeView_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store_));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView_), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeView_), FALSE);

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
    GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
        str(Str::ColumnChannel), renderer, "text", COL_LABEL, "weight", COL_WEIGHT, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeView_), column);

    g_signal_connect(treeView_, "row-activated", G_CALLBACK(&MainWindow::OnRowActivated), this);
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView_));
    g_signal_connect(selection, "changed", G_CALLBACK(&MainWindow::OnSelectionChanged), this);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), treeView_);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget* statusRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    sidebarStatus_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(sidebarStatus_), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(sidebarStatus_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_sensitive(sidebarStatus_, FALSE);
    spinner_ = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(statusRow), sidebarStatus_, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(statusRow), spinner_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), statusRow, FALSE, FALSE, 0);

    return box;
}

// Symbolic icon names are monochrome by definition and follow the text colour,
// so the row of transport buttons stays grey whatever the icon theme is.
GtkWidget* MainWindow::IconButton(const char* iconName, const char* tooltip, GCallback handler) {
    GtkWidget* button = gtk_button_new_from_icon_name(iconName, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button, tooltip);
    g_signal_connect_swapped(button, "clicked", handler, this);
    return button;
}

GtkWidget* MainWindow::BuildPlayerPane() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    videoArea_ = gtk_drawing_area_new();
    gtk_widget_set_size_request(videoArea_, 480, 270);
    // libVLC draws straight into this window; GTK must not paint over it.
    gtk_widget_set_app_paintable(videoArea_, TRUE);
    // Deprecated in GTK 3.14 but still the documented way to hand an X window
    // to an external renderer without GTK repainting over it.
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_widget_set_double_buffered(videoArea_, FALSE);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_widget_add_events(videoArea_, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(videoArea_, "realize", G_CALLBACK(&MainWindow::OnVideoRealized), this);
    g_signal_connect(videoArea_, "button-press-event",
                     G_CALLBACK(&MainWindow::OnVideoButtonPress), this);
    // Paint the letterbox black ourselves; the themed background would show
    // through around the video.
    g_signal_connect(videoArea_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
                         cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                         cairo_paint(cr);
                         return FALSE;
                     }),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(box), videoArea_, TRUE, TRUE, 0);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 8);

    playPauseButton_ = IconButton("media-playback-start-symbolic", str(Str::TipPlayPause),
                                  G_CALLBACK(+[](MainWindow* self) { self->TogglePlayPause(); }));
    GtkWidget* stopButton = IconButton("media-playback-stop-symbolic", str(Str::TipStop),
                                       G_CALLBACK(+[](MainWindow* self) { self->Stop(); }));
    favoriteButton_ = IconButton("non-starred-symbolic", str(Str::TipFavorite),
                                 G_CALLBACK(+[](MainWindow* self) { self->ToggleFavorite(); }));
    gtk_box_pack_start(GTK_BOX(bar), playPauseButton_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), stopButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), favoriteButton_, FALSE, FALSE, 0);

    GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    nowPlayingLabel_ = gtk_label_new(str(Str::SelectChannel));
    gtk_label_set_xalign(GTK_LABEL(nowPlayingLabel_), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(nowPlayingLabel_), PANGO_ELLIPSIZE_END);
    stateLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(stateLabel_), 0.0f);
    gtk_widget_set_sensitive(stateLabel_, FALSE);
    gtk_box_pack_start(GTK_BOX(labels), nowPlayingLabel_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels), stateLabel_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), labels, TRUE, TRUE, 12);

    muteButton_ = IconButton("audio-volume-high-symbolic", str(Str::TipMute),
                             G_CALLBACK(+[](MainWindow* self) { self->ToggleMute(); }));
    volumeScale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_scale_set_draw_value(GTK_SCALE(volumeScale_), FALSE);
    gtk_range_set_value(GTK_RANGE(volumeScale_), 80);
    gtk_widget_set_size_request(volumeScale_, 120, -1);
    g_signal_connect(volumeScale_, "value-changed", G_CALLBACK(&MainWindow::OnVolumeChanged),
                     this);
    GtkWidget* fullScreenButton =
        IconButton("view-fullscreen-symbolic", str(Str::TipFullScreen),
                   G_CALLBACK(+[](MainWindow* self) { self->ToggleFullScreen(); }));

    gtk_box_pack_start(GTK_BOX(bar), muteButton_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), volumeScale_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), fullScreenButton, FALSE, FALSE, 0);

    controlBar_ = bar;
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    return box;
}

gboolean MainWindow::OnVideoRealized(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    MediaPlayer* player = self->controller_->player();
    GdkWindow* window = gtk_widget_get_window(widget);
    if (player != nullptr && window != nullptr && GDK_IS_X11_WINDOW(window)) {
        // libVLC renders into the X window of this drawing area.
        player->attachVideoView(reinterpret_cast<void*>(GDK_WINDOW_XID(window)));
        self->videoAttached_ = true;
    }
    return FALSE;
}


void MainWindow::AppendCategory(const CategoryNode& node, GtkTreeIter* parent, bool expandAll) {
    GtkTreeIter iter;
    gtk_tree_store_append(store_, &iter, parent);

    std::string label =
        node.isFavorites ? "\u2605 " + std::string(str(Str::Favorites)) : node.title;
    if (!node.countryCode.empty()) {
        std::string flag = countryFlagEmoji(node.countryCode);
        if (!flag.empty()) label = flag + " " + label;
    }
    label += "  " + formatCount(node.totalChannels);
    gtk_tree_store_set(store_, &iter, COL_LABEL, label.c_str(), COL_INDEX, (gint64)-1,
                       COL_IS_CHANNEL, FALSE, COL_WEIGHT, PANGO_WEIGHT_SEMIBOLD, -1);

    for (size_t i = 0; i < node.children.size(); ++i)
        AppendCategory(node.children[i], &iter, expandAll);

    const ChannelIndex& index = controller_->index();
    for (size_t i = 0; i < node.channels.size(); ++i) {
        const Channel& channel = index.channelAt(node.channels[i]);
        std::string text;
        if (controller_->favorites().contains(channel.url)) text += "★ ";
        // In country mode the flag already sits on the parent row.
        if (controller_->index().grouping() != Grouping::Country) {
            std::string flag = countryFlagEmoji(channel.country);
            if (!flag.empty()) text += flag + " ";
        }
        text += channel.name;

        GtkTreeIter child;
        gtk_tree_store_append(store_, &child, &iter);
        gtk_tree_store_set(store_, &child, COL_LABEL, text.c_str(), COL_INDEX,
                           (gint64)node.channels[i], COL_IS_CHANNEL, TRUE, COL_WEIGHT,
                           PANGO_WEIGHT_NORMAL, -1);
    }
}

void MainWindow::ReloadChannelTree() {
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeView_), nullptr);  // detach while filling
    gtk_tree_store_clear(store_);

    // A filtered result set is small enough to show fully expanded.
    const bool expandAll = !controller_->index().filter().empty() &&
                           controller_->index().visibleChannels() <= 400;

    const std::vector<CategoryNode>& roots = controller_->index().roots();
    for (size_t i = 0; i < roots.size(); ++i) AppendCategory(roots[i], nullptr, expandAll);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeView_), GTK_TREE_MODEL(store_));
    if (expandAll) gtk_tree_view_expand_all(GTK_TREE_VIEW(treeView_));

    UpdateSidebarStatus();
}

void MainWindow::UpdateSidebarStatus() {
    const char* origin = str(Str::NoPlaylist);
    switch (controller_->playlistSource()) {
        case PlaylistSource::Network: origin = str(Str::Online); break;
        case PlaylistSource::Cache: origin = str(Str::OfflineCache); break;
        case PlaylistSource::Seed: origin = str(Str::BundledSnapshot); break;
        case PlaylistSource::None: break;
    }

    std::string when;
    std::time_t fetched = controller_->playlistFetchedAt();
    if (fetched > 0) {
        char buffer[32];
        std::tm tm {};
        localtime_r(&fetched, &tm);
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
        when = std::string(" · ") + buffer;
    }

    std::string text = format(Str::ChannelCount,
                              formatCount(controller_->index().visibleChannels()),
                              formatCount(controller_->index().totalChannels())) +
                       " \u00b7 " + origin + when;
    if (!refreshNote_.empty()) text += " · " + refreshNote_;
    gtk_label_set_text(GTK_LABEL(sidebarStatus_), text.c_str());
    gtk_widget_set_tooltip_text(sidebarStatus_, text.c_str());
}

void MainWindow::UpdateFavoriteButton() {
    size_t channelIndex = 0;
    if (!SelectedChannel(&channelIndex)) return;
    const Channel& channel = controller_->index().channelAt(channelIndex);
    const bool isFavorite = controller_->favorites().contains(channel.url);
    gtk_button_set_image(
        GTK_BUTTON(favoriteButton_),
        gtk_image_new_from_icon_name(isFavorite ? "starred-symbolic" : "non-starred-symbolic",
                                     GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(favoriteButton_, isFavorite ? str(Str::TipRemoveFavorite)
                                                            : str(Str::TipAddFavorite));
}


bool MainWindow::SelectedChannel(size_t* channelIndexOut) const {
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView_));
    GtkTreeIter iter;
    GtkTreeModel* model = nullptr;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return false;

    gboolean isChannel = FALSE;
    gint64 index = -1;
    gtk_tree_model_get(model, &iter, COL_IS_CHANNEL, &isChannel, COL_INDEX, &index, -1);
    if (!isChannel || index < 0) return false;
    if (channelIndexOut != nullptr) *channelIndexOut = (size_t)index;
    return true;
}

void MainWindow::PlayChannel(size_t channelIndex) {
    const Channel& channel = controller_->index().channelAt(channelIndex);
    gtk_label_set_text(GTK_LABEL(nowPlayingLabel_), channel.name.c_str());
    gtk_window_set_title(GTK_WINDOW(window_), ("R Television - " + channel.name).c_str());

    MediaPlayer* player = controller_->player();
    if (player == nullptr) {
        gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::NoBackend));
        return;
    }
    streamStarted_ = false;
    gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::Opening));
    gtk_button_set_image(GTK_BUTTON(playPauseButton_),
                         gtk_image_new_from_icon_name("media-playback-pause-symbolic",
                                                      GTK_ICON_SIZE_BUTTON));
    if (!player->play(channel))
        gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::CannotPlay));
}

void MainWindow::TogglePlayPause() {
    MediaPlayer* player = controller_->player();
    if (player == nullptr) return;
    if (player->state() == PlaybackState::Idle || player->state() == PlaybackState::Stopped) {
        size_t channelIndex = 0;
        if (SelectedChannel(&channelIndex)) PlayChannel(channelIndex);
        return;
    }
    const bool pause = !player->isPaused();
    player->setPaused(pause);
    gtk_button_set_image(
        GTK_BUTTON(playPauseButton_),
        gtk_image_new_from_icon_name(pause ? "media-playback-start-symbolic" : "media-playback-pause-symbolic",
                                     GTK_ICON_SIZE_BUTTON));
}

void MainWindow::Stop() {
    if (controller_->player() != nullptr) controller_->player()->stop();
    gtk_button_set_image(GTK_BUTTON(playPauseButton_),
                         gtk_image_new_from_icon_name("media-playback-start-symbolic",
                                                      GTK_ICON_SIZE_BUTTON));
}

void MainWindow::ToggleMute() {
    MediaPlayer* player = controller_->player();
    if (player == nullptr) return;
    const bool muted = !player->isMuted();
    player->setMuted(muted);
    gtk_button_set_image(
        GTK_BUTTON(muteButton_),
        gtk_image_new_from_icon_name(muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic",
                                     GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(muteButton_, muted ? str(Str::TipUnmute) : str(Str::TipMute));
    gtk_widget_set_sensitive(volumeScale_, !muted);
}

void MainWindow::ToggleFullScreen() {
    fullScreen_ = !fullScreen_;

    // Full screen means the picture and nothing else. Left in place, the channel
    // list and the transport bar keep their space and letterbox the video into a
    // corner of the screen. GtkPaned drops its handle once a child is hidden, so
    // the video pane ends up owning every pixel.
    if (sidebar_ != nullptr) gtk_widget_set_visible(sidebar_, !fullScreen_);
    if (controlBar_ != nullptr) gtk_widget_set_visible(controlBar_, !fullScreen_);

    if (fullScreen_) gtk_window_fullscreen(GTK_WINDOW(window_));
    else gtk_window_unfullscreen(GTK_WINDOW(window_));
}

void MainWindow::ToggleFavorite() {
    size_t channelIndex = 0;
    if (!SelectedChannel(&channelIndex)) return;
    controller_->toggleFavorite(controller_->index().channelAt(channelIndex).url);
    ReloadChannelTree();
    UpdateFavoriteButton();
}

void MainWindow::StepChannel(int delta) {
    GtkTreeModel* model = GTK_TREE_MODEL(store_);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_first(model, &iter)) return;

    // Walk the visible rows in order and start the first channel found.
    GtkTreePath* path = gtk_tree_path_new_first();
    for (int guard = 0; guard < 100000; ++guard) {
        GtkTreeIter current;
        if (!gtk_tree_model_get_iter(model, &current, path)) break;
        gboolean isChannel = FALSE;
        gint64 index = -1;
        gtk_tree_model_get(model, &current, COL_IS_CHANNEL, &isChannel, COL_INDEX, &index, -1);
        if (isChannel && index >= 0) {
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(treeView_), path);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeView_), path, nullptr, FALSE);
            PlayChannel((size_t)index);
            break;
        }
        // Depth-first: first child, else next sibling, else up.
        if (gtk_tree_model_iter_has_child(model, &current)) {
            gtk_tree_path_down(path);
            continue;
        }
        while (true) {
            GtkTreePath* next = gtk_tree_path_copy(path);
            gtk_tree_path_next(next);
            GtkTreeIter probe;
            if (gtk_tree_model_get_iter(model, &probe, next)) {
                gtk_tree_path_free(path);
                path = next;
                break;
            }
            gtk_tree_path_free(next);
            if (!gtk_tree_path_up(path) || gtk_tree_path_get_depth(path) == 0) {
                gtk_tree_path_free(path);
                return;
            }
        }
    }
    gtk_tree_path_free(path);
    (void)delta;
}

void MainWindow::RefreshPlaylist() {
    if (controller_->refreshing()) return;
    gtk_widget_show(spinner_);
    gtk_spinner_start(GTK_SPINNER(spinner_));
    refreshNote_ = str(Str::Downloading);
    UpdateSidebarStatus();

    controller_->refreshAsync([this](const RefreshResult& result) {
        RefreshResult copy = result;
        RunOnMainLoop([this, copy]() { OnRefreshFinished(copy); });
    });
}


void MainWindow::OnRefreshFinished(const RefreshResult& result) {
    gtk_spinner_stop(GTK_SPINNER(spinner_));
    gtk_widget_hide(spinner_);

    if (result.updated) {
        refreshNote_.clear();
        ReloadChannelTree();
    } else if (result.notModified) {
        refreshNote_.clear();
        UpdateSidebarStatus();
    } else {
        // Browsing and playback continue from the offline copy.
        refreshNote_ = std::string(str(Str::RefreshFailed)) + " (" + result.error + ")";
        UpdateSidebarStatus();
    }
}

void MainWindow::OnPlayerState(PlaybackState state, const std::string& message) {
    switch (state) {
        case PlaybackState::Opening:
            streamStarted_ = false;
            gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::Opening));
            break;
        case PlaybackState::Buffering:
            // VLC keeps emitting these during playback; only show them until the
            // stream has actually started.
            if (!streamStarted_)
                gtk_label_set_text(GTK_LABEL(stateLabel_),
                                   (std::string(str(Str::Buffering)) + " " + message).c_str());
            break;
        case PlaybackState::Playing:
            streamStarted_ = true;
            gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::Playing));
            gtk_button_set_image(GTK_BUTTON(playPauseButton_),
                                 gtk_image_new_from_icon_name("media-playback-pause-symbolic",
                                                              GTK_ICON_SIZE_BUTTON));
            break;
        case PlaybackState::Paused:
            gtk_label_set_text(GTK_LABEL(stateLabel_), str(Str::Paused));
            break;
        case PlaybackState::Stopped:
            streamStarted_ = false;
            gtk_label_set_text(GTK_LABEL(stateLabel_),
                               message.empty() ? str(Str::Stopped) : message.c_str());
            break;
        case PlaybackState::Error:
            streamStarted_ = false;
            gtk_label_set_text(GTK_LABEL(stateLabel_),
                               (std::string(str(Str::PlaybackFailed)) + " - " +
                                (message.empty() ? std::string(str(Str::UnknownError))
                                                 : message)).c_str());
            break;
        case PlaybackState::Idle:
            break;
    }
}

void MainWindow::OnGroupingToggled(GtkToggleButton* button, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    const bool country = gtk_toggle_button_get_active(button);
    self->controller_->index().setGrouping(country ? Grouping::Country : Grouping::Category);
    self->ReloadChannelTree();
}

void MainWindow::OnSearchChanged(GtkSearchEntry* entry, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    self->controller_->index().setFilter(gtk_entry_get_text(GTK_ENTRY(entry)));
    self->ReloadChannelTree();
}

void MainWindow::OnRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column,
                                gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->store_), &iter, path)) return;

    gboolean isChannel = FALSE;
    gint64 index = -1;
    gtk_tree_model_get(GTK_TREE_MODEL(self->store_), &iter, COL_IS_CHANNEL, &isChannel, COL_INDEX,
                       &index, -1);
    if (isChannel && index >= 0) {
        self->PlayChannel((size_t)index);
        return;
    }
    if (gtk_tree_view_row_expanded(view, path)) gtk_tree_view_collapse_row(view, path);
    else gtk_tree_view_expand_row(view, path, FALSE);
}

void MainWindow::OnSelectionChanged(GtkTreeSelection* selection, gpointer data) {
    static_cast<MainWindow*>(data)->UpdateFavoriteButton();
}

gboolean MainWindow::OnKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;

    if (event->keyval == GDK_KEY_F11) {
        self->ToggleFullScreen();
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape && self->fullScreen_) {
        self->ToggleFullScreen();
        return TRUE;
    }
    if (!ctrl) return FALSE;

    switch (event->keyval) {
        case GDK_KEY_f: gtk_widget_grab_focus(self->searchEntry_); return TRUE;
        case GDK_KEY_r: self->RefreshPlaylist(); return TRUE;
        case GDK_KEY_d: self->ToggleFavorite(); return TRUE;
        case GDK_KEY_m: self->ToggleMute(); return TRUE;
        case GDK_KEY_period: self->Stop(); return TRUE;
        case GDK_KEY_space: self->TogglePlayPause(); return TRUE;
        default: return FALSE;
    }
}

gboolean MainWindow::OnVideoButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type == GDK_2BUTTON_PRESS) {
        static_cast<MainWindow*>(data)->ToggleFullScreen();
        return TRUE;
    }
    return FALSE;
}

void MainWindow::OnVolumeChanged(GtkRange* range, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    if (self->controller_->player() != nullptr)
        self->controller_->player()->setVolume((int)gtk_range_get_value(range));
}

}  // namespace linux_ui
}  // namespace tv
