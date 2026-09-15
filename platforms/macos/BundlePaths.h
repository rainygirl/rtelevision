#pragma once

#include "core/AppController.h"

namespace tv {
namespace macos {

// Resolves the data dir, bundled seed playlist and VLC plugin tree, falling
// back to a system VLC install when the app runs un-bundled (e.g. from a build
// directory during development).
AppPaths resolveAppPaths();

}  // namespace macos
}  // namespace tv
