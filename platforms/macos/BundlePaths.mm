#import <Foundation/Foundation.h>

#include "BundlePaths.h"
#include "core/Paths.h"

namespace tv {
namespace macos {
namespace {

std::string toStd(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }

NSString* firstExistingDirectory(NSArray<NSString*>* candidates) {
    NSFileManager* fm = NSFileManager.defaultManager;
    for (NSString* path in candidates) {
        BOOL isDir = NO;
        if (path.length && [fm fileExistsAtPath:path isDirectory:&isDir] && isDir) return path;
    }
    return nil;
}

}  // namespace

AppPaths resolveAppPaths() {
    AppPaths paths;
    paths.dataDir = appDataDir();

    NSBundle* bundle = NSBundle.mainBundle;
    NSString* seed = [bundle pathForResource:@"seed-playlist" ofType:@"m3u"];
    paths.seedPath = toStd(seed);

    NSMutableArray<NSString*>* pluginCandidates = [NSMutableArray array];
    if (bundle.bundlePath.length) {
        [pluginCandidates addObject:[bundle.bundlePath
                                        stringByAppendingPathComponent:@"Contents/MacOS/plugins"]];
    }
    // Development fallbacks: the vendored SDK, then a system VLC install.
    NSString* exeDir = bundle.executablePath.stringByDeletingLastPathComponent;
    if (exeDir.length) {
        [pluginCandidates addObject:[exeDir stringByAppendingPathComponent:@"plugins"]];
        [pluginCandidates addObject:[exeDir stringByAppendingPathComponent:
                                                @"../../../third_party/vlc-macos/plugins"]];
    }
    [pluginCandidates addObject:@"/Applications/VLC.app/Contents/MacOS/plugins"];

    NSString* plugins = firstExistingDirectory(pluginCandidates);
    paths.pluginPath = toStd(plugins.stringByStandardizingPath);
    return paths;
}

}  // namespace macos
}  // namespace tv
