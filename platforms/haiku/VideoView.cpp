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
      fPlaceholder("") {
    SetViewColor(0, 0, 0);
    SetExplicitMinSize(BSize(320, 180));
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

void VideoView::Draw(BRect updateRect) {
    BAutolock lock(fLock);
    SetHighColor(0, 0, 0);
    FillRect(updateRect);

    if (fBitmap != NULL) {
        DrawBitmap(fBitmap, fBitmap->Bounds(), LetterboxedRect(), B_FILTER_BITMAP_BILINEAR);
        return;
    }

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
