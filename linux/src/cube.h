#pragma once

#include <gtk/gtk.h>

#include <chrono>
#include <string>

#include "balance.h"
#include "pacer.h"
#include "render.h"
#include "sound.h"

// 悬浮小方块本体：窗口、交互、状态机、绘制编排。
// 结构和 Windows 版（../src/cube.h）一一对应，只是消息循环换成了 GTK 的信号 + 定时器。
class Cube {
public:
    Cube(bool demo, double selfTestSeconds);
    ~Cube();

    Cube(const Cube&) = delete;
    Cube& operator=(const Cube&) = delete;

    bool create();
    void run();

    // 后台线程取完数据后，通过 g_idle_add 回到主线程调这个。
    // 必须公开：投递用的空闲回调是文件作用域的普通函数。
    void onFetchDone(const FetchResult& result);

private:
    struct State {
        bool hasPayload = false;
        std::string balance = "--";   // 原始余额字符串，给节奏器用
        std::string amount = "--";    // 卡片上显示的文字
        std::string error;
        bool busy = false;
    };

    void placeWindow();
    void render();
    void applyInputShape();
    void loadIcon();

    void refresh();
    void startWorker(const std::string& apiKey);
    double nextDelay(bool fresh);
    void scheduleRefresh(double seconds);
    void cancelRefreshTimer();

    void startBounce();
    void showMenu(GdkEventButton* event);
    void closeMenu();
    GtkWidget* addMenuItem(GtkWidget* box, const char* text, bool checked, GCallback callback);
    void quit();

    // ---- GTK 信号回调（都转发到同名/相近的成员函数上）----
    static gboolean onDraw(GtkWidget* widget, cairo_t* cr, gpointer self);
    static gboolean onButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer self);
    static gboolean onButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer self);
    static gboolean onMotion(GtkWidget* widget, GdkEventMotion* event, gpointer self);
    static gboolean onKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer self);
    static void onDestroy(GtkWidget* widget, gpointer self);

    // ---- 定时器回调 ----
    static gboolean onTickTimeout(gpointer self);         // 每秒重绘峰谷倒计时
    static gboolean onStartupTimeout(gpointer self);       // 首帧之后的第一次刷新
    static gboolean onRefreshTimeout(gpointer self);       // 自动刷新
    static gboolean onClickRefreshTimeout(gpointer self);  // 点击后延迟 90ms 再刷新
    static gboolean onSelfTestTimeout(gpointer self);      // --self-test 到点退出
    static gboolean onAnimationTick(GtkWidget* widget, GdkFrameClock* clock, gpointer self);

    // ---- 右键菜单（自定义弹出窗口，不用 GtkMenu，原因见 cube.cpp）----
    static gboolean onMenuButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer self);
    static gboolean onMenuKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer self);
    static void onMenuDestroy(GtkWidget* widget, gpointer self);
    static void onMenuRefresh(GtkButton* button, gpointer self);
    static void onMenuTopmost(GtkButton* button, gpointer self);
    static void onMenuSound(GtkButton* button, gpointer self);
    static void onMenuColor(GtkButton* button, gpointer self);
    static void onMenuQuit(GtkButton* button, gpointer self);

    GtkWidget* window_ = nullptr;
    PangoContext* pango_ = nullptr;
    GtkWidget* menu_ = nullptr;   // 打开着的右键菜单弹窗，没有就是 nullptr

    bool demo_ = false;
    double selfTestSeconds_ = 0.0;

    State state_;
    ClickSound sound_;
    RefreshPacer pacer_;

    bool topmost_ = true;
    bool colorCycle_ = true;
    int colorIndex_ = 0;
    bool colorStarted_ = false;   // 第一次拿到的余额仍用原色

    // 弹性动画
    double scale_ = 1.0;
    std::chrono::steady_clock::time_point animationStart_{};
    guint animationCallback_ = 0;

    // 定时器句柄
    guint tickTimer_ = 0;
    guint startupTimer_ = 0;
    guint refreshTimer_ = 0;
    guint clickTimer_ = 0;
    guint selfTestTimer_ = 0;

    // 拖动
    bool pressed_ = false;
    bool dragging_ = false;
    bool pressedSecondary_ = false;   // 右键按下过，等松手再弹菜单
    double pressRootX_ = 0.0;
    double pressRootY_ = 0.0;
    int pressWindowX_ = 0;
    int pressWindowY_ = 0;
};
