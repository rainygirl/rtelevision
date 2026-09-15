// Haiku entry point.
#include <Application.h>
#include <LocaleRoster.h>
#include <Message.h>
#include <image.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "MainWindow.h"
#include "core/AppController.h"
#include "core/Paths.h"
#include "core/Strings.h"

namespace {

// The directory the binary sits in. Haiku's runtime loader also searches it for
// libraries, which is how the bundled libvlc.so is found without an rpath.
std::string executableDir() {
    image_info info;
    int32 cookie = 0;
    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
        if (info.type != B_APP_IMAGE) continue;
        std::string path(info.name);
        size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    }
    return ".";
}

std::string firstExisting(const std::vector<std::string>& candidates) {
    for (const std::string& path : candidates)
        if (!path.empty() && tv::fileExists(path)) return path;
    return std::string();
}

tv::AppPaths resolvePaths() {
    tv::AppPaths paths;
    paths.dataDir = tv::appDataDir();  // ~/config/settings/RTelevision

    const char* home = getenv("HOME");
    std::string homeDir = home ? home : "/boot/home";
    const std::string exeDir = executableDir();
    paths.seedPath = firstExisting({
        exeDir + "/seed-playlist.m3u",
        homeDir + "/config/non-packaged/data/RTelevision/seed-playlist.m3u",
        "/boot/system/data/RTelevision/seed-playlist.m3u",
        "../../resources/seed-playlist.m3u",
    });

    // Bundled module tree first, then a system VLC install; empty lets libVLC
    // decide for itself.
    for (const std::string& dir : {exeDir + "/vlc/plugins",
                                   std::string("/boot/system/lib/vlc/plugins")}) {
        if (tv::fileExists(dir + "/plugins.dat") || tv::fileExists(dir + "/libaccess_plugin.so")) {
            paths.pluginPath = dir;
            break;
        }
    }
    return paths;
}

// The Locale preferences hold an ordered list of languages; the first one the
// app can speak wins. Without the Locale Kit (or with an unknown language) the
// core falls back to the environment and then to English.
void setUpLanguage() {
    BMessage languages;
    if (BLocaleRoster::Default()->GetPreferredLanguages(&languages) == B_OK) {
        BString tag;
        for (int32 i = 0; languages.FindString("language", i, &tag) == B_OK; ++i) {
            if (tv::hasTranslation(tag.String())) {
                tv::setLanguage(tv::languageFromTag(tag.String()));
                return;
            }
        }
    }
    tv::setLanguage(tv::detectLanguage());
}

class RTelevisionApp : public BApplication {
public:
    RTelevisionApp() : BApplication("application/x-vnd.r-television") {}

    void ReadyToRun() override {
        fController = std::make_shared<tv::AppController>(resolvePaths());

        std::string error;
        fController->startPlayer(&error);
        fController->loadLocalPlaylist();

        (new tv::haiku::MainWindow(fController))->Show();
    }

private:
    std::shared_ptr<tv::AppController> fController;
};

}  // namespace

int main() {
    setUpLanguage();
    RTelevisionApp app;
    app.Run();
    return 0;
}
