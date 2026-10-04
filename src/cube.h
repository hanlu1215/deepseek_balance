#pragma once

#include <windows.h>

#include <chrono>
#include <string>

#include "balance.h"
#include "pacer.h"
#include "render.h"
#include "sound.h"

// 悬浮小方块本体：窗口、交互、状态机、绘制。
class Cube {
public:
    Cube(bool demo, double selfTestSeconds);
    ~Cube();

    Cube(const Cube&) = delete;
    Cube& operator=(const Cube&) = delete;

    bool create(HINSTANCE instance);
    void run();

private:
    struct State {
        bool hasPayload = false;
        std::string balance = "--";   // 原始余额字符串，给节奏器用
        std::wstring amount = L"--";  // 卡片上显示的文字
        std::wstring error;
        bool busy = false;
    };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void computeMetrics();
    void placeWindow();
    void render();
    void present();
    void drawCard(Gdiplus::Graphics& graphics);

    void refresh();
    void startWorker(const std::wstring& apiKey);
    void onFetchDone(FetchResult* result);
    double nextDelay(bool fresh);
    void scheduleRefresh(double seconds);
    void cancelRefreshTimer();

    void startBounce();
    void onAnimationTick();

    void showMenu();
    void handleMenuCommand(int command);
    void quit();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    bool demo_ = false;
    double selfTestSeconds_ = 0.0;

    // 设计像素 × DPI 之后的实际尺寸
    float uiScale_ = 1.0f;
    int pxW_ = 0;
    int pxH_ = 0;
    int pxMargin_ = 0;
    int totalW_ = 0;
    int totalH_ = 0;
    float pxRadius_ = 0.0f;

    // 弹性动画
    float scale_ = 1.0f;
    std::chrono::steady_clock::time_point animationStart_{};
    bool animationTimerTight_ = false;   // 是否已经把系统定时器精度提到 1ms

    State state_;
    LayeredSurface surface_;
    ClickSound sound_;
    RefreshPacer pacer_;

    bool topmost_ = true;
    bool colorCycle_ = true;
    int colorIndex_ = 0;
    bool colorStarted_ = false;   // 第一次拿到的余额仍用原色

    // 拖动
    bool pressed_ = false;
    bool dragging_ = false;
    POINT pressCursor_{};
    POINT pressWindow_{};
};
