//! DeepSeek 余额小方块 —— Rust / Win32 版
//!
//! 显示内容只有两样：
//!     1. 余额数字（大字号，尽量占满卡片）
//!     2. 峰 / 谷 徽标 + 该时段剩余时长（时:分:秒）
//!
//! 交互：
//!     左键点击  -> 木鱼「笃」一声 + 轻微 Q 弹一下 + 刷新余额
//!     拖动      -> 移动窗口
//!     右键菜单  -> 刷新 / 置顶开关 / 点击音效开关 / 颜色轮换开关 / 退出
//!     Esc       -> 退出
//!
//! 配置：见 src/config.rs。命令行选项：
//!     --demo             用假数据渲染，不发起网络请求
//!     --self-test 秒     渲染指定秒数后退出
//!     --check-key        只检查 Key 和网络，不开窗口
//!     -h, --help         显示帮助

#![windows_subsystem = "windows"]

mod balance;
mod config;
mod cube;
mod pacer;
mod peak;
mod render;
mod sound;
mod util;

use windows::core::PCWSTR;
use windows::Win32::Foundation::HINSTANCE;
use windows::Win32::Graphics::GdiPlus::Ok as GDIP_OK;
use windows::Win32::Graphics::GdiPlus::{GdiplusShutdown, GdiplusStartup, GdiplusStartupInput};
use windows::Win32::Storage::FileSystem::{GetFileType, WriteFile, FILE_TYPE_CHAR, FILE_TYPE_DISK, FILE_TYPE_PIPE};
use windows::Win32::System::Console::{AttachConsole, GetStdHandle, WriteConsoleW, ATTACH_PARENT_PROCESS, STD_OUTPUT_HANDLE};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::{
    MessageBoxW, SetProcessDPIAware, MB_ICONERROR, MB_ICONINFORMATION, MB_OK, MB_TOPMOST,
};

use cube::Cube;

const APP_TITLE: &str = "DeepSeek 余额";

#[derive(Default)]
struct Options {
    demo: bool,
    check_key: bool,
    help: bool,
    self_test: f64,
}

fn parse_command_line() -> Options {
    let mut options = Options::default();
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let argument = &args[i];
        match argument.as_str() {
            "--demo" => options.demo = true,
            "--check-key" => options.check_key = true,
            "--help" | "-h" | "/?" => options.help = true,
            "--self-test" if i + 1 < args.len() => {
                i += 1;
                options.self_test = args[i].parse().unwrap_or(0.0);
            }
            _ => {
                if let Some(value) = argument.strip_prefix("--self-test=") {
                    options.self_test = value.parse().unwrap_or(0.0);
                }
            }
        }
        i += 1;
    }
    options
}

/// 打包成窗口程序（没有控制台）时，把父进程的控制台借过来用。
fn attach_parent_console() {
    unsafe {
        let _ = AttachConsole(ATTACH_PARENT_PROCESS);
    }
}

/// 尽量把文字报给用户：有控制台就打出来，没有就弹对话框。
fn report(title: &str, lines: &[String]) {
    if lines.is_empty() {
        return;
    }
    let text = lines.join("\n") + "\n";

    unsafe {
        if let Ok(out) = GetStdHandle(STD_OUTPUT_HANDLE) {
            if !out.is_invalid() {
                match GetFileType(out) {
                    FILE_TYPE_CHAR => {
                        let wide = util::to_wide(&text);
                        let mut written = 0u32;
                        if WriteConsoleW(out, &wide[..wide.len() - 1], Some(&mut written), None).is_ok() {
                            return;
                        }
                    }
                    t if t == FILE_TYPE_PIPE || t == FILE_TYPE_DISK => {
                        let mut written = 0u32;
                        if WriteFile(out, Some(text.as_bytes()), Some(&mut written), None).is_ok() {
                            return;
                        }
                    }
                    _ => {}
                }
            }
        }

        MessageBoxW(
            None,
            PCWSTR(util::to_wide(&text).as_ptr()),
            PCWSTR(util::to_wide(title).as_ptr()),
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST,
        );
    }
}

fn show_help() {
    report(
        &format!("{APP_TITLE} · 帮助"),
        &[
            format!("{APP_TITLE}（Rust / Win32 版）"),
            String::new(),
            "用法：deepseek_balance_cube.exe [选项]".to_string(),
            String::new(),
            "  --demo             用假数据渲染，不发起网络请求".to_string(),
            "  --self-test 秒     渲染指定秒数后退出".to_string(),
            "  --check-key        只检查 Key 和网络，不开窗口".to_string(),
            "  -h, --help         显示这份帮助".to_string(),
            String::new(),
            "左键点击：木鱼音效 + Q 弹一下 + 刷新余额".to_string(),
            "拖动：移动窗口      右键：菜单      Esc：退出".to_string(),
            String::new(),
            "配置在 src/config.rs（API Key、刷新节奏、配色、尺寸等）。".to_string(),
        ],
    );
}

/// 命令行排查：Key 从哪儿来、长什么样、接口到底怎么回。
/// 0 = 一切正常，1 = 请求失败，2 = 根本没找到 Key。
fn check_api_key() -> i32 {
    let raw = balance::raw_env_key();
    let key = balance::resolve_api_key();
    let mut lines = vec![format!("Key 来源：{}", balance::key_source())];

    if key.is_empty() {
        lines.push(String::new());
        lines.push("没有读到 Key。两种给 Key 的方式（二选一）：".to_string());
        lines.push("  1) 把 src/config.rs 里的 API_KEY_LITERAL 改成 \"sk-你的key\"，重新编译".to_string());
        lines.push(format!(
            "  2) 设一个临时的环境变量 {}，然后「在同一个窗口里」启动：",
            config::API_KEY_ENV
        ));
        lines.push(format!("       PowerShell :  $env:{} = \"sk-你的key\"", config::API_KEY_ENV));
        lines.push(format!("       cmd        :  set {}=sk-你的key", config::API_KEY_ENV));
        lines.push(format!("       Git Bash   :  export {}=sk-你的key", config::API_KEY_ENV));
        lines.push(String::new());
        lines.push("两个最容易踩的坑：".to_string());
        lines.push("  · PowerShell 里的 set 不是 cmd 的 set：`set 变量=值` 不会设置环境变量，".to_string());
        lines.push("    只是建了个名字里带等号的 PowerShell 变量，$env: 里还是空的。".to_string());
        lines.push("  · 临时变量只活在那个窗口里：双击图标、换窗口、从编辑器启动都读不到。".to_string());
        report(&format!("{APP_TITLE} · Key 检查"), &lines);
        return 2;
    }

    lines.push(format!("Key 掩码：{}    长度：{}", balance::mask_key(&key), key.chars().count()));
    if !raw.is_empty() && balance::clean_key(&raw) != raw {
        lines.push("提醒：环境变量的值首尾多了空白或引号，程序已自动清理；建议重设成干净的值。".to_string());
    }
    if !key.starts_with("sk-") {
        lines.push("提醒：DeepSeek 的 Key 一般以 sk- 开头，这个看着不太像。".to_string());
    }

    lines.push(format!("正在请求 https://{}{} …", config::API_HOST, config::API_PATH));
    let result = balance::fetch_balance(&key);
    if !result.ok {
        lines.push(format!("结果：失败 —— {}", result.error));
        lines.push(String::new());
        lines.push("对号入座：".to_string());
        lines.push("  · Key 无效 / 401        → Key 抄错了、带引号了，或者已经被删掉".to_string());
        lines.push("  · 余额不足 402          → 账户欠费，去官网充值".to_string());
        lines.push("  · 网络不可达            → 断网、需要代理/VPN，或防火墙拦了 api.deepseek.com".to_string());
        lines.push("  · 请求过频 429          → 等一下再试".to_string());
        report(&format!("{APP_TITLE} · Key 检查"), &lines);
        return 1;
    }

    let mut currency = String::new();
    if let Ok(document) = serde_json::from_str::<serde_json::Value>(&result.payload) {
        if let Some(code) = document
            .get("balance_infos")
            .and_then(|infos| infos.get(0))
            .and_then(|first| first.get("currency"))
            .and_then(|c| c.as_str())
        {
            currency = code.to_string();
        }
        let mut summary = format!("结果：成功，余额 {}", balance::first_balance(&document));
        if !currency.is_empty() {
            summary.push(' ');
            summary.push_str(&currency);
        }
        lines.push(summary);
    } else {
        lines.push("结果：成功（但返回不是合法 JSON）".to_string());
    }
    report(&format!("{APP_TITLE} · Key 检查"), &lines);
    0
}

fn real_main() -> i32 {
    let options = parse_command_line();

    if options.help || options.check_key {
        attach_parent_console();
        if options.help {
            show_help();
            return 0;
        }
        return check_api_key();
    }

    // 清单里已经声明了 dpiAware；这里再兜一次底，万一清单没生效也能有正确的尺寸。
    unsafe {
        let _ = SetProcessDPIAware();
    }

    let mut gdiplus_token = 0usize;
    let startup_input = GdiplusStartupInput {
        GdiplusVersion: 1,
        DebugEventCallback: 0,
        SuppressBackgroundThread: false.into(),
        SuppressExternalCodecs: false.into(),
    };
    let status = unsafe { GdiplusStartup(&mut gdiplus_token, &startup_input, std::ptr::null_mut()) };
    if status != GDIP_OK {
        unsafe {
            MessageBoxW(
                None,
                PCWSTR(util::to_wide("GDI+ 初始化失败，程序无法绘制界面。").as_ptr()),
                PCWSTR(util::to_wide(APP_TITLE).as_ptr()),
                MB_OK | MB_ICONERROR,
            );
        }
        return 2;
    }

    let mut exit_code = 0;
    {
        // 这段作用域很关键：所有 GDI+ 对象必须在 GdiplusShutdown 之前析构干净。
        let instance = HINSTANCE(
            unsafe { GetModuleHandleW(None) }
                .map(|m| m.0)
                .unwrap_or_default(),
        );
        let mut cube = Box::new(Cube::new(options.demo, options.self_test));
        if !cube.create(instance) {
            unsafe {
                MessageBoxW(
                    None,
                    PCWSTR(util::to_wide("无法创建窗口。").as_ptr()),
                    PCWSTR(util::to_wide(APP_TITLE).as_ptr()),
                    MB_OK | MB_ICONERROR,
                );
            }
            exit_code = 2;
        } else {
            cube.run();
        }
    }

    unsafe {
        GdiplusShutdown(gdiplus_token);
    }
    exit_code
}

fn main() {
    std::process::exit(real_main());
}
