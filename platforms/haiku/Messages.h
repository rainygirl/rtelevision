// Message constants shared by the Haiku window and its child views.
#pragma once

namespace tv {
namespace haiku {

enum {
    kMsgSearchChanged = 'srch',
    kMsgChannelInvoked = 'chan',
    kMsgPlayPause = 'plps',
    kMsgStop = 'stop',
    kMsgFavorite = 'favo',
    kMsgFullScreen = 'full',
    kMsgVolume = 'volu',
    kMsgRefresh = 'refr',
    kMsgPlayerState = 'pstt',
    kMsgGrouping = 'grup',
};

}  // namespace haiku
}  // namespace tv
