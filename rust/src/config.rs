//! DeepSeek 余额小方块 —— Rust / Win32 版
//!
//! ★ 配置区：逐条对应 C++ 版的 src/config.h。
//!   把 Key 写在 API_KEY_LITERAL，非空则优先使用；留空则回退到环境变量。

// 方式一：把 Key 填在这里（改完要重新编译）。
//         最省事，双击 exe 也能用；缺点是这个 exe 别发给别人。
pub const API_KEY_LITERAL: &str = "";
// 方式二：设环境变量 DEEPSEEK_API_KEY。注意这是「临时」变量，只在设它的那个
//         命令行窗口里有效，窗口一关就没了，所以要在同一个窗口里启动 exe。
//             PowerShell :  $env:DEEPSEEK_API_KEY = "sk-你的key"
//             cmd        :  set DEEPSEEK_API_KEY=sk-你的key
//             Git Bash   :  export DEEPSEEK_API_KEY=sk-你的key
//         别写成 `set DEEPSEEK_API_KEY=...` 就跑（那是 cmd 的写法）：PowerShell
//         里的 set 只是建了个 PowerShell 变量，环境变量还是空的。
// 优先级：API_KEY_LITERAL 填了就用它，留空才回退到环境变量。
pub const API_KEY_ENV: &str = "DEEPSEEK_API_KEY";

// 余额自动刷新：0 = 自适应（默认，按消耗速度在 30 秒 ~ 5 分钟之间自动调节）
//               正数 = 固定间隔（秒），例如 10 就是固定每 10 秒刷一次
//               负数 = 关掉自动刷新，只能手动点方块刷新
pub const BALANCE_REFRESH_SECONDS: f64 = 0.0;

pub const API_HOST: &str = "api.deepseek.com";
pub const API_PATH: &str = "/user/balance";
pub const TIMEOUT_SECONDS: u32 = 20;

// ---- 方块尺寸（设计像素，运行时会按系统 DPI 缩放）----
pub const CARD_WIDTH: f32 = 156.0;
pub const CARD_HEIGHT: f32 = 120.0;
pub const CORNER_RADIUS: f32 = 34.0; // 圆角半径：越大越圆润
pub const OUTER_MARGIN: f32 = 8.0;   // 卡片四周的透明留白

// ---- 文字排版（设计像素）----
pub const AMOUNT_FONT_PX: f32 = 46.0;      // 余额数字字号
pub const PILL_FONT_PX: f32 = 20.0;        // 「峰/谷」徽标与倒计时字号
pub const TEXT_WIDTH_RATIO: f32 = 0.84;    // 单行文字最多占卡片宽度比例，超出自动缩字号
pub const AMOUNT_CENTER_Y: f32 = 0.352;    // 余额数字中心位置（卡片高度比例）
pub const PILL_CENTER_Y: f32 = 0.728;      // 徽标中心位置（卡片高度比例）
                                           // 上面两个值决定两行文字的间距：往中间各靠
                                           // 0.017 ≈ 卡片高的 2px，空隙就收紧 4px
pub const PILL_PAD_X: f32 = 10.0;          // 徽标左右内边距
pub const PILL_GAP: f32 = 7.0;             // 徽标内「峰/谷」与倒计时的间距
pub const PILL_PAD_Y: f32 = 2.0;           // 徽标上下内边距
pub const PILL_SIDE_MARGIN: f32 = 13.0;    // 徽标距离卡片左右边缘的最小留白
pub const BORDER_WIDTH: f32 = 1.2;         // 卡片描边宽度（像素）

// ---- 动画（“Q 弹”）----
pub const BOUNCE_AMPLITUDE: f64 = 0.06; // 缩放幅度：最小缩到 94%，越小越含蓄
pub const BOUNCE_DURATION: f64 = 0.55;  // 一次 Q 弹的时长（秒）
pub const BOUNCE_FRAME_MS: u32 = 16;    // 动画帧间隔（毫秒）

// ---- 点击音效（木鱼）----
pub const CLICK_SOUND: bool = true;          // 点击刷新时敲一记木鱼
pub const CLICK_SOUND_VOLUME: f64 = 0.75;    // 音量 0~1
pub const CLICK_SOUND_SECONDS: f64 = 0.15;   // 声音长度（秒），木鱼是短促的“笃”
// 可选：想换成自己的声音，就把这个 wav 放在 exe 同目录，程序只读不写。
// 没有这个文件就用内置合成的那记「笃」——波形在代码里算出来，不落盘。
pub const CLICK_SOUND_FILE: &str = "mokugyo_click.wav";

// ---- 自适应刷新节奏（BALANCE_REFRESH_SECONDS = 0 时生效）----
pub const REFRESH_MIN_SECONDS: f64 = 30.0;  // 最快间隔：钱掉得快时最短 30 秒看一眼
pub const REFRESH_MAX_SECONDS: f64 = 300.0; // 最慢间隔：余额不动时 5 分钟看一眼
pub const REFRESH_JITTER: f64 = 0.05;       // 间隔随机抖动 ±5%，避免每次都卡在同一秒
pub const REFRESH_RATE_TAU: f64 = 300.0;    // 消耗速率的平滑时间常数（秒），越大越迟钝
pub const REFRESH_SHRINK: f64 = 0.5;        // 这次余额变了：间隔 ×0.5（更快）
pub const REFRESH_GROW: f64 = 1.3;          // 这次余额没变：间隔 ×1.3（更慢）
pub const BALANCE_STEP: f64 = 0.01;         // 余额精度（元）：大概消耗这么多就值得看一眼

// ---- 窗口 ----
pub const ALWAYS_ON_TOP: bool = true;

#[derive(Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)] // 四个位置都是配置项，当前默认只用 TopRight
pub enum StartAt {
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft,
}
pub const START_AT: StartAt = StartAt::TopRight;
pub const SCREEN_MARGIN: i32 = 12; // 窗口离屏幕（工作区）边缘的距离

// ---- 配色（0xAARRGGBB）----
pub const COLOR_TOP_ARGB: u32 = 0xFF2C2F38;        // 卡片渐变起始 (44,47,56)
pub const COLOR_BOTTOM_ARGB: u32 = 0xFF1A1C21;     // 卡片渐变结束 (26,28,33)
pub const COLOR_BORDER_ARGB: u32 = 0xFF4E5460;     // 描边 (78,84,96)
pub const COLOR_AMOUNT_DIM_ARGB: u32 = 0xFF7D7360; // 刷新中
pub const COLOR_ERROR_ARGB: u32 = 0xFFFF7070;
pub const COLOR_PEAK_ARGB: u32 = 0xFFFF8F5E; // 峰：橙
pub const COLOR_PEAK_BG_ARGB: u32 = 0xFF3A2318;
pub const COLOR_OFF_ARGB: u32 = 0xFF4EDE9A; // 谷：绿
pub const COLOR_OFF_BG_ARGB: u32 = 0xFF16301F;

// 余额数字的颜色：每刷新成功一次就换成下一个（第一个是原来的颜色）
pub const AMOUNT_COLOR_CYCLE: bool = true; // 关掉就固定用列表里的第一个颜色
pub const AMOUNT_COLOR_LIST: [u32; 6] = [
    0xFFFFD88A, // 金（原来的颜色）
    0xFFFF9F45, // 橘
    0xFFFF5F5F, // 红
    0xFF5AA9FF, // 蓝
    0xFF43D6C8, // 青
    0xFFEEF2F7, // 白
];

// 界面字体：微软雅黑 UI，取不到会自动回退
pub const FONT_FAMILY: &str = "Microsoft YaHei UI";
