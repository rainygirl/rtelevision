// User-visible text in English, Italian, Japanese and Korean.
//
// Lives in the core so every front end says the same thing. The language is
// picked once at start-up from the system settings; anything we do not have a
// translation for falls back to English.
#pragma once

#include <string>

namespace tv {

enum class Language { English, Italian, Japanese, Korean };

enum class Str {
    AppName,
    SearchPlaceholder,
    ColumnChannel,
    TabCategory,
    TabCountry,
    Favorites,
    SelectChannel,

    // Playback state
    Opening,
    Buffering,
    Playing,
    Paused,
    Stopped,
    PlaybackFailed,
    UnknownError,
    CannotPlay,
    NoBackend,
    NoBackendHint,
    BackendInitFailed,

    // Playlist status
    Online,
    OfflineCache,
    BundledSnapshot,
    NoPlaylist,
    Downloading,
    RefreshFailed,
    ChannelCount,       // two %s: channels shown, channels in total

    // Buttons
    TipPlayPause,
    TipStop,
    TipFavorite,
    TipAddFavorite,
    TipRemoveFavorite,
    TipFullScreen,
    TipMute,
    TipUnmute,

    // Menus
    MenuAbout,
    MenuOpenCacheFolder,
    MenuQuit,
    MenuPlayback,
    MenuPlayPause,
    MenuStop,
    MenuMute,
    MenuNextChannel,
    MenuPreviousChannel,
    MenuToggleFavorite,
    MenuChannels,
    MenuSearch,
    MenuRefresh,
    MenuWindow,
    MenuFullScreen,
    MenuMinimize,

    Count
};

// Reads LC_ALL, then LC_MESSAGES, then LANG. Front ends that can ask the system
// directly (NSLocale, BLocaleRoster) should do that and call languageFromTag().
Language detectLanguage();

// Accepts tags like "ko", "ko_KR.UTF-8", "ja-JP"; anything else gives English.
Language languageFromTag(const std::string& tag);

// True when the tag names a language we actually have text for, English
// included. Lets a caller walk a preference list instead of taking the first
// entry, since an unsupported tag is indistinguishable from English otherwise.
bool hasTranslation(const std::string& tag);

void setLanguage(Language language);
Language language();

const char* str(Str id);

// Substitutes the %s placeholders of a string from the table, in order.
std::string format(Str id, const std::string& first, const std::string& second = std::string());

}  // namespace tv
