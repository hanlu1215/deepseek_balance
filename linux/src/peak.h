#pragma once

#include <chrono>
#include <string>

// 高峰时段（北京时间）：周一至周五 09:00-12:00、14:00-18:00，法定节假日整日低谷。
//
// 注意：这里的「北京时间」是靠进程时区（见 config.h 的 kTimeZone）实现的。
// 启动时会把 TZ 设成 Asia/Shanghai，否则机器时区不是东八区时结果会错。

// 该时刻是否处于高峰计费时段。
bool isPeak(std::chrono::system_clock::time_point moment);

// 下一次 峰<->谷 切换的时刻。结果按分钟缓存：弹性动画一秒内要重绘几十次，
// 而长假里这个前向扫描要跑上万次，缓存后同一分钟内只算一次。
std::chrono::system_clock::time_point nextTransition(std::chrono::system_clock::time_point moment);

// 剩余时长 -> 时:分:秒（跨天时小时数继续累加，例如 49:30:05）。
std::string formatRemaining(double seconds);

// 把倒计时里的数字都换成等宽的 8：徽标宽度就不会随秒数跳动。
std::string canonicalClock(const std::string& clock);
