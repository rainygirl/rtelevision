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
    void FrameResized(float width, float height) override;
    void AttachedToWindow() override;

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
    void FillAround(BRect inner, BRect updateRect);

    BLocker fLock;
    BBitmap* fBitmap;
    unsigned fWidth;   // the buffer libVLC decodes into
    unsigned fHeight;
    // setupFormat() runs on a libVLC thread and cannot lock the window, so the
    // view size it needs is cached here and refreshed from FrameResized().
    float fViewWidth;
    float fViewHeight;
    BString fPlaceholder;
};

}  // namespace haiku
}  // namespace tv
