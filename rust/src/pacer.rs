//! 自适应刷新节奏 —— 逐行对应 C++ 版 pacer.cpp。
//!
//! 平时 5 分钟看一眼；余额在掉就砍半（最快 30 秒）；余额不动慢慢放回 5 分钟；
//! 每次再加 ±5% 随机抖动，不固定踩在同一秒。

use rand::rngs::SmallRng;
use rand::{Rng, SeedableRng};

use crate::config;

pub struct RefreshPacer {
    minimum: f64,
    maximum: f64,
    interval: f64,
    rate: f64,
    last_balance: f64,
    last_moment: f64,
    has_last: bool,
    rng: SmallRng,
}

impl RefreshPacer {
    pub fn new(minimum: f64, maximum: f64) -> Self {
        Self {
            minimum,
            maximum,
            interval: maximum,
            rate: 0.0,
            last_balance: 0.0,
            last_moment: 0.0,
            has_last: false,
            rng: SmallRng::from_rng(&mut rand::rng()),
        }
    }

    pub fn observe(&mut self, has_balance: bool, balance: f64, now_seconds: f64) -> f64 {
        if !has_balance {
            return self.delay();
        }

        let now = now_seconds;
        if self.has_last {
            let elapsed = (now - self.last_moment).max(1.0);
            let spent = (self.last_balance - balance).max(0.0); // 充值导致的变多不算消耗
            let instant = spent / elapsed;
            let blend = 1.0 - (-elapsed / config::REFRESH_RATE_TAU).exp();
            self.rate += (instant - self.rate) * blend;

            let mut target = if self.rate > 0.0 {
                config::BALANCE_STEP / self.rate
            } else {
                self.maximum
            };
            if spent > 0.0 {
                target = target.min(self.interval * config::REFRESH_SHRINK);
            } else if elapsed >= self.minimum.max(self.interval * 0.5) {
                // 只有观察窗口够长，「余额没变」才真的说明消耗慢；
                // 手动连点这种 1 秒的窗口不做判断，免得把间隔越点越长
                target = target.max(self.interval * config::REFRESH_GROW);
            }
            self.interval = target.clamp(self.minimum, self.maximum);
        }

        self.last_balance = balance;
        self.last_moment = now;
        self.has_last = true;
        self.delay()
    }

    pub fn delay(&mut self) -> f64 {
        let jitter = self.rng.random_range(
            (1.0 - config::REFRESH_JITTER)..=(1.0 + config::REFRESH_JITTER),
        );
        self.interval * jitter
    }
}
