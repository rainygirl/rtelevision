// Linux entry point (GTK3 + libVLC).
#include <gtk/gtk.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "MainWindow.h"
#include "core/AppController.h"
#include "core/Paths.h"
#include "core/Strings.h"

namespace {

std::string executableDir() {
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return ".";
    buffer[length] = '\0';
    std::string path(buffer);
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string firstExistingFile(const std::vector<std::string>& candidates) {
    for (const std::string& path : candidates)
        if (!path.empty() && tv::fileExists(path)) return path;
    return std::string();
}

std::string firstExistingDir(const std::vector<std::string>& candidates) {
    for (const std::string& path : candidates)
        if (!path.empty() && g_file_test(path.c_str(), G_FILE_TEST_IS_DIR)) return path;
    return std::string();
}

// libVLC dlopens its plugins, and those link against helper libraries that ship
// in the bundle (libvlc_xcb_events, libvlc_pulse, libvlc_vdpau). The executable's
// RPATH does not cover a dlopened object's own dependencies, and glibc reads
// LD_LIBRARY_PATH once at startup - so set it and start over. Without this the
// video output modules fail to load and the picture stays black.
void reExecWithBundleLibraryPath(char** argv) {
    const std::string libDir = executableDir() + "/vlc/lib";
    if (!g_file_test(libDir.c_str(), G_FILE_TEST_IS_DIR)) return;  // system libvlc build

    const char* current = std::getenv("LD_LIBRARY_PATH");
    if (current != nullptr && std::strstr(current, libDir.c_str()) != nullptr) return;

    std::string next = libDir;
    if (current != nullptr && *current != '\0') next += std::string(":") + current;
    setenv("LD_LIBRARY_PATH", next.c_str(), 1);
    execv("/proc/self/exe", argv);
    // Falling through only costs the extra plugins; carry on.
}

tv::AppPaths resolvePaths() {
    const std::string exeDir = executableDir();

    tv::AppPaths paths;
    paths.dataDir = tv::appDataDir();  // ~/.local/share/RTelevision

    paths.seedPath = firstExistingFile({
        exeDir + "/seed-playlist.m3u",                       // portable layout
        exeDir + "/../share/RTelevision/seed-playlist.m3u",   // make install prefix
        "/usr/local/share/RTelevision/seed-playlist.m3u",
        "/usr/share/RTelevision/seed-playlist.m3u",
        exeDir + "/../../../resources/seed-playlist.m3u",    // build tree
    });

    // The vendored plugin tree ships next to the binary; fall back to a system
    // VLC install when the app was built against one.
    paths.pluginPath = firstExistingDir({
        exeDir + "/vlc/plugins",
        exeDir + "/../lib/RTelevision/vlc/plugins",
        "/usr/lib/x86_64-linux-gnu/vlc/plugins",
        "/usr/lib/vlc/plugins",
    });
    return paths;
}

}  // namespace

int main(int argc, char** argv) {
    reExecWithBundleLibraryPath(argv);
    gtk_init(&argc, &argv);
    tv::setLanguage(tv::detectLanguage());

    const std::string iconPath = firstExistingFile({
        executableDir() + "/icon.png",
        executableDir() + "/../share/icons/hicolor/256x256/apps/r-television.png",
        "/usr/local/share/icons/hicolor/256x256/apps/r-television.png",
        "/usr/share/icons/hicolor/256x256/apps/r-television.png",
        executableDir() + "/../../../resources/icon/icon_256.png",
    });
    if (!iconPath.empty()) gtk_window_set_default_icon_from_file(iconPath.c_str(), nullptr);

    auto controller = std::make_shared<tv::AppController>(resolvePaths());

    std::string error;
    if (!controller->startPlayer(&error)) {
        GtkWidget* dialog = gtk_message_dialog_new(
            nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "%s\n%s", tv::str(tv::Str::BackendInitFailed), error.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    // Offline copy first so the window is usable before any network call.
    controller->loadLocalPlaylist();

    tv::linux_ui::MainWindow window(controller);
    window.Show();
    gtk_main();
    return 0;
}
