#pragma once

#include <cairo.h>
#include <pango/pangocairo.h>

#include <chrono>
#include <string>

#include "config.h"

// 绘制层：只依赖 cairo + Pango，不碰任何 GTK 控件或窗口。
//
// 这样切分是为了能离屏渲染：窗口的 draw 信号和 --render-png 走的是同一个
// paintCard()，两边排版必然一致，出问题也不用靠截图去猜。

// 卡片要画什么。全部由调用方（状态机）喂进来。
struct CardContent {
    std::string amount = "--";   // 余额数字；出错时这里放的是错误短句
    bool hasError = false;       // true 时数字用错误色
    bool busy = false;           // 刷新中：数字用暗色
    unsigned int amountColorArgb = kAmountColorList[0];
    double scale = 1.0;          // Q 弹缩放，1.0 = 不缩
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
};

// 卡片总尺寸（含四周透明留白）。
int cardTotalWidth();
int cardTotalHeight();

// 界面字体：按 config.h 的候选表挑第一个系统里真实存在的族，
// 挑不到就交给 Pango 自己回退。返回的 desc 由调用方 free。
PangoFontDescription* uiFont(double pixels);

// 圆角矩形路径。和 GDI+ 版一样是「追加」语义，调用前路径必须是空的。
void addRoundedRect(cairo_t* cr, double x, double y, double width, double height, double radius);

// ---- 文字度量 ----
//
// 用 logical extents（字形 advance 之和），不用 ink extents：ink 受字形左右
// side bearing 影响，拿它居中会歪。logical 等价于 GDI+ 的 GenericTypographic
// （不带左右各 1/6 em 的排版留白）。

double measureTextWidth(PangoContext* context, PangoFontDescription* font, const std::string& text);

// 等价 Tk 的 linespace 与 GDI+ 的 cell ascent/descent：
// 中线的几何中心 = (ascent + descent) 的中心。
struct TextMetrics {
    double ascent = 0.0;
    double descent = 0.0;

    double lineSpace() const { return ascent + descent; }
};

TextMetrics textMetrics(PangoContext* context, PangoFontDescription* font);

// 字号自适应：文本太宽就等比缩小——字尽量大，但绝不溢出卡片。
double fitFontPixels(PangoContext* context, double pixels, const std::string& text, double maxWidth);

// 以 (centerX, centerY) 为中心画一行文字，语义和 Tk 的 anchor="center" 一致：
// 文字盒（ascent+descent）的几何中心落在给定点上。
void drawCenteredText(cairo_t* cr, PangoContext* context, PangoFontDescription* font,
                      const TextMetrics& metrics, const std::string& text, unsigned int argb,
                      double centerX, double centerY);

// 画一整张卡片。坐标系是逻辑像素，(0,0) 在左上角，画布尺寸 = cardTotalWidth/Height。
// 调用方负责先把背景清成全透明。
void paintCard(cairo_t* cr, PangoContext* context, const CardContent& content);
