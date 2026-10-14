//! 木鱼波形合成 + 播放 —— 逐条对应 C++ 版 sound.cpp。
//!
//! 波形由代码现场合成，仓库里不需要任何音频素材文件；全程在内存里，不落盘。
//! exe 同目录若有 mokugyo_click.wav 则优先用用户的（只读不写）。

use std::path::PathBuf;

use rand::rngs::SmallRng;
use rand::{Rng, SeedableRng};
use windows::core::PCWSTR;
use windows::Win32::Media::Audio::{PlaySoundW, SND_FLAGS};

use crate::config;
use crate::util;

// mmsystem.h 的播放标志（windows crate 未收录，这里按 SDK 定义写死）。
const SND_ASYNC: u32 = 0x0001;
const SND_NODEFAULT: u32 = 0x0002;
const SND_MEMORY: u32 = 0x0004;
const SND_FILENAME: u32 = 0x0002_0000;

const PI: f64 = std::f64::consts::PI;
const SAMPLE_RATE: u32 = 44100;

/// 16bit 单声道样本 -> WAV 字节流。
fn wav_bytes(samples: &[i16], sample_rate: u32) -> Vec<u8> {
    let data_bytes = (samples.len() * 2) as u32;
    let mut out = Vec::with_capacity(44 + data_bytes as usize);

    let put32 = |out: &mut Vec<u8>, value: u32| out.extend_from_slice(&value.to_le_bytes());
    let put16 = |out: &mut Vec<u8>, value: u16| out.extend_from_slice(&value.to_le_bytes());

    out.extend_from_slice(b"RIFF");
    put32(&mut out, 36 + data_bytes);
    out.extend_from_slice(b"WAVE");

    out.extend_from_slice(b"fmt ");
    put32(&mut out, 16); // 子块大小
    put16(&mut out, 1); // PCM
    put16(&mut out, 1); // 单声道
    put32(&mut out, sample_rate);
    put32(&mut out, sample_rate * 2); // 字节率
    put16(&mut out, 2); // 块对齐
    put16(&mut out, 16); // 位深

    out.extend_from_slice(b"data");
    put32(&mut out, data_bytes);
    for &sample in samples {
        put16(&mut out, sample as u16);
    }
    out
}

/// 现场合成一记木鱼「笃」。
fn build_mokugyo_wav(volume: f64) -> Vec<u8> {
    let count = (SAMPLE_RATE as f64 * config::CLICK_SOUND_SECONDS) as usize;

    struct Partial {
        freq: f64, // 频率 Hz
        amp: f64,  // 振幅
        tau: f64,  // 衰减时间常数（秒）
    }
    let partials = [
        Partial { freq: 430.0, amp: 0.30, tau: 0.045 },  // 木腔的低频“闷”
        Partial { freq: 1180.0, amp: 1.00, tau: 0.030 }, // 主音
        Partial { freq: 1810.0, amp: 0.52, tau: 0.018 }, // 非谐波泛音，木头味就靠它
        Partial { freq: 2470.0, amp: 0.24, tau: 0.011 },
    ];

    // 固定种子：每次听起来都一样
    let mut rng = SmallRng::seed_from_u64(20261003);
    let noise: Vec<f64> = (0..count).map(|_| rng.random_range(-1.0..1.0)).collect();

    let mut raw = vec![0.0f64; count];
    let mut peak = 0.0f64;
    for i in 0..count {
        let t = i as f64 / SAMPLE_RATE as f64;
        let bend = 1.0 + 0.055 * (-t / 0.022).exp(); // 敲下去那一瞬间略高

        let mut value = 0.0;
        for partial in &partials {
            value += partial.amp * (-t / partial.tau).exp() * (2.0 * PI * partial.freq * bend * t).sin();
        }
        value += 0.55 * noise[i] * (-t / 0.0014).exp(); // 起手的敲击声
        value *= 1.0 - (-t / 0.0004).exp(); // 0.4ms 淡入，防爆音

        raw[i] = value;
        peak = peak.max(value.abs());
    }

    if peak <= 0.0 {
        peak = 1.0;
    }
    let scale = volume.clamp(0.0, 1.0) * 32767.0 * 0.92 / peak;

    let samples: Vec<i16> = raw.iter().map(|v| (v * scale).round() as i16).collect();
    wav_bytes(&samples, SAMPLE_RATE)
}

/// 存在、是 RIFF/WAVE、且带数据块，就当作可用（也允许用户自己换一个）。
fn looks_like_wav(path: &std::path::Path) -> bool {
    let Ok(meta) = std::fs::metadata(path) else {
        return false;
    };
    if !meta.is_file() || meta.len() <= 44 {
        return false;
    }
    let Ok(head) = std::fs::read(path).map(|data| data[..12].to_vec()) else {
        return false;
    };
    &head[0..4] == b"RIFF" && &head[8..12] == b"WAVE"
}

pub struct ClickSound {
    enabled: bool,
    volume: f64,
    ready: bool,
    path: Option<PathBuf>,
    memory: Vec<u8>,
}

impl ClickSound {
    pub fn new(enabled: bool, volume: f64) -> Self {
        Self {
            enabled,
            volume,
            ready: false,
            path: None,
            memory: Vec::new(),
        }
    }

    pub fn enabled(&self) -> bool {
        self.enabled
    }

    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }

    pub fn prepare(&mut self) {
        if self.ready {
            return;
        }
        self.ready = true;

        if !self.enabled {
            return;
        }

        // 用户自备的 wav 优先（只读，不会创建或修改任何文件），否则用内置合成的。
        // 合成只要几毫秒，所以完全可以启动时现算，不需要缓存到磁盘。
        let candidate = util::executable_dir().join(config::CLICK_SOUND_FILE);
        if looks_like_wav(&candidate) {
            self.path = Some(candidate);
            return;
        }
        self.memory = build_mokugyo_wav(self.volume);
    }

    pub fn play(&mut self) {
        if !self.enabled {
            return;
        }
        if !self.ready {
            self.prepare();
        }
        if !self.enabled {
            return;
        }

        if let Some(path) = &self.path {
            // 文件播放可以真异步：立即返回，不占线程、不卡界面
            let wide = util::to_wide(&path.display().to_string());
            unsafe {
                let _ = PlaySoundW(
                    PCWSTR(wide.as_ptr()),
                    None,
                    SND_FLAGS(SND_FILENAME | SND_ASYNC | SND_NODEFAULT),
                );
            }
        } else if !self.memory.is_empty() {
            // 内存播放只能同步，丢到后台线程去；数据拷一份带走，
            // 免得线程还在放、对象已经没了。
            let data = self.memory.clone();
            std::thread::spawn(move || unsafe {
                let _ = PlaySoundW(
                    PCWSTR(data.as_ptr() as *const u16),
                    None,
                    SND_FLAGS(SND_MEMORY | SND_NODEFAULT),
                );
            });
        }
    }
}
