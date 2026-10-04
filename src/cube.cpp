#include "cube.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>

#include <mmsystem.h>   // timeBeginPeriod：把定时器精度提到 1ms，Q 弹才顺

#include "config.h"
#include "json.h"
#include "peak.h"
#include "util.h"

namespace {

constexpr UINT_PTR kTimerTick = 1;          // 每秒刷新峰谷倒计时
constexpr UINT_PTR kTimerAnimation = 2;     // Q 弹动画
constexpr UINT_PTR kTimerRefresh = 3;       // 自动刷新
constexpr UINT_PTR kTimerStartup = 4;       // 首帧之后的第一次刷新
constexpr UINT_PTR kTimerClickRefresh = 5;  // 点击后延迟 90ms 再刷新
constexpr UINT_PTR kTimerSelfTest = 6;      // --self-test 到点退出

constexpr UINT kMessageFetchDone = WM_APP + 1;

constexpr int kMenuRefresh = 1001;
constexpr int kMenuTopmost = 1002;
constexpr int kMenuSound = 1003;
constexpr int kMenuColor = 1004;
constexpr int kMenuQuit = 1005;

constexpr wchar_t kWindowClass[] = L"DeepSeekBalanceCubeWindow";
constexpr wchar_t kWindowTitle[] = L"DeepSeek 余额";

double steadyNow() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Cube::Cube(bool demo, double selfTestSeconds)
    : demo_(demo),
      selfTestSeconds_(selfTestSeconds),
      sound_(kClickSound, kClickSoundVolume),
      pacer_(kRefreshMinSeconds, kRefreshMaxSeconds) {}

Cube::~Cube() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kTimerTick);
        KillTimer(hwnd_, kTimerAnimation);
        KillTimer(hwnd_, kTimerRefresh);
        KillTimer(hwnd_, kTimerStartup);
        KillTimer(hwnd_, kTimerClickRefresh);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClass, instance_);
    }
}

bool Cube::create(HINSTANCE instance) {
    instance_ = instance;
    topmost_ = kAlwaysOnTop;
    colorCycle_ = kAmountColorCycle;

    // 木鱼波形：有缓存文件就直接用，没有才合成。C++ 里合成只要几毫秒，
    // 启动时顺手做完就行，第一次点击不用等。
    sound_.prepare();
    computeMetrics();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Cube::windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 资源 1 就是 ks.ico（见 src/resources/app.rc.in）
    windowClass.hIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) return false;

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | (topmost_ ? WS_EX_TOPMOST : 0),
        kWindowClass, kWindowTitle, WS_POPUP,
        0, 0, totalW_, totalH_, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;

    placeWindow();
    render();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    SetTimer(hwnd_, kTimerTick, 1000, nullptr);
    SetTimer(hwnd_, kTimerStartup, 80, nullptr);
    if (selfTestSeconds_ > 0.0) {
        SetTimer(hwnd_, kTimerSelfTest, static_cast<UINT>(selfTestSeconds_ * 1000.0), nullptr);
    }
    return true;
}

void Cube::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Cube::computeMetrics() {
    int dpi = 96;
    if (HDC screen = GetDC(nullptr)) {
        dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
    }
    if (dpi <= 0) dpi = 96;

    // Tk 那套 scaling/1.3333 换算下来就是 dpi/96；比 100% 还低的时候不缩。
    uiScale_ = std::max(1.0f, static_cast<float>(dpi) / 96.0f);

    pxW_ = static_cast<int>(std::lround(kCardWidth * uiScale_));
    pxH_ = static_cast<int>(std::lround(kCardHeight * uiScale_));
    pxRadius_ = kCornerRadius * uiScale_;
    pxMargin_ = std::max(4, static_cast<int>(std::lround(kOuterMargin * uiScale_)));

    totalW_ = pxW_ + pxMargin_ * 2;
    totalH_ = pxH_ + pxMargin_ * 2;
}

void Cube::placeWindow() {
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) == FALSE) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    int x = 0;
    int y = 0;
    switch (kStartAt) {
        case StartAt::TopLeft:
            x = work.left + kScreenMargin;
            y = work.top + kScreenMargin;
            break;
        case StartAt::BottomLeft:
            x = work.left + kScreenMargin;
            y = work.bottom - totalH_ - kScreenMargin;
            break;
        case StartAt::BottomRight:
            x = work.right - totalW_ - kScreenMargin;
            y = work.bottom - totalH_ - kScreenMargin;
            break;
        case StartAt::TopRight:
        default:
            x = work.right - totalW_ - kScreenMargin;
            y = work.top + kScreenMargin;
            break;
    }

    SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                 std::max(0, x), std::max(0, y), totalW_, totalH_, SWP_NOACTIVATE);
}

// ---------------------------------------------------------------- 绘制
void Cube::render() {
    if (hwnd_ == nullptr) return;
    if (!surface_.ensure(totalW_, totalH_)) return;

    Gdiplus::Graphics* graphics = surface_.graphics();
    if (graphics == nullptr) return;

    graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    // 分层窗口不能用 ClearType（次像素抗锯齿要靠不透明底色），灰阶抗锯齿正好
    graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    graphics->Clear(Gdiplus::Color(0, 0, 0, 0));

    drawCard(*graphics);

    surface_.flush();
    present();
}

void Cube::present() {
    POINT source{0, 0};
    SIZE size{totalW_, totalH_};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    // pptDst 传 nullptr = 位置不动，只更新内容
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, surface_.dc(), &source, 0, &blend,
                        ULW_ALPHA);
}

void Cube::drawCard(Gdiplus::Graphics& graphics) {
    const float scale = scale_;
    const float left = pxMargin_ + pxW_ * (1.0f - scale) / 2.0f;
    const float top = pxMargin_ + pxH_ * (1.0f - scale) / 2.0f;
    const float width = pxW_ * scale;
    const float height = pxH_ * scale;
    const float radius = std::max(2.0f, std::min(pxRadius_ * scale, std::min(width, height) / 2.0f));
    const float zoom = scale * uiScale_;   // 弹性动画缩放 + 高 DPI
    const float centerX = left + width / 2.0f;

    Gdiplus::FontFamily* family = uiFontFamily();

    // ---- 卡片底：先整块填描边色，再把内缩 BORDER_WIDTH 的区域填渐变 ----
    Gdiplus::GraphicsPath outer;
    addRoundedRect(outer, left, top, width, height, radius);
    // 别写成 SolidBrush brush(Color(x))：那会被当成函数声明（最烦人的解析）
    const Gdiplus::Color borderColor(kColorBorderArgb);
    Gdiplus::SolidBrush borderBrush(borderColor);
    graphics.FillPath(&borderBrush, &outer);

    const float border = kBorderWidth * zoom;
    if (width - 2 * border > 0.0f && height - 2 * border > 0.0f) {
        Gdiplus::GraphicsPath inner;
        addRoundedRect(inner, left + border, top + border, width - 2 * border, height - 2 * border,
                       std::max(0.0f, radius - border));
        Gdiplus::LinearGradientBrush gradient(
            Gdiplus::PointF(centerX, top), Gdiplus::PointF(centerX, top + height),
            Gdiplus::Color(kColorTopArgb), Gdiplus::Color(kColorBottomArgb));
        graphics.FillPath(&gradient, &inner);
    }

    // ---- 余额数字：出错时直接把原因写在卡片上，别只给个红杠 ----
    Gdiplus::Color amountColor(kAmountColorList[colorIndex_ % kAmountColorCount]);
    if (!state_.error.empty()) {
        amountColor = Gdiplus::Color(kColorErrorArgb);
    } else if (state_.busy) {
        amountColor = Gdiplus::Color(kColorAmountDimArgb);
    }
    const float amountPixels = fitFontPixels(graphics, *family, Gdiplus::FontStyleBold,
                                             kAmountFontPx * zoom, state_.amount,
                                             width * kTextWidthRatio);
    Gdiplus::Font amountFont(family, amountPixels, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    drawCenteredText(graphics, state_.amount, amountFont,
                     textMetrics(amountFont, *family, Gdiplus::FontStyleBold), amountColor, centerX,
                     top + height * kAmountCenterY);

    // ---- 峰 / 谷 徽标 + 时:分:秒 倒计时 ----
    const auto now = std::chrono::system_clock::now();
    const bool peak = isPeak(now);
    const std::wstring label = peak ? L"峰" : L"谷";
    const std::wstring clock = formatRemaining(
        std::chrono::duration<double>(nextTransition(now) - now).count());

    float pillFontPixels = kPillFontPx * zoom;
    float pad = kPillPadX * zoom;
    float gap = kPillGap * zoom;

    struct PillBox {
        float labelWidth = 0.0f;
        float clockWidth = 0.0f;
        float totalWidth = 0.0f;
        TextMetrics metrics;
    };

    // 宽度按「把数字都换成 8」的等宽版本算：秒数变化时徽标不会抖
    auto measurePill = [&](float pixels) {
        Gdiplus::Font probe(family, pixels, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        PillBox box;
        box.labelWidth = measureTextWidth(graphics, probe, label);
        box.clockWidth = measureTextWidth(graphics, probe, canonicalClock(clock));
        box.totalWidth = box.labelWidth + gap + box.clockWidth + pad * 2.0f;
        box.metrics = textMetrics(probe, *family, Gdiplus::FontStyleBold);
        return box;
    };

    PillBox box = measurePill(pillFontPixels);

    // 长假时倒计时会出现三位数小时，整块徽标等比缩小也要塞进卡片
    const float available = width - 2.0f * kPillSideMargin * zoom;
    if (box.totalWidth > available && available > 0.0f) {
        const float shrink = available / box.totalWidth;
        pad *= shrink;
        gap *= shrink;
        pillFontPixels *= shrink;
        box = measurePill(pillFontPixels);
    }

    const float pillHeight = box.metrics.lineSpace() + 2.0f * kPillPadY * zoom;
    const float pillLeft = centerX - box.totalWidth / 2.0f;
    const float pillCenterY = top + height * kPillCenterY;

    Gdiplus::GraphicsPath pillPath;
    addRoundedRect(pillPath, pillLeft, pillCenterY - pillHeight / 2.0f, box.totalWidth, pillHeight,
                   std::min(box.totalWidth, pillHeight) / 2.0f);
    const Gdiplus::Color pillBackground(peak ? kColorPeakBgArgb : kColorOffBgArgb);
    Gdiplus::SolidBrush pillBrush(pillBackground);
    graphics.FillPath(&pillBrush, &pillPath);

    Gdiplus::Font pillFont(family, pillFontPixels, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    const Gdiplus::Color pillColor(peak ? kColorPeakArgb : kColorOffArgb);
    drawCenteredText(graphics, label, pillFont, box.metrics, pillColor,
                     pillLeft + pad + box.labelWidth / 2.0f, pillCenterY);
    drawCenteredText(graphics, clock, pillFont, box.metrics, pillColor,
                     pillLeft + box.totalWidth - pad - box.clockWidth / 2.0f, pillCenterY);
}

// ---------------------------------------------------------------- 刷新
void Cube::refresh() {
    cancelRefreshTimer();
    if (state_.busy) return;

    const std::wstring apiKey = resolveApiKey();
    if (apiKey.empty() && !demo_) {
        state_.busy = false;
        state_.error = L"缺 Key";
        state_.amount = state_.error;
        render();
        return;
    }

    state_.busy = true;
    render();

    if (demo_) {
        auto* result = new FetchResult();
        result->ok = true;
        result->payload = demoPayload();
        PostMessageW(hwnd_, kMessageFetchDone, 0, reinterpret_cast<LPARAM>(result));
        return;
    }
    startWorker(apiKey);
}

void Cube::startWorker(const std::wstring& apiKey) {
    // 后台线程只做网络请求，绝不碰窗口。结果通过 PostMessage 交给 UI 线程，
    // 谁分配谁释放：投递成功就由 UI 线程 delete，失败就自己删。
    const HWND hwnd = hwnd_;
    std::thread([hwnd, apiKey]() {
        auto* result = new FetchResult(fetchBalance(apiKey));
        if (PostMessageW(hwnd, kMessageFetchDone, 0, reinterpret_cast<LPARAM>(result)) == FALSE) {
            delete result;
        }
    }).detach();
}

void Cube::onFetchDone(FetchResult* result) {
    state_.busy = false;

    if (result->ok) {
        json::Value document;
        if (json::parse(result->payload, document)) {
            state_.balance = firstBalance(document);
        } else {
            state_.balance = "--";
        }
        state_.hasPayload = true;
        state_.amount = toWide(state_.balance);
        state_.error.clear();

        if (colorCycle_ && colorStarted_) {
            // 每刷到一次新数据就换下一个颜色（启动那一次仍显示原来的颜色）
            colorIndex_ = (colorIndex_ + 1) % kAmountColorCount;
        }
        colorStarted_ = true;
    } else {
        state_.error = result->error;
        state_.amount = shortError(result->error);
    }

    render();

    if (kBalanceRefreshSeconds < 0.0) return;   // 负数 = 关掉自动刷新，只留手动点
    scheduleRefresh(nextDelay(result->ok));
}

double Cube::nextDelay(bool fresh) {
    if (kBalanceRefreshSeconds > 0.0) return kBalanceRefreshSeconds;
    if (!fresh) return pacer_.delay();   // 这次没拿到数据，维持当前节奏再来

    double value = 0.0;
    const bool hasValue = parseBalance(state_.balance, value);
    return pacer_.observe(hasValue, value, steadyNow());
}

void Cube::scheduleRefresh(double seconds) {
    const double milliseconds = std::max(1.0, seconds * 1000.0);
    SetTimer(hwnd_, kTimerRefresh, static_cast<UINT>(std::min(milliseconds, 2.0e9)), nullptr);
}

void Cube::cancelRefreshTimer() {
    KillTimer(hwnd_, kTimerRefresh);
}

// ---------------------------------------------------------------- 动画
void Cube::startBounce() {
    animationStart_ = std::chrono::steady_clock::now();
    // 系统默认的定时器精度是 15.6ms，直接 SetTimer(16) 会时而 15.6ms、时而
    // 31ms，弹簧动画看着一顿一顿的。动画期间把精度提到 1ms，结束后立刻还回去。
    if (!animationTimerTight_) {
        timeBeginPeriod(1);
        animationTimerTight_ = true;
    }
    SetTimer(hwnd_, kTimerAnimation, kBounceFrameMs, nullptr);
}

void Cube::onAnimationTick() {
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - animationStart_).count();

    if (elapsed >= kBounceDuration) {
        scale_ = 1.0f;
        KillTimer(hwnd_, kTimerAnimation);
        if (animationTimerTight_) {
            timeEndPeriod(1);
            animationTimerTight_ = false;
        }
        render();
        return;
    }

    // 欠阻尼弹簧：1-幅度 -> 1.0，回弹两下（幅度小，含蓄一点）
    const double progress = elapsed / kBounceDuration;
    scale_ = static_cast<float>(1.0 - kBounceAmplitude * std::exp(-4.2 * progress) *
                                          std::cos(11.0 * progress));
    render();
}

// ---------------------------------------------------------------- 菜单
void Cube::showMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;

    AppendMenuW(menu, MF_STRING, kMenuRefresh, L"刷新");
    AppendMenuW(menu, MF_STRING | (topmost_ ? MF_CHECKED : MF_UNCHECKED), kMenuTopmost, L"置顶");
    AppendMenuW(menu, MF_STRING | (sound_.enabled() ? MF_CHECKED : MF_UNCHECKED), kMenuSound,
                L"点击音效");
    AppendMenuW(menu, MF_STRING | (colorCycle_ ? MF_CHECKED : MF_UNCHECKED), kMenuColor,
                L"颜色轮换");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"退出");

    POINT cursor{};
    GetCursorPos(&cursor);

    // TrackPopupMenu 要求窗口在前台，否则点别处菜单不会消失
    SetForegroundWindow(hwnd_);
    const int command = static_cast<int>(
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                       cursor.x, cursor.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);   // 喂掉菜单留下的空消息，避免卡住前台锁

    handleMenuCommand(command);
}

void Cube::handleMenuCommand(int command) {
    switch (command) {
        case kMenuRefresh:
            // 右键菜单里的刷新：也敲一记木鱼
            sound_.play();
            refresh();
            break;
        case kMenuTopmost:
            topmost_ = !topmost_;
            SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        case kMenuSound:
            sound_.setEnabled(!sound_.enabled());
            if (sound_.enabled()) sound_.play();   // 重新打开时立刻响一声，方便确认
            break;
        case kMenuColor:
            // 关掉就回到第一个颜色，不再变
            colorCycle_ = !colorCycle_;
            if (!colorCycle_) colorIndex_ = 0;
            render();
            break;
        case kMenuQuit:
            quit();
            break;
        default:
            break;
    }
}

void Cube::quit() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
}

// ---------------------------------------------------------------- 消息
LRESULT CALLBACK Cube::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<Cube*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return self->handleMessage(message, wparam, lparam);
    }

    auto* self = reinterpret_cast<Cube*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);

    self->hwnd_ = hwnd;
    return self->handleMessage(message, wparam, lparam);
}

LRESULT Cube::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_TIMER:
            switch (wparam) {
                case kTimerTick:
                    render();   // 重画本身很轻，卡片底图是现算的
                    break;
                case kTimerStartup:
                    KillTimer(hwnd_, kTimerStartup);
                    refresh();
                    break;
                case kTimerRefresh:
                    KillTimer(hwnd_, kTimerRefresh);
                    refresh();
                    break;
                case kTimerClickRefresh:
                    KillTimer(hwnd_, kTimerClickRefresh);
                    refresh();
                    break;
                case kTimerAnimation:
                    onAnimationTick();
                    break;
                case kTimerSelfTest:
                    quit();
                    break;
                default:
                    break;
            }
            return 0;

        case kMessageFetchDone: {
            std::unique_ptr<FetchResult> result(reinterpret_cast<FetchResult*>(lparam));
            onFetchDone(result.get());
            return 0;
        }

        case WM_LBUTTONDOWN:
            SetForegroundWindow(hwnd_);   // 拿到焦点，Esc 才收得到
            SetCapture(hwnd_);
            GetCursorPos(&pressCursor_);
            {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                pressWindow_.x = rect.left;
                pressWindow_.y = rect.top;
            }
            pressed_ = true;
            dragging_ = false;
            return 0;

        case WM_MOUSEMOVE: {
            if (!pressed_) return 0;
            POINT cursor{};
            GetCursorPos(&cursor);
            const int dx = cursor.x - pressCursor_.x;
            const int dy = cursor.y - pressCursor_.y;
            if (std::abs(dx) > 3 || std::abs(dy) > 3) dragging_ = true;
            if (dragging_) {
                SetWindowPos(hwnd_, nullptr, pressWindow_.x + dx, pressWindow_.y + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            // ReleaseCapture 会「同步」给本窗口发一条 WM_CAPTURECHANGED，
            // 而那个分支会把 pressed_ 清掉。所以必须先取值再松捕获，
            // 否则下面这个 if 永远是假 —— 点击就彻底没反应了。
            const bool wasPressed = pressed_;
            const bool wasDragging = dragging_;
            if (GetCapture() == hwnd_) ReleaseCapture();
            pressed_ = false;
            dragging_ = false;
            if (wasPressed && !wasDragging) {
                sound_.play();       // 木鱼「笃」
                startBounce();       // 先 Q 弹，再刷新
                SetTimer(hwnd_, kTimerClickRefresh, 90, nullptr);
            }
            return 0;
        }

        case WM_CAPTURECHANGED:
            pressed_ = false;
            dragging_ = false;
            return 0;

        case WM_RBUTTONUP:
            showMenu();
            return 0;

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                quit();
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            return 1;   // 分层窗口不靠 WM_PAINT，别擦背景

        case WM_CLOSE:
            quit();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}
