#include "peak.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <utility>
#include <vector>

namespace {

struct Date {
    int year;
    int month;
    int day;
};

// 中国法定节假日（国务院办公厅 国办发明电〔2025〕7 号，2026 年）。
// 高峰时段在这些日期内整日不生效。临近新年时记得补充。
const std::vector<std::pair<Date, Date>>& holidayRanges() {
    static const std::vector<std::pair<Date, Date>> ranges = {
        {{2026, 1, 1},  {2026, 1, 3}},    // 元旦
        {{2026, 2, 15}, {2026, 2, 23}},   // 春节
        {{2026, 4, 4},  {2026, 4, 6}},    // 清明
        {{2026, 5, 1},  {2026, 5, 5}},    // 劳动节
        {{2026, 6, 19}, {2026, 6, 21}},   // 端午
        {{2026, 9, 25}, {2026, 9, 27}},   // 中秋
        {{2026, 10, 1}, {2026, 10, 7}},   // 国庆
    };
    return ranges;
}

// 高峰时段的整点区间（小时）。
constexpr std::pair<int, int> kPeakWindows[] = {{9, 12}, {14, 18}};

bool notAfter(const Date& a, const Date& b) {
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    return a.day <= b.day;
}

bool isHoliday(const std::tm& local) {
    const Date day{local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
    for (const auto& range : holidayRanges()) {
        if (notAfter(range.first, day) && notAfter(day, range.second)) return true;
    }
    return false;
}

// 用 POSIX 的 localtime_r：可重入，而且不用像 Windows 版那样在 localtime_s /
// localtime_r 之间挑（那两个的签名各家实现还不一致）。
bool localTimeOf(std::time_t moment, std::tm& out) {
    return ::localtime_r(&moment, &out) != nullptr;
}

bool isPeakAt(std::time_t moment) {
    std::tm local{};
    if (!localTimeOf(moment, local)) return false;

    if (local.tm_wday == 0 || local.tm_wday == 6) return false;   // 周六周日整日低谷
    if (isHoliday(local)) return false;

    const int minutes = local.tm_hour * 60 + local.tm_min;
    for (const auto& window : kPeakWindows) {
        if (window.first * 60 <= minutes && minutes < window.second * 60) return true;
    }
    return false;
}

std::string pad2(long long value) {
    std::string text = std::to_string(value);
    if (text.size() < 2) text.insert(text.begin(), '0');
    return text;
}

}  // namespace

bool isPeak(std::chrono::system_clock::time_point moment) {
    return isPeakAt(std::chrono::system_clock::to_time_t(moment));
}

std::chrono::system_clock::time_point nextTransition(std::chrono::system_clock::time_point moment) {
    const std::time_t now = std::chrono::system_clock::to_time_t(moment);
    const std::time_t key = now - (now % 60);   // 对齐到整分钟

    // 只留当前这一分钟，别让缓存无限长大
    static std::time_t cachedKey = 0;
    static std::time_t cachedValue = 0;
    if (cachedValue != 0 && cachedKey == key) {
        return std::chrono::system_clock::from_time_t(cachedValue);
    }

    const bool base = isPeakAt(key);
    std::time_t candidate = key + 60;
    for (int step = 0; step < 60 * 24 * 40; ++step) {   // 最多扫 40 天，足够跨过任意假期
        if (isPeakAt(candidate) != base) break;
        candidate += 60;
    }

    cachedKey = key;
    cachedValue = candidate;
    return std::chrono::system_clock::from_time_t(candidate);
}

std::string formatRemaining(double seconds) {
    const long long total = static_cast<long long>(std::ceil(std::max(0.0, seconds)));
    const long long hours = total / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long secs = total % 60;
    return pad2(hours) + ":" + pad2(minutes) + ":" + pad2(secs);
}

std::string canonicalClock(const std::string& clock) {
    std::string out;
    out.reserve(clock.size());
    for (const char c : clock) {
        out.push_back(c >= '0' && c <= '9' ? '8' : c);
    }
    return out;
}
