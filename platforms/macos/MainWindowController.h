#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/AppController.h"

NS_ASSUME_NONNULL_BEGIN

@interface MainWindowController : NSWindowController

- (instancetype)initWithController:(std::shared_ptr<tv::AppController>)controller;

- (void)refreshPlaylist:(nullable id)sender;
- (void)togglePlayPause:(nullable id)sender;
- (void)stopPlayback:(nullable id)sender;
- (void)toggleMute:(nullable id)sender;
- (void)toggleFavoriteForSelection:(nullable id)sender;
- (void)focusSearchField:(nullable id)sender;
- (void)selectNextChannel:(nullable id)sender;
- (void)selectPreviousChannel:(nullable id)sender;
- (void)revealCacheFolder:(nullable id)sender;

@end

NS_ASSUME_NONNULL_END
