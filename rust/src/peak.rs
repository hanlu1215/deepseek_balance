//! 峰 / 谷判定、法定节假日、倒计时 —— 逐条对应 C++ 版 peak.cpp。
//!
//! 高峰：周一至周五 09:00-12:00、14:00-18:00；周末和法定假日整日低谷。
//! Windows 版隐含「本机就是北京时间」，所以直接用本机时区（和 C++ 版一致）。

use std::cell::Cell;
use std::time::{SystemTime, UNIX_EPOCH};

use windows::Win32::Foundation::{FILETIME, SYSTEMTIME};
use windows::Win32::System::Time::{FileTimeToSystemTime, SystemTimeToTzSpecificLocalTime};

#[derive(Clone, Copy, PartialEq, Eq)]
struct Date {
    year: i32,
    month: i32,
    day: i32,
}

/// 中国法定节假日（国务院办公厅 国办发明电〔2025〕7 号，2026 年）。
/// 高峰时段在这些日期内整日不生效。临近新年时记得补充。
const HOLIDAY_RANGES: &[(Date, Date)] = &[
    (Date { year: 2026, month: 1, day: 1 }, Date { year: 2026, month: 1, day: 3 }),    // 元旦
    (Date { year: 2026, month: 2, day: 15 }, Date { year: 2026, month: 2, day: 23 }),  // 春节
    (Date { year: 2026, month: 4, day: 4 }, Date { year: 2026, month: 4, day: 6 }),    // 清明
    (Date { year: 2026, month: 5, day: 1 }, Date { year: 2026, month: 5, day: 5 }),    // 劳动节
    (Date { year: 2026, month: 6, day: 19 }, Date { year: 2026, month: 6, day: 21 }),  // 端午
    (Date { year: 2026, month: 9, day: 25 }, Date { year: 2026, month: 9, day: 27 }),  // 中秋
    (Date { year: 2026, month: 10, day: 1 }, Date { year: 2026, month: 10, day: 7 }),  // 国庆
];

/// 高峰时段的整点区间（小时）。
const PEAK_WINDOWS: &[(i32, i32)] = &[(9, 12), (14, 18)];

struct LocalTime {
    year: i32,
    month: i32,
    day: i32,
    weekday: i32, // 0 = 周日
    hour: i32,
    minute: i32,
}

/// unix 秒 → FILETIME（1601 年起的 100ns 计数）。
fn filetime_of(unix_seconds: i64) -> FILETIME {
    let ticks = (unix_seconds + 11_644_473_600) as u64 * 10_000_000;
    FILETIME {
        dwLowDateTime: (ticks & 0xFFFF_FFFF) as u32,
        dwHighDateTime: (ticks >> 32) as u32,
    }
}

/// unix 秒 → 本机时区的日历时间。
fn local_time_of(unix_seconds: i64) -> Option<LocalTime> {
    let filetime = filetime_of(unix_seconds);
    let mut utc = SYSTEMTIME::default();
    let mut local = SYSTEMTIME::default();
    unsafe {
        if FileTimeToSystemTime(&filetime, &mut utc).is_err() {
            return None;
        }
        if SystemTimeToTzSpecificLocalTime(None, &utc, &mut local).is_err() {
            return None;
        }
    }
    Some(LocalTime {
        year: local.wYear as i32,
        month: local.wMonth as i32,
        day: local.wDay as i32,
        weekday: local.wDayOfWeek as i32,
        hour: local.wHour as i32,
        minute: local.wMinute as i32,
    })
}

/// a 不晚于 b。
fn not_after(a: Date, b: Date) -> bool {
    if a.year != b.year {
        return a.year < b.year;
    }
    if a.month != b.month {
        return a.month < b.month;
    }
    a.day <= b.day
}

fn is_holiday(local: &LocalTime) -> bool {
    let day = Date { year: local.year, month: local.month, day: local.day };
    HOLIDAY_RANGES
        .iter()
        .any(|(first, last)| not_after(*first, day) && not_after(day, *last))
}

fn is_peak_at(unix_seconds: i64) -> bool {
    let Some(local) = local_time_of(unix_seconds) else {
        return false;
    };

    if local.weekday == 0 || local.weekday == 6 {
        return false; // 周六周日整日低谷
    }
    if is_holiday(&local) {
        return false;
    }

    let minutes = local.hour * 60 + local.minute;
    PEAK_WINDOWS
        .iter()
        .any(|(start, end)| start * 60 <= minutes && minutes < end * 60)
}

fn to_unix(moment: SystemTime) -> i64 {
    moment
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn from_unix(unix_seconds: i64) -> SystemTime {
    UNIX_EPOCH + std::time::Duration::from_secs(unix_seconds.max(0) as u64)
}

/// 此刻是不是高峰。
pub fn is_peak(moment: SystemTime) -> bool {
    is_peak_at(to_unix(moment))
}

thread_local! {
    // next_transition 的按分钟缓存（只在 UI 线程调用）。
    static CACHED_KEY: Cell<i64> = const { Cell::new(0) };
    static CACHED_VALUE: Cell<i64> = const { Cell::new(0) };
}

/// 下一次峰↔谷切换的时刻。
pub fn next_transition(moment: SystemTime) -> SystemTime {
    let now = to_unix(moment);
    let key = now - now.rem_euclid(60); // 对齐到整分钟

    if let Some(hit) = CACHED_VALUE.with(|value| {
        let cached = value.get();
        if cached != 0 && CACHED_KEY.with(|k| k.get()) == key {
            Some(cached)
        } else {
            None
        }
    }) {
        return from_unix(hit);
    }

    let base = is_peak_at(key);
    let mut candidate = key + 60;
    for _ in 0..60 * 24 * 40 {
        // 最多扫 40 天，足够跨过任意假期
        if is_peak_at(candidate) != base {
            break;
        }
        candidate += 60;
    }

    CACHED_KEY.with(|k| k.set(key));
    CACHED_VALUE.with(|v| v.set(candidate));
    from_unix(candidate)
}

fn pad2(value: i64) -> String {
    format!("{value:02}")
}

/// 剩余秒数 → 「时:分:秒」。
pub fn format_remaining(seconds: f64) -> String {
    let total = seconds.max(0.0).ceil() as i64;
    let hours = total / 3600;
    let minutes = (total % 3600) / 60;
    let secs = total % 60;
    format!("{}:{}:{}", pad2(hours), pad2(minutes), pad2(secs))
}

/// 把倒计时里的数字都换成 8：量宽度用，秒数变化时徽标不会抖。
pub fn canonical_clock(clock: &str) -> String {
    clock
        .chars()
        .map(|c| if c.is_ascii_digit() { '8' } else { c })
        .collect()
}
