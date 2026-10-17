//! 悬浮小方块本体：窗口、交互、状态机、绘制 —— 对应 C++ 版 cube.cpp。
//!
//! 交互：
//!     左键点击  -> 木鱼「笃」一声 + 轻微 Q 弹一下 + 刷新余额
//!     拖动      -> 移动窗口
//!     右键菜单  -> 刷新 / 置顶开关 / 点击音效开关 / 颜色轮换开关 / 退出
//!     Esc       -> 退出

use std::ffi::c_void;
use std::sync::OnceLock;
use std::time::{Instant, SystemTime};

use windows::core::PCWSTR;
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, POINT, RECT, SIZE, WPARAM};
use windows::Win32::Graphics::Gdi::{
    GetDC, GetDeviceCaps, ReleaseDC, AC_SRC_ALPHA, AC_SRC_OVER, BLENDFUNCTION, HBRUSH, LOGPIXELSX,
};
use windows::Win32::Graphics::GdiPlus::{
    GdipGraphicsClear, GdipSetPixelOffsetMode, GdipSetSmoothingMode, GdipSetTextRenderingHint,
    GpGraphics, PixelOffsetModeHalf, SmoothingModeAntiAlias, TextRenderingHintAntiAlias,
};
use windows::Win32::Media::{timeBeginPeriod, timeEndPeriod};
use windows::Win32::UI::Input::KeyboardAndMouse::{GetCapture, ReleaseCapture, SetCapture, VK_ESCAPE};
use windows::Win32::UI::WindowsAndMessaging::*;

use crate::balance;
use crate::config::{self, StartAt};
use crate::pacer::RefreshPacer;
use crate::peak;
use crate::render::{self, BrushGuard, FontGuard, LayeredSurface, PathGuard, TextMetrics};
use crate::sound::ClickSound;
use crate::util;

const TIMER_TICK: usize = 1; // 每秒刷新峰谷倒计时
const TIMER_ANIMATION: usize = 2; // Q 弹动画
const TIMER_REFRESH: usize = 3; // 自动刷新
const TIMER_STARTUP: usize = 4; // 首帧之后的第一次刷新
const TIMER_CLICK_REFRESH: usize = 5; // 点击后延迟 90ms 再刷新
const TIMER_SELF_TEST: usize = 6; // --self-test 到点退出

const WM_FETCH_DONE: u32 = WM_APP + 1;

const MENU_REFRESH: usize = 1001;
const MENU_TOPMOST: usize = 1002;
const MENU_SOUND: usize = 1003;
const MENU_COLOR: usize = 1004;
const MENU_QUIT: usize = 1005;

const FONT_STYLE_BOLD: i32 = 1;

/// 单调时钟（秒），给节奏器用。只有差值有意义。
fn steady_now() -> f64 {
    static START: OnceLock<Instant> = OnceLock::new();
    START.get_or_init(Instant::now).elapsed().as_secs_f64()
}

#[derive(Default)]
struct State {
    has_payload: bool,
    balance: String,        // 原始余额字符串，给节奏器用
    amount: String,         // 卡片上显示的文字
    error: String,
    busy: bool,
}

impl State {
    fn new() -> Self {
        Self {
            balance: "--".to_string(),
            amount: "--".to_string(),
            ..Default::default()
        }
    }
}

pub struct Cube {
    instance: HINSTANCE,
    hwnd: HWND,

    demo: bool,
    self_test_seconds: f64,

    // 设计像素 × DPI 之后的实际尺寸
    ui_scale: f32,
    px_w: i32,
    px_h: i32,
    px_margin: i32,
    total_w: i32,
    total_h: i32,
    px_radius: f32,

    // 弹性动画
    scale: f32,
    animation_start: Instant,
    animation_timer_tight: bool, // 是否已经把系统定时器精度提到 1ms

    state: State,
    surface: LayeredSurface,
    sound: ClickSound,
    pacer: RefreshPacer,

    topmost: bool,
    color_cycle: bool,
    color_index: usize,
    color_started: bool, // 第一次拿到的余额仍用原色

    // 拖动
    pressed: bool,
    dragging: bool,
    press_cursor: POINT,
    press_window: POINT,
}

impl Cube {
    pub fn new(demo: bool, self_test_seconds: f64) -> Self {
        Self {
            instance: HINSTANCE::default(),
            hwnd: HWND::default(),
            demo,
            self_test_seconds,
            ui_scale: 1.0,
            px_w: 0,
            px_h: 0,
            px_margin: 0,
            total_w: 0,
            total_h: 0,
            px_radius: 0.0,
            scale: 1.0,
            animation_start: Instant::now(),
            animation_timer_tight: false,
            state: State::new(),
            surface: LayeredSurface::new(),
            sound: ClickSound::new(config::CLICK_SOUND, config::CLICK_SOUND_VOLUME),
            pacer: RefreshPacer::new(config::REFRESH_MIN_SECONDS, config::REFRESH_MAX_SECONDS),
            topmost: true,
            color_cycle: true,
            color_index: 0,
            color_started: false,
            pressed: false,
            dragging: false,
            press_cursor: POINT::default(),
            press_window: POINT::default(),
        }
    }

    pub fn create(&mut self, instance: HINSTANCE) -> bool {
        self.instance = instance;
        self.topmost = config::ALWAYS_ON_TOP;
        self.color_cycle = config::AMOUNT_COLOR_CYCLE;

        // 木鱼波形：有缓存文件就直接用，没有才合成。合成只要几毫秒，
        // 启动时顺手做完就行，第一次点击不用等。
        self.sound.prepare();
        self.compute_metrics();

        let class_name = util::to_wide("DeepSeekBalanceCubeWindowRust");
        let title = util::to_wide("DeepSeek 余额");

        // 资源 1 就是 ks.ico（见 build.rs 生成的 app.rc）
        let icon = unsafe {
            LoadImageW(
                Some(instance),
                PCWSTR(1usize as *const u16),
                IMAGE_ICON,
                0,
                0,
                LR_DEFAULTSIZE | LR_SHARED,
            )
            .unwrap_or_default()
        };

        let window_class = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(Self::window_proc),
            hInstance: instance,
            hCursor: unsafe { LoadCursorW(None, IDC_ARROW).unwrap_or_default() },
            hIcon: HICON(icon.0),
            hIconSm: HICON(icon.0),
            hbrBackground: HBRUSH::default(),
            lpszClassName: PCWSTR(class_name.as_ptr()),
            ..Default::default()
        };
        if unsafe { RegisterClassExW(&window_class) } == 0 {
            return false;
        }

        let ex_style = WS_EX_LAYERED
            | WS_EX_TOOLWINDOW
            | if self.topmost { WS_EX_TOPMOST } else { WINDOW_EX_STYLE(0) };
        let hwnd = unsafe {
            CreateWindowExW(
                ex_style,
                PCWSTR(class_name.as_ptr()),
                PCWSTR(title.as_ptr()),
                WS_POPUP,
                0,
                0,
                self.total_w,
                self.total_h,
                None,
                None,
                Some(instance),
                Some(self as *mut Cube as *const c_void),
            )
        };
        let Ok(hwnd) = hwnd else {
            return false;
        };
        // WM_NCCREATE 里已经记过 hwnd 了；这里再兜一次底。
        self.hwnd = hwnd;

        self.place_window();
        self.render();
        unsafe {
            let _ = ShowWindow(hwnd, SW_SHOWNOACTIVATE);

            let _ = SetTimer(Some(hwnd), TIMER_TICK, 1000, None);
            let _ = SetTimer(Some(hwnd), TIMER_STARTUP, 80, None);
            if self.self_test_seconds > 0.0 {
                let _ = SetTimer(
                    Some(hwnd),
                    TIMER_SELF_TEST,
                    (self.self_test_seconds * 1000.0) as u32,
                    None,
                );
            }
        }
        true
    }

    pub fn run(&self) {
        let mut message = MSG::default();
        unsafe {
            while GetMessageW(&mut message, None, 0, 0).as_bool() {
                let _ = TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }

    fn compute_metrics(&mut self) {
        let mut dpi = 96;
        unsafe {
            let screen = GetDC(None);
            if !screen.is_invalid() {
                dpi = GetDeviceCaps(Some(screen), LOGPIXELSX);
                ReleaseDC(None, screen);
            }
        }
        if dpi <= 0 {
            dpi = 96;
        }

        // Tk 那套 scaling/1.3333 换算下来就是 dpi/96；比 100% 还低的时候不缩。
        self.ui_scale = (dpi as f32 / 96.0).max(1.0);

        self.px_w = (config::CARD_WIDTH * self.ui_scale).round() as i32;
        self.px_h = (config::CARD_HEIGHT * self.ui_scale).round() as i32;
        self.px_radius = config::CORNER_RADIUS * self.ui_scale;
        self.px_margin = ((config::OUTER_MARGIN * self.ui_scale).round() as i32).max(4);

        self.total_w = self.px_w + self.px_margin * 2;
        self.total_h = self.px_h + self.px_margin * 2;
    }

    fn place_window(&self) {
        let mut work = RECT::default();
        let ok = unsafe {
            SystemParametersInfoW(
                SPI_GETWORKAREA,
                0,
                Some(&mut work as *mut RECT as *mut c_void),
                SYSTEM_PARAMETERS_INFO_UPDATE_FLAGS(0),
            )
        };
        if ok.is_err() {
            work.left = 0;
            work.top = 0;
            work.right = unsafe { GetSystemMetrics(SM_CXSCREEN) };
            work.bottom = unsafe { GetSystemMetrics(SM_CYSCREEN) };
        }

        let (x, y) = match config::START_AT {
            StartAt::TopLeft => (work.left + config::SCREEN_MARGIN, work.top + config::SCREEN_MARGIN),
            StartAt::BottomLeft => (
                work.left + config::SCREEN_MARGIN,
                work.bottom - self.total_h - config::SCREEN_MARGIN,
            ),
            StartAt::BottomRight => (
                work.right - self.total_w - config::SCREEN_MARGIN,
                work.bottom - self.total_h - config::SCREEN_MARGIN,
            ),
            StartAt::TopRight => (
                work.right - self.total_w - config::SCREEN_MARGIN,
                work.top + config::SCREEN_MARGIN,
            ),
        };

        unsafe {
            let _ = SetWindowPos(
                self.hwnd,
                Some(if self.topmost { HWND_TOPMOST } else { HWND_NOTOPMOST }),
                x.max(0),
                y.max(0),
                self.total_w,
                self.total_h,
                SWP_NOACTIVATE,
            );
        }
    }

    // ---------------------------------------------------------------- 绘制
    fn render(&mut self) {
        if self.hwnd.is_invalid() {
            return;
        }
        if !self.surface.ensure(self.total_w, self.total_h) {
            return;
        }
        let graphics = self.surface.graphics();
        if graphics.is_null() {
            return;
        }

        unsafe {
            GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
            GdipSetPixelOffsetMode(graphics, PixelOffsetModeHalf);
            // 分层窗口不能用 ClearType（次像素抗锯齿要靠不透明底色），灰阶抗锯齿正好
            GdipSetTextRenderingHint(graphics, TextRenderingHintAntiAlias);
            GdipGraphicsClear(graphics, 0);

            self.draw_card(graphics);
            self.present();
        }
    }

    fn present(&self) {
        let source = POINT { x: 0, y: 0 };
        let size = SIZE {
            cx: self.total_w,
            cy: self.total_h,
        };
        let blend = BLENDFUNCTION {
            BlendOp: AC_SRC_OVER as u8,
            BlendFlags: 0,
            SourceConstantAlpha: 255,
            AlphaFormat: AC_SRC_ALPHA as u8,
        };

        // pptDst 传 None = 位置不动，只更新内容
        unsafe {
            let _ = UpdateLayeredWindow(
                self.hwnd,
                None,
                None,
                Some(&size),
                Some(self.surface.dc()),
                Some(&source),
                COLORREF(0),
                Some(&blend),
                ULW_ALPHA,
            );
        }
    }

    unsafe fn draw_card(&mut self, graphics: *mut GpGraphics) {
        let scale = self.scale;
        let left = self.px_margin as f32 + self.px_w as f32 * (1.0 - scale) / 2.0;
        let top = self.px_margin as f32 + self.px_h as f32 * (1.0 - scale) / 2.0;
        let width = self.px_w as f32 * scale;
        let height = self.px_h as f32 * scale;
        let radius = (self.px_radius * scale).clamp(2.0, width.min(height) / 2.0);
        let zoom = scale * self.ui_scale; // 弹性动画缩放 + 高 DPI
        let center_x = left + width / 2.0;

        let family = render::ui_font_family();
        if family.is_null() {
            return;
        }

        // ---- 卡片底：先整块填描边色，再把内缩 BORDER_WIDTH 的区域填渐变 ----
        if let Some(outer) = PathGuard::new() {
            render::add_rounded_rect(outer.0, left, top, width, height, radius);
            if let Some(border_brush) = BrushGuard::solid(config::COLOR_BORDER_ARGB) {
                render::fill_path(graphics, &border_brush, &outer);
            }

            let border = config::BORDER_WIDTH * zoom;
            if width - 2.0 * border > 0.0 && height - 2.0 * border > 0.0 {
                if let Some(inner) = PathGuard::new() {
                    render::add_rounded_rect(
                        inner.0,
                        left + border,
                        top + border,
                        width - 2.0 * border,
                        height - 2.0 * border,
                        (radius - border).max(0.0),
                    );
                    if let Some(gradient) = BrushGuard::gradient(
                        top,
                        top + height,
                        center_x,
                        config::COLOR_TOP_ARGB,
                        config::COLOR_BOTTOM_ARGB,
                    ) {
                        render::fill_path(graphics, &gradient, &inner);
                    }
                }
            }
        }

        // ---- 余额数字：出错时直接把原因写在卡片上，别只给个红杠 ----
        let mut amount_color = config::AMOUNT_COLOR_LIST[self.color_index % config::AMOUNT_COLOR_LIST.len()];
        if !self.state.error.is_empty() {
            amount_color = config::COLOR_ERROR_ARGB;
        } else if self.state.busy {
            amount_color = config::COLOR_AMOUNT_DIM_ARGB;
        }
        let amount_pixels = render::fit_font_pixels(
            graphics,
            family,
            FONT_STYLE_BOLD,
            config::AMOUNT_FONT_PX * zoom,
            &self.state.amount,
            width * config::TEXT_WIDTH_RATIO,
        );
        if let Some(amount_font) = FontGuard::new(family, amount_pixels, FONT_STYLE_BOLD) {
            let metrics = render::text_metrics(&amount_font, family, FONT_STYLE_BOLD);
            render::draw_centered_text(
                graphics,
                &self.state.amount,
                amount_font.0,
                &metrics,
                amount_color,
                center_x,
                top + height * config::AMOUNT_CENTER_Y,
            );
        }

        // ---- 峰 / 谷 徽标 + 时:分:秒 倒计时 ----
        let now = SystemTime::now();
        let peak_now = peak::is_peak(now);
        let label = if peak_now { "峰" } else { "谷" };
        let remaining = peak::next_transition(now)
            .duration_since(now)
            .map(|d| d.as_secs_f64())
            .unwrap_or(0.0);
        let clock = peak::format_remaining(remaining);
        let canonical = peak::canonical_clock(&clock);

        let mut pill_font_pixels = config::PILL_FONT_PX * zoom;
        let mut pad = config::PILL_PAD_X * zoom;
        let mut gap = config::PILL_GAP * zoom;

        // 宽度按「把数字都换成 8」的等宽版本算：秒数变化时徽标不会抖
        let measure_pill = |pixels: f32, pad: f32, gap: f32| -> Option<(f32, f32, f32, TextMetrics)> {
            let probe = FontGuard::new(family, pixels, FONT_STYLE_BOLD)?;
            let label_width = render::measure_text_width(graphics, probe.0, label);
            let clock_width = render::measure_text_width(graphics, probe.0, &canonical);
            let total_width = label_width + gap + clock_width + pad * 2.0;
            let metrics = render::text_metrics(&probe, family, FONT_STYLE_BOLD);
            Some((label_width, clock_width, total_width, metrics))
        };

        let Some((mut label_width, mut clock_width, mut total_width, mut metrics)) =
            measure_pill(pill_font_pixels, pad, gap)
        else {
            return;
        };

        // 长假时倒计时会出现三位数小时，整块徽标等比缩小也要塞进卡片
        let available = width - 2.0 * config::PILL_SIDE_MARGIN * zoom;
        if total_width > available && available > 0.0 {
            let shrink = available / total_width;
            pad *= shrink;
            gap *= shrink;
            pill_font_pixels *= shrink;
            if let Some(measured) = measure_pill(pill_font_pixels, pad, gap) {
                label_width = measured.0;
                clock_width = measured.1;
                total_width = measured.2;
                metrics = measured.3;
            }
        }

        let pill_height = metrics.line_space() + 2.0 * config::PILL_PAD_Y * zoom;
        let pill_left = center_x - total_width / 2.0;
        let pill_center_y = top + height * config::PILL_CENTER_Y;

        if let Some(pill_path) = PathGuard::new() {
            render::add_rounded_rect(
                pill_path.0,
                pill_left,
                pill_center_y - pill_height / 2.0,
                total_width,
                pill_height,
                total_width.min(pill_height) / 2.0,
            );
            let pill_background = if peak_now { config::COLOR_PEAK_BG_ARGB } else { config::COLOR_OFF_BG_ARGB };
            if let Some(pill_brush) = BrushGuard::solid(pill_background) {
                render::fill_path(graphics, &pill_brush, &pill_path);
            }
        }

        if let Some(pill_font) = FontGuard::new(family, pill_font_pixels, FONT_STYLE_BOLD) {
            let pill_color = if peak_now { config::COLOR_PEAK_ARGB } else { config::COLOR_OFF_ARGB };
            render::draw_centered_text(
                graphics,
                label,
                pill_font.0,
                &metrics,
                pill_color,
                pill_left + pad + label_width / 2.0,
                pill_center_y,
            );
            render::draw_centered_text(
                graphics,
                &clock,
                pill_font.0,
                &metrics,
                pill_color,
                pill_left + total_width - pad - clock_width / 2.0,
                pill_center_y,
            );
        }
    }

    // ---------------------------------------------------------------- 刷新
    fn refresh(&mut self) {
        self.cancel_refresh_timer();

        // 重新声明置顶：长时间挂着会被其他非置顶窗口意外覆盖，每次刷新都拨一下。
        if self.topmost {
            unsafe {
                let _ = SetWindowPos(
                    self.hwnd,
                    Some(HWND_TOPMOST),
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
                );
            }
        }

        if self.state.busy {
            return;
        }

        let api_key = balance::resolve_api_key();
        if api_key.is_empty() && !self.demo {
            self.state.busy = false;
            self.state.error = "缺 Key".to_string();
            self.state.amount = self.state.error.clone();
            self.render();
            return;
        }

        self.state.busy = true;
        self.render();

        if self.demo {
            let result = Box::new(balance::FetchResult {
                ok: true,
                payload: balance::demo_payload().to_string(),
                error: String::new(),
            });
            let raw = Box::into_raw(result);
            unsafe {
                if PostMessageW(Some(self.hwnd), WM_FETCH_DONE, WPARAM(0), LPARAM(raw as isize)).is_err() {
                    drop(Box::from_raw(raw));
                }
            }
            return;
        }
        self.start_worker(api_key);
    }

    fn start_worker(&self, api_key: String) {
        // 后台线程只做网络请求，绝不碰窗口。结果通过 PostMessage 交给 UI 线程，
        // 谁分配谁释放：投递成功就由 UI 线程 drop，失败就自己 drop。
        // HWND 不实现 Send，按地址值搬过去再还原（窗口句柄本来就是进程内共享的）。
        let hwnd_raw = self.hwnd.0 as usize;
        std::thread::spawn(move || {
            let hwnd = HWND(hwnd_raw as *mut c_void);
            let result = Box::new(balance::fetch_balance(&api_key));
            let raw = Box::into_raw(result);
            unsafe {
                if PostMessageW(Some(hwnd), WM_FETCH_DONE, WPARAM(0), LPARAM(raw as isize)).is_err() {
                    drop(Box::from_raw(raw));
                }
            }
        });
    }

    fn on_fetch_done(&mut self, result: &balance::FetchResult) {
        self.state.busy = false;

        if result.ok {
            match serde_json::from_str::<serde_json::Value>(&result.payload) {
                Ok(document) => {
                    self.state.balance = balance::first_balance(&document);
                }
                Err(_) => {
                    self.state.balance = "--".to_string();
                }
            }
            self.state.has_payload = true;
            self.state.amount = self.state.balance.clone();
            self.state.error.clear();

            if self.color_cycle && self.color_started {
                // 每刷到一次新数据就换下一个颜色（启动那一次仍显示原来的颜色）
                self.color_index = (self.color_index + 1) % config::AMOUNT_COLOR_LIST.len();
            }
            self.color_started = true;
        } else {
            self.state.error = result.error.clone();
            self.state.amount = balance::short_error(&result.error);
        }

        self.render();

        if config::BALANCE_REFRESH_SECONDS < 0.0 {
            return; // 负数 = 关掉自动刷新，只留手动点
        }
        let delay = self.next_delay(result.ok);
        self.schedule_refresh(delay);
    }

    fn next_delay(&mut self, fresh: bool) -> f64 {
        if config::BALANCE_REFRESH_SECONDS > 0.0 {
            return config::BALANCE_REFRESH_SECONDS;
        }
        if !fresh {
            return self.pacer.delay(); // 这次没拿到数据，维持当前节奏再来
        }

        let has_value = balance::parse_balance(&self.state.balance);
        match has_value {
            Some(value) => self.pacer.observe(true, value, steady_now()),
            None => self.pacer.observe(false, 0.0, steady_now()),
        }
    }

    fn schedule_refresh(&self, seconds: f64) {
        let milliseconds = (seconds * 1000.0).max(1.0).min(2.0e9) as u32;
        unsafe {
            let _ = SetTimer(Some(self.hwnd), TIMER_REFRESH, milliseconds, None);
        }
    }

    fn cancel_refresh_timer(&self) {
        unsafe {
            let _ = KillTimer(Some(self.hwnd), TIMER_REFRESH);
        }
    }

    // ---------------------------------------------------------------- 动画
    fn start_bounce(&mut self) {
        self.animation_start = Instant::now();
        // 系统默认的定时器精度是 15.6ms，直接 SetTimer(16) 会时而 15.6ms、时而
        // 31ms，弹簧动画看着一顿一顿的。动画期间把精度提到 1ms，结束后立刻还回去。
        if !self.animation_timer_tight {
            unsafe {
                timeBeginPeriod(1);
            }
            self.animation_timer_tight = true;
        }
        unsafe {
            let _ = SetTimer(Some(self.hwnd), TIMER_ANIMATION, config::BOUNCE_FRAME_MS, None);
        }
    }

    fn on_animation_tick(&mut self) {
        let elapsed = self.animation_start.elapsed().as_secs_f64();

        if elapsed >= config::BOUNCE_DURATION {
            self.scale = 1.0;
            unsafe {
                let _ = KillTimer(Some(self.hwnd), TIMER_ANIMATION);
            }
            if self.animation_timer_tight {
                unsafe {
                    timeEndPeriod(1);
                }
                self.animation_timer_tight = false;
            }
            self.render();
            return;
        }

        // 欠阻尼弹簧：1-幅度 -> 1.0，回弹两下（幅度小，含蓄一点）
        let progress = elapsed / config::BOUNCE_DURATION;
        self.scale = (1.0 - config::BOUNCE_AMPLITUDE * (-4.2 * progress).exp() * (11.0 * progress).cos()) as f32;
        self.render();
    }

    // ---------------------------------------------------------------- 菜单
    fn show_menu(&mut self) {
        let Ok(menu) = (unsafe { CreatePopupMenu() }) else {
            return;
        };

        let checked = |on: bool| if on { MF_CHECKED } else { MF_UNCHECKED };
        unsafe {
            let _ = AppendMenuW(menu, MF_STRING, MENU_REFRESH, util::to_wide("刷新").as_pcwstr());
            let _ = AppendMenuW(
                menu,
                MF_STRING | checked(self.topmost),
                MENU_TOPMOST,
                util::to_wide("置顶").as_pcwstr(),
            );
            let _ = AppendMenuW(
                menu,
                MF_STRING | checked(self.sound.enabled()),
                MENU_SOUND,
                util::to_wide("点击音效").as_pcwstr(),
            );
            let _ = AppendMenuW(
                menu,
                MF_STRING | checked(self.color_cycle),
                MENU_COLOR,
                util::to_wide("颜色轮换").as_pcwstr(),
            );
            let _ = AppendMenuW(menu, MF_SEPARATOR, 0, PCWSTR::null());
            let _ = AppendMenuW(menu, MF_STRING, MENU_QUIT, util::to_wide("退出").as_pcwstr());
        }

        let mut cursor = POINT::default();
        unsafe {
            let _ = GetCursorPos(&mut cursor);

            // TrackPopupMenu 要求窗口在前台，否则点别处菜单不会消失
            let _ = SetForegroundWindow(self.hwnd);
            let command = TrackPopupMenu(
                menu,
                TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                cursor.x,
                cursor.y,
                Some(0),
                self.hwnd,
                None,
            )
            .0;
            let _ = DestroyMenu(menu);
            let _ = PostMessageW(Some(self.hwnd), WM_NULL, WPARAM(0), LPARAM(0)); // 喂掉菜单留下的空消息，避免卡住前台锁

            self.handle_menu_command(command as usize);
        }
    }

    fn handle_menu_command(&mut self, command: usize) {
        match command {
            MENU_REFRESH => {
                // 右键菜单里的刷新：也敲一记木鱼
                self.sound.play();
                self.refresh();
            }
            MENU_TOPMOST => {
                self.topmost = !self.topmost;
                unsafe {
                    let _ = SetWindowPos(
                        self.hwnd,
                        Some(if self.topmost { HWND_TOPMOST } else { HWND_NOTOPMOST }),
                        0,
                        0,
                        0,
                        0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
                    );
                }
            }
            MENU_SOUND => {
                self.sound.set_enabled(!self.sound.enabled());
                if self.sound.enabled() {
                    self.sound.play(); // 重新打开时立刻响一声，方便确认
                }
            }
            MENU_COLOR => {
                // 关掉就回到第一个颜色，不再变
                self.color_cycle = !self.color_cycle;
                if !self.color_cycle {
                    self.color_index = 0;
                }
                self.render();
            }
            MENU_QUIT => self.quit(),
            _ => {}
        }
    }

    fn quit(&self) {
        if !self.hwnd.is_invalid() {
            unsafe {
                let _ = DestroyWindow(self.hwnd);
            }
        }
    }

    // ---------------------------------------------------------------- 消息
    unsafe extern "system" fn window_proc(
        hwnd: HWND,
        message: u32,
        wparam: WPARAM,
        lparam: LPARAM,
    ) -> LRESULT {
        if message == WM_NCCREATE {
            let create = &*(lparam.0 as *const CREATESTRUCTW);
            let this = create.lpCreateParams as *mut Cube;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, this as isize);
            (*this).hwnd = hwnd;
            return (*this).handle_message(message, wparam, lparam);
        }

        let this = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Cube;
        if this.is_null() {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        (*this).hwnd = hwnd;
        (*this).handle_message(message, wparam, lparam)
    }

    unsafe fn handle_message(&mut self, message: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
        match message {
            WM_TIMER => {
                match wparam.0 {
                    TIMER_TICK => self.render(), // 重画本身很轻，卡片底图是现算的
                    TIMER_STARTUP => {
                        let _ = KillTimer(Some(self.hwnd), TIMER_STARTUP);
                        self.refresh();
                    }
                    TIMER_REFRESH => {
                        let _ = KillTimer(Some(self.hwnd), TIMER_REFRESH);
                        self.refresh();
                    }
                    TIMER_CLICK_REFRESH => {
                        let _ = KillTimer(Some(self.hwnd), TIMER_CLICK_REFRESH);
                        self.refresh();
                    }
                    TIMER_ANIMATION => self.on_animation_tick(),
                    TIMER_SELF_TEST => self.quit(),
                    _ => {}
                }
                LRESULT(0)
            }

            WM_FETCH_DONE => {
                let result = Box::from_raw(lparam.0 as *mut balance::FetchResult);
                self.on_fetch_done(&result);
                LRESULT(0)
            }

            WM_LBUTTONDOWN => {
                let _ = SetForegroundWindow(self.hwnd); // 拿到焦点，Esc 才收得到
                SetCapture(self.hwnd);
                let _ = GetCursorPos(&mut self.press_cursor);
                let mut rect = RECT::default();
                let _ = GetWindowRect(self.hwnd, &mut rect);
                self.press_window.x = rect.left;
                self.press_window.y = rect.top;
                self.pressed = true;
                self.dragging = false;
                LRESULT(0)
            }

            WM_MOUSEMOVE => {
                if !self.pressed {
                    return LRESULT(0);
                }
                let mut cursor = POINT::default();
                let _ = GetCursorPos(&mut cursor);
                let dx = cursor.x - self.press_cursor.x;
                let dy = cursor.y - self.press_cursor.y;
                if dx.abs() > 3 || dy.abs() > 3 {
                    self.dragging = true;
                }
                if self.dragging {
                    let _ = SetWindowPos(
                        self.hwnd,
                        None,
                        self.press_window.x + dx,
                        self.press_window.y + dy,
                        0,
                        0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE,
                    );
                }
                LRESULT(0)
            }

            WM_LBUTTONUP => {
                // ReleaseCapture 会「同步」给本窗口发一条 WM_CAPTURECHANGED，
                // 而那个分支会把 pressed 清掉。所以必须先取值再松捕获，
                // 否则下面这个 if 永远是假 —— 点击就彻底没反应了。
                let was_pressed = self.pressed;
                let was_dragging = self.dragging;
                if GetCapture() == self.hwnd {
                    let _ = ReleaseCapture();
                }
                self.pressed = false;
                self.dragging = false;
                if was_pressed && !was_dragging {
                    self.sound.play(); // 木鱼「笃」
                    self.start_bounce(); // 先 Q 弹，再刷新
                    let _ = SetTimer(Some(self.hwnd), TIMER_CLICK_REFRESH, 90, None);
                }
                LRESULT(0)
            }

            WM_CAPTURECHANGED => {
                self.pressed = false;
                self.dragging = false;
                LRESULT(0)
            }

            WM_RBUTTONUP => {
                self.show_menu();
                LRESULT(0)
            }

            WM_KEYDOWN => {
                if wparam.0 as u16 == VK_ESCAPE.0 {
                    self.quit();
                    return LRESULT(0);
                }
                DefWindowProcW(self.hwnd, message, wparam, lparam)
            }

            WM_ERASEBKGND => LRESULT(1), // 分层窗口不靠 WM_PAINT，别擦背景

            WM_CLOSE => {
                self.quit();
                LRESULT(0)
            }

            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }

            _ => DefWindowProcW(self.hwnd, message, wparam, lparam),
        }
    }
}

impl Drop for Cube {
    fn drop(&mut self) {
        unsafe {
            if !self.hwnd.is_invalid() {
                let _ = KillTimer(Some(self.hwnd), TIMER_TICK);
                let _ = KillTimer(Some(self.hwnd), TIMER_ANIMATION);
                let _ = KillTimer(Some(self.hwnd), TIMER_REFRESH);
                let _ = KillTimer(Some(self.hwnd), TIMER_STARTUP);
                let _ = KillTimer(Some(self.hwnd), TIMER_CLICK_REFRESH);
                let _ = DestroyWindow(self.hwnd);
                self.hwnd = HWND::default();
            }
            if !self.instance.is_invalid() {
                let class_name = util::to_wide("DeepSeekBalanceCubeWindowRust");
                let _ = UnregisterClassW(PCWSTR(class_name.as_ptr()), Some(self.instance));
            }
        }
    }
}

/// Vec<u16> → PCWSTR 的小帮手（AppendMenuW 用）。
trait AsPcwstr {
    fn as_pcwstr(&self) -> PCWSTR;
}
impl AsPcwstr for Vec<u16> {
    fn as_pcwstr(&self) -> PCWSTR {
        PCWSTR(self.as_ptr())
    }
}
