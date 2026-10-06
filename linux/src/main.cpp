// DeepSeek 余额小方块 —— C++ / Linux (GTK3) 版
//
// 显示内容只有两样：
//     1. 余额数字（大字号，尽量占满卡片）
//     2. 峰 / 谷 徽标 + 该时段剩余时长（时:分:秒）
//
// 交互：
//     左键点击  -> 木鱼「笃」一声 + 轻微 Q 弹一下 + 刷新余额
//     拖动      -> 移动窗口
//     右键菜单  -> 刷新 / 置顶开关 / 点击音效开关 / 颜色轮换开关 / 退出
//     Esc       -> 退出
//
// 配置：见 config.h。命令行选项：
//     --demo               用假数据渲染，不发起网络请求
//     --self-test 秒       渲染指定秒数后退出
//     --check-key          只检查 Key 和网络，不开窗口
//     --render-png 路径    把卡片离屏渲染成 PNG（不开窗口，验证排版用）
//     -h, --help           显示帮助
//
// 和 Windows 版的唯一行为差异：启动时把进程时区设成 Asia/Shanghai，
// 因为峰谷时段是按北京时间硬编码的。详见 config.h 的 kTimeZone。

#include <gtk/gtk.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "balance.h"
#include "config.h"
#include "cube.h"
#include "json.h"
#include "render.h"
#include "util.h"

namespace {

constexpr const char* kAppTitle = "DeepSeek 余额";

struct Options {
    bool demo = false;
    bool checkKey = false;
    bool help = false;
    bool foreground = false;
    double selfTest = 0.0;
    std::string renderPng;
    std::string renderPngText;
    double renderPngAt = 0.0;    // unix 秒；0 = 用当前时间
    int renderPngScale = 1;
};

// 把自己转成后台进程：fork 两次 + setsid，彻底脱离控制终端。
//
// 图形程序从终端启动时，shell 会一直等着它退出，那个终端就等于被占住了。
// 这里 fork 完就让父进程立刻退出，shell 提示符马上回来，挂件自己留在后台跑。
//
// 标准流一律接到 /dev/null：既不再拽着终端，也免得后台进程往终端里插字。
// 代价是启动失败时的报错也看不见了，所以留了 --foreground。
void detachToBackground() {
    const pid_t first = fork();
    if (first < 0) return;                 // fork 失败就当没这回事，老老实实在前台跑
    if (first > 0) _exit(EXIT_SUCCESS);    // 父进程退出，shell 提示符立刻回来

    (void)setsid();                        // 自成会话，脱离控制终端

    // 再 fork 一次：确保留下来的不是会话首进程，以后也不会重新拿到控制终端
    const pid_t second = fork();
    if (second > 0) _exit(EXIT_SUCCESS);

    const int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) close(devNull);
    }
}

Options parseCommandLine(int argc, char** argv) {
    Options options;

    // 取下一个参数（缺了就保持原样）
    auto takeValue = [&](int& i) -> const char* {
        return (i + 1 < argc) ? argv[++i] : nullptr;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--demo") {
            options.demo = true;
        } else if (argument == "--check-key") {
            options.checkKey = true;
        } else if (argument == "--foreground" || argument == "-f") {
            options.foreground = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--self-test") {
            if (const char* value = takeValue(i)) options.selfTest = std::strtod(value, nullptr);
        } else if (argument.rfind("--self-test=", 0) == 0) {
            options.selfTest = std::strtod(argument.c_str() + 12, nullptr);
        } else if (argument == "--render-png") {
            if (const char* value = takeValue(i)) options.renderPng = value;
        } else if (argument.rfind("--render-png=", 0) == 0) {
            options.renderPng = argument.substr(13);
        } else if (argument == "--render-png-text") {
            if (const char* value = takeValue(i)) options.renderPngText = value;
        } else if (argument == "--render-png-at") {
            if (const char* value = takeValue(i)) options.renderPngAt = std::strtod(value, nullptr);
        } else if (argument == "--render-png-scale") {
            if (const char* value = takeValue(i)) options.renderPngScale = std::atoi(value);
        }
    }
    return options;
}

void showHelp() {
    std::printf(
        "%s（C++ / Linux GTK3 版）\n"
        "\n"
        "用法：deepseek_balance_cube [选项]\n"
        "\n"
        "启动后会自动转入后台，不占用终端（想留在前台用 --foreground）。\n"
        "\n"
        "  --demo                用假数据渲染，不发起网络请求\n"
        "  --foreground, -f      留在前台运行，报错能直接看到（调试用）\n"
        "  --self-test 秒        渲染指定秒数后退出\n"
        "  --check-key           只检查 Key 和网络，不开窗口\n"
        "  --render-png 路径     把卡片离屏渲染成 PNG，不开窗口（验证排版用）\n"
        "  --render-png-text 文字  配合 --render-png：显示指定的文字（试错误短句的长度）\n"
        "  --render-png-at unix秒  配合 --render-png：按指定时刻算峰谷（试长假的三位数倒计时）\n"
        "  --render-png-scale N  配合 --render-png：按 N 倍分辨率渲染（默认 1）\n"
        "  -h, --help            显示这份帮助\n"
        "\n"
        "左键点击：木鱼音效 + Q 弹一下 + 刷新余额\n"
        "拖动：移动窗口      右键：菜单      Esc：退出\n"
        "\n"
        "配置在 config.h（API Key、刷新节奏、配色、尺寸等）。\n",
        kAppTitle);
}

// 命令行排查：Key 从哪儿来、长什么样、接口到底怎么回。
// 0 = 一切正常，1 = 请求失败，2 = 根本没找到 Key。
int checkApiKey() {
    const std::string raw = rawEnvKey();
    const std::string key = resolveApiKey();

    std::printf("Key 来源：%s\n", keySource().c_str());

    if (key.empty()) {
        std::printf(
            "\n"
            "没有读到 Key。两种给 Key 的方式（二选一）：\n"
            "  1) 把 config.h 里的 kApiKeyLiteral 改成 \"sk-你的key\"，重新编译\n"
            "  2) 设一个临时的环境变量 %s，然后「在同一个终端里」启动：\n"
            "       export %s=sk-你的key\n"
            "       ./build/bin/deepseek_balance_cube\n"
            "\n"
            "想让变量一直有效就写进 ~/.bashrc 或 ~/.profile（注意别把 Key 提交进仓库）。\n",
            kApiKeyEnv, kApiKeyEnv);
        return 2;
    }

    std::printf("Key 掩码：%s    长度：%zu\n", maskKey(key).c_str(), key.size());
    if (!raw.empty() && cleanKey(raw) != raw) {
        std::printf("提醒：环境变量的值首尾多了空白或引号，程序已自动清理；建议重设成干净的值。\n");
    }
    if (key.rfind("sk-", 0) != 0) {
        std::printf("提醒：DeepSeek 的 Key 一般以 sk- 开头，这个看着不太像。\n");
    }

    std::printf("正在请求 https://%s%s …\n", kApiHost, kApiPath);
    const FetchResult result = fetchBalance(key);
    if (!result.ok) {
        std::printf(
            "结果：失败 —— %s\n"
            "\n"
            "对号入座：\n"
            "  · Key 无效 / 401        → Key 抄错了、带引号了，或者已经被删掉\n"
            "  · 余额不足 / 402        → 账户欠费，去官网充值\n"
            "  · 网络不可达            → 断网、需要代理/VPN，或防火墙拦了 api.deepseek.com\n"
            "  · 请求过频 / 429        → 等一下再试\n",
            result.error.c_str());
        return 1;
    }

    std::string currency;
    json::Value document;
    if (json::parse(result.payload, document)) {
        const json::Value* infos = document.find("balance_infos");
        const json::Value* first = infos != nullptr ? infos->at(0) : nullptr;
        const json::Value* code = first != nullptr ? first->find("currency") : nullptr;
        if (code != nullptr) currency = code->asString("");
    }

    std::string summary = "结果：成功，余额 " + firstBalance(document);
    if (!currency.empty()) {
        summary += " ";
        summary += currency;
    }
    std::printf("%s\n", summary.c_str());
    return 0;
}

// 把卡片画到一张离屏的 cairo 图上存成 PNG。
//
// 用的是窗口 draw 信号里同一个 paintCard()，所以这里看到的排版就是屏幕上看到的。
// 好处是验证排版不用截屏、不用起窗口、也不需要 DISPLAY，SSH 里就能跑。
int renderPng(const Options& options) {
    const int width = cardTotalWidth();
    const int height = cardTotalHeight();
    const int scale = std::max(1, options.renderPngScale);

    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "无法创建画布。\n");
        cairo_surface_destroy(surface);
        return 1;
    }

    cairo_t* cr = cairo_create(surface);
    // 模拟 GDK 在 HiDPI 下对 draw 画布的预缩放：几何和文字会一起放大。
    if (scale != 1) cairo_scale(cr, scale, scale);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    PangoContext* context = pango_font_map_create_context(pango_cairo_font_map_get_default());
    cairo_font_options_t* fontOptions = cairo_font_options_create();
    cairo_font_options_set_antialias(fontOptions, CAIRO_ANTIALIAS_GRAY);
    pango_cairo_context_set_font_options(context, fontOptions);
    cairo_font_options_destroy(fontOptions);

    CardContent content;
    content.amount = options.demo ? "18.23" : "--";
    if (!options.renderPngText.empty()) content.amount = options.renderPngText;
    content.now = options.renderPngAt > 0.0
                      ? std::chrono::system_clock::from_time_t(static_cast<std::time_t>(options.renderPngAt))
                      : std::chrono::system_clock::now();

    paintCard(cr, context, content);

    const cairo_status_t status = cairo_surface_write_to_png(surface, options.renderPng.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(context);

    if (status != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "写入 %s 失败：%s\n", options.renderPng.c_str(),
                     cairo_status_to_string(status));
        return 1;
    }

    std::printf("已写入 %s（%dx%d，scale=%d）\n", options.renderPng.c_str(), width * scale,
                height * scale, scale);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // 播放器提前退出时，往它的 stdin 写数据会收到 SIGPIPE，默认动作是直接杀掉进程。
    // 必须忽略掉：write 会返回 EPIPE，那边已经处理了。放在最前面，越早越好。
    std::signal(SIGPIPE, SIG_IGN);

    // 强制 X11 后端。原因：原生 Wayland 下合成器不允许客户端自己定位窗口，
    // 也没有置顶协议，「贴右上角 / 拖动移动 / 置顶」会全部失效。
    // 有 DISPLAY（说明跑着 XWayland 或真 Xorg）就切过去；没有就让它自己选。
    if (g_getenv("GDK_BACKEND") == nullptr && g_getenv("DISPLAY") != nullptr) {
        g_setenv("GDK_BACKEND", "x11", TRUE);
    }

    // 峰谷时段是按北京时间硬编码的，进程时区统一成东八区（只影响本进程，不动系统）。
    // 要在任何 localtime 调用之前设好。
    g_setenv("TZ", kTimeZone, TRUE);
    ::tzset();

    // 代理：GNOME 下 GIO 默认用「读系统代理设置」那个解析器，会无视 http_proxy /
    // https_proxy 环境变量。Linux 上很多人就是靠环境变量配代理的（curl、wget 都认），
    // 所以设了这几个变量就切到「读环境变量」的解析器；没设就维持默认（等价 Windows
    // 版 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 的「跟随系统代理设置」）。
    // 必须赶在第一次建立网络连接之前设好。
    if (g_getenv("GIO_USE_PROXY_RESOLVER") == nullptr &&
        (g_getenv("http_proxy") != nullptr || g_getenv("https_proxy") != nullptr ||
         g_getenv("all_proxy") != nullptr)) {
        g_setenv("GIO_USE_PROXY_RESOLVER", "gioenvironmentproxy", TRUE);
    }

    const Options options = parseCommandLine(argc, argv);

    // --check-key / --render-png / -h 都是要往终端打印结果的，不能甩到后台去。
    if (options.help) {
        showHelp();
        return 0;
    }
    if (options.checkKey) {
        return checkApiKey();
    }
    if (!options.renderPng.empty()) {
        return renderPng(options);
    }

    // --self-test 是给脚本/测试用的，得留在前台让调用方等得到。
    if (!options.foreground && options.selfTest <= 0.0) {
        // 转后台之后报错就看不见了，所以先把最常见的「根本没有显示环境」挡在前面。
        if (g_getenv("DISPLAY") == nullptr && g_getenv("WAYLAND_DISPLAY") == nullptr) {
            std::fprintf(stderr,
                         "%s：没有 DISPLAY 也没有 WAYLAND_DISPLAY，界面起不来。\n",
                         kAppTitle);
            return 2;
        }
        std::printf("%s 已在后台运行（想留在前台加 --foreground）。\n", kAppTitle);
        std::fflush(stdout);
        detachToBackground();
    }

    if (!gtk_init_check(&argc, &argv)) {
        std::fprintf(stderr,
                     "%s：连不上显示服务器，界面起不来。\n"
                     "  · 在纯命令行 / SSH 里想验证排版，用 --render-png 路径\n"
                     "  · 想查 Key 和网络，用 --check-key\n",
                     kAppTitle);
        return 2;
    }

    if (g_getenv("DISPLAY") == nullptr) {
        std::fprintf(stderr,
                     "提示：没有 DISPLAY，走的不是 X11 后端。窗口位置、置顶和拖动可能不生效。\n");
    }

    int exitCode = 0;
    {
        Cube cube(options.demo, options.selfTest);
        if (!cube.create()) {
            std::fprintf(stderr, "%s：无法创建窗口。\n", kAppTitle);
            exitCode = 2;
        } else {
            cube.run();
        }
    }
    return exitCode;
}
