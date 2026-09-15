#include "Icons.h"

#include <View.h>

#include <cmath>

namespace tv {
namespace haiku {
namespace {

void DrawPlay(BView* v, float s) {
    BPoint points[3] = {BPoint(s * 0.30f, s * 0.18f), BPoint(s * 0.82f, s * 0.50f),
                        BPoint(s * 0.30f, s * 0.82f)};
    v->FillPolygon(points, 3);
}

void DrawPause(BView* v, float s) {
    v->FillRect(BRect(s * 0.26f, s * 0.18f, s * 0.42f, s * 0.82f));
    v->FillRect(BRect(s * 0.58f, s * 0.18f, s * 0.74f, s * 0.82f));
}

void DrawStop(BView* v, float s) {
    v->FillRect(BRect(s * 0.24f, s * 0.24f, s * 0.76f, s * 0.76f));
}

void StarPoints(float s, BPoint out[10]) {
    const float cx = s * 0.5f, cy = s * 0.52f;
    const float outer = s * 0.40f, inner = s * 0.17f;
    for (int i = 0; i < 10; ++i) {
        const float r = (i % 2 == 0) ? outer : inner;
        const float a = static_cast<float>(-M_PI / 2.0 + i * M_PI / 5.0);
        out[i] = BPoint(cx + r * cosf(a), cy + r * sinf(a));
    }
}

void DrawStar(BView* v, float s, bool filled) {
    BPoint points[10];
    StarPoints(s, points);
    if (filled) {
        v->FillPolygon(points, 10);
    } else {
        v->SetPenSize(s * 0.09f);
        v->StrokePolygon(points, 10, true);
    }
}

// Four corner brackets read as "fill the screen" at any size.
void DrawFullScreen(BView* v, float s) {
    // Short arms with a clear gap: longer ones merge into a plain box at the
    // 16-20px the buttons actually use.
    const float t = s * 0.105f;        // bracket thickness
    const float len = s * 0.30f;       // bracket arm length
    const float a = s * 0.13f, b = s - a;
    v->FillRect(BRect(a, a, a + len, a + t));
    v->FillRect(BRect(a, a, a + t, a + len));
    v->FillRect(BRect(b - len, a, b, a + t));
    v->FillRect(BRect(b - t, a, b, a + len));
    v->FillRect(BRect(a, b - t, a + len, b));
    v->FillRect(BRect(a, b - len, a + t, b));
    v->FillRect(BRect(b - len, b - t, b, b));
    v->FillRect(BRect(b - t, b - len, b, b));
}

}  // namespace

BBitmap* MakeIcon(IconKind kind, float size, rgb_color colour) {
    if (size < 4.0f) return NULL;

    BRect bounds(0, 0, size - 1, size - 1);
    BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
    if (bitmap->InitCheck() != B_OK) {
        delete bitmap;
        return NULL;
    }

    BView* view = new BView(bounds, "icon", B_FOLLOW_NONE, B_WILL_DRAW | B_SUBPIXEL_PRECISE);
    bitmap->AddChild(view);
    if (!bitmap->Lock()) {
        delete bitmap;
        return NULL;
    }

    // Start fully transparent, then draw the glyph in one flat colour.
    view->SetDrawingMode(B_OP_COPY);
    view->SetHighColor(0, 0, 0, 0);
    view->FillRect(bounds);
    view->SetDrawingMode(B_OP_ALPHA);
    view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
    view->SetHighColor(colour);

    switch (kind) {
        case IconKind::Play: DrawPlay(view, size); break;
        case IconKind::Pause: DrawPause(view, size); break;
        case IconKind::Stop: DrawStop(view, size); break;
        case IconKind::Star: DrawStar(view, size, false); break;
        case IconKind::StarFilled: DrawStar(view, size, true); break;
        case IconKind::FullScreen: DrawFullScreen(view, size); break;
    }

    view->Sync();
    bitmap->Unlock();
    bitmap->RemoveChild(view);
    delete view;
    return bitmap;
}

}  // namespace haiku
}  // namespace tv
