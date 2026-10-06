#pragma once

// ============================================================================
//  DeepSeek 余额小方块 —— C++ / Linux (GTK3) 版
//
//  ★ 配置区：把 Key 写在这里，非空则优先使用；留空则回退到环境变量
//
//  字段名和 Windows 版（../src/config.h）一一对应，方便两边对着改。
//  区别只有两处：字符串从 UTF-16 换成 UTF-8（不再有 L 前缀），字体名换成
//  Linux 上真实存在的字体族。
// ============================================================================

// 方式一：把 Key 填在这里（改完要重新编译）。
//         最省事，直接跑也能用；缺点是这个二进制别发给别人。
inline constexpr const char* kApiKeyLiteral = "";
// 方式二：设环境变量 DEEPSEEK_API_KEY。注意这是「临时」变量，只在设它的那个
//         终端里有效，终端一关就没了，所以要在同一个终端里启动：
//             export DEEPSEEK_API_KEY=sk-你的key
//             ./build/bin/deepseek_balance_cube
//         想让它一直有效，就写进 ~/.bashrc 或 ~/.profile（注意别把 Key 提交进仓库）。
// 优先级：kApiKeyLiteral 填了就用它，留空才回退到环境变量。
inline constexpr const char* kApiKeyEnv = "DEEPSEEK_API_KEY";

// 余额自动刷新：0 = 自适应（默认，按消耗速度在 30 秒 ~ 5 分钟之间自动调节）
//               正数 = 固定间隔（秒），例如 10 就是固定每 10 秒刷一次
//               负数 = 关掉自动刷新，只能手动点方块刷新
inline constexpr double kBalanceRefreshSeconds = 0;

inline constexpr const char* kApiHost = "api.deepseek.com";
inline constexpr const char* kApiPath = "/user/balance";
inline constexpr int kTimeoutSeconds = 20;

// ---- 方块尺寸（逻辑像素）----
//  注意：这里不需要 Windows 版那套 dpi/96 的手工缩放。GTK 在 draw 时会把
//  cairo 画布按窗口的 scale factor 预乘，几何和文字会一起等比放大。
inline constexpr double kCardWidth = 156.0;
inline constexpr double kCardHeight = 120.0;
inline constexpr double kCornerRadius = 34.0;   // 圆角半径：越大越圆润
inline constexpr double kOuterMargin = 8.0;     // 卡片四周的透明留白

// ---- 文字排版（逻辑像素）----
inline constexpr double kAmountFontPx = 46.0;   // 余额数字字号
inline constexpr double kPillFontPx = 20.0;     // 「峰/谷」徽标与倒计时字号
inline constexpr double kTextWidthRatio = 0.84; // 单行文字最多占卡片宽度比例，超出自动缩字号
inline constexpr double kAmountCenterY = 0.352; // 余额数字中心位置（卡片高度比例）
inline constexpr double kPillCenterY = 0.728;   // 徽标中心位置（卡片高度比例）
                                                // 上面两个值决定两行文字的间距：往中间各靠
                                                // 0.017 ≈ 卡片高的 2px，空隙就收紧 4px
inline constexpr double kPillPadX = 10.0;       // 徽标左右内边距
inline constexpr double kPillGap = 7.0;         // 徽标内「峰/谷」与倒计时的间距
inline constexpr double kPillPadY = 2.0;        // 徽标上下内边距
inline constexpr double kPillSideMargin = 13.0; // 徽标距离卡片左右边缘的最小留白
inline constexpr double kBorderWidth = 1.2;     // 卡片描边宽度（像素）

// ---- 动画（“Q 弹”）----
inline constexpr double kBounceAmplitude = 0.06;  // 缩放幅度：最小缩到 94%，越小越含蓄
inline constexpr double kBounceDuration = 0.55;   // 一次 Q 弹的时长（秒）

// ---- 点击音效（木鱼）----
inline constexpr bool kClickSound = true;             // 点击刷新时敲一记木鱼
inline constexpr double kClickSoundVolume = 0.75;     // 音量 0~1
inline constexpr double kClickSoundSeconds = 0.15;    // 声音长度（秒），木鱼是短促的“笃”

// ---- 自适应刷新节奏（kBalanceRefreshSeconds = 0 时生效）----
inline constexpr double kRefreshMinSeconds = 30.0;    // 最快间隔：钱掉得快时最短 30 秒看一眼
inline constexpr double kRefreshMaxSeconds = 300.0;   // 最慢间隔：余额不动时 5 分钟看一眼
inline constexpr double kRefreshJitter = 0.05;        // 间隔随机抖动 ±5%，避免每次都卡在同一秒
inline constexpr double kRefreshRateTau = 300.0;      // 消耗速率的平滑时间常数（秒），越大越迟钝
inline constexpr double kRefreshShrink = 0.5;         // 这次余额变了：间隔 ×0.5（更快）
inline constexpr double kRefreshGrow = 1.3;           // 这次余额没变：间隔 ×1.3（更慢）
inline constexpr double kBalanceStep = 0.01;          // 余额精度（元）：大概消耗这么多就值得看一眼

// ---- 窗口 ----
inline constexpr bool kAlwaysOnTop = true;

enum class StartAt { TopRight, TopLeft, BottomRight, BottomLeft };
inline constexpr StartAt kStartAt = StartAt::TopRight;
inline constexpr int kScreenMargin = 12;        // 窗口离屏幕（工作区）边缘的距离

// ---- 配色（0xAARRGGBB，和 Windows 版同一套数值）----
inline constexpr unsigned int kColorTopArgb = 0xFF2C2F38;        // 卡片渐变起始 (44,47,56)
inline constexpr unsigned int kColorBottomArgb = 0xFF1A1C21;     // 卡片渐变结束 (26,28,33)
inline constexpr unsigned int kColorBorderArgb = 0xFF4E5460;     // 描边 (78,84,96)
inline constexpr unsigned int kColorAmountDimArgb = 0xFF7D7360;  // 刷新中
inline constexpr unsigned int kColorErrorArgb = 0xFFFF7070;
inline constexpr unsigned int kColorPeakArgb = 0xFFFF8F5E;       // 峰：橙
inline constexpr unsigned int kColorPeakBgArgb = 0xFF3A2318;
inline constexpr unsigned int kColorOffArgb = 0xFF4EDE9A;        // 谷：绿
inline constexpr unsigned int kColorOffBgArgb = 0xFF16301F;

// 余额数字的颜色：每刷新成功一次就换成下一个（第一个是原来的颜色）
inline constexpr bool kAmountColorCycle = true;   // 关掉就固定用列表里的第一个颜色
inline constexpr unsigned int kAmountColorList[] = {
    0xFFFFD88A,   // 金（原来的颜色）
    0xFFFF9F45,   // 橘
    0xFFFF5F5F,   // 红
    0xFF5AA9FF,   // 蓝
    0xFF43D6C8,   // 青
    0xFFEEF2F7,   // 白
};
inline constexpr int kAmountColorCount =
    static_cast<int>(sizeof(kAmountColorList) / sizeof(kAmountColorList[0]));

// 界面字体候选：按顺序取第一个系统里真实存在的。
// Windows 版写死 "Microsoft YaHei UI"，Linux 上没有；Noto Sans CJK / 思源黑体 /
// 文泉驿微米黑 都是常见的无衬线中文字体，取不到就由 Pango 自己回退。
inline constexpr const char* kFontFamilies[] = {
    "Noto Sans CJK SC",
    "Source Han Sans SC",
    "Noto Sans SC",
    "WenQuanYi Micro Hei",
    "WenQuanYi Zen Hei",
    "DejaVu Sans",
};
inline constexpr int kFontFamilyCount =
    static_cast<int>(sizeof(kFontFamilies) / sizeof(kFontFamilies[0]));

// ---- 时区 ----
// peak.h 里的峰谷时段是按北京时间硬编码的（周一至周五 09:00-12:00、14:00-18:00）。
// Windows 版隐含「本机就是北京时间」；Linux 上如果 TZ 不是东八区，峰谷会算错。
// 所以启动时统一把进程时区设成 Asia/Shanghai（只影响本进程，不动系统设置）。
inline constexpr const char* kTimeZone = "Asia/Shanghai";
