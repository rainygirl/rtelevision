#import "AppDelegate.h"

#include <memory>

#import "BundlePaths.h"
#import "Localization.h"
#import "MainWindowController.h"

@implementation AppDelegate {
    std::shared_ptr<tv::AppController> _controller;
    MainWindowController* _mainWindow;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    TVSetUpLanguage();

    tv::AppPaths paths = tv::macos::resolveAppPaths();
    _controller = std::make_shared<tv::AppController>(paths);

    std::string error;
    if (!_controller->startPlayer(&error)) {
        [self showFatalAlert:TVStr(tv::Str::BackendInitFailed)
                        info:[NSString stringWithUTF8String:error.c_str()]];
    }

    // Offline copy first so the window is usable before any network call.
    if (!_controller->loadLocalPlaylist()) {
        NSLog(@"[iptv] no cached or bundled playlist yet; relying on the network refresh");
    }

    _mainWindow = [[MainWindowController alloc] initWithController:_controller];
    [self buildMenuBar];
    [_mainWindow showWindow:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // Then bring the cache up to date in the background.
    [_mainWindow refreshPlaylist:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

- (void)showFatalAlert:(NSString*)message info:(NSString*)info {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = message;
    alert.informativeText = info;
    alert.alertStyle = NSAlertStyleCritical;
    [alert runModal];
}

- (void)buildMenuBar {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:TVStr(tv::Str::MenuAbout)
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:TVStr(tv::Str::MenuOpenCacheFolder)
                       action:@selector(revealCacheFolder:)
                keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:TVStr(tv::Str::MenuQuit)
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [mainMenu addItem:appItem];

    NSMenuItem* playbackItem = [[NSMenuItem alloc] init];
    NSMenu* playbackMenu = [[NSMenu alloc] initWithTitle:TVStr(tv::Str::MenuPlayback)];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuPlayPause)
                            action:@selector(togglePlayPause:)
                     keyEquivalent:@" "];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuStop)
                            action:@selector(stopPlayback:)
                     keyEquivalent:@"."];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuMute)
                            action:@selector(toggleMute:)
                     keyEquivalent:@""];
    [playbackMenu addItem:NSMenuItem.separatorItem];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuNextChannel)
                            action:@selector(selectNextChannel:)
                     keyEquivalent:@"]"];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuPreviousChannel)
                            action:@selector(selectPreviousChannel:)
                     keyEquivalent:@"["];
    [playbackMenu addItem:NSMenuItem.separatorItem];
    [playbackMenu addItemWithTitle:TVStr(tv::Str::MenuToggleFavorite)
                            action:@selector(toggleFavoriteForSelection:)
                     keyEquivalent:@"d"];
    playbackItem.submenu = playbackMenu;
    [mainMenu addItem:playbackItem];

    NSMenuItem* listItem = [[NSMenuItem alloc] init];
    NSMenu* listMenu = [[NSMenu alloc] initWithTitle:TVStr(tv::Str::MenuChannels)];
    [listMenu addItemWithTitle:TVStr(tv::Str::MenuSearch)
                        action:@selector(focusSearchField:)
                 keyEquivalent:@"f"];
    [listMenu addItemWithTitle:TVStr(tv::Str::MenuRefresh)
                        action:@selector(refreshPlaylist:)
                 keyEquivalent:@"r"];
    listItem.submenu = listMenu;
    [mainMenu addItem:listItem];

    NSMenuItem* windowItem = [[NSMenuItem alloc] init];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:TVStr(tv::Str::MenuWindow)];
    [windowMenu addItemWithTitle:TVStr(tv::Str::MenuFullScreen)
                          action:@selector(toggleFullScreen:)
                   keyEquivalent:@"f"];
    windowMenu.itemArray.lastObject.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagControl;
    [windowMenu addItemWithTitle:TVStr(tv::Str::MenuMinimize)
                          action:@selector(performMiniaturize:)
                   keyEquivalent:@"m"];
    windowItem.submenu = windowMenu;
    [mainMenu addItem:windowItem];

    NSApp.mainMenu = mainMenu;
    NSApp.windowsMenu = windowMenu;
}

@end
