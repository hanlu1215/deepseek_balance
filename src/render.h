#pragma once

#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>

// GDI+ 头文件里有些地方直接用不带限定的 min/max，NOMINMAX 之下得先把
// std::min / std::max 引到全局作用域，否则老版本 SDK 编不过。
using std::max;
using std::min;

// gdiplus.h 用到 PROPID，而 PROPID 来自 objidl.h —— WIN32_LEAN_AND_MEAN 会把
// objidl.h 从 windows.h 里剔掉，所以这里必须显式补上（MinGW 尤其躲不过）。
#include <objidl.h>
#include <gdiplus.h>

// 一块 32bpp 预乘 alpha 的内存画布。
//
// 之所以不用 Tk 那套「透明色抠图」：UpdateLayeredWindow + PARGB 能拿到真正的
// 逐像素透明度，圆角边缘是按 alpha 混合的，不会在深色卡片边上留一圈杂色。
class LayeredSurface {
public:
    LayeredSurface() = default;
    ~LayeredSurface();

    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;

    // 尺寸没变就复用，变了就重建。
    bool ensure(int width, int height);

    HDC dc() const { return memDC_; }
    Gdiplus::Graphics* graphics() const { return graphics_.get(); }
    int width() const { return width_; }
    int height() const { return height_; }

    void flush() {
        if (graphics_) graphics_->Flush(Gdiplus::FlushIntentionSync);
    }

private:
    void release();

    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::unique_ptr<Gdiplus::Bitmap> bitmap_;
    std::unique_ptr<Gdiplus::Graphics> graphics_;
};

// 界面字体（带回退：微软雅黑 UI -> 微软雅黑 -> Segoe UI -> 宋体 -> Arial）。
Gdiplus::FontFamily* uiFontFamily();

// 往 path 里追加一个圆角矩形。path 必须是空的（AddArc 是追加语义）。
void addRoundedRect(Gdiplus::GraphicsPath& path, float x, float y, float width, float height,
                    float radius);

// 量一行文字的宽度（排版用，不做换行）。
float measureTextWidth(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text);

// Tk 那套字体度量：ascent / descent 取字体的 cell 度量，linespace = ascent + descent。
//
// 必须用它而不是 GDI+ 的 Font::GetHeight()：GetHeight 把行距（leading）也算进去，
// 盒高比 Tk 的 linespace 大一截，拿它居中会让文字整体偏上、徽标也比 Python 版高。
struct TextMetrics {
    float ascent = 0.0f;
    float descent = 0.0f;

    float lineSpace() const { return ascent + descent; }
};

TextMetrics textMetrics(Gdiplus::Font& font, const Gdiplus::FontFamily& family, int style);

// 字号自适应：文本太宽就等比缩小——字尽量大，但绝不溢出卡片。
float fitFontPixels(Gdiplus::Graphics& graphics, const Gdiplus::FontFamily& family, int style,
                    float pixels, const std::wstring& text, float maxWidth);

// 以 (centerX, centerY) 为中心画一行文字，语义和 Tk 的 anchor="center" 完全一致：
// 文字盒（ascent+descent）的几何中心落在给定点上。
void drawCenteredText(Gdiplus::Graphics& graphics, const std::wstring& text, Gdiplus::Font& font,
                      const TextMetrics& metrics, const Gdiplus::Color& color,
                      float centerX, float centerY);
