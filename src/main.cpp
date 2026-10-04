// DeepSeek 余额小方块 —— C++ / Win32 版
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
// 配置：见 src/config.h。命令行选项：
//     --demo             用假数据渲染，不发起网络请求
//     --self-test 秒     渲染指定秒数后退出
//     --check-key        只检查 Key 和网络，不开窗口
//     -h, --help         显示帮助

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// cube.h -> render.h 会先把 std::min/std::max 引到全局再包进 gdiplus.h，
// 所以这里不单独包含 <gdiplus.h>，免得顺序反了编不过。
#include "balance.h"
#include "config.h"
#include "cube.h"
#include "json.h"
#include "util.h"

namespace {

constexpr wchar_t kAppTitle[] = L"DeepSeek 余额";

struct Options {
    bool demo = false;
    bool checkKey = false;
    bool help = false;
    double selfTest = 0.0;
};

Options parseCommandLine() {
    Options options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return options;

    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--demo") {
            options.demo = true;
        } else if (argument == L"--check-key") {
            options.checkKey = true;
        } else if (argument == L"--help" || argument == L"-h" || argument == L"/?") {
            options.help = true;
        } else if (argument == L"--self-test" && i + 1 < argc) {
            options.selfTest = std::wcstod(argv[++i], nullptr);
        } else if (argument.rfind(L"--self-test=", 0) == 0) {
            options.selfTest = std::wcstod(argument.c_str() + 12, nullptr);
        }
    }

    LocalFree(argv);
    return options;
}

// 打包成窗口程序（没有控制台）时，把父进程的控制台借过来用。
//
// 千万别在这里顺手 freopen("CONOUT$", "w", stdout)：CRT 会把 fd 1 原本的句柄
// 关掉，而 GetStdHandle(STD_OUTPUT_HANDLE) 指的还是同一个句柄，关完直接变成
// FILE_TYPE_UNKNOWN，重定向到文件时就什么都写不出去了。
// AttachConsole 本身就会在进程没有标准句柄时把它们接到控制台上，够用了。
void attachParentConsole() {
    AttachConsole(ATTACH_PARENT_PROCESS);
}

// 尽量把文字报给用户：有控制台就打出来，没有就弹对话框。
void report(const std::wstring& title, const std::vector<std::wstring>& lines) {
    std::wstring text;
    for (const std::wstring& line : lines) {
        text += line;
        text += L"\n";
    }
    if (text.empty()) return;

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        const DWORD type = GetFileType(out);
        if (type == FILE_TYPE_CHAR) {
            DWORD written = 0;
            if (WriteConsoleW(out, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr)) {
                return;
            }
        } else if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
            const std::string utf8 = toUtf8(text);
            DWORD written = 0;
            WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            return;
        }
    }

    MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

void showHelp() {
    report(std::wstring(kAppTitle) + L" · 帮助", {
        std::wstring(kAppTitle) + L"（C++ / Win32 版）",
        L"",
        L"用法：deepseek_balance_cube.exe [选项]",
        L"",
        L"  --demo             用假数据渲染，不发起网络请求",
        L"  --self-test 秒     渲染指定秒数后退出",
        L"  --check-key        只检查 Key 和网络，不开窗口",
        L"  -h, --help         显示这份帮助",
        L"",
        L"左键点击：木鱼音效 + Q 弹一下 + 刷新余额",
        L"拖动：移动窗口      右键：菜单      Esc：退出",
        L"",
        L"配置在 src/config.h（API Key、刷新节奏、配色、尺寸等）。",
    });
}

// 命令行排查：Key 从哪儿来、长什么样、接口到底怎么回。
// 0 = 一切正常，1 = 请求失败，2 = 根本没找到 Key。
int checkApiKey() {
    const std::wstring raw = rawEnvKey();
    const std::wstring key = resolveApiKey();
    std::vector<std::wstring> lines;
    lines.push_back(L"Key 来源：" + keySource());

    if (key.empty()) {
        lines.push_back(L"");
        lines.push_back(L"没有读到 Key。两种给 Key 的方式（二选一）：");
        lines.push_back(L"  1) 把 src/config.h 里的 kApiKeyLiteral 改成 L\"sk-你的key\"，重新编译");
        lines.push_back(std::wstring(L"  2) 设一个临时的环境变量 ") + kApiKeyEnv + L"，然后「在同一个窗口里」启动：");
        lines.push_back(std::wstring(L"       PowerShell :  $env:") + kApiKeyEnv + L" = \"sk-你的key\"");
        lines.push_back(std::wstring(L"       cmd        :  set ") + kApiKeyEnv + L"=sk-你的key");
        lines.push_back(std::wstring(L"       Git Bash   :  export ") + kApiKeyEnv + L"=sk-你的key");
        lines.push_back(L"");
        lines.push_back(L"两个最容易踩的坑：");
        lines.push_back(L"  · PowerShell 里的 set 不是 cmd 的 set：`set 变量=值` 不会设置环境变量，");
        lines.push_back(L"    只是建了个名字里带等号的 PowerShell 变量，$env: 里还是空的。");
        lines.push_back(L"  · 临时变量只活在那个窗口里：双击图标、换窗口、从编辑器启动都读不到。");
        report(std::wstring(kAppTitle) + L" · Key 检查", lines);
        return 2;
    }

    lines.push_back(L"Key 掩码：" + maskKey(key) + L"    长度：" + std::to_wstring(key.size()));
    if (!raw.empty() && cleanKey(raw) != raw) {
        lines.push_back(L"提醒：环境变量的值首尾多了空白或引号，程序已自动清理；建议重设成干净的值。");
    }
    if (key.rfind(L"sk-", 0) != 0) {
        lines.push_back(L"提醒：DeepSeek 的 Key 一般以 sk- 开头，这个看着不太像。");
    }

    lines.push_back(std::wstring(L"正在请求 https://") + kApiHost + kApiPath + L" …");
    const FetchResult result = fetchBalance(key);
    if (!result.ok) {
        lines.push_back(L"结果：失败 —— " + result.error);
        lines.push_back(L"");
        lines.push_back(L"对号入座：");
        lines.push_back(L"  · Key 无效 / 401        → Key 抄错了、带引号了，或者已经被删掉");
        lines.push_back(L"  · 余额不足 402          → 账户欠费，去官网充值");
        lines.push_back(L"  · 网络不可达            → 断网、需要代理/VPN，或防火墙拦了 api.deepseek.com");
        lines.push_back(L"  · 请求过频 429          → 等一下再试");
        report(std::wstring(kAppTitle) + L" · Key 检查", lines);
        return 1;
    }

    std::wstring currency;
    json::Value document;
    if (json::parse(result.payload, document)) {
        const json::Value* infos = document.find("balance_infos");
        const json::Value* first = infos != nullptr ? infos->at(0) : nullptr;
        const json::Value* code = first != nullptr ? first->find("currency") : nullptr;
        if (code != nullptr) currency = toWide(code->asString(""));
    }

    std::wstring summary = std::wstring(L"结果：成功，余额 ") + toWide(firstBalance(document));
    if (!currency.empty()) {
        summary += L" ";
        summary += currency;
    }
    lines.push_back(summary);
    report(std::wstring(kAppTitle) + L" · Key 检查", lines);
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    const Options options = parseCommandLine();

    if (options.help || options.checkKey) {
        attachParentConsole();
        if (options.help) {
            showHelp();
            return 0;
        }
        return checkApiKey();
    }

    // 清单里已经声明了 dpiAware；这里再兜一次底，万一清单没生效也能有正确的尺寸。
    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+ 初始化失败，程序无法绘制界面。", kAppTitle,
                    MB_OK | MB_ICONERROR);
        return 2;
    }

    int exitCode = 0;
    {
        // 这段作用域很关键：所有 GDI+ 对象必须在 GdiplusShutdown 之前析构干净。
        Cube cube(options.demo, options.selfTest);
        if (!cube.create(instance)) {
            MessageBoxW(nullptr, L"无法创建窗口。", kAppTitle, MB_OK | MB_ICONERROR);
            exitCode = 2;
        } else {
            cube.run();
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return exitCode;
}
