// C boundary between the C++ core and the Cocoa front end.
//
// Tiger's Cocoa headers are only really understood by Apple's own GCC 4.0.1,
// and the core needs GCC 4.7 for C++11. The two cannot share a C++ ABI, so the
// front end is plain Objective-C and meets the core here, where everything is
// C. GCC 4.7 compiles this file's implementation; GCC 4.0.1 compiles the UI
// that includes this header.
//
// The channel tree is handed over already flattened into rows. An outline view
// would have to ask about children one level at a time, which is a lot of
// crossings for a machine this slow; a flat indented list draws the same thing.
#ifndef RTV_UI_BRIDGE_H
#define RTV_UI_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RtvApp RtvApp;

enum {
    kRtvStateIdle = 0,
    kRtvStateOpening,
    kRtvStateBuffering,
    kRtvStatePlaying,
    kRtvStatePaused,
    kRtvStateStopped,
    kRtvStateError
};

typedef struct RtvRow {
    const char* title;   /* owned by the bridge, valid until the rows change */
    int depth;           /* indentation level */
    int isChannel;       /* 0 for a category heading */
    size_t channelIndex; /* meaningful when isChannel */
    size_t count;        /* channels at or under a category */
} RtvRow;

/* Playback state, and the refresh result, arrive on worker threads. The front
   end must hop to the main thread before touching any view. */
typedef void (*RtvStateCallback)(int state, const char* message, void* context);
typedef void (*RtvRefreshCallback)(int updated, const char* error, void* context);

RtvApp* rtv_create(const char* seedPath, const char* pluginPath, char* errorOut, size_t errorSize);
void rtv_destroy(RtvApp*);

/* Cache or bundled snapshot; no network. */
int rtv_load_local(RtvApp*);
void rtv_refresh(RtvApp*, RtvRefreshCallback, void* context);

void rtv_set_filter(RtvApp*, const char* text);

/* Standard definition only.
   A G3 or G4 cannot decode 720p, so the list leaves out the channels whose name
   announces it. rtv_prefers_standard_definition() reports whether this machine
   should have it on to begin with; a G5 does not. */
int rtv_prefers_standard_definition(void);
void rtv_set_standard_definition_only(RtvApp*, int on);
int rtv_standard_definition_only(RtvApp*);
void rtv_set_grouping_by_country(RtvApp*, int byCountry);
size_t rtv_row_count(RtvApp*);
int rtv_row(RtvApp*, size_t index, RtvRow* out);
/* "11,035 of 11,035 channels - offline cache" for the status line. */
void rtv_status(RtvApp*, char* buffer, size_t size);

void rtv_attach_video(RtvApp*, void* nsView);

/* Software video.
   This hardware has no Quartz Extreme, and libVLC's OpenGL output refuses to
   start without it, so the picture is decoded into a buffer the front end draws
   itself - the same path the Haiku front end uses. The backend fixes the buffer
   size, so the view letterboxes what it gets; the frame callback arrives on a
   decoder thread. */
typedef void (*RtvFrameCallback)(void* context);
void rtv_set_frame_callback(RtvApp*, RtvFrameCallback, void* context);
/* Three bytes per pixel, red first. NULL until a frame has arrived; every
   successful lock must be matched by an unlock. */
const void* rtv_video_lock(RtvApp*, int* width, int* height, int* pitch);
void rtv_video_unlock(RtvApp*);
int rtv_play_row(RtvApp*, size_t index);
void rtv_stop(RtvApp*);
void rtv_toggle_pause(RtvApp*);
int rtv_is_paused(RtvApp*);
void rtv_set_volume(RtvApp*, int percent);
void rtv_set_state_callback(RtvApp*, RtvStateCallback, void* context);

/* Favorites, by row. */
void rtv_toggle_favorite(RtvApp*, size_t index);
int rtv_is_favorite(RtvApp*, size_t index);

/* Interface text in the system language, by the same ids the core uses. */
const char* rtv_text_select_channel(void);
const char* rtv_text_search(void);
const char* rtv_text_category(void);
const char* rtv_text_country(void);
const char* rtv_text_sd_only(void);

#ifdef __cplusplus
}
#endif

#endif  /* RTV_UI_BRIDGE_H */
