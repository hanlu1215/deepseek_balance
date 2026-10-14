//! 余额查询：WinHTTP 请求、Key 解析、错误归类 —— 逐条对应 C++ 版 balance.cpp。

use windows::core::PCWSTR;
use windows::Win32::Foundation::GetLastError;
use windows::Win32::Networking::WinHttp::{
    WinHttpAddRequestHeaders, WinHttpCloseHandle, WinHttpConnect, WinHttpOpen,
    WinHttpOpenRequest, WinHttpQueryDataAvailable, WinHttpQueryHeaders, WinHttpReadData,
    WinHttpReceiveResponse, WinHttpSendRequest, WinHttpSetTimeouts,
    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_ADDREQ_FLAG_ADD, WINHTTP_ADDREQ_FLAG_REPLACE,
    WINHTTP_FLAG_SECURE, WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_QUERY_STATUS_CODE,
};

use crate::config;
use crate::util;

const DEMO_PAYLOAD: &str = "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\",\"total_balance\":\"18.23\",\
    \"granted_balance\":\"8.23\",\"topped_up_balance\":\"10.00\"}]}";

pub const INTERNET_DEFAULT_HTTPS_PORT: u16 = 443;

#[derive(Default)]
pub struct FetchResult {
    pub ok: bool,
    pub payload: String,
    pub error: String,
}

/// 一个能自动收尾的 WinHTTP 句柄。
struct Handle(isize);

impl Handle {
    fn new(raw: *mut core::ffi::c_void) -> Option<Self> {
        if raw.is_null() {
            None
        } else {
            Some(Self(raw as isize))
        }
    }
    fn get(&self) -> *mut core::ffi::c_void {
        self.0 as *mut core::ffi::c_void
    }
}

impl Drop for Handle {
    fn drop(&mut self) {
        if self.0 != 0 {
            unsafe {
                let _ = WinHttpCloseHandle(self.get());
            }
        }
    }
}

fn last_error_message() -> String {
    util::win32_error_message(unsafe { GetLastError() }.0)
}

/// 把错误压成一行短句子，塞得进卡片。
fn http_failure(status: u32, body: &str) -> String {
    let hint = match status {
        401 => "Key 无效".to_string(),
        402 => "余额不足".to_string(),
        403 => "无权限".to_string(),
        429 => "请求过频".to_string(),
        _ => format!("HTTP {status}"),
    };

    // 尽量把服务端给的 message 抠出来，抠不到就用原始正文的前 160 个字节。
    let mut message = String::new();
    if let Ok(document) = serde_json::from_str::<serde_json::Value>(body) {
        if let Some(text) = document
            .get("error")
            .and_then(|e| e.get("message"))
            .and_then(|m| m.as_str())
        {
            message = text.to_string();
        }
    }
    if message.is_empty() {
        let trimmed = util::trim(body);
        let cut: usize = trimmed.len().min(160);
        // 按字节截 160 时别劈开 UTF-8 字符
        let mut end = cut;
        while end > 0 && !trimmed.is_char_boundary(end) {
            end -= 1;
        }
        message = trimmed[..end].to_string();
    }

    if message.is_empty() {
        hint
    } else {
        format!("{hint}: {message}")
    }
}

/// 去掉首尾空白和成对的引号。
pub fn clean_key(value: &str) -> String {
    let mut text = util::trim(value).to_string();
    loop {
        let chars: Vec<char> = text.chars().collect();
        if chars.len() >= 2
            && chars[0] == chars[chars.len() - 1]
            && (chars[0] == '"' || chars[0] == '\'')
        {
            text = util::trim(&text[1..text.len() - 1]).to_string();
        } else {
            return text;
        }
    }
}

/// 环境变量里的原始 Key（没清理过的）。
pub fn raw_env_key() -> String {
    std::env::var(config::API_KEY_ENV).unwrap_or_default()
}

/// Key 解析：配置区字面量优先，留空回退到环境变量。
pub fn resolve_api_key() -> String {
    if !util::trim(config::API_KEY_LITERAL).is_empty() {
        return clean_key(config::API_KEY_LITERAL);
    }
    clean_key(&raw_env_key())
}

pub fn key_source() -> String {
    if !util::trim(config::API_KEY_LITERAL).is_empty() {
        return "程序里的 API_KEY".to_string();
    }
    if !resolve_api_key().is_empty() {
        return format!("系统环境变量 {}", config::API_KEY_ENV);
    }
    "没找到".to_string()
}

/// 掩码：前 4 后 2，中间打星。
pub fn mask_key(key: &str) -> String {
    let chars: Vec<char> = key.chars().collect();
    if chars.len() <= 8 {
        return "*".repeat(chars.len());
    }
    let head: String = chars[..4].iter().collect();
    let tail: String = chars[chars.len() - 2..].iter().collect();
    format!("{head}{}{tail}", "*".repeat(chars.len() - 6))
}

/// 把长错误压成 10 个字符的短句，显示在卡片上。
pub fn short_error(error: &str) -> String {
    let head = match error.find(':') {
        Some(colon) => util::trim(&error[..colon]),
        None => util::trim(error),
    };
    let head = if head.is_empty() { error } else { head };
    util::truncate_chars(head, 10)
}

/// 从接口返回里取第一个币种的余额。
pub fn first_balance(payload: &serde_json::Value) -> String {
    payload
        .get("balance_infos")
        .and_then(|infos| infos.get(0))
        .and_then(|first| first.get("total_balance"))
        .and_then(|total| total.as_str())
        .unwrap_or("--")
        .to_string()
}

/// 严格解析余额数字（尾部有垃圾字符就算失败）。
pub fn parse_balance(text: &str) -> Option<f64> {
    let trimmed = util::trim(text);
    if trimmed.is_empty() {
        return None;
    }
    trimmed.parse::<f64>().ok()
}

/// 真实请求 https://api.deepseek.com/user/balance。
///
/// DEFAULT_PROXY = 走用户当前的系统代理设置（代理/VPN 环境同样能用）。
pub fn fetch_balance(api_key: &str) -> FetchResult {
    let mut result = FetchResult::default();

    unsafe {
        let agent = util::to_wide("DeepSeekBalanceCube/1.0");
        let Some(session) = Handle::new(WinHttpOpen(
            PCWSTR(agent.as_ptr()),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            PCWSTR::null(),
            PCWSTR::null(),
            0,
        )) else {
            result.error = format!("网络不可达: {}", last_error_message());
            return result;
        };

        let timeout_ms = (config::TIMEOUT_SECONDS * 1000) as i32;
        let _ = WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        let host = util::to_wide(config::API_HOST);
        let Some(connection) = Handle::new(WinHttpConnect(
            session.get(),
            PCWSTR(host.as_ptr()),
            INTERNET_DEFAULT_HTTPS_PORT,
            0,
        )) else {
            result.error = format!("网络不可达: {}", last_error_message());
            return result;
        };

        let verb = util::to_wide("GET");
        let path = util::to_wide(config::API_PATH);
        let Some(request) = Handle::new(WinHttpOpenRequest(
            connection.get(),
            PCWSTR(verb.as_ptr()),
            PCWSTR(path.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            std::ptr::null(),
            WINHTTP_FLAG_SECURE,
        )) else {
            result.error = format!("网络不可达: {}", last_error_message());
            return result;
        };

        let headers = util::to_wide(&format!(
            "Accept: application/json\r\nAuthorization: Bearer {api_key}\r\n"
        ));
        if WinHttpAddRequestHeaders(
            request.get(),
            &headers[..headers.len() - 1],
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE,
        )
        .is_err()
        {
            result.error = format!("网络不可达: {}", last_error_message());
            return result;
        }

        if WinHttpSendRequest(request.get(), None, None, 0, 0, 0).is_err()
            || WinHttpReceiveResponse(request.get(), std::ptr::null_mut()).is_err()
        {
            result.error = format!("网络不可达: {}", last_error_message());
            return result;
        }

        let mut status = 0u32;
        let mut status_size = std::mem::size_of::<u32>() as u32;
        let _ = WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            PCWSTR::null(),
            Some(&mut status as *mut u32 as *mut core::ffi::c_void),
            &mut status_size,
            std::ptr::null_mut(),
        );

        let mut body: Vec<u8> = Vec::new();
        loop {
            let mut available = 0u32;
            if WinHttpQueryDataAvailable(request.get(), &mut available).is_err() || available == 0 {
                break;
            }
            let offset = body.len();
            body.resize(offset + available as usize, 0);
            let mut read = 0u32;
            if WinHttpReadData(
                request.get(),
                body[offset..].as_mut_ptr() as *mut core::ffi::c_void,
                available,
                &mut read,
            )
            .is_err()
            {
                break;
            }
            body.truncate(offset + read as usize);
            if read == 0 {
                break;
            }
        }

        let body_text = String::from_utf8_lossy(&body).into_owned();

        if !(200..300).contains(&status) {
            result.error = http_failure(status, &body_text);
            return result;
        }

        if serde_json::from_str::<serde_json::Value>(&body_text).is_err() {
            result.error = "返回不是合法 JSON".to_string();
            return result;
        }

        result.ok = true;
        result.payload = body_text;
        result
    }
}

pub fn demo_payload() -> &'static str {
    DEMO_PAYLOAD
}
