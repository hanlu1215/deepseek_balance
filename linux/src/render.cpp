#include "render.h"

#include <algorithm>
#include <cmath>

#include "peak.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// 0xAARRGGBB -> cairo 的 0..1 浮点分量。
void argbToRgba(unsigned int argb, double& r, double& g, double& b, double& a) {
    a = static_cast<double>((argb >> 24) & 0xFF) / 255.0;
    r = static_cast<double>((argb >> 16) & 0xFF) / 255.0;
    g = static_cast<double>((argb >> 8) & 0xFF) / 255.0;
    b = static_cast<double>(argb & 0xFF) / 255.0;
}

void setSourceArgb(cairo_t* cr, unsigned int argb) {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
    argbToRgba(argb, r, g, b, a);
    cairo_set_source_rgba(cr, r, g, b, a);
}

// 挑一个系统里真实存在的字体族。跑一次就缓存住。
const char* uiFontFamily() {
    static std::string chosen;
    static bool resolved = false;
    if (resolved) return chosen.c_str();
    resolved = true;

    PangoContext* context = pango_font_map_create_context(pango_cairo_font_map_get_default());
    PangoFontFamily** families = nullptr;
    int count = 0;
    pango_context_list_families(context, &families, &count);

    for (int candidate = 0; candidate < kFontFamilyCount && chosen.empty(); ++candidate) {
        for (int i = 0; i < count; ++i) {
            const char* name = pango_font_family_get_name(families[i]);
            if (name != nullptr && g_ascii_strcasecmp(name, kFontFamilies[candidate]) == 0) {
                chosen = kFontFamilies[candidate];
                break;
            }
        }
    }

    g_free(families);
    g_object_unref(context);

    // 都没匹配上也不要紧：Pango 自己会回退，给个通用无衬线就行。
    if (chosen.empty()) chosen = kFontFamilies[kFontFamilyCount - 1];
    return chosen.c_str();
}

PangoLayout* makeLayout(PangoContext* context, PangoFontDescription* font, const std::string& text) {
    PangoLayout* layout = pango_layout_new(context);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
    return layout;
}

}  // namespace

int cardTotalWidth() {
    return static_cast<int>(std::lround(kCardWidth + 2.0 * kOuterMargin));
}

int cardTotalHeight() {
    return static_cast<int>(std::lround(kCardHeight + 2.0 * kOuterMargin));
}

PangoFontDescription* uiFont(double pixels) {
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, uiFontFamily());
    pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    // 必须用 absolute_size（设备无关的像素），不能用 set_size（点值）：
    // 点值会再乘一次 font-map 的 resolution，Xft.dpi 被设成 192 时文字会大一倍。
    pango_font_description_set_absolute_size(desc, pixels * PANGO_SCALE);
    return desc;
}

void addRoundedRect(cairo_t* cr, double x, double y, double width, double height, double radius) {
    if (width <= 0.0 || height <= 0.0) return;

    const double r = std::max(0.0, std::min(radius, std::min(width, height) / 2.0));
    if (r <= 0.5) {
        cairo_rectangle(cr, x, y, width, height);
        cairo_close_path(cr);
        return;
    }

    // cairo 的角度：0 在三点钟方向，y 轴朝下，所以正方向看着是顺时针。
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - r, y + r, r, -kPi / 2.0, 0.0);              // 右上
    cairo_arc(cr, x + width - r, y + height - r, r, 0.0, kPi / 2.0);      // 右下
    cairo_arc(cr, x + r, y + height - r, r, kPi / 2.0, kPi);              // 左下
    cairo_arc(cr, x + r, y + r, r, kPi, 3.0 * kPi / 2.0);                 // 左上
    cairo_close_path(cr);
}

double measureTextWidth(PangoContext* context, PangoFontDescription* font, const std::string& text) {
    if (text.empty()) return 0.0;

    PangoLayout* layout = makeLayout(context, font, text);
    PangoRectangle ink{};
    PangoRectangle logical{};
    // 用 get_extents 而不是 get_pixel_extents：前者保留 PANGO_SCALE 精度，
    // 像素版会四舍五入成整数，倒计时跳秒时可能看出 1px 的台阶。
    pango_layout_get_extents(layout, &ink, &logical);
    g_object_unref(layout);

    return static_cast<double>(logical.width) / PANGO_SCALE;
}

TextMetrics textMetrics(PangoContext* context, PangoFontDescription* font) {
    PangoFontMetrics* metrics = pango_context_get_metrics(context, font, nullptr);

    TextMetrics out;
    out.ascent = static_cast<double>(pango_font_metrics_get_ascent(metrics)) / PANGO_SCALE;
    out.descent = static_cast<double>(pango_font_metrics_get_descent(metrics)) / PANGO_SCALE;

    pango_font_metrics_unref(metrics);
    return out;
}

double fitFontPixels(PangoContext* context, double pixels, const std::string& text, double maxWidth) {
    PangoFontDescription* probe = uiFont(pixels);
    const double width = measureTextWidth(context, probe, text);
    pango_font_description_free(probe);

    if (maxWidth > 0.0 && width > maxWidth) {
        pixels = pixels * maxWidth / width;
    }
    return std::max(6.0, pixels);
}

void drawCenteredText(cairo_t* cr, PangoContext* context, PangoFontDescription* font,
                      const TextMetrics& metrics, const std::string& text, unsigned int argb,
                      double centerX, double centerY) {
    if (text.empty()) return;

    PangoLayout* layout = makeLayout(context, font, text);
    const double width = measureTextWidth(context, font, text);

    setSourceArgb(cr, argb);
    // pango_cairo_show_layout 把 layout 逻辑盒的左上角放在当前点，所以这里得自己
    // 按「文字盒中心」算左上角——和 GDI+ 版、和 Tk 的 anchor="center" 完全一致。
    cairo_move_to(cr, centerX - width / 2.0, centerY - metrics.lineSpace() / 2.0);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
}

void paintCard(cairo_t* cr, PangoContext* context, const CardContent& content) {
    const double scale = content.scale;
    const double zoom = scale;   // HiDPI 由 GDK 缩放整块画布，这里不再乘第二遍

    const double left = kOuterMargin + kCardWidth * (1.0 - scale) / 2.0;
    const double top = kOuterMargin + kCardHeight * (1.0 - scale) / 2.0;
    const double width = kCardWidth * scale;
    const double height = kCardHeight * scale;
    const double radius =
        std::max(2.0, std::min(kCornerRadius * scale, std::min(width, height) / 2.0));
    const double centerX = left + width / 2.0;

    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // ---- 卡片底：先整块填描边色，再把内缩 BORDER_WIDTH 的区域填渐变 ----
    addRoundedRect(cr, left, top, width, height, radius);
    setSourceArgb(cr, kColorBorderArgb);
    cairo_fill(cr);

    const double border = kBorderWidth * zoom;
    if (width - 2.0 * border > 0.0 && height - 2.0 * border > 0.0) {
        addRoundedRect(cr, left + border, top + border, width - 2.0 * border, height - 2.0 * border,
                       std::max(0.0, radius - border));

        cairo_pattern_t* gradient =
            cairo_pattern_create_linear(centerX, top, centerX, top + height);
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        double a = 1.0;
        argbToRgba(kColorTopArgb, r, g, b, a);
        cairo_pattern_add_color_stop_rgba(gradient, 0.0, r, g, b, a);
        argbToRgba(kColorBottomArgb, r, g, b, a);
        cairo_pattern_add_color_stop_rgba(gradient, 1.0, r, g, b, a);

        cairo_set_source(cr, gradient);
        cairo_fill(cr);
        cairo_pattern_destroy(gradient);
    }

    // ---- 余额数字：出错时直接把原因写在卡片上，别只给个红杠 ----
    unsigned int amountColor = content.amountColorArgb;
    if (content.hasError) {
        amountColor = kColorErrorArgb;
    } else if (content.busy) {
        amountColor = kColorAmountDimArgb;
    }

    const double amountPixels =
        fitFontPixels(context, kAmountFontPx * zoom, content.amount, width * kTextWidthRatio);
    PangoFontDescription* amountFont = uiFont(amountPixels);
    drawCenteredText(cr, context, amountFont, textMetrics(context, amountFont), content.amount,
                     amountColor, centerX, top + height * kAmountCenterY);
    pango_font_description_free(amountFont);

    // ---- 峰 / 谷 徽标 + 时:分:秒 倒计时 ----
    const bool peak = isPeak(content.now);
    const std::string label = peak ? "峰" : "谷";
    const std::string clock = formatRemaining(
        std::chrono::duration<double>(nextTransition(content.now) - content.now).count());

    double pillFontPixels = kPillFontPx * zoom;
    double pad = kPillPadX * zoom;
    double gap = kPillGap * zoom;

    struct PillBox {
        double labelWidth = 0.0;
        double clockWidth = 0.0;
        double totalWidth = 0.0;
        TextMetrics metrics;
    };

    // 宽度按「把数字都换成 8」的等宽版本算：秒数变化时徽标不会抖
    auto measurePill = [&](double pixels) {
        PangoFontDescription* probe = uiFont(pixels);
        PillBox box;
        box.labelWidth = measureTextWidth(context, probe, label);
        box.clockWidth = measureTextWidth(context, probe, canonicalClock(clock));
        box.totalWidth = box.labelWidth + gap + box.clockWidth + pad * 2.0;
        box.metrics = textMetrics(context, probe);
        pango_font_description_free(probe);
        return box;
    };

    PillBox box = measurePill(pillFontPixels);

    // 长假时倒计时会出现三位数小时，整块徽标等比缩小也要塞进卡片
    const double available = width - 2.0 * kPillSideMargin * zoom;
    if (box.totalWidth > available && available > 0.0) {
        const double shrink = available / box.totalWidth;
        pad *= shrink;
        gap *= shrink;
        pillFontPixels *= shrink;
        box = measurePill(pillFontPixels);
    }

    const double pillHeight = box.metrics.lineSpace() + 2.0 * kPillPadY * zoom;
    const double pillLeft = centerX - box.totalWidth / 2.0;
    const double pillCenterY = top + height * kPillCenterY;

    addRoundedRect(cr, pillLeft, pillCenterY - pillHeight / 2.0, box.totalWidth, pillHeight,
                   std::min(box.totalWidth, pillHeight) / 2.0);
    setSourceArgb(cr, peak ? kColorPeakBgArgb : kColorOffBgArgb);
    cairo_fill(cr);

    PangoFontDescription* pillFont = uiFont(pillFontPixels);
    const unsigned int pillColor = peak ? kColorPeakArgb : kColorOffArgb;
    drawCenteredText(cr, context, pillFont, box.metrics, label, pillColor,
                     pillLeft + pad + box.labelWidth / 2.0, pillCenterY);
    drawCenteredText(cr, context, pillFont, box.metrics, clock, pillColor,
                     pillLeft + box.totalWidth - pad - box.clockWidth / 2.0, pillCenterY);
    pango_font_description_free(pillFont);
}
