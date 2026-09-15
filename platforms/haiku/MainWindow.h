// Haiku front end: channel tree on the left, video on the right.
#pragma once

#include <Button.h>
#include <ListItem.h>
#include <OutlineListView.h>
#include <RadioButton.h>
#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <memory>

#include "AudioOutput.h"
#include "Icons.h"
#include "Messages.h"
#include "VideoView.h"
#include "core/AppController.h"

namespace tv {
namespace haiku {

// Leaf row: one channel.
class ChannelItem : public BStringItem {
public:
    ChannelItem(const char* label, size_t channelIndex, uint32 level)
        : BStringItem(label, level, false), fChannelIndex(channelIndex) {}
    size_t ChannelIndex() const { return fChannelIndex; }

private:
    size_t fChannelIndex;
};

// Branch row: one level of the group-title hierarchy.
class CategoryItem : public BStringItem {
public:
    CategoryItem(const char* label, const char* path, uint32 level, bool expanded)
        : BStringItem(label, level, expanded), fPath(path) {}
    const BString& Path() const { return fPath; }

private:
    BString fPath;
};

class MainWindow : public BWindow {
public:
    explicit MainWindow(std::shared_ptr<AppController> controller);

    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;

private:
    void BuildLayout();
    void ReloadChannelTree();
    void AddCategory(const CategoryNode& node, CategoryItem* parent, bool expanded);
    void PlayChannel(size_t channelIndex);
    void RefreshPlaylist();
    void UpdateStatus();
    void ToggleFullScreen();
    void SetButtonIcon(BButton* button, IconKind kind);
    ChannelItem* SelectedChannel() const;

    std::shared_ptr<AppController> fController;
    BView* fSidebar;
    BView* fControlBar;
    BOutlineListView* fChannelList;
    BTextControl* fSearchField;
    BRadioButton* fCategoryButton;
    BRadioButton* fCountryButton;
    BStringView* fStatusView;
    BStringView* fNowPlayingView;
    BStringView* fStateView;
    VideoView* fVideoView;
    AudioOutput fAudioOutput;
    BButton* fPlayPauseButton;
    BButton* fFavoriteButton;
    BSlider* fVolumeSlider;

    BRect fSavedFrame;
    bool fFullScreen;
    BString fRefreshNote;
};

}  // namespace haiku
}  // namespace tv
