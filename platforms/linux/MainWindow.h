// GTK3 front end: channel tree on the left, video on the right.
#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>

#include "core/AppController.h"

namespace tv {
namespace linux_ui {

class MainWindow {
public:
    explicit MainWindow(std::shared_ptr<AppController> controller);
    ~MainWindow();

    void Show();

private:
    // --- construction
    void BuildInterface();
    GtkWidget* BuildSidebar();
    GtkWidget* BuildPlayerPane();
    GtkWidget* IconButton(const char* iconName, const char* tooltip, GCallback handler);

    // --- model -> view
    void ReloadChannelTree();
    void AppendCategory(const CategoryNode& node, GtkTreeIter* parent, bool expandAll);
    void UpdateSidebarStatus();
    void UpdateFavoriteButton();

    // --- actions
    void PlayChannel(size_t channelIndex);
    void TogglePlayPause();
    void Stop();
    void ToggleMute();
    void ToggleFullScreen();
    void ToggleFavorite();
    void RefreshPlaylist();
    void StepChannel(int delta);
    bool SelectedChannel(size_t* channelIndexOut) const;

    // --- callbacks marshalled onto the GTK main loop
    void OnRefreshFinished(const RefreshResult& result);
    void OnPlayerState(PlaybackState state, const std::string& message);

    static gboolean OnVideoRealized(GtkWidget* widget, gpointer self);
    static void OnSearchChanged(GtkSearchEntry* entry, gpointer self);
    static void OnGroupingToggled(GtkToggleButton* button, gpointer self);
    static void OnRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column,
                               gpointer self);
    static void OnSelectionChanged(GtkTreeSelection* selection, gpointer self);
    static gboolean OnKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer self);
    static gboolean OnVideoButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer self);
    static void OnVolumeChanged(GtkRange* range, gpointer self);

    std::shared_ptr<AppController> controller_;

    GtkWidget* window_ = nullptr;
    GtkWidget* sidebar_ = nullptr;
    GtkWidget* controlBar_ = nullptr;
    GtkWidget* searchEntry_ = nullptr;
    GtkWidget* categoryButton_ = nullptr;
    GtkWidget* countryButton_ = nullptr;
    GtkWidget* treeView_ = nullptr;
    GtkTreeStore* store_ = nullptr;
    GtkWidget* sidebarStatus_ = nullptr;
    GtkWidget* spinner_ = nullptr;
    GtkWidget* videoArea_ = nullptr;
    GtkWidget* nowPlayingLabel_ = nullptr;
    GtkWidget* stateLabel_ = nullptr;
    GtkWidget* playPauseButton_ = nullptr;
    GtkWidget* favoriteButton_ = nullptr;
    GtkWidget* muteButton_ = nullptr;
    GtkWidget* volumeScale_ = nullptr;

    std::string refreshNote_;
    bool fullScreen_ = false;
    bool streamStarted_ = false;
    bool videoAttached_ = false;
};

}  // namespace linux_ui
}  // namespace tv
