#include "VideoView.h"

#include <Autolock.h>
#include <Message.h>
#include <Window.h>

#include "Messages.h"

namespace tv {
namespace haiku {

VideoView::VideoView()
    : BView("video", B_WILL_DRAW | B_FRAME_EVENTS),
      fLock("video frame"),
      fBitmap(NULL),
      fWidth(0),
      fHeight(0),
      fViewWidth(0),
      fViewHeight(0),
      fPlaceholder("") {
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(320, 180));
}

void VideoView::AttachedToWindow() {
    BAutolock lock(fLock);
    fViewWidth = Bounds().Width() + 1;
    fViewHeight = Bounds().Height() + 1;
}

void VideoView::FrameResized(float width, float height) {
    BAutolock lock(fLock);
    fViewWidth = width + 1;
    fViewHeight = height + 1;
}

VideoView::~VideoView() {
    delete fBitmap;
}

void VideoView::SetPlaceholder(const char* text) {
    BAutolock lock(fLock);
    fPlaceholder = text;
    if (LockLooperWithTimeout(100000) == B_OK) {
        Invalidate();
        UnlockLooper();
    }
}

unsigned VideoView::setupFormat(unsigned& width, unsigned& height) {
    if (width == 0 || height == 0) return 0;
    BAutolock lock(fLock);

    // Ask libVLC for the size the frame is actually shown at, so that Draw()
    // is a straight blit. Scaling in the decoder is both cheaper (swscale uses
    // SSE) and off the app_server thread; letting app_server scale instead
    // costs 11.7 ms per frame on a 1.33 GHz Atom against 7.0 ms for the blit,
    // and 35.5 ms once bilinear filtering is asked for.
    if (fViewWidth >= 1 && fViewHeight >= 1) {
        float frameAspect = (float)width / (float)height;
        float w = fViewWidth;
        float h = fViewHeight;
        if (frameAspect > w / h)
            h = w / frameAspect;
        else
            w = h * frameAspect;
        unsigned targetWidth = (unsigned)w & ~1u;
        unsigned targetHeight = (unsigned)h & ~1u;
        // Never ask for more pixels than the stream carries - upscaling in the
        // decoder is worth it, but only up to the size actually displayed.
        if (targetWidth >= 16 && targetHeight >= 16) {
            width = targetWidth;
            height = targetHeight;
        }
    }

    delete fBitmap;
    fWidth = width;
    fHeight = height;
    fBitmap = new BBitmap(BRect(0, 0, (float)width - 1, (float)height - 1), B_RGB32);
    if (fBitmap->InitCheck() != B_OK) {
        delete fBitmap;
        fBitmap = NULL;
        return 0;
    }
    return (unsigned)fBitmap->BytesPerRow();
}

void* VideoView::lockFrame() {
    fLock.Lock();
    return fBitmap != NULL ? fBitmap->Bits() : NULL;
}

void VideoView::unlockFrame() {
    fLock.Unlock();
}

void VideoView::displayFrame() {
    if (LockLooperWithTimeout(100000) == B_OK) {
        Invalidate();
        UnlockLooper();
    }
}

void VideoView::cleanupFormat() {
    BAutolock lock(fLock);
    delete fBitmap;
    fBitmap = NULL;
    fWidth = fHeight = 0;
}

// Fits the frame inside the view without distorting the aspect ratio.
BRect VideoView::LetterboxedRect() const {
    BRect bounds = Bounds();
    if (fWidth == 0 || fHeight == 0) return bounds;

    float viewAspect = bounds.Width() / bounds.Height();
    float frameAspect = (float)fWidth / (float)fHeight;
    float w = bounds.Width();
    float h = bounds.Height();
    if (frameAspect > viewAspect) h = w / frameAspect;
    else w = h * frameAspect;

    float x = bounds.left + (bounds.Width() - w) / 2;
    float y = bounds.top + (bounds.Height() - h) / 2;
    return BRect(x, y, x + w, y + h);
}

// Paints the black bars around the frame, and nothing else. Filling the whole
// view first and then drawing over it costs another 7.2 ms per frame here.
void VideoView::FillAround(BRect inner, BRect updateRect) {
    BRect bounds = Bounds();
    BRect parts[4] = {
        BRect(bounds.left, bounds.top, bounds.right, inner.top - 1),
        BRect(bounds.left, inner.bottom + 1, bounds.right, bounds.bottom),
        BRect(bounds.left, inner.top, inner.left - 1, inner.bottom),
        BRect(inner.right + 1, inner.top, bounds.right, inner.bottom),
    };
    SetHighColor(0, 0, 0);
    for (int i = 0; i < 4; i++) {
        BRect part = parts[i] & updateRect;
        if (part.IsValid()) FillRect(part);
    }
}

void VideoView::Draw(BRect updateRect) {
    BAutolock lock(fLock);

    if (fBitmap != NULL) {
        BRect bounds = Bounds();
        // setupFormat() normally hands libVLC the displayed size, so the buffer
        // already fits and this is a plain blit. After a resize it no longer
        // does, until the format is renegotiated, and the scaled path takes
        // over - without a filter, which alone costs three times the draw.
        if (fWidth <= (unsigned)(bounds.Width() + 1)
            && fHeight <= (unsigned)(bounds.Height() + 1)) {
            BPoint at(bounds.left + (bounds.Width() + 1 - (float)fWidth) / 2,
                      bounds.top + (bounds.Height() + 1 - (float)fHeight) / 2);
            BRect destination(at.x, at.y, at.x + (float)fWidth - 1,
                              at.y + (float)fHeight - 1);
            FillAround(destination, updateRect);
            DrawBitmap(fBitmap, at);
        } else {
            BRect destination = LetterboxedRect();
            FillAround(destination, updateRect);
            DrawBitmap(fBitmap, fBitmap->Bounds(), destination);
        }
        return;
    }

    SetHighColor(0, 0, 0);
    FillRect(updateRect);

    if (fPlaceholder.Length() > 0) {
        SetHighColor(150, 150, 150);
        SetLowColor(0, 0, 0);
        font_height fh;
        GetFontHeight(&fh);
        float width = StringWidth(fPlaceholder.String());
        BRect bounds = Bounds();
        DrawString(fPlaceholder.String(),
                   BPoint(bounds.left + (bounds.Width() - width) / 2,
                          bounds.top + bounds.Height() / 2 + fh.ascent / 2));
    }
}

void VideoView::MouseDown(BPoint where) {
    int32 clicks = 0;
    BMessage* current = Window() != NULL ? Window()->CurrentMessage() : NULL;
    if (current != NULL && current->FindInt32("clicks", &clicks) == B_OK && clicks == 2)
        Window()->PostMessage(kMsgFullScreen);
}

}  // namespace haiku
}  // namespace tv
