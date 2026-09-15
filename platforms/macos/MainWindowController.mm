#import "MainWindowController.h"

#import "Localization.h"

#include <ctime>

#include "core/Country.h"
#include "core/StringUtil.h"

#pragma mark - Outline nodes

@interface TVChannelNode : NSObject
@property(nonatomic) NSUInteger channelIndex;
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* subtitle;
@property(nonatomic) BOOL favorite;
@end

@implementation TVChannelNode
@end

// One level of the group-title hierarchy. A node can hold both subcategories
// ("Animation;Kids") and channels that stop at this level ("Animation").
@interface TVCategoryNode : NSObject
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* path;
@property(nonatomic) NSUInteger total;
@property(nonatomic) BOOL favorites;
@property(nonatomic, strong) NSMutableArray<TVCategoryNode*>* subcategories;
@property(nonatomic, strong) NSMutableArray<TVChannelNode*>* channels;
- (NSUInteger)childCount;
- (id)childAtIndex:(NSUInteger)index;
@end

@implementation TVCategoryNode

- (instancetype)init {
    self = [super init];
    if (self) {
        _subcategories = [NSMutableArray array];
        _channels = [NSMutableArray array];
    }
    return self;
}

// Subcategories are listed before the channels that live directly on this node.
- (NSUInteger)childCount { return _subcategories.count + _channels.count; }

- (id)childAtIndex:(NSUInteger)index {
    if (index < _subcategories.count) return _subcategories[index];
    return _channels[index - _subcategories.count];
}

@end

#pragma mark - Channel list

// Return/Enter starts the highlighted channel; plain arrow keys only move the
// selection so browsing a 10k-entry list does not spawn a stream per row.
@interface TVOutlineView : NSOutlineView
@property(nonatomic, weak) id playTarget;
@property(nonatomic) SEL playAction;
@end

@implementation TVOutlineView

- (void)keyDown:(NSEvent*)event {
    unichar key = event.charactersIgnoringModifiers.length
                      ? [event.charactersIgnoringModifiers characterAtIndex:0]
                      : 0;
    if ((key == NSCarriageReturnCharacter || key == NSEnterCharacter) && self.playTarget) {
        [NSApp sendAction:self.playAction to:self.playTarget from:self];
        return;
    }
    [super keyDown:event];
}

@end

#pragma mark - Split view

// NSSplitView's own resizing left the player pane at its initial width, so the
// window grew but the pane did not. Laying the two panes out here guarantees
// they always add up to the full width; the sidebar keeps whatever width a drag
// gave it.
@interface TVSplitView : NSSplitView
@end

@implementation TVSplitView

- (void)layout {
    [super layout];
    if (self.subviews.count < 2) return;

    NSView* sidebar = self.subviews[0];
    NSView* pane = self.subviews[1];
    const CGFloat total = NSWidth(self.bounds);
    const CGFloat height = NSHeight(self.bounds);
    const CGFloat divider = self.dividerThickness;
    if (total <= 0 || height <= 0) return;

    CGFloat width = NSWidth(sidebar.frame);
    if (width <= 0) width = 300;
    const CGFloat maxWidth = MAX(220, total - divider - 400);
    width = MAX(220, MIN(width, maxWidth));

    sidebar.frame = NSMakeRect(0, 0, width, height);
    pane.frame = NSMakeRect(width + divider, 0, total - width - divider, height);
}

@end

#pragma mark - Video surface

// Plain layer-backed view handed to libVLC via set_nsobject. VLC installs its
// own vout subview inside it, so this class only owns the background and the
// double-click-to-fullscreen gesture.
@interface TVVideoView : NSView
@end

@implementation TVVideoView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = NSColor.blackColor.CGColor;
    }
    return self;
}

- (BOOL)isOpaque { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (void)mouseDown:(NSEvent*)event {
    if (event.clickCount == 2) [self.window toggleFullScreen:nil];
    else [super mouseDown:event];
}

@end

#pragma mark - Window controller

@interface MainWindowController () <NSOutlineViewDataSource, NSOutlineViewDelegate,
                                    NSSplitViewDelegate, NSSearchFieldDelegate>
@end

@implementation MainWindowController {
    std::shared_ptr<tv::AppController> _controller;

    TVSplitView* _splitView;
    NSSearchField* _searchField;
    NSSegmentedControl* _groupingControl;
    TVOutlineView* _outlineView;
    NSTextField* _sidebarStatus;
    TVVideoView* _videoView;
    NSTextField* _nowPlayingLabel;
    NSTextField* _stateLabel;
    NSButton* _playPauseButton;
    NSButton* _favoriteButton;
    NSButton* _muteButton;
    NSSlider* _volumeSlider;
    NSProgressIndicator* _busyIndicator;

    NSMutableArray<TVCategoryNode*>* _roots;
    NSMutableSet<NSString*>* _expandedPaths;
    NSString* _currentUrl;
    NSString* _refreshNote;
    BOOL _streamStarted;
}

- (instancetype)initWithController:(std::shared_ptr<tv::AppController>)controller {
    // Open at 80% of the screen, clamped, so the video pane starts usable.
    NSRect visible = NSScreen.mainScreen.visibleFrame;
    CGFloat width = MIN(MAX(NSWidth(visible) * 0.8, 1000), 1800);
    CGFloat height = MIN(MAX(NSHeight(visible) * 0.8, 600), 1100);
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"R Television";
    window.minSize = NSMakeSize(900, 520);
    window.frameAutosaveName = @"MainWindow";

    self = [super initWithWindow:window];
    if (!self) return nil;

    _controller = std::move(controller);
    _roots = [NSMutableArray array];
    _expandedPaths = [NSMutableSet set];

    [self buildInterface];
    [self reloadChannelTree];
    [self installPlayerCallback];

    [window center];
    return self;
}

#pragma mark - Interface construction

- (void)buildInterface {
    NSView* content = self.window.contentView;
    content.autoresizesSubviews = YES;

    _splitView = [[TVSplitView alloc] initWithFrame:content.bounds];
    _splitView.vertical = YES;
    _splitView.dividerStyle = NSSplitViewDividerStyleThin;
    _splitView.delegate = self;
    _splitView.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:_splitView];
    // Pinned rather than autoresized: the panes must follow every window
    // resize exactly, with no gap left on either side.
    [NSLayoutConstraint activateConstraints:@[
        [_splitView.topAnchor constraintEqualToAnchor:content.topAnchor],
        [_splitView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [_splitView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [_splitView.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    ]];

    [_splitView addSubview:[self buildSidebar]];
    [_splitView addSubview:[self buildPlayerPane]];
    [_splitView setPosition:300 ofDividerAtIndex:0];
}

- (NSView*)buildSidebar {
    NSView* sidebar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 760)];

    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = TVStr(tv::Str::SearchPlaceholder);
    _searchField.delegate = self;
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:_searchField];

    NSArray* groupingLabels = @[ TVStr(tv::Str::TabCategory), TVStr(tv::Str::TabCountry) ];
    _groupingControl = [NSSegmentedControl segmentedControlWithLabels:groupingLabels
                                                        trackingMode:NSSegmentSwitchTrackingSelectOne
                                                              target:self
                                                              action:@selector(groupingChanged:)];
    _groupingControl.selectedSegment = 0;
    _groupingControl.segmentDistribution = NSSegmentDistributionFillEqually;
    _groupingControl.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:_groupingControl];

    _outlineView = [[TVOutlineView alloc] initWithFrame:NSZeroRect];
    _outlineView.playTarget = self;
    _outlineView.playAction = @selector(playSelectedChannel:);
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"channel"];
    column.resizingMask = NSTableColumnAutoresizingMask;
    [_outlineView addTableColumn:column];
    _outlineView.outlineTableColumn = column;
    _outlineView.headerView = nil;
    _outlineView.rowSizeStyle = NSTableViewRowSizeStyleDefault;
    _outlineView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleSourceList;
    _outlineView.floatsGroupRows = NO;
    _outlineView.dataSource = self;
    _outlineView.delegate = self;
    _outlineView.target = self;
    _outlineView.action = @selector(outlineRowClicked:);
    _outlineView.doubleAction = @selector(outlineRowClicked:);

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = _outlineView;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.drawsBackground = NO;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:scroll];

    _sidebarStatus = [self makeLabelWithSize:11 secondary:YES];
    _sidebarStatus.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:_sidebarStatus];

    _busyIndicator = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _busyIndicator.style = NSProgressIndicatorStyleSpinning;
    _busyIndicator.controlSize = NSControlSizeSmall;
    _busyIndicator.displayedWhenStopped = NO;
    _busyIndicator.translatesAutoresizingMaskIntoConstraints = NO;
    [sidebar addSubview:_busyIndicator];

    [NSLayoutConstraint activateConstraints:@[
        [_searchField.topAnchor constraintEqualToAnchor:sidebar.topAnchor constant:10],
        [_searchField.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:10],
        [_searchField.trailingAnchor constraintEqualToAnchor:sidebar.trailingAnchor constant:-10],

        [_groupingControl.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:8],
        [_groupingControl.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:10],
        [_groupingControl.trailingAnchor constraintEqualToAnchor:sidebar.trailingAnchor constant:-10],

        [scroll.topAnchor constraintEqualToAnchor:_groupingControl.bottomAnchor constant:8],
        [scroll.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:sidebar.trailingAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:_sidebarStatus.topAnchor constant:-6],

        [_sidebarStatus.leadingAnchor constraintEqualToAnchor:sidebar.leadingAnchor constant:10],
        [_sidebarStatus.trailingAnchor constraintEqualToAnchor:_busyIndicator.leadingAnchor
                                                      constant:-6],
        [_sidebarStatus.bottomAnchor constraintEqualToAnchor:sidebar.bottomAnchor constant:-8],

        [_busyIndicator.trailingAnchor constraintEqualToAnchor:sidebar.trailingAnchor constant:-10],
        [_busyIndicator.centerYAnchor constraintEqualToAnchor:_sidebarStatus.centerYAnchor],
        [_busyIndicator.widthAnchor constraintEqualToConstant:14],
        [_busyIndicator.heightAnchor constraintEqualToConstant:14],
    ]];
    return sidebar;
}

- (NSView*)buildPlayerPane {
    NSView* pane = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 980, 760)];

    _videoView = [[TVVideoView alloc] initWithFrame:NSZeroRect];
    _videoView.translatesAutoresizingMaskIntoConstraints = NO;
    [pane addSubview:_videoView];

    NSView* bar = [[NSView alloc] initWithFrame:NSZeroRect];
    bar.translatesAutoresizingMaskIntoConstraints = NO;
    [pane addSubview:bar];

    _playPauseButton = [self iconButton:@"play.fill"
                                 action:@selector(togglePlayPause:)
                                tooltip:TVStr(tv::Str::TipPlayPause)];
    NSButton* stopButton = [self iconButton:@"stop.fill"
                                     action:@selector(stopPlayback:)
                                    tooltip:TVStr(tv::Str::TipStop)];
    _favoriteButton = [self iconButton:@"star"
                                action:@selector(toggleFavoriteForSelection:)
                               tooltip:TVStr(tv::Str::TipFavorite)];
    NSButton* fullScreenButton = [self iconButton:@"arrow.up.left.and.arrow.down.right"
                                           action:@selector(toggleFullScreen:)
                                          tooltip:TVStr(tv::Str::TipFullScreen)];

    _nowPlayingLabel = [self makeLabelWithSize:13 secondary:NO];
    _nowPlayingLabel.stringValue = TVStr(tv::Str::SelectChannel);
    _stateLabel = [self makeLabelWithSize:11 secondary:YES];

    _volumeSlider = [NSSlider sliderWithValue:80
                                     minValue:0
                                     maxValue:150
                                       target:self
                                       action:@selector(volumeChanged:)];
    _volumeSlider.continuous = YES;

    NSStackView* buttons = [NSStackView stackViewWithViews:@[
        _playPauseButton, stopButton, _favoriteButton
    ]];
    buttons.spacing = 6;

    _muteButton = [self iconButton:@"speaker.wave.2.fill"
                            action:@selector(toggleMute:)
                           tooltip:TVStr(tv::Str::TipMute)];

    NSStackView* right =
        [NSStackView stackViewWithViews:@[ _muteButton, _volumeSlider, fullScreenButton ]];
    right.spacing = 8;

    NSStackView* texts = [NSStackView stackViewWithViews:@[ _nowPlayingLabel, _stateLabel ]];
    texts.orientation = NSUserInterfaceLayoutOrientationVertical;
    texts.alignment = NSLayoutAttributeLeading;
    texts.spacing = 1;

    for (NSView* v in @[ buttons, texts, right ]) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [bar addSubview:v];
    }
    [_volumeSlider.widthAnchor constraintEqualToConstant:110].active = YES;

    [NSLayoutConstraint activateConstraints:@[
        [_videoView.topAnchor constraintEqualToAnchor:pane.topAnchor],
        [_videoView.leadingAnchor constraintEqualToAnchor:pane.leadingAnchor],
        [_videoView.trailingAnchor constraintEqualToAnchor:pane.trailingAnchor],
        [_videoView.bottomAnchor constraintEqualToAnchor:bar.topAnchor],

        [bar.leadingAnchor constraintEqualToAnchor:pane.leadingAnchor],
        [bar.trailingAnchor constraintEqualToAnchor:pane.trailingAnchor],
        [bar.bottomAnchor constraintEqualToAnchor:pane.bottomAnchor],
        [bar.heightAnchor constraintEqualToConstant:54],

        [buttons.leadingAnchor constraintEqualToAnchor:bar.leadingAnchor constant:12],
        [buttons.centerYAnchor constraintEqualToAnchor:bar.centerYAnchor],

        [texts.leadingAnchor constraintEqualToAnchor:buttons.trailingAnchor constant:16],
        [texts.trailingAnchor constraintLessThanOrEqualToAnchor:right.leadingAnchor constant:-16],
        [texts.centerYAnchor constraintEqualToAnchor:bar.centerYAnchor],

        [right.trailingAnchor constraintEqualToAnchor:bar.trailingAnchor constant:-12],
        [right.centerYAnchor constraintEqualToAnchor:bar.centerYAnchor],
    ]];
    return pane;
}

// Monochrome transport icons. A plain template image would be drawn in the
// system accent colour; a hierarchical configuration in the label colour keeps
// them grey and still lets them follow light/dark appearance.
- (NSImage*)symbolImage:(NSString*)symbolName description:(NSString*)description {
    NSImage* image = [NSImage imageWithSystemSymbolName:symbolName
                               accessibilityDescription:description];
    if (@available(macOS 12.0, *)) {
        NSImageSymbolConfiguration* configuration =
            [NSImageSymbolConfiguration configurationWithHierarchicalColor:NSColor.labelColor];
        NSImage* configured = [image imageWithSymbolConfiguration:configuration];
        if (configured != nil) {
            [configured setTemplate:NO];  // `template` is a keyword in Objective-C++
            return configured;
        }
    }
    [image setTemplate:YES];
    return image;
}

- (NSButton*)iconButton:(NSString*)symbolName action:(SEL)action tooltip:(NSString*)tooltip {
    NSImage* image = [self symbolImage:symbolName description:tooltip];
    NSButton* button = [NSButton buttonWithImage:image target:self action:action];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.imagePosition = NSImageOnly;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.toolTip = tooltip;
    [button.widthAnchor constraintEqualToConstant:38].active = YES;
    return button;
}

- (NSTextField*)makeLabelWithSize:(CGFloat)size secondary:(BOOL)secondary {
    NSTextField* label = [NSTextField labelWithString:@""];
    label.font = [NSFont systemFontOfSize:size];
    label.textColor = secondary ? NSColor.secondaryLabelColor : NSColor.labelColor;
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    return label;
}

#pragma mark - Video attachment

- (void)windowDidLoad {
    [super windowDidLoad];
}

- (void)showWindow:(id)sender {
    [super showWindow:sender];
    // set_nsobject needs a view that already belongs to a window.
    if (_controller->player()) _controller->player()->attachVideoView((__bridge void*)_videoView);

    // Debug aid (also handy when bringing up a new platform): start a channel
    // matching RTV_AUTOPLAY as soon as the window is up.
    const char* autoplay = getenv("RTV_AUTOPLAY");
    if (autoplay && *autoplay) {
        _searchField.stringValue = [NSString stringWithUTF8String:autoplay];
        _controller->index().setFilter(autoplay);
        [self reloadChannelTree];
        [self stepChannel:1];
    }
}

#pragma mark - Model -> view

- (void)reloadChannelTree {
    [self rememberExpandedPaths];
    NSString* selectedUrl = _currentUrl;

    [_roots removeAllObjects];
    const tv::ChannelIndex& index = _controller->index();
    for (const tv::CategoryNode& root : index.roots())
        [_roots addObject:[self nodeFromCategory:root]];

    [_outlineView reloadData];
    [self restoreExpandedPaths];

    [self updateSidebarStatus];
    _currentUrl = selectedUrl;
}

- (TVCategoryNode*)nodeFromCategory:(const tv::CategoryNode&)category {
    TVCategoryNode* node = [[TVCategoryNode alloc] init];
    node.title = [NSString stringWithUTF8String:category.title.c_str()];
    if (!category.countryCode.empty()) {
        std::string flag = tv::countryFlagEmoji(category.countryCode);
        if (!flag.empty())
            node.title = [NSString stringWithFormat:@"%@ %@",
                                                    [NSString stringWithUTF8String:flag.c_str()],
                                                    node.title];
    }
    node.path = [NSString stringWithUTF8String:category.path.c_str()];
    node.total = category.totalChannels;
    node.favorites = category.isFavorites;

    for (const tv::CategoryNode& child : category.children)
        [node.subcategories addObject:[self nodeFromCategory:child]];

    const tv::ChannelIndex& index = _controller->index();
    for (size_t channelIndex : category.channels) {
        const tv::Channel& channel = index.channelAt(channelIndex);
        TVChannelNode* leaf = [[TVChannelNode alloc] init];
        leaf.channelIndex = channelIndex;
        leaf.title = [NSString stringWithUTF8String:channel.name.c_str()];
        // In country mode the flag is already on the parent row.
        std::string flag = _controller->index().grouping() == tv::Grouping::Country
                               ? std::string()
                               : tv::countryFlagEmoji(channel.country);
        leaf.subtitle = flag.empty() ? @"" : [NSString stringWithUTF8String:flag.c_str()];
        leaf.favorite = _controller->favorites().contains(channel.url);
        [node.channels addObject:leaf];
    }
    return node;
}

#pragma mark - Expansion state

- (void)rememberExpandedPaths {
    if (_outlineView.numberOfRows == 0) return;
    [_expandedPaths removeAllObjects];
    for (NSInteger row = 0; row < _outlineView.numberOfRows; ++row) {
        id item = [_outlineView itemAtRow:row];
        if ([item isKindOfClass:TVCategoryNode.class] && [_outlineView isItemExpanded:item])
            [_expandedPaths addObject:[(TVCategoryNode*)item path]];
    }
}

- (void)restoreExpandedPaths {
    // A filtered result set is small enough to show fully expanded.
    BOOL filtering = !_controller->index().filter().empty();
    BOOL expandAll = filtering && _controller->index().visibleChannels() <= 400;
    for (TVCategoryNode* node in _roots) [self restoreNode:node expandAll:expandAll];
}

- (void)restoreNode:(TVCategoryNode*)node expandAll:(BOOL)expandAll {
    if (node.subcategories.count == 0 && node.channels.count == 0) return;
    if (expandAll || node.favorites || [_expandedPaths containsObject:node.path]) {
        [_outlineView expandItem:node];
        for (TVCategoryNode* child in node.subcategories)
            [self restoreNode:child expandAll:expandAll];
    }
}

- (void)updateSidebarStatus {
    const tv::ChannelIndex& index = _controller->index();
    NSString* origin = @"";
    switch (_controller->playlistSource()) {
        case tv::PlaylistSource::Network: origin = TVStr(tv::Str::Online); break;
        case tv::PlaylistSource::Cache: origin = TVStr(tv::Str::OfflineCache); break;
        case tv::PlaylistSource::Seed: origin = TVStr(tv::Str::BundledSnapshot); break;
        case tv::PlaylistSource::None: origin = TVStr(tv::Str::NoPlaylist); break;
    }

    NSString* when = @"";
    std::time_t fetched = _controller->playlistFetchedAt();
    if (fetched > 0) {
        NSDate* date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)fetched];
        NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm";
        when = [NSString stringWithFormat:@" · %@", [formatter stringFromDate:date]];
    }

    NSNumberFormatter* numbers = [[NSNumberFormatter alloc] init];
    numbers.numberStyle = NSNumberFormatterDecimalStyle;
    NSString* shown = [numbers stringFromNumber:@(index.visibleChannels())];
    NSString* total = [numbers stringFromNumber:@(index.totalChannels())];

    NSString* status =
        [NSString stringWithFormat:@"%@ · %@%@",
         TVStr2(tv::Str::ChannelCount, shown, total), origin, when];
    if (_refreshNote.length) status = [NSString stringWithFormat:@"%@ · %@", status, _refreshNote];
    _sidebarStatus.stringValue = status;
    _sidebarStatus.toolTip = status;
}

#pragma mark - NSOutlineViewDataSource

- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item {
    if (item == nil) return (NSInteger)_roots.count;
    if ([item isKindOfClass:TVCategoryNode.class]) return (NSInteger)[(TVCategoryNode*)item childCount];
    return 0;
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)idx ofItem:(id)item {
    if (item == nil) return _roots[(NSUInteger)idx];
    return [(TVCategoryNode*)item childAtIndex:(NSUInteger)idx];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item {
    return [item isKindOfClass:TVCategoryNode.class] && [(TVCategoryNode*)item childCount] > 0;
}

#pragma mark - NSOutlineViewDelegate

- (BOOL)outlineView:(NSOutlineView*)outlineView isGroupItem:(id)item {
    // Every category keeps a disclosure triangle, at every depth, so the whole
    // hierarchy can be folded. Group rows would lose that.
    return NO;
}

- (BOOL)outlineView:(NSOutlineView*)outlineView shouldSelectItem:(id)item {
    return YES;
}

- (NSView*)outlineView:(NSOutlineView*)outlineView
    viewForTableColumn:(NSTableColumn*)tableColumn
                  item:(id)item {
    BOOL isCategory = [item isKindOfClass:TVCategoryNode.class];
    NSString* identifier = isCategory ? @"category" : @"channel";
    NSTableCellView* cell = [outlineView makeViewWithIdentifier:identifier owner:self];
    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = identifier;
        NSTextField* field = [NSTextField labelWithString:@""];
        field.translatesAutoresizingMaskIntoConstraints = NO;
        field.lineBreakMode = NSLineBreakByTruncatingTail;
        [cell addSubview:field];
        cell.textField = field;
        [NSLayoutConstraint activateConstraints:@[
            [field.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:2],
            [field.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
            [field.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }

    if (isCategory) {
        TVCategoryNode* category = item;
        NSString* title = category.favorites
                             ? [NSString stringWithFormat:@"\u2605 %@", TVStr(tv::Str::Favorites)]
                             : category.title;
        cell.textField.stringValue =
            [NSString stringWithFormat:@"%@  %lu", title, (unsigned long)category.total];
        cell.textField.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
        cell.textField.textColor = NSColor.secondaryLabelColor;
    } else {
        TVChannelNode* channel = item;
        NSString* star = channel.favorite ? @"\u2605 " : @"";
        NSString* flag = channel.subtitle.length ? [channel.subtitle stringByAppendingString:@" "]
                                                 : @"";
        cell.textField.stringValue =
            [NSString stringWithFormat:@"%@%@%@", star, flag, channel.title];
        cell.textField.font = [NSFont systemFontOfSize:13];
        cell.textField.textColor = NSColor.labelColor;
    }
    return cell;
}

- (void)outlineViewSelectionDidChange:(NSNotification*)notification {
    [self updateFavoriteButton];
}

- (void)setPlayPauseSymbol:(NSString*)symbolName {
    _playPauseButton.image = [self symbolImage:symbolName description:@"play/pause"];
}

- (void)updateFavoriteButton {
    TVChannelNode* node = [self selectedChannelNode];
    if (!node) return;
    const tv::Channel& channel = _controller->index().channelAt(node.channelIndex);
    BOOL isFavorite = _controller->favorites().contains(channel.url);
    _favoriteButton.image = [self symbolImage:isFavorite ? @"star.fill" : @"star"
                                  description:@"favorite"];
    _favoriteButton.toolTip =
        isFavorite ? TVStr(tv::Str::TipRemoveFavorite) : TVStr(tv::Str::TipAddFavorite);
}

#pragma mark - NSSplitViewDelegate

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMinCoordinate:(CGFloat)proposed
               ofSubviewAt:(NSInteger)index {
    return 220;
}

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMaxCoordinate:(CGFloat)proposed
               ofSubviewAt:(NSInteger)index {
    return NSWidth(splitView.bounds) - 480;
}

- (BOOL)splitView:(NSSplitView*)splitView canCollapseSubview:(NSView*)subview {
    return subview == splitView.subviews.firstObject;
}

#pragma mark - Search

- (void)controlTextDidChange:(NSNotification*)notification {
    if (notification.object != _searchField) return;
    _controller->index().setFilter(_searchField.stringValue.UTF8String ?: "");
    [self reloadChannelTree];
}

#pragma mark - Actions

- (TVChannelNode*)selectedChannelNode {
    NSInteger row = _outlineView.selectedRow;
    if (row < 0) return nil;
    id item = [_outlineView itemAtRow:row];
    return [item isKindOfClass:TVChannelNode.class] ? item : nil;
}

- (void)playSelectedChannel:(id)sender {
    TVChannelNode* node = [self selectedChannelNode];
    if (node) [self playChannelNode:node];
}

- (void)outlineRowClicked:(id)sender {
    NSInteger row = _outlineView.clickedRow;
    if (row < 0) return;
    id item = [_outlineView itemAtRow:row];
    if ([item isKindOfClass:TVCategoryNode.class]) {
        if ([_outlineView isItemExpanded:item]) [_outlineView collapseItem:item];
        else [_outlineView expandItem:item];
        return;
    }
    [self playChannelNode:item];
}

- (void)playChannelNode:(TVChannelNode*)node {
    tv::MediaPlayer* player = _controller->player();
    if (!player) {
        _stateLabel.stringValue = TVStr(tv::Str::NoBackend);
        return;
    }
    const tv::Channel& channel = _controller->index().channelAt(node.channelIndex);
    _currentUrl = [NSString stringWithUTF8String:channel.url.c_str()];
    _nowPlayingLabel.stringValue = [NSString stringWithUTF8String:channel.name.c_str()];
    self.window.title = [NSString stringWithFormat:@"R Television - %@", _nowPlayingLabel.stringValue];
    _stateLabel.stringValue = TVStr(tv::Str::Opening);
    _streamStarted = NO;
    [self setPlayPauseSymbol:@"pause.fill"];
    player->attachVideoView((__bridge void*)_videoView);
    player->play(channel);
}

- (void)togglePlayPause:(id)sender {
    tv::MediaPlayer* player = _controller->player();
    if (!player) return;
    if (player->state() == tv::PlaybackState::Idle ||
        player->state() == tv::PlaybackState::Stopped) {
        TVChannelNode* node = [self selectedChannelNode];
        if (node) [self playChannelNode:node];
        return;
    }
    BOOL pause = !player->isPaused();
    player->setPaused(pause);
    [self setPlayPauseSymbol:pause ? @"play.fill" : @"pause.fill"];
}

- (void)stopPlayback:(id)sender {
    if (_controller->player()) _controller->player()->stop();
    [self setPlayPauseSymbol:@"play.fill"];
}

- (void)toggleMute:(id)sender {
    tv::MediaPlayer* player = _controller->player();
    if (!player) return;
    BOOL muted = !player->isMuted();
    player->setMuted(muted);
    _muteButton.image = [self symbolImage:muted ? @"speaker.slash.fill" : @"speaker.wave.2.fill"
                              description:@"volume"];
    _muteButton.toolTip = muted ? TVStr(tv::Str::TipUnmute) : TVStr(tv::Str::TipMute);
    _volumeSlider.enabled = !muted;
}

- (void)toggleFullScreen:(id)sender {
    [self.window toggleFullScreen:sender];
}

- (void)volumeChanged:(id)sender {
    if (_controller->player()) _controller->player()->setVolume((int)_volumeSlider.doubleValue);
}

- (void)toggleFavoriteForSelection:(id)sender {
    TVChannelNode* node = [self selectedChannelNode];
    if (!node) return;
    const tv::Channel& channel = _controller->index().channelAt(node.channelIndex);
    _controller->toggleFavorite(channel.url);
    [self reloadChannelTree];
    [self updateFavoriteButton];
}

- (void)groupingChanged:(id)sender {
    _controller->index().setGrouping(_groupingControl.selectedSegment == 1
                                         ? tv::Grouping::Country
                                         : tv::Grouping::Category);
    [self reloadChannelTree];
}

- (void)focusSearchField:(id)sender {
    [self.window makeFirstResponder:_searchField];
}

- (void)selectNextChannel:(id)sender { [self stepChannel:1]; }
- (void)selectPreviousChannel:(id)sender { [self stepChannel:-1]; }

- (void)stepChannel:(NSInteger)delta {
    NSInteger row = _outlineView.selectedRow;
    NSInteger count = _outlineView.numberOfRows;
    if (count == 0) return;
    NSInteger next = row < 0 ? 0 : row + delta;
    while (next >= 0 && next < count) {
        id item = [_outlineView itemAtRow:next];
        if ([item isKindOfClass:TVChannelNode.class]) {
            [_outlineView selectRowIndexes:[NSIndexSet indexSetWithIndex:next]
                      byExtendingSelection:NO];
            [_outlineView scrollRowToVisible:next];
            [self playChannelNode:item];
            return;
        }
        next += delta == 0 ? 1 : delta;
    }
}

- (void)revealCacheFolder:(id)sender {
    NSString* dir = [NSString stringWithUTF8String:_controller->store().cachePath().c_str()];
    [NSWorkspace.sharedWorkspace selectFile:dir inFileViewerRootedAtPath:@""];
}

#pragma mark - Playlist refresh

- (void)refreshPlaylist:(id)sender {
    if (_controller->refreshing()) return;
    [_busyIndicator startAnimation:nil];
    _refreshNote = TVStr(tv::Str::Downloading);
    [self updateSidebarStatus];

    __weak MainWindowController* weakSelf = self;
    _controller->refreshAsync([weakSelf](const tv::RefreshResult& result) {
        tv::RefreshResult copy = result;
        dispatch_async(dispatch_get_main_queue(), ^{
            MainWindowController* strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf refreshFinished:copy];
        });
    });
}

- (void)refreshFinished:(const tv::RefreshResult&)result {
    [_busyIndicator stopAnimation:nil];
    if (result.updated) {
        _refreshNote = nil;
        [self reloadChannelTree];
    } else if (result.notModified) {
        _refreshNote = nil;
        [self updateSidebarStatus];
    } else {
        // Playback is unaffected: the offline copy is still what the list shows.
        _refreshNote = [NSString stringWithFormat:@"%@ (%@)", TVStr(tv::Str::RefreshFailed),
                                                  [NSString stringWithUTF8String:result.error.c_str()]];
        [self updateSidebarStatus];
    }
}

#pragma mark - Player state

- (void)installPlayerCallback {
    tv::MediaPlayer* player = _controller->player();
    if (!player) return;
    __weak MainWindowController* weakSelf = self;
    player->setStateCallback([weakSelf](tv::PlaybackState state, const std::string& message) {
        NSString* text = [NSString stringWithUTF8String:message.c_str()] ?: @"";
        dispatch_async(dispatch_get_main_queue(), ^{
            MainWindowController* strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf playerStateChanged:state message:text];
        });
    });
}

- (void)playerStateChanged:(tv::PlaybackState)state message:(NSString*)message {
    switch (state) {
        case tv::PlaybackState::Opening:
            _streamStarted = NO;
            _stateLabel.stringValue = TVStr(tv::Str::Opening);
            break;
        case tv::PlaybackState::Buffering:
            // VLC keeps emitting buffering events during playback; only surface
            // them until the stream has actually started.
            if (!_streamStarted)
                _stateLabel.stringValue =
                    [NSString stringWithFormat:@"%@ %@", TVStr(tv::Str::Buffering), message];
            break;
        case tv::PlaybackState::Playing:
            _streamStarted = YES;
            _stateLabel.stringValue = TVStr(tv::Str::Playing);
            [self setPlayPauseSymbol:@"pause.fill"];
            break;
        case tv::PlaybackState::Paused:
            _stateLabel.stringValue = TVStr(tv::Str::Paused);
            [self setPlayPauseSymbol:@"play.fill"];
            break;
        case tv::PlaybackState::Stopped:
            _streamStarted = NO;
            _stateLabel.stringValue = message.length ? message : TVStr(tv::Str::Stopped);
            [self setPlayPauseSymbol:@"play.fill"];
            break;
        case tv::PlaybackState::Error:
            _streamStarted = NO;
            _stateLabel.stringValue =
                [NSString stringWithFormat:@"%@ - %@", TVStr(tv::Str::PlaybackFailed),
                                           message.length ? message
                                                          : TVStr(tv::Str::UnknownError)];
            [self setPlayPauseSymbol:@"play.fill"];
            break;
        case tv::PlaybackState::Idle:
            break;
    }
}

@end
