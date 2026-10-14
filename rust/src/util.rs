//! 杂项工具：宽字符串转换、trim、按码点截断、可执行文件目录、Win32 错误文案。

use std::path::PathBuf;

use windows::Win32::Foundation::LocalFree;
use windows::Win32::System::Diagnostics::Debug::{
    FormatMessageW, FORMAT_MESSAGE_ALLOCATE_BUFFER, FORMAT_MESSAGE_FROM_SYSTEM,
    FORMAT_MESSAGE_IGNORE_INSERTS,
};
use windows::core::PWSTR;

/// UTF-8 &str → 以 0 结尾的 UTF-16（Win32 API 要的形式）。
pub fn to_wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(std::iter::once(0)).collect()
}

/// C++ 版 trimWide 的同构实现：只认 ASCII 空白那六个字符。
pub fn trim(text: &str) -> &str {
    text.trim_matches(|c| matches!(c, ' ' | '\t' | '\n' | '\r' | '\x0c' | '\x0b'))
}

/// 按码点截断（C++ Windows 版按 wchar_t 数、Linux 版按码点，这里对齐码点版本，
/// 不会把任何字符劈成半个）。
pub fn truncate_chars(text: &str, count: usize) -> String {
    text.chars().take(count).collect()
}

/// exe 所在目录（末尾不带分隔符）。
pub fn executable_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_default()
}

/// MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)。
const LANG_NEUTRAL_DEFAULT: u32 = 0x0400;

/// Win32 错误码 → 系统给的错误文案（取不到就回退成「错误码 N」）。
pub fn win32_error_message(code: u32) -> String {
    let mut raw: *mut u16 = std::ptr::null_mut();
    let length = unsafe {
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            None,
            code,
            LANG_NEUTRAL_DEFAULT,
            PWSTR(&mut raw as *mut *mut u16 as *mut u16),
            0,
            None,
        )
    };

    let message = if length > 0 && !raw.is_null() {
        let slice = unsafe { std::slice::from_raw_parts(raw, length as usize) };
        let text = String::from_utf16_lossy(slice);
        unsafe { LocalFree(Some(windows::Win32::Foundation::HLOCAL(raw as *mut _))) };
        text
    } else {
        format!("错误码 {code}")
    };
    trim(&message).to_string()
}
