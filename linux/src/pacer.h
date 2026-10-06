#pragma once

#include <random>

// 按「烧钱速度」自动决定下一次刷新间隔（30 秒 ~ 5 分钟）。
//
// 思路：
//   1. 每次拿到新余额，用 消耗量 / 间隔 估一个即时速率，再做时间加权平滑，
//      这样偶尔一次波动不会立刻改变节奏，但持续消耗会攒起来；
//   2. 间隔基线 = 一个最小可见变化（kBalanceStep 元）÷ 当前速率，
//      也就是「大概花掉 0.01 元就去看一眼」——速率越快间隔越短；
//   3. 这次余额变了就把间隔再砍一半（刚花过钱，盯紧点），
//      没变就放长 1.3 倍（钱没动，少打扰）——两者夹在 30 秒和 5 分钟之间；
//   4. 最后加随机抖动，避免每次都踩在同一秒。
class RefreshPacer {
public:
    explicit RefreshPacer(double minimum, double maximum);

    // 喂入最新余额，返回下一次该等多少秒（已含随机抖动）。
    // balance 传 false 表示这次没拿到数字，维持当前节奏。
    double observe(bool hasBalance, double balance, double nowSeconds);

    // 当前间隔 + 随机抖动（秒）。
    double delay();

private:
    double minimum_;
    double maximum_;
    double interval_;      // 还没测出速率时先按最慢的来
    double rate_ = 0.0;    // 平滑后的消耗速率（元/秒）
    bool hasLast_ = false;
    double lastBalance_ = 0.0;
    double lastMoment_ = 0.0;
    std::mt19937 rng_;
};
