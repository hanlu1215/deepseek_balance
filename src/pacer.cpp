#include "pacer.h"

#include <algorithm>
#include <cmath>

#include "config.h"

RefreshPacer::RefreshPacer(double minimum, double maximum)
    : minimum_(minimum),
      maximum_(maximum),
      interval_(maximum),
      rng_(std::random_device{}()) {}

double RefreshPacer::observe(bool hasBalance, double balance, double nowSeconds) {
    if (!hasBalance) return delay();

    const double now = nowSeconds;
    if (hasLast_) {
        const double elapsed = std::max(1.0, now - lastMoment_);
        const double spent = std::max(0.0, lastBalance_ - balance);   // 充值导致的变多不算消耗
        const double instant = spent / elapsed;
        const double blend = 1.0 - std::exp(-elapsed / kRefreshRateTau);
        rate_ += (instant - rate_) * blend;

        double target = rate_ > 0.0 ? kBalanceStep / rate_ : maximum_;
        if (spent > 0.0) {
            target = std::min(target, interval_ * kRefreshShrink);
        } else if (elapsed >= std::max(minimum_, interval_ * 0.5)) {
            // 只有观察窗口够长，「余额没变」才真的说明消耗慢；
            // 手动连点这种 1 秒的窗口不做判断，免得把间隔越点越长
            target = std::max(target, interval_ * kRefreshGrow);
        }
        interval_ = std::min(maximum_, std::max(minimum_, target));
    }

    lastBalance_ = balance;
    lastMoment_ = now;
    hasLast_ = true;
    return delay();
}

double RefreshPacer::delay() {
    std::uniform_real_distribution<double> jitter(1.0 - kRefreshJitter, 1.0 + kRefreshJitter);
    return interval_ * jitter(rng_);
}
