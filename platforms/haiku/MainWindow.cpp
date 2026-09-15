#include "MainWindow.h"

#include <Application.h>
#include <Autolock.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <Screen.h>
#include <SplitView.h>

#include <cmath>

#include "core/Country.h"
#include "core/Strings.h"

namespace tv {
namespace haiku {

MainWindow::MainWindow(std::shared_ptr<AppController> controller)
    : BWindow(BRect(40, 40, 1000, 740), "R Television", B_TITLED_WINDOW,
              B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
      fController(std::move(controller)),
      fSidebar(NULL),
      fControlBar(NULL),
      fChannelList(NULL),
      fSearchField(NULL),
      fCategoryButton(NULL),
      fCountryButton(NULL),
      fStatusView(NULL),
      fNowPlayingView(NULL),
      fStateView(NULL),
      fVideoView(NULL),
      fPlayPauseButton(NULL),
      fFavoriteButton(NULL),
      fVolumeSlider(NULL),
      fFullScreen(false) {
    BuildLayout();
    ReloadChannelTree();

    MediaPlayer* player = fController->player();
    if (player != NULL) {
        // Only the FFmpeg backend needs these; libVLC drives its own output.
        player->attachAudioSink(&fAudioOutput);
        if (!player->attachVideoSink(fVideoView))
            fVideoView->SetPlaceholder(str(Str::NoBackendHint));
        else
            fVideoView->SetPlaceholder(str(Str::SelectChannel));

        // Player callbacks arrive on a backend thread; hop to this looper.
        BMessenger messenger(this);
        player->setStateCallback([messenger](PlaybackState state, const std::string& text) {
            BMessage note(kMsgPlayerState);
            note.AddInt32("state", (int32)state);
            note.AddString("message", text.c_str());
            BMessenger(messenger).SendMessage(&note);
        });
    }
    RefreshPlaylist();
}

void MainWindow::BuildLayout() {
    fSearchField = new BTextControl("search", NULL, "", new BMessage(kMsgSearchChanged));
    fSearchField->SetModificationMessage(new BMessage(kMsgSearchChanged));
    fSearchField->SetToolTip(str(Str::SearchPlaceholder));

    fCategoryButton =
        new BRadioButton("bycategory", str(Str::TabCategory), new BMessage(kMsgGrouping));
    fCountryButton =
        new BRadioButton("bycountry", str(Str::TabCountry), new BMessage(kMsgGrouping));
    fCategoryButton->SetValue(B_CONTROL_ON);

    fChannelList = new BOutlineListView("channels", B_SINGLE_SELECTION_LIST);
    fChannelList->SetInvocationMessage(new BMessage(kMsgChannelInvoked));
    fChannelList->SetSelectionMessage(new BMessage(kMsgChannelInvoked));
    BScrollView* listScroll =
        new BScrollView("list scroll", fChannelList, 0, false, true, B_FANCY_BORDER);

    fStatusView = new BStringView("status", "");
    fNowPlayingView = new BStringView("now playing", str(Str::SelectChannel));
    fStateView = new BStringView("state", "");
    // A BStringView's minimum width is its whole text, which on a small screen
    // pushed the volume slider and the full screen button off the window. Let
    // them shrink and truncate instead.
    for (BStringView* label : {fStatusView, fNowPlayingView, fStateView}) {
        label->SetExplicitMinSize(BSize(40, B_SIZE_UNSET));
        label->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
        label->SetTruncation(B_TRUNCATE_END);
    }
    fVideoView = new VideoView();

    // Icon-only transport buttons; the labels live in the tooltips.
    fPlayPauseButton = new BButton("playpause", NULL, new BMessage(kMsgPlayPause));
    BButton* stopButton = new BButton("stop", NULL, new BMessage(kMsgStop));
    fFavoriteButton = new BButton("favorite", NULL, new BMessage(kMsgFavorite));
    BButton* fullScreenButton = new BButton("fullscreen", NULL, new BMessage(kMsgFullScreen));

    SetButtonIcon(fPlayPauseButton, IconKind::Play);
    SetButtonIcon(stopButton, IconKind::Stop);
    SetButtonIcon(fFavoriteButton, IconKind::Star);
    SetButtonIcon(fullScreenButton, IconKind::FullScreen);

    fPlayPauseButton->SetToolTip(str(Str::TipPlayPause));
    stopButton->SetToolTip(str(Str::TipStop));
    fFavoriteButton->SetToolTip(str(Str::TipFavorite));
    fullScreenButton->SetToolTip(str(Str::TipFullScreen));

    fVolumeSlider = new BSlider("volume", NULL, new BMessage(kMsgVolume), 0, 150, B_HORIZONTAL);
    fVolumeSlider->SetValue(80);
    fVolumeSlider->SetExplicitMinSize(BSize(90, B_SIZE_UNSET));
    fVolumeSlider->SetExplicitMaxSize(BSize(140, B_SIZE_UNSET));

    BGroupView* sidebar = new BGroupView(B_VERTICAL);
    BLayoutBuilder::Group<>(sidebar)
        .Add(fSearchField)
        .AddGroup(B_HORIZONTAL)
            .Add(fCategoryButton)
            .Add(fCountryButton)
            .AddGlue()
        .End()
        .Add(listScroll)
        .Add(fStatusView)
        .SetInsets(6, 6, 6, 6);
    sidebar->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
    fSidebar = sidebar;

    // Its own view rather than a nested group, so full screen can hide it.
    BGroupView* controls = new BGroupView(B_HORIZONTAL);
    BLayoutBuilder::Group<>(controls)
        .Add(fPlayPauseButton)
        .Add(stopButton)
        .Add(fFavoriteButton)
        .AddGroup(B_VERTICAL, 0)
            .Add(fNowPlayingView)
            .Add(fStateView)
        .End()
        .AddGlue()
        .Add(fVolumeSlider)
        .Add(fullScreenButton)
        .SetInsets(6, 6, 6, 6);
    fControlBar = controls;

    BGroupView* playerPane = new BGroupView(B_VERTICAL, 0);
    BLayoutBuilder::Group<>(playerPane)
        .Add(fVideoView)
        .Add(controls);

    // BSplitView takes the two panes; the sidebar keeps a modest share.
    BSplitView* split = new BSplitView(B_HORIZONTAL);
    split->AddChild(sidebar);
    split->AddChild(playerPane);
    split->SetItemWeight(0, 0.28f, false);
    split->SetItemWeight(1, 0.72f, true);
    split->SetCollapsible(false);

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(split);
}

// Rows are appended in depth-first order and nested purely by outline level.
// AddUnder() would not work here: it inserts directly after the superitem, so a
// category's own channels would land in front of its subcategories.
// The glyph colour follows the panel text colour, so it stays monochrome in
// whatever colour set the user runs.
void MainWindow::SetButtonIcon(BButton* button, IconKind kind) {
    const float size = ceilf(be_plain_font->Size() * 1.6f);
    BBitmap* icon = MakeIcon(kind, size, ui_color(B_CONTROL_TEXT_COLOR));
    if (icon == NULL) return;
    button->SetIcon(icon);
    delete icon;
}

void MainWindow::AddCategory(const CategoryNode& node, CategoryItem* parent, bool expanded) {
    uint32 level = parent != NULL ? parent->OutlineLevel() + 1 : 0;

    BString label;
    if (!node.countryCode.empty()) {
        std::string flag = countryFlagEmoji(node.countryCode);
        label.SetToFormat("%s %s  %ld", flag.c_str(), node.title.c_str(),
                          (long)node.totalChannels);
    } else {
        label.SetToFormat("%s  %ld", node.title.c_str(), (long)node.totalChannels);
    }
    CategoryItem* item = new CategoryItem(label.String(), node.path.c_str(), level, expanded);
    fChannelList->AddItem(item);

    for (size_t i = 0; i < node.children.size(); ++i)
        AddCategory(node.children[i], item, expanded);

    const ChannelIndex& index = fController->index();
    for (size_t i = 0; i < node.channels.size(); ++i) {
        const Channel& channel = index.channelAt(node.channels[i]);
        // In country mode the flag already sits on the parent row.
        std::string text;
        if (fController->index().grouping() != Grouping::Country) {
            text = countryFlagEmoji(channel.country);
            if (!text.empty()) text += " ";
        }
        text += channel.name;
        fChannelList->AddItem(new ChannelItem(text.c_str(), node.channels[i], level + 1));
    }
}

void MainWindow::ReloadChannelTree() {
    while (fChannelList->CountItems() > 0) delete fChannelList->RemoveItem((int32)0);

    // A filtered result set is small enough to show fully expanded. The state is
    // set as the items are created: calling Expand() afterwards on a collapsed
    // parent puts rows in the visible list at the wrong position.
    const bool expanded = !fController->index().filter().empty() &&
                          fController->index().visibleChannels() <= 400;

    const std::vector<CategoryNode>& roots = fController->index().roots();
    for (size_t i = 0; i < roots.size(); ++i) AddCategory(roots[i], NULL, expanded);

    UpdateStatus();
}

void MainWindow::UpdateStatus() {
    const char* origin = str(Str::NoPlaylist);
    switch (fController->playlistSource()) {
        case PlaylistSource::Network: origin = str(Str::Online); break;
        case PlaylistSource::Cache: origin = str(Str::OfflineCache); break;
        case PlaylistSource::Seed: origin = str(Str::BundledSnapshot); break;
        case PlaylistSource::None: break;
    }
    BString status;
    BString shown, total;
    shown.SetToFormat("%ld", (long)fController->index().visibleChannels());
    total.SetToFormat("%ld", (long)fController->index().totalChannels());
    status = format(Str::ChannelCount, shown.String(), total.String()).c_str();
    status << " \u00b7 " << origin;
    if (fRefreshNote.Length() > 0) {
        status << " · " << fRefreshNote;
    }
    fStatusView->SetText(status.String());
}

ChannelItem* MainWindow::SelectedChannel() const {
    int32 selected = fChannelList->CurrentSelection();
    if (selected < 0) return NULL;
    return dynamic_cast<ChannelItem*>(fChannelList->ItemAt(selected));
}

void MainWindow::PlayChannel(size_t channelIndex) {
    const Channel& channel = fController->index().channelAt(channelIndex);
    fNowPlayingView->SetText(channel.name.c_str());

    MediaPlayer* player = fController->player();
    if (player == NULL || !player->play(channel)) {
        fStateView->SetText(str(Str::CannotPlay));
        return;
    }
    fStateView->SetText(str(Str::Opening));
    SetButtonIcon(fPlayPauseButton, IconKind::Pause);
}

void MainWindow::RefreshPlaylist() {
    fRefreshNote = str(Str::Downloading);
    UpdateStatus();

    BMessenger messenger(this);
    fController->refreshAsync([messenger](const RefreshResult& result) {
        BMessage note(kMsgRefresh);
        note.AddBool("updated", result.updated);
        note.AddBool("notModified", result.notModified);
        note.AddString("error", result.error.c_str());
        BMessenger(messenger).SendMessage(&note);
    });
}

// Haiku has no BWindow::SetFullScreen; drop the border, fill the screen and
// show the video alone. The split layout skips hidden items, so the sidebar's
// divider disappears with it.
void MainWindow::ToggleFullScreen() {
    if (!fFullScreen) {
        fSavedFrame = Frame();
        fSidebar->Hide();
        fControlBar->Hide();
        BScreen screen(this);
        BRect frame = screen.Frame();
        SetLook(B_NO_BORDER_WINDOW_LOOK);
        MoveTo(frame.left, frame.top);
        ResizeTo(frame.Width(), frame.Height());
    } else {
        fSidebar->Show();
        fControlBar->Show();
        SetLook(B_TITLED_WINDOW_LOOK);
        MoveTo(fSavedFrame.left, fSavedFrame.top);
        ResizeTo(fSavedFrame.Width(), fSavedFrame.Height());
    }
    fFullScreen = !fFullScreen;
}

void MainWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kMsgSearchChanged:
            fController->index().setFilter(fSearchField->Text());
            ReloadChannelTree();
            break;

        case kMsgChannelInvoked: {
            ChannelItem* item = SelectedChannel();
            if (item == NULL) break;
            const Channel& channel = fController->index().channelAt(item->ChannelIndex());
            SetButtonIcon(fFavoriteButton, fController->favorites().contains(channel.url)
                                               ? IconKind::StarFilled
                                               : IconKind::Star);
            PlayChannel(item->ChannelIndex());
            break;
        }

        case kMsgPlayPause: {
            MediaPlayer* player = fController->player();
            if (player == NULL) break;
            if (player->state() == PlaybackState::Idle ||
                player->state() == PlaybackState::Stopped) {
                ChannelItem* item = SelectedChannel();
                if (item != NULL) PlayChannel(item->ChannelIndex());
                break;
            }
            bool pause = !player->isPaused();
            player->setPaused(pause);
            SetButtonIcon(fPlayPauseButton, pause ? IconKind::Play : IconKind::Pause);
            break;
        }

        case kMsgStop:
            if (fController->player() != NULL) fController->player()->stop();
            SetButtonIcon(fPlayPauseButton, IconKind::Play);
            break;

        case kMsgFavorite: {
            ChannelItem* item = SelectedChannel();
            if (item == NULL) break;
            const std::string& url = fController->index().channelAt(item->ChannelIndex()).url;
            fController->toggleFavorite(url);
            SetButtonIcon(fFavoriteButton, fController->favorites().contains(url)
                                               ? IconKind::StarFilled
                                               : IconKind::Star);
            ReloadChannelTree();
            break;
        }

        case kMsgGrouping:
            fController->index().setGrouping(fCountryButton->Value() == B_CONTROL_ON
                                                 ? Grouping::Country
                                                 : Grouping::Category);
            ReloadChannelTree();
            break;

        case kMsgFullScreen:
            ToggleFullScreen();
            break;

        case kMsgVolume: {
            if (fController->player() != NULL)
                fController->player()->setVolume(fVolumeSlider->Value());
            break;
        }

        case kMsgRefresh: {
            bool updated = false;
            bool notModified = false;
            BString error;
            message->FindBool("updated", &updated);
            message->FindBool("notModified", &notModified);
            message->FindString("error", &error);

            if (updated) {
                fRefreshNote = "";
                ReloadChannelTree();
            } else if (notModified) {
                fRefreshNote = "";
                UpdateStatus();
            } else {
                // Playback and browsing continue from the offline copy.
                fRefreshNote = str(Str::RefreshFailed);
                if (error.Length() > 0) fRefreshNote << " (" << error << ")";
                UpdateStatus();
            }
            break;
        }

        case kMsgPlayerState: {
            int32 state = 0;
            BString text;
            message->FindInt32("state", &state);
            message->FindString("message", &text);
            switch ((PlaybackState)state) {
                case PlaybackState::Opening: fStateView->SetText(str(Str::Opening)); break;
                case PlaybackState::Buffering: {
                    // The relay reports how full its start buffer is ("45%").
                    BString label(str(Str::Buffering));
                    if (text.Length() > 0) label << " " << text;
                    fStateView->SetText(label.String());
                    break;
                }
                case PlaybackState::Playing:
                    fStateView->SetText(str(Str::Playing));
                    SetButtonIcon(fPlayPauseButton, IconKind::Pause);
                    break;
                case PlaybackState::Paused: fStateView->SetText(str(Str::Paused)); break;
                case PlaybackState::Stopped:
                    fStateView->SetText(text.Length() > 0 ? text.String() : str(Str::Stopped));
                    SetButtonIcon(fPlayPauseButton, IconKind::Play);
                    break;
                case PlaybackState::Error: {
                    BString note(str(Str::PlaybackFailed));
                    if (text.Length() > 0) note << " - " << text;
                    fStateView->SetText(note.String());
                    SetButtonIcon(fPlayPauseButton, IconKind::Play);
                    break;
                }
                case PlaybackState::Idle: break;
            }
            break;
        }

        default:
            BWindow::MessageReceived(message);
    }
}

bool MainWindow::QuitRequested() {
    MediaPlayer* player = fController->player();
    if (player != NULL) {
        player->stop();
        player->attachVideoSink(NULL);
        player->attachAudioSink(NULL);
    }
    fAudioOutput.cleanupAudio();
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

}  // namespace haiku
}  // namespace tv
