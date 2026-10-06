#include "cube.h"

#include <algorithm>
#include <cmath>

#include "config.h"
#include "json.h"
#include "peak.h"
#include "util.h"

namespace {

// 后台线程的任务包。谁分配谁释放：投递成功就由 g_idle_add_full 的 destroy notify
// 释放，投递失败则当场释放。
struct FetchJob {
    GWeakRef windowRef;   // 弱引用：窗口没了就拿到 NULL，不会 use-after-free
    std::string apiKey;
    FetchResult result;
};

void fetchJobFree(gpointer data) {
    FetchJob* job = static_cast<FetchJob*>(data);
    g_weak_ref_clear(&job->windowRef);
    delete job;
}

gboolean onFetchIdle(gpointer data) {
    FetchJob* job = static_cast<FetchJob*>(data);

    GtkWidget* window = static_cast<GtkWidget*>(g_weak_ref_get(&job->windowRef));
    if (window == nullptr) return G_SOURCE_REMOVE;   // 窗口已经销毁了

    auto* cube = static_cast<Cube*>(g_object_get_data(G_OBJECT(window), "cube"));
    if (cube != nullptr) cube->onFetchDone(job->result);

    g_object_unref(window);
    return G_SOURCE_REMOVE;
}

// 后台线程：只做网络请求，绝不碰任何 GTK/GDK 对象。
gpointer fetchWorker(gpointer data) {
    FetchJob* job = static_cast<FetchJob*>(data);
    job->result = fetchBalance(job->apiKey);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, onFetchIdle, job, fetchJobFree);
    return nullptr;
}

}  // namespace

Cube::Cube(bool demo, double selfTestSeconds)
    : demo_(demo),
      selfTestSeconds_(selfTestSeconds),
      sound_(kClickSound, kClickSoundVolume),
      pacer_(kRefreshMinSeconds, kRefreshMaxSeconds) {}

Cube::~Cube() {
    // 走到这里时窗口通常已经销毁过了（gtk_main 是在 "destroy" 里退出的），
    // 所以句柄要先判空；还活着就顺手收掉。
    if (tickTimer_ != 0) g_source_remove(tickTimer_);
    if (startupTimer_ != 0) g_source_remove(startupTimer_);
    if (refreshTimer_ != 0) g_source_remove(refreshTimer_);
    if (clickTimer_ != 0) g_source_remove(clickTimer_);
    if (selfTestTimer_ != 0) g_source_remove(selfTestTimer_);
    closeMenu();   // 菜单还开着就先收掉，别留下抓取
    if (window_ != nullptr) gtk_widget_destroy(window_);
}

bool Cube::create() {
    topmost_ = kAlwaysOnTop;
    colorCycle_ = kAmountColorCycle;

    // 木鱼波形几毫秒就合成完了，启动时顺手做完，第一次点击不用等。
    sound_.prepare();

    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (window_ == nullptr) return false;

    g_object_set_data(G_OBJECT(window_), "cube", this);

    // ---- 逐像素半透明的无边框窗口 ----
    // app_paintable 关掉 GTK 的默认背景填充，RGBA visual 才能让圆角外的区域真正透明。
    gtk_widget_set_app_paintable(window_, TRUE);
    GdkScreen* screen = gtk_widget_get_screen(window_);
    GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
    if (visual != nullptr) {
        gtk_widget_set_visual(window_, visual);
    } else {
        // 没有合成器就只能不透明，卡片会带黑底。
        g_warning("当前显示环境不支持 RGBA visual（多半是没有合成器），窗口将不透明。");
    }

    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    // UTILITY 而不是 NORMAL：配合 skip_taskbar，让它像 Windows 版的
    // WS_EX_TOOLWINDOW 一样不出现在任务栏和 Alt-Tab 里。
    gtk_window_set_type_hint(GTK_WINDOW(window_), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window_), topmost_);
    gtk_window_stick(GTK_WINDOW(window_));   // 所有工作区都可见
    // 对应 Win32 的 SW_SHOWNOACTIVATE：显示时不抢焦点，但点它还是能拿到焦点（Esc 要用）。
    gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window_), TRUE);
    gtk_window_set_gravity(GTK_WINDOW(window_), GDK_GRAVITY_STATIC);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_NONE);   // 别让 WM 覆盖我们的定位

    const int totalWidth = cardTotalWidth();
    const int totalHeight = cardTotalHeight();
    gtk_window_set_default_size(GTK_WINDOW(window_), totalWidth, totalHeight);
    gtk_widget_set_size_request(window_, totalWidth, totalHeight);

    // 不注册这些掩码就收不到 motion，拖动会「跳」而不是跟手。
    gtk_widget_add_events(window_, GDK_POINTER_MOTION_MASK | GDK_BUTTON1_MOTION_MASK |
                                      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                      GDK_KEY_PRESS_MASK | GDK_STRUCTURE_MASK);
    gtk_widget_set_can_focus(window_, TRUE);

    g_signal_connect(window_, "draw", G_CALLBACK(onDraw), this);
    g_signal_connect(window_, "button-press-event", G_CALLBACK(onButtonPress), this);
    g_signal_connect(window_, "button-release-event", G_CALLBACK(onButtonRelease), this);
    g_signal_connect(window_, "motion-notify-event", G_CALLBACK(onMotion), this);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(onKeyPress), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(onDestroy), this);

    pango_ = gtk_widget_get_pango_context(window_);
    if (pango_ != nullptr) {
        // 半透明窗口上不能用次像素抗锯齿（ClearType 那一套要靠不透明底色），灰阶正好。
        cairo_font_options_t* options = cairo_font_options_create();
        cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
        pango_cairo_context_set_font_options(pango_, options);
        cairo_font_options_destroy(options);
    }

    loadIcon();

    gtk_widget_realize(window_);
    gtk_widget_show(window_);
    placeWindow();

    // 必须在 realize 之后：输入区域是设在 GdkWindow 上的。
    applyInputShape();

    tickTimer_ = g_timeout_add(1000, onTickTimeout, this);
    startupTimer_ = g_timeout_add(80, onStartupTimeout, this);
    if (selfTestSeconds_ > 0.0) {
        selfTestTimer_ =
            g_timeout_add(static_cast<guint>(selfTestSeconds_ * 1000.0), onSelfTestTimeout, this);
    }
    return true;
}

void Cube::run() {
    gtk_main();
}

void Cube::loadIcon() {
    // 图标是仓库根目录那个 ks.ico，gdk-pixbuf 自带 ICO 加载器，不需要另做 PNG。
    // 先按「可执行文件的位置」往上找（build/bin -> build -> linux/），这样不管从哪个
    // 目录启动都能找到；再退回当前目录和它的上一级（在仓库里就地跑的情况）。
    // 全都找不到就算了，不报错 —— 没图标不影响运行。
    const std::string beside = executableDir();
    const std::string candidates[] = {
        beside + "ks.ico",           // build/bin/ks.ico
        beside + "../ks.ico",        // build/ks.ico
        beside + "../../ks.ico",     // linux/ks.ico  ← linux/ 被单独拷走时的位置
        beside + "../../../ks.ico",  // 仓库根 ks.ico ← 在仓库里就地构建时的位置
        "ks.ico",                    // 当前目录
        "../ks.ico",
    };

    for (const std::string& path : candidates) {
        if (path.empty()) continue;
        GError* error = nullptr;
        if (gtk_window_set_icon_from_file(GTK_WINDOW(window_), path.c_str(), &error)) return;
        g_clear_error(&error);
    }
}

void Cube::placeWindow() {
    GdkDisplay* display = gtk_widget_get_display(window_);
    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    if (monitor == nullptr && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (monitor == nullptr) return;   // 拿不到显示器信息就别乱挪，交给 WM

    GdkRectangle work = {0, 0, 0, 0};
    gdk_monitor_get_workarea(monitor, &work);

    const int width = cardTotalWidth();
    const int height = cardTotalHeight();

    int x = 0;
    int y = 0;
    switch (kStartAt) {
        case StartAt::TopLeft:
            x = work.x + kScreenMargin;
            y = work.y + kScreenMargin;
            break;
        case StartAt::BottomLeft:
            x = work.x + kScreenMargin;
            y = work.y + work.height - height - kScreenMargin;
            break;
        case StartAt::BottomRight:
            x = work.x + work.width - width - kScreenMargin;
            y = work.y + work.height - height - kScreenMargin;
            break;
        case StartAt::TopRight:
        default:
            x = work.x + work.width - width - kScreenMargin;
            y = work.y + kScreenMargin;
            break;
    }

    gtk_window_move(GTK_WINDOW(window_), std::max(0, x), std::max(0, y));
}

void Cube::applyInputShape() {
    // 透明的那圈留白在 X11 上照样会吃掉鼠标事件（ARGB 透明不产生输入空洞），
    // 那样点卡片旁边的下层窗口就没反应了。把可点区域裁成卡片本身的圆角形状。
    const int width = cardTotalWidth();
    const int height = cardTotalHeight();

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }

    cairo_t* cr = cairo_create(surface);
    addRoundedRect(cr, kOuterMargin, kOuterMargin, kCardWidth, kCardHeight, kCornerRadius);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);
    cairo_destroy(cr);

    cairo_region_t* region = gdk_cairo_region_create_from_surface(surface);
    cairo_surface_destroy(surface);
    if (region == nullptr) return;

    gtk_widget_input_shape_combine_region(window_, region);
    cairo_region_destroy(region);
}

void Cube::render() {
    if (window_ == nullptr) return;
    gtk_widget_queue_draw(window_);
}

// ---------------------------------------------------------------- 绘制
gboolean Cube::onDraw(GtkWidget* widget, cairo_t* cr, gpointer self) {
    (void)widget;
    auto* cube = static_cast<Cube*>(self);

    // 先整车清成全透明。不用 CAIRO_OPERATOR_SOURCE 的话，上一帧的东西会留下来。
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (cube->pango_ == nullptr) return FALSE;

    CardContent content;
    content.amount = cube->state_.amount;
    content.hasError = !cube->state_.error.empty();
    content.busy = cube->state_.busy;
    content.amountColorArgb = kAmountColorList[cube->colorIndex_ % kAmountColorCount];
    content.scale = cube->scale_;
    content.now = std::chrono::system_clock::now();

    paintCard(cr, cube->pango_, content);
    return FALSE;
}

// ---------------------------------------------------------------- 刷新
void Cube::refresh() {
    cancelRefreshTimer();
    if (state_.busy) return;

    const std::string apiKey = resolveApiKey();
    if (apiKey.empty() && !demo_) {
        state_.busy = false;
        state_.error = "缺 Key";
        state_.amount = state_.error;
        render();
        return;
    }

    state_.busy = true;
    render();

    if (demo_) {
        FetchResult result;
        result.ok = true;
        result.payload = demoPayload();
        onFetchDone(result);
        return;
    }
    startWorker(apiKey);
}

void Cube::startWorker(const std::string& apiKey) {
    auto* job = new FetchJob();
    g_weak_ref_init(&job->windowRef, window_);
    job->apiKey = apiKey;
    g_thread_new("balance-fetch", fetchWorker, job);
}

void Cube::onFetchDone(const FetchResult& result) {
    state_.busy = false;

    if (result.ok) {
        json::Value document;
        if (json::parse(result.payload, document)) {
            state_.balance = firstBalance(document);
        } else {
            state_.balance = "--";
        }
        state_.hasPayload = true;
        state_.amount = state_.balance;
        state_.error.clear();

        if (colorCycle_ && colorStarted_) {
            // 每刷到一次新数据就换下一个颜色（启动那一次仍显示原来的颜色）
            colorIndex_ = (colorIndex_ + 1) % kAmountColorCount;
        }
        colorStarted_ = true;
    } else {
        state_.error = result.error;
        state_.amount = shortError(result.error);
    }

    render();

    if (kBalanceRefreshSeconds < 0.0) return;   // 负数 = 关掉自动刷新，只留手动点
    scheduleRefresh(nextDelay(result.ok));
}

double Cube::nextDelay(bool fresh) {
    if (kBalanceRefreshSeconds > 0.0) return kBalanceRefreshSeconds;
    if (!fresh) return pacer_.delay();   // 这次没拿到数据，维持当前节奏再来

    double value = 0.0;
    const bool hasValue = parseBalance(state_.balance, value);
    const double now = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    return pacer_.observe(hasValue, value, now);
}

void Cube::scheduleRefresh(double seconds) {
    cancelRefreshTimer();
    const double milliseconds = std::max(1.0, seconds * 1000.0);
    refreshTimer_ =
        g_timeout_add(static_cast<guint>(std::min(milliseconds, 2.0e9)), onRefreshTimeout, this);
}

void Cube::cancelRefreshTimer() {
    if (refreshTimer_ != 0) {
        g_source_remove(refreshTimer_);
        refreshTimer_ = 0;
    }
}

// ---------------------------------------------------------------- 动画
void Cube::startBounce() {
    animationStart_ = std::chrono::steady_clock::now();

    // 用帧时钟而不是 g_timeout_add(16)：帧时钟跟着 vsync 走、由合成器节流，
    // 窗口不可见时还会自动停。Windows 版得靠 timeBeginPeriod(1) 硬压定时器精度，
    // 这里天然就没那个问题。
    if (animationCallback_ != 0) {
        gtk_widget_remove_tick_callback(window_, animationCallback_);
        animationCallback_ = 0;
    }
    animationCallback_ = gtk_widget_add_tick_callback(window_, onAnimationTick, this, nullptr);
    render();
}

gboolean Cube::onAnimationTick(GtkWidget* widget, GdkFrameClock* clock, gpointer self) {
    (void)widget;
    (void)clock;
    auto* cube = static_cast<Cube*>(self);

    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - cube->animationStart_)
                               .count();

    if (elapsed >= kBounceDuration) {
        cube->scale_ = 1.0;
        cube->animationCallback_ = 0;
        cube->render();
        return G_SOURCE_REMOVE;
    }

    // 欠阻尼弹簧：1-幅度 -> 1.0，回弹两下（幅度小，含蓄一点）
    const double progress = elapsed / kBounceDuration;
    cube->scale_ = 1.0 - kBounceAmplitude * std::exp(-4.2 * progress) * std::cos(11.0 * progress);
    cube->render();
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------- 菜单
//
// 这里**故意不用 GtkMenu**，原因值得写下来：
//
// GtkMenu 弹出时会做一次 gdk_seat_grab(..., owner_events = FALSE)，也就是
// XGrabPointer 且 owner_events 为假。在本机的 Wayland + XWayland 组合下，这种抓取
// 一旦生效，菜单项就再也收不到能触发激活的点击 —— 菜单能弹出、能高亮、点一下也会
// 关闭，但 "activate" 永远不发（GtkMenu 内部的 state 走不到激活分支）。
// 实测：连一个教科书式的最小 GTK 程序也一样坏。
//
// 换成自定义弹出窗口，指针抓取改用 owner_events = TRUE：
//   · 点在菜单里 -> 事件正常送给按钮，"clicked" 照常触发；
//   · 点在菜单外 -> 事件同样报给弹窗（应用内、应用外都报），据此关闭菜单。
// 两个行为都实测通过，顺带还能把菜单配色和卡片统一起来。

namespace {

// 菜单样式：跟着卡片的深色配色走，不用系统主题的浅色。
constexpr const char* kMenuCss =
    ".dsb-menu {"
    "  background-color: #22242A;"
    "  border: 1px solid #4E5460;"
    "  border-radius: 10px;"
    "  padding: 5px;"
    "}"
    ".dsb-menu button {"
    "  background-image: none;"
    "  background-color: transparent;"
    "  border: none;"
    "  box-shadow: none;"
    "  text-shadow: none;"
    "  border-radius: 6px;"
    "  padding: 6px 10px;"
    "  color: #E8ECF2;"
    "}"
    ".dsb-menu button:hover { background-color: rgba(255,255,255,0.10); }"
    ".dsb-menu button:active { background-color: rgba(255,255,255,0.16); }"
    ".dsb-menu button:disabled { color: #6B7280; }"
    ".dsb-menu separator { background-color: #4E5460; margin: 4px 2px; }";

void installMenuCss() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, kMenuCss, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

}  // namespace

GtkWidget* Cube::addMenuItem(GtkWidget* box, const char* text, bool checked, GCallback callback) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

    // 左边留一格放勾：不管勾没勾，文字都从同一列开始，不会左右跳。
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* check = gtk_label_new(checked ? "✓" : "");
    gtk_widget_set_size_request(check, 12, -1);
    gtk_widget_set_halign(check, GTK_ALIGN_START);

    GtkWidget* label = gtk_label_new(text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), row);

    g_signal_connect(button, "clicked", callback, this);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);
    return button;
}

void Cube::showMenu(GdkEventButton* event) {
    closeMenu();

    GtkWidget* popup = gtk_window_new(GTK_WINDOW_POPUP);
    if (popup == nullptr) return;

    // POPUP = override-redirect：不经过窗口管理器，不受「窗口位置由合成器说了算」
    // 那一套限制，也就不会像 GtkPopover 那样被 172x136 的主窗口裁掉。
    gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(popup), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "dsb-menu");
    gtk_container_add(GTK_CONTAINER(popup), box);

    addMenuItem(box, "刷新", false, G_CALLBACK(onMenuRefresh));
    addMenuItem(box, "置顶", topmost_, G_CALLBACK(onMenuTopmost));
    GtkWidget* soundItem = addMenuItem(box, "点击音效", sound_.enabled(), G_CALLBACK(onMenuSound));
    addMenuItem(box, "颜色轮换", colorCycle_, G_CALLBACK(onMenuColor));
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    addMenuItem(box, "退出", false, G_CALLBACK(onMenuQuit));

    // 系统里一个播放器都没有，这一项就别让人点了
    gtk_widget_set_sensitive(soundItem, sound_.available());

    installMenuCss();
    gtk_widget_show_all(popup);

    // 尺寸要在 show 之后才准（否则拿到的是 0）
    gint width = 0;
    gint height = 0;
    gtk_window_get_size(GTK_WINDOW(popup), &width, &height);
    if (width <= 0 || height <= 0) {
        GtkRequisition request;
        gtk_widget_get_preferred_size(popup, nullptr, &request);
        width = request.width;
        height = request.height;
    }

    // 贴着鼠标弹出，但别越过工作区边缘
    GdkDisplay* display = gtk_widget_get_display(window_);
    GdkMonitor* monitor =
        gdk_display_get_monitor_at_point(display, (gint)event->x_root, (gint)event->y_root);
    if (monitor == nullptr) monitor = gdk_display_get_primary_monitor(display);

    gint x = (gint)event->x_root;
    gint y = (gint)event->y_root;
    GdkRectangle work = {0, 0, 0, 0};
    if (monitor != nullptr) {
        gdk_monitor_get_workarea(monitor, &work);
        if (work.width > 0 && work.height > 0) {
            x = CLAMP(x, work.x, MAX(work.x, work.x + work.width - width));
            y = CLAMP(y, work.y, MAX(work.y, work.y + work.height - height));
        }
    }
    gtk_window_move(GTK_WINDOW(popup), x, y);

    menu_ = popup;
    // 菜单开着的时候，gtk_grab_add 会把事件（包括按键）都路由到弹窗上来，
    // 所以 Esc 得由弹窗自己接 —— 光靠主窗口那个处理器收不到。
    gtk_widget_add_events(popup, GDK_KEY_PRESS_MASK);
    g_signal_connect(popup, "button-press-event", G_CALLBACK(onMenuButtonPress), this);
    g_signal_connect(popup, "key-press-event", G_CALLBACK(onMenuKeyPress), this);
    g_signal_connect(popup, "destroy", G_CALLBACK(onMenuDestroy), this);

    // 抓指针。owner_events 必须是 TRUE：
    //   TRUE  -> 菜单里的点击照常送给按钮；菜单外的点击也报给弹窗，我们据此关闭。
    //   FALSE -> 连菜单项都点不动，这正是 GtkMenu 在这台机器上坏掉的原因。
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat != nullptr) {
        gdk_seat_grab(seat, gtk_widget_get_window(popup), GDK_SEAT_CAPABILITY_POINTER, TRUE,
                      nullptr, nullptr, nullptr, nullptr);
    }
    gtk_grab_add(popup);
}

gboolean Cube::onMenuButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer self) {
    auto* cube = static_cast<Cube*>(self);

    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;
    gtk_window_get_position(GTK_WINDOW(widget), &x, &y);
    gtk_window_get_size(GTK_WINDOW(widget), &width, &height);

    const gboolean inside = event->x_root >= x && event->x_root < x + width &&
                            event->y_root >= y && event->y_root < y + height;
    if (!inside) {
        // 点到菜单外面：收掉菜单，并且把这一下吃掉 —— 菜单该有的行为就是
        // 「第一下只用来关菜单」，不该同时作用到下面的窗口。
        cube->closeMenu();
        return TRUE;
    }
    return FALSE;   // 落在菜单里，交给按钮
}

gboolean Cube::onMenuKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer self) {
    (void)widget;
    if (event->keyval != GDK_KEY_Escape) return FALSE;
    static_cast<Cube*>(self)->closeMenu();
    return TRUE;
}

void Cube::onMenuDestroy(GtkWidget* widget, gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    if (cube->menu_ == widget) cube->menu_ = nullptr;
}

void Cube::closeMenu() {
    if (menu_ == nullptr) return;

    // 先断引用再销毁：destroy 回调里会看 menu_，别让它重入。
    GtkWidget* popup = menu_;
    menu_ = nullptr;

    GdkSeat* seat = gdk_display_get_default_seat(gtk_widget_get_display(popup));
    if (seat != nullptr) gdk_seat_ungrab(seat);
    gtk_grab_remove(popup);
    gtk_widget_destroy(popup);
}

void Cube::onMenuRefresh(GtkButton* button, gpointer self) {
    (void)button;
    auto* cube = static_cast<Cube*>(self);
    cube->closeMenu();
    cube->sound_.play();   // 右键菜单里的刷新：也敲一记木鱼
    cube->refresh();
}

void Cube::onMenuTopmost(GtkButton* button, gpointer self) {
    (void)button;
    auto* cube = static_cast<Cube*>(self);
    cube->closeMenu();
    cube->topmost_ = !cube->topmost_;
    if (cube->window_ != nullptr) {
        gtk_window_set_keep_above(GTK_WINDOW(cube->window_), cube->topmost_);
    }
}

void Cube::onMenuSound(GtkButton* button, gpointer self) {
    (void)button;
    auto* cube = static_cast<Cube*>(self);
    cube->closeMenu();
    cube->sound_.setEnabled(!cube->sound_.enabled());
    if (cube->sound_.enabled()) cube->sound_.play();   // 重新打开时立刻响一声，方便确认
}

void Cube::onMenuColor(GtkButton* button, gpointer self) {
    (void)button;
    auto* cube = static_cast<Cube*>(self);
    cube->closeMenu();
    cube->colorCycle_ = !cube->colorCycle_;
    if (!cube->colorCycle_) cube->colorIndex_ = 0;   // 关掉就回到第一个颜色
    cube->render();
}

void Cube::onMenuQuit(GtkButton* button, gpointer self) {
    (void)button;
    static_cast<Cube*>(self)->quit();
}

void Cube::quit() {
    closeMenu();
    if (window_ != nullptr) gtk_widget_destroy(window_);
}

// ---------------------------------------------------------------- 信号
gboolean Cube::onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer self) {
    auto* cube = static_cast<Cube*>(self);

    if (event->button == GDK_BUTTON_SECONDARY) {
        // 只记下来，等松手时再弹菜单。
        //
        // 千万别在这里直接弹：按钮按着的时候 X11 有一个「隐式指针抓取」还生效着，
        // 菜单此时去 XGrabPointer 拿不到干净状态 —— 实测表现为菜单能弹出、能高亮，
        // 但点菜单项只关闭不触发（GTK 内部的 in_click 状态被卡住）。
        gtk_widget_grab_focus(widget);
        cube->pressedSecondary_ = true;
        return TRUE;
    }
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;

    gtk_widget_grab_focus(widget);   // 拿到焦点，Esc 才收得到
    gtk_window_get_position(GTK_WINDOW(widget), &cube->pressWindowX_, &cube->pressWindowY_);
    cube->pressRootX_ = event->x_root;
    cube->pressRootY_ = event->y_root;
    cube->pressed_ = true;
    cube->dragging_ = false;
    return TRUE;
}

gboolean Cube::onMotion(GtkWidget* widget, GdkEventMotion* event, gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    if (!cube->pressed_) return FALSE;

    // 按下按钮时 X server 会给这个窗口一个隐式指针抓取，指针移出窗口后
    // motion/release 仍然发给我们，所以不需要显式 grab。
    const double dx = event->x_root - cube->pressRootX_;
    const double dy = event->y_root - cube->pressRootY_;
    if (std::abs(dx) > 3.0 || std::abs(dy) > 3.0) cube->dragging_ = true;

    if (cube->dragging_) {
        gtk_window_move(GTK_WINDOW(widget), static_cast<int>(std::lround(cube->pressWindowX_ + dx)),
                        static_cast<int>(std::lround(cube->pressWindowY_ + dy)));
    }
    return TRUE;
}

gboolean Cube::onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer self) {
    auto* cube = static_cast<Cube*>(self);

    if (event->button == GDK_BUTTON_SECONDARY) {
        const bool wasPressed = cube->pressedSecondary_;
        cube->pressedSecondary_ = false;
        if (wasPressed) cube->showMenu(event);
        return TRUE;
    }
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    (void)widget;
    const bool clicked = cube->pressed_ && !cube->dragging_;
    cube->pressed_ = false;
    cube->dragging_ = false;

    if (clicked) {
        cube->sound_.play();       // 木鱼「笃」
        cube->startBounce();       // 先 Q 弹，再刷新
        if (cube->clickTimer_ != 0) g_source_remove(cube->clickTimer_);
        cube->clickTimer_ = g_timeout_add(90, onClickRefreshTimeout, cube);
    }
    return TRUE;
}

gboolean Cube::onKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer self) {
    (void)widget;
    if (event->keyval != GDK_KEY_Escape) return FALSE;

    auto* cube = static_cast<Cube*>(self);
    // 菜单开着的时候 Esc 先收菜单（弹出窗口不抢键盘焦点，按键还是发到主窗口来），
    // 再按一次才退出。
    if (cube->menu_ != nullptr) {
        cube->closeMenu();
        return TRUE;
    }
    cube->quit();
    return TRUE;
}

void Cube::onDestroy(GtkWidget* widget, gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    // 先把句柄断掉：后台线程的 idle 回调醒来时会靠它判断「还该不该干活」。
    g_object_set_data(G_OBJECT(widget), "cube", nullptr);
    cube->window_ = nullptr;
    cube->pango_ = nullptr;
    gtk_main_quit();
}

// ---------------------------------------------------------------- 定时器
gboolean Cube::onTickTimeout(gpointer self) {
    // 重画本身很轻，卡片底图是现算的
    static_cast<Cube*>(self)->render();
    return G_SOURCE_CONTINUE;
}

gboolean Cube::onStartupTimeout(gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    cube->startupTimer_ = 0;
    cube->refresh();
    return G_SOURCE_REMOVE;
}

gboolean Cube::onRefreshTimeout(gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    cube->refreshTimer_ = 0;
    cube->refresh();
    return G_SOURCE_REMOVE;
}

gboolean Cube::onClickRefreshTimeout(gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    cube->clickTimer_ = 0;
    cube->refresh();
    return G_SOURCE_REMOVE;
}

gboolean Cube::onSelfTestTimeout(gpointer self) {
    auto* cube = static_cast<Cube*>(self);
    cube->selfTestTimer_ = 0;
    cube->quit();
    return G_SOURCE_REMOVE;
}
