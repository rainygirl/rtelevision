// Monochrome glyphs for the transport buttons, drawn at runtime.
//
// Haiku has no icon set to borrow from here and shipping PNGs would mean
// carrying the translators too, so the handful of shapes are drawn straight
// into a BBitmap. They come out in a single colour by construction.
#pragma once

#include <Bitmap.h>
#include <InterfaceDefs.h>

namespace tv {
namespace haiku {

enum class IconKind {
    Play,
    Pause,
    Stop,
    Star,
    StarFilled,
    FullScreen,
};

// Caller owns the returned bitmap. Returns NULL if it could not be made.
BBitmap* MakeIcon(IconKind kind, float size, rgb_color colour);

}  // namespace haiku
}  // namespace tv
