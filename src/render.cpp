#include "render.h"

#include "config.h"

LayeredSurface::~LayeredSurface() {
    release();
}

void LayeredSurface::release() {
    // 顺序要紧：GDI+ 对象先没，再还 GDI 位图，最后删 DC。
    graphics_.reset();
    bitmap_.reset();

    if (memDC_ != nullptr && oldBitmap_ != nullptr) {
        SelectObject(memDC_, oldBitmap_);
    }
    oldBitmap_ = nullptr;

    if (dib_ != nullptr) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    if (memDC_ != nullptr) {
        DeleteDC(memDC_);
        memDC_ = nullptr;
    }
    bits_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool LayeredSurface::ensure(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (graphics_ && width == width_ && height == height_) return true;

    release();

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // 负数 = 自上而下，行序和 GDI+ 一致
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) {
        if (dib != nullptr) DeleteObject(dib);
        return false;
    }

    HDC memDC = CreateCompatibleDC(nullptr);
    if (memDC == nullptr) {
        DeleteObject(dib);
        return false;
    }

    memDC_ = memDC;
    dib_ = dib;
    bits_ = bits;
    oldBitmap_ = SelectObject(memDC_, dib);

    // PixelFormat32bppPARGB：预乘 alpha，正好是 UpdateLayeredWindow(ULW_ALPHA) 要的格式。
    bitmap_ = std::make_unique<Gdiplus::Bitmap>(width, height, width * 4,
                                                PixelFormat32bppPARGB,
                                                static_cast<BYTE*>(bits));
    if (bitmap_->GetLastStatus() != Gdiplus::Ok) {
        release();
        return false;
    }

    graphics_ = std::make_unique<Gdiplus::Graphics>(bitmap_.get());
    if (graphics_->GetLastStatus() != Gdiplus::Ok) {
        release();
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

Gdiplus::FontFamily* uiFontFamily() {
    // 这里的 family 是「故意泄漏」的，别改成 unique_ptr：
    // 函数内静态变量的析构发生在 main 返回之后，那时 GdiplusShutdown 早就跑过了，
    // 再去析构一个 GDI+ 对象必然崩在 _execute_onexit_table 里。
    static Gdiplus::FontFamily* family = nullptr;
    if (family != nullptr) return family;

    const wchar_t* candidates[] = {kFontFamily, L"Microsoft YaHei", L"Segoe UI", L"SimSun"};
    for (const wchar_t* name : candidates) {
        auto* probe = new Gdiplus::FontFamily(name);
        if (probe->IsAvailable()) {
            family = probe;   // 就它了，一直留着
            break;
        }
        delete probe;         // 不合适就当场释放——此刻 GDI+ 还活着，安全
    }
    if (family == nullptr) family = new Gdiplus::FontFamily(L"Arial");
    return family;
}

void addRoundedRect(Gdiplus::GraphicsPath& path, float x, float y, float width, float height,
                    float radius) {
    if (width <= 0.0f || height <= 0.0f) return;

    radius = std::max(0.0f, std::min(radius, std::min(width, height) / 2.0f));
    if (radius <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, width, height));
        return;
    }

    // GDI+ 的角度：0° 在三点钟方向，正方向顺时针，四个角各扫 90°。
    const float d = radius * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);                                   // 左上
    path.AddArc(x + width - d, y, d, d, 270.0f, 90.0f);                       // 右上
    path.AddArc(x + width - d, y + height - d, d, d, 0.0f, 90.0f);            // 右下
    path.AddArc(x, y + height - d, d, d, 90.0f, 90.0f);                       // 左下
    path.CloseFigure();
}

float measureTextWidth(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text) {
    if (text.empty()) return 0.0f;

    // GenericTypographic 不会在两边塞 1/6 em 的排版留白，量出来更接近真实字宽。
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    Gdiplus::RectF layout(0.0f, 0.0f, 1.0e5f, 1.0e5f);
    Gdiplus::RectF bounds;
    graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, layout, &format, &bounds);
    return bounds.Width;
}

TextMetrics textMetrics(Gdiplus::Font& font, const Gdiplus::FontFamily& family, int style) {
    TextMetrics metrics;

    // 字号是以像素给的（UnitPixel），所以 cell 度量要按 em 等比换算成像素
    const float emHeight = static_cast<float>(family.GetEmHeight(style));
    const float pixels = font.GetSize();
    if (emHeight <= 0.0f) {
        metrics.ascent = pixels * 0.8f;   // 兜底，正常不会走到
        metrics.descent = pixels * 0.2f;
        return metrics;
    }

    metrics.ascent = static_cast<float>(family.GetCellAscent(style)) * pixels / emHeight;
    metrics.descent = static_cast<float>(family.GetCellDescent(style)) * pixels / emHeight;
    return metrics;
}

float fitFontPixels(Gdiplus::Graphics& graphics, const Gdiplus::FontFamily& family, int style,
                    float pixels, const std::wstring& text, float maxWidth) {
    Gdiplus::Font probe(&family, pixels, style, Gdiplus::UnitPixel);
    const float width = measureTextWidth(graphics, probe, text);
    if (maxWidth > 0.0f && width > maxWidth) {
        pixels = pixels * maxWidth / width;
    }
    return std::max(6.0f, pixels);
}

void drawCenteredText(Gdiplus::Graphics& graphics, const std::wstring& text, Gdiplus::Font& font,
                      const TextMetrics& metrics, const Gdiplus::Color& color,
                      float centerX, float centerY) {
    if (text.empty()) return;

    // 两处都不能想当然：
    //
    // 1) 必须用 GenericTypographic 这个 format 来画。GDI+ 默认的 StringFormat
    //    会在文字左右各留 1/6 em 的排版留白，量宽度时用的是 GenericTypographic
    //    （没有留白），画的时候用默认的（有留白），左边多出来的那点留白就把
    //    整行字往右推了约 1/6 em —— 46px 的字正好偏 7px，肉眼很明显。
    //    量、画用同一个 format，偏移就没了。
    // 2) 不用 StringAlignmentCenter：GDI+ 的居中按整行盒（含行距）算，和 Tk 的
    //    anchor="center" 对不齐。改成自己算左上角 + Near 对齐。
    const float width = measureTextWidth(graphics, font, text);
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);

    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
                        Gdiplus::PointF(centerX - width / 2.0f,
                                        centerY - metrics.lineSpace() / 2.0f),
                        &format, &brush);
}
