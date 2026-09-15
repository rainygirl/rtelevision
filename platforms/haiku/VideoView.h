// Haiku video surface.
//
// libVLC 3 has no BView handle setter (no counterpart to set_nsobject), so the
// Haiku front end takes the software path: the backend decodes into a BGRA
// buffer through tv::VideoFrameSink and this view blits it.
#pragma once

#include <Bitmap.h>
#include <Locker.h>
#include <View.h>

#include "core/MediaPlayer.h"

namespace tv {
namespace haiku {

class VideoView : public BView, public VideoFrameSink {
public:
    VideoView();
    ~VideoView() override;

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;

    bool HasFrame() const { return fBitmap != NULL; }
    void SetPlaceholder(const char* text);

    // VideoFrameSink
    unsigned setupFormat(unsigned& width, unsigned& height) override;
    void* lockFrame() override;
    void unlockFrame() override;
    void displayFrame() override;
    void cleanupFormat() override;

private:
    BRect LetterboxedRect() const;

    BLocker fLock;
    BBitmap* fBitmap;
    unsigned fWidth;
    unsigned fHeight;
    BString fPlaceholder;
};

}  // namespace haiku
}  // namespace tv
