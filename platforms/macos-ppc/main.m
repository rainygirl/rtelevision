// R Television - Mac OS X 10.4 front end.
//
// Plain Objective-C, compiled by Apple's GCC 4.0.1, which is the compiler that
// knows this system's Cocoa headers. It reaches the C++ core only through
// UiBridge.h, so the two compilers never share a C++ ABI. See docs/PORTING.md.
//
// Everything here predates Objective-C 2.0: no properties, no dot syntax, no
// fast enumeration, no blocks, no NSInteger, and retain/release by hand.
#import <Cocoa/Cocoa.h>

#include <pthread.h>

#include "UiBridge.h"

// The channel list arrives already flattened, so a table view draws it; the
// depth becomes leading spaces. An outline view would have to ask about
// children one level at a time, across the bridge, on a very slow machine.
static NSString* IndentedTitle(const RtvRow* row)
{
    NSMutableString* text = [NSMutableString string];
    int i;
    for (i = 0; i < row->depth; ++i) [text appendString:@"    "];
    [text appendString:[NSString stringWithUTF8String:row->title]];
    if (!row->isChannel) [text appendFormat:@"  (%lu)", (unsigned long)row->count];
    return text;
}

// ------------------------------------------------------------------ video

// The picture, drawn by hand.
//
// This hardware has no Quartz Extreme, so libVLC's OpenGL output will not start
// (it checks CGDisplayUsesOpenGLAcceleration and gives up). The core hands over
// a plain RGB buffer instead and this draws it, letterboxed.
@interface TVVideoView : NSView
{
    RtvApp* app;
    NSString* placeholder;
    // Kept between frames: building an NSBitmapImageRep per frame is pure
    // overhead on a machine this slow, and the buffer does not move.
    NSBitmapImageRep* cachedFrame;
    const void* cachedPixels;
    int cachedWidth;
    int cachedHeight;
    int cachedPitch;
}
- (void)setApp:(RtvApp*)value;
- (void)setPlaceholder:(NSString*)text;
- (void)frameArrived;
@end

@implementation TVVideoView

- (BOOL)isOpaque
{
    return YES;
}

- (void)setApp:(RtvApp*)value
{
    app = value;
}

- (void)setPlaceholder:(NSString*)text
{
    [placeholder release];
    placeholder = [text retain];
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [placeholder release];
    [cachedFrame release];
    [super dealloc];
}

- (void)frameArrived
{
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)rect
{
    [[NSColor blackColor] set];
    NSRectFill(rect);

    int width = 0;
    int height = 0;
    int pitch = 0;
    const void* pixels = app ? rtv_video_lock(app, &width, &height, &pitch) : NULL;
    if (!pixels) {
        if ([placeholder length] > 0) {
            NSDictionary* style = [NSDictionary
                dictionaryWithObjectsAndKeys:[NSColor grayColor], NSForegroundColorAttributeName,
                                             [NSFont systemFontOfSize:12], NSFontAttributeName, nil];
            NSSize size = [placeholder sizeWithAttributes:style];
            NSRect bounds = [self bounds];
            [placeholder drawAtPoint:NSMakePoint(NSMidX(bounds) - size.width / 2,
                                                 NSMidY(bounds) - size.height / 2)
                      withAttributes:style];
        }
        return;
    }

    if (!cachedFrame || cachedPixels != pixels || cachedWidth != width ||
        cachedHeight != height || cachedPitch != pitch) {
        unsigned char* planes[5];
        planes[0] = (unsigned char*)pixels;
        planes[1] = planes[2] = planes[3] = planes[4] = NULL;
        [cachedFrame release];
        cachedFrame = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:planes
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:3
                            hasAlpha:NO
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:pitch
                        bitsPerPixel:24];
        cachedPixels = pixels;
        cachedWidth = width;
        cachedHeight = height;
        cachedPitch = pitch;
    }

    NSRect bounds = [self bounds];
    float scale = NSWidth(bounds) / width;
    if (NSHeight(bounds) / height < scale) scale = NSHeight(bounds) / height;
    NSRect target = NSMakeRect(NSMinX(bounds) + (NSWidth(bounds) - width * scale) / 2,
                               NSMinY(bounds) + (NSHeight(bounds) - height * scale) / 2,
                               width * scale, height * scale);
    // Nearest neighbour: the smooth scaler costs more than the picture is worth
    // here, and the buffer is already close to the size it is drawn at.
    [[NSGraphicsContext currentContext] setImageInterpolation:NSImageInterpolationNone];
    [cachedFrame drawInRect:target];
    rtv_video_unlock(app);
}

@end

// ------------------------------------------------------------- controller

@interface TVController : NSObject
{
    RtvApp* app;
    NSWindow* window;
    NSTableView* table;
    NSSearchField* searchField;
    NSButton* sdOnlyButton;
    NSTextField* statusLabel;
    NSTextField* nowPlayingLabel;
    NSTextField* stateLabel;
    TVVideoView* videoView;
    NSButton* playPauseButton;
    NSMatrix* groupingMatrix;
}
- (void)run;
- (void)reloadRows;
- (void)applyState:(NSArray*)pair;
@end

static TVController* gController = nil;

// All three callbacks arrive on a worker thread.
static void FrameReady(void* context)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [(TVVideoView*)context performSelectorOnMainThread:@selector(frameArrived)
                                            withObject:nil
                                         waitUntilDone:NO];
    [pool release];
}


static void StateChanged(int state, const char* message, void* context)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSArray* pair = [NSArray arrayWithObjects:
        [NSNumber numberWithInt:state],
        [NSString stringWithUTF8String:message ? message : ""], nil];
    [(TVController*)context performSelectorOnMainThread:@selector(applyState:)
                                             withObject:pair
                                          waitUntilDone:NO];
    [pool release];
}

static void RefreshFinished(int updated, const char* error, void* context)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    if (updated)
        [(TVController*)context performSelectorOnMainThread:@selector(reloadRows)
                                                 withObject:nil
                                              waitUntilDone:NO];
    [pool release];
}

@implementation TVController

- (id)init
{
    self = [super init];
    if (!self) return nil;

    NSString* resources = [[NSBundle mainBundle] resourcePath];
    NSString* seed = [resources stringByAppendingPathComponent:@"seed-playlist.m3u"];

    // 10.4 has no certificate store libcurl can use, and this libcurl was built
    // without one compiled in, so it would trust nothing. The core reads this
    // variable and hands it to curl.
    setenv("CURL_CA_BUNDLE",
           [[resources stringByAppendingPathComponent:@"cacert.pem"] fileSystemRepresentation], 1);

    // libVLC 0.9 has no HLS demuxer, and almost every channel is HLS. The relay
    // reads the playlist and fetches the segments itself either way; this makes
    // it hand the backend one continuous MPEG-TS stream instead of a playlist
    // the backend could not read, and decrypt AES-128 segments on its behalf.
    if (getenv("RTV_RELAY_TS") == NULL) setenv("RTV_RELAY_TS", "1", 1);
    NSString* modules =
        [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents/MacOS/modules"];

    char error[512];
    error[0] = '\0';
    app = rtv_create([seed fileSystemRepresentation], [modules fileSystemRepresentation],
                     error, sizeof error);
    if (error[0] != '\0')
        NSLog(@"[rtv] media backend: %s", error);
    return self;
}

- (void)dealloc
{
    rtv_destroy(app);
    [super dealloc];
}

- (void)buildWindow
{
    NSRect frame = NSMakeRect(60, 60, 900, 620);
    window = [[NSWindow alloc] initWithContentRect:frame
                                         styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                                                    NSMiniaturizableWindowMask | NSResizableWindowMask)
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [window setTitle:@"R Television"];
    [window setMinSize:NSMakeSize(640, 420)];
    [window setDelegate:self];

    NSView* content = [window contentView];
    NSRect bounds = [content bounds];
    const float sidebar = 280.0f;
    const float barHeight = 40.0f;

    // ---- left: search, grouping, channel list, status
    // The search field shares its row with the SD switch: on a G3 or G4 that
    // switch decides whether most of the list is worth showing at all, so it
    // belongs where the filtering happens rather than in a menu.
    const float sdWidth = 92.0f;
    searchField = [[NSSearchField alloc]
        initWithFrame:NSMakeRect(8, NSMaxY(bounds) - 30, sidebar - 24 - sdWidth, 22)];
    [searchField setAutoresizingMask:NSViewMinYMargin];
    [searchField setTarget:self];
    [searchField setAction:@selector(searchChanged:)];
    [[searchField cell] setSendsWholeSearchString:NO];
    [content addSubview:searchField];

    sdOnlyButton = [[NSButton alloc]
        initWithFrame:NSMakeRect(sidebar - 8 - sdWidth, NSMaxY(bounds) - 29, sdWidth, 20)];
    [sdOnlyButton setButtonType:NSSwitchButton];
    [sdOnlyButton setTitle:[NSString stringWithUTF8String:rtv_text_sd_only()]];
    [sdOnlyButton setState:rtv_standard_definition_only(app) ? NSOnState : NSOffState];
    [sdOnlyButton setTarget:self];
    [sdOnlyButton setAction:@selector(standardDefinitionChanged:)];
    [sdOnlyButton setAutoresizingMask:NSViewMinYMargin];
    [content addSubview:sdOnlyButton];

    NSButtonCell* prototype = [[[NSButtonCell alloc] init] autorelease];
    [prototype setButtonType:NSRadioButton];
    groupingMatrix = [[NSMatrix alloc] initWithFrame:NSMakeRect(8, NSMaxY(bounds) - 56, sidebar - 16, 20)
                                                mode:NSRadioModeMatrix
                                           prototype:prototype
                                        numberOfRows:1
                                     numberOfColumns:2];
    [groupingMatrix setAutoresizingMask:NSViewMinYMargin];
    [[groupingMatrix cellAtRow:0 column:0] setTitle:[NSString stringWithUTF8String:rtv_text_category()]];
    [[groupingMatrix cellAtRow:0 column:1] setTitle:[NSString stringWithUTF8String:rtv_text_country()]];
    [groupingMatrix selectCellAtRow:0 column:0];
    [groupingMatrix setTarget:self];
    [groupingMatrix setAction:@selector(groupingChanged:)];
    [content addSubview:groupingMatrix];

    NSRect listFrame = NSMakeRect(8, 28, sidebar - 16, NSHeight(bounds) - 92);
    NSScrollView* scroll = [[[NSScrollView alloc] initWithFrame:listFrame] autorelease];
    [scroll setHasVerticalScroller:YES];
    [scroll setAutoresizingMask:(NSViewHeightSizable)];
    [scroll setBorderType:NSBezelBorder];

    table = [[NSTableView alloc] initWithFrame:[[scroll contentView] bounds]];
    NSTableColumn* column = [[[NSTableColumn alloc] initWithIdentifier:@"channel"] autorelease];
    [column setWidth:sidebar - 40];
    [[column headerCell] setStringValue:@""];
    // A table column is editable by default, so a double click started renaming
    // the row instead of reaching the double action.
    [column setEditable:NO];
    [[column dataCell] setEditable:NO];
    [[column dataCell] setSelectable:NO];
    [table addTableColumn:column];
    [table setHeaderView:nil];
    [table setDataSource:self];
    [table setDelegate:self];
    [table setTarget:self];
    [table setDoubleAction:@selector(playSelection:)];
    [table setAllowsMultipleSelection:NO];
    [scroll setDocumentView:table];
    [content addSubview:scroll];

    statusLabel = [self makeLabel:NSMakeRect(8, 6, sidebar - 16, 16) small:YES];
    [statusLabel setAutoresizingMask:NSViewMaxYMargin];
    [content addSubview:statusLabel];

    // ---- right: video and the transport bar
    NSRect videoFrame = NSMakeRect(sidebar, barHeight, NSWidth(bounds) - sidebar,
                                   NSHeight(bounds) - barHeight);
    videoView = [[TVVideoView alloc] initWithFrame:videoFrame];
    [videoView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [content addSubview:videoView];

    playPauseButton = [[NSButton alloc] initWithFrame:NSMakeRect(sidebar + 8, 8, 70, 24)];
    [playPauseButton setBezelStyle:NSRoundedBezelStyle];
    [playPauseButton setTitle:@"Play"];
    [playPauseButton setTarget:self];
    [playPauseButton setAction:@selector(togglePause:)];
    [playPauseButton setAutoresizingMask:NSViewMaxXMargin];
    [content addSubview:playPauseButton];

    NSButton* stopButton = [[[NSButton alloc]
        initWithFrame:NSMakeRect(sidebar + 82, 8, 60, 24)] autorelease];
    [stopButton setBezelStyle:NSRoundedBezelStyle];
    [stopButton setTitle:@"Stop"];
    [stopButton setTarget:self];
    [stopButton setAction:@selector(stopPlayback:)];
    [stopButton setAutoresizingMask:NSViewMaxXMargin];
    [content addSubview:stopButton];

    nowPlayingLabel = [self makeLabel:NSMakeRect(sidebar + 150, 20, 300, 16) small:NO];
    [nowPlayingLabel setAutoresizingMask:NSViewWidthSizable];
    [nowPlayingLabel setStringValue:[NSString stringWithUTF8String:rtv_text_select_channel()]];
    [content addSubview:nowPlayingLabel];

    stateLabel = [self makeLabel:NSMakeRect(sidebar + 150, 4, 300, 14) small:YES];
    [stateLabel setAutoresizingMask:NSViewWidthSizable];
    [content addSubview:stateLabel];

    NSSlider* volume = [[[NSSlider alloc]
        initWithFrame:NSMakeRect(NSMaxX(bounds) - 110, 8, 100, 20)] autorelease];
    [volume setMinValue:0];
    [volume setMaxValue:150];
    [volume setFloatValue:80];
    [volume setTarget:self];
    [volume setAction:@selector(volumeChanged:)];
    [volume setAutoresizingMask:NSViewMinXMargin];
    [content addSubview:volume];
}

- (NSTextField*)makeLabel:(NSRect)frame small:(BOOL)small
{
    NSTextField* label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
    [label setEditable:NO];
    [label setBordered:NO];
    [label setDrawsBackground:NO];
    [label setSelectable:NO];
    [label setFont:[NSFont systemFontOfSize:small ? 10.0f : 12.0f]];
    if (small) [label setTextColor:[NSColor darkGrayColor]];
    return label;
}

- (void)run
{
    [self buildWindow];
    rtv_load_local(app);
    [self reloadRows];
    [window makeKeyAndOrderFront:nil];

    // set_drawable wants a view that already belongs to a window.
    [videoView setApp:app];
    [videoView setPlaceholder:[NSString stringWithUTF8String:rtv_text_select_channel()]];
    rtv_attach_video(app, videoView);
    rtv_set_frame_callback(app, FrameReady, videoView);
    rtv_set_state_callback(app, StateChanged, self);
    rtv_refresh(app, RefreshFinished, self);

    // Same debug aid as the other front ends: start a channel matching
    // RTV_AUTOPLAY as soon as the window is up. On a machine with no way to
    // drive the interface remotely this is how playback gets tested at all.
    const char* autoplay = getenv("RTV_AUTOPLAY");
    if (autoplay && *autoplay) {
        [searchField setStringValue:[NSString stringWithUTF8String:autoplay]];
        rtv_set_filter(app, autoplay);
        [self reloadRows];
        size_t count = rtv_row_count(app);
        size_t i;
        for (i = 0; i < count; ++i) {
            RtvRow info;
            if (!rtv_row(app, i, &info) || !info.isChannel) continue;
            [table selectRow:(int)i byExtendingSelection:NO];
            [self playSelection:nil];
            break;
        }
    }
}

- (void)reloadRows
{
    [table reloadData];
    char status[256];
    status[0] = '\0';
    rtv_status(app, status, sizeof status);
    [statusLabel setStringValue:[NSString stringWithUTF8String:status]];
}

// ---- actions

- (void)searchChanged:(id)sender
{
    rtv_set_filter(app, [[searchField stringValue] UTF8String]);
    [self reloadRows];
}

- (void)standardDefinitionChanged:(id)sender
{
    rtv_set_standard_definition_only(app, [sdOnlyButton state] == NSOnState ? 1 : 0);
    [self reloadRows];
}

- (void)groupingChanged:(id)sender
{
    rtv_set_grouping_by_country(app, [groupingMatrix selectedColumn] == 1 ? 1 : 0);
    [self reloadRows];
}

- (void)playSelection:(id)sender
{
    int row = [table selectedRow];
    if (row < 0) return;
    RtvRow info;
    if (!rtv_row(app, (size_t)row, &info) || !info.isChannel) return;
    [nowPlayingLabel setStringValue:[NSString stringWithUTF8String:info.title]];
    rtv_play_row(app, (size_t)row);
}

- (void)togglePause:(id)sender
{
    rtv_toggle_pause(app);
    [playPauseButton setTitle:rtv_is_paused(app) ? @"Play" : @"Pause"];
}

- (void)stopPlayback:(id)sender
{
    rtv_stop(app);
    [playPauseButton setTitle:@"Play"];
}

- (void)volumeChanged:(id)sender
{
    rtv_set_volume(app, [sender intValue]);
}

// ---- table

- (int)numberOfRowsInTableView:(NSTableView*)view
{
    return (int)rtv_row_count(app);
}

- (id)tableView:(NSTableView*)view
    objectValueForTableColumn:(NSTableColumn*)column
                          row:(int)row
{
    RtvRow info;
    if (!rtv_row(app, (size_t)row, &info)) return @"";
    return IndentedTitle(&info);
}

- (void)tableViewSelectionDidChange:(NSNotification*)note
{
    [self playSelection:nil];
}

// ---- player state, on the main thread

- (void)applyState:(NSArray*)pair
{
    int state = [[pair objectAtIndex:0] intValue];
    NSString* message = [pair objectAtIndex:1];
    NSString* text = @"";
    switch (state) {
        case kRtvStateOpening:   text = @"Opening";   break;
        case kRtvStateBuffering: text = @"Buffering"; break;
        case kRtvStatePlaying:   text = @"Playing";   break;
        case kRtvStatePaused:    text = @"Paused";    break;
        case kRtvStateStopped:   text = @"Stopped";   break;
        case kRtvStateError:     text = @"Cannot play this channel"; break;
        default: break;
    }
    if ([message length] > 0 && state != kRtvStateOpening)
        text = [NSString stringWithFormat:@"%@ - %@", text, message];
    [stateLabel setStringValue:text];
    [playPauseButton setTitle:(state == kRtvStatePlaying) ? @"Pause" : @"Play"];
}

- (BOOL)windowShouldClose:(id)sender
{
    rtv_stop(app);
    [NSApp terminate:nil];
    return YES;
}

@end

// ------------------------------------------------------------------ main

int main(int argc, const char* argv[])
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSApplication* application = [NSApplication sharedApplication];
    gController = [[TVController alloc] init];
    [application setDelegate:gController];
    [gController run];
    [application run];
    [pool release];
    return 0;
}
