#include "sound.h"

#include <gio/gio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <random>
#include <thread>

#include "config.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 44100;

static_assert(sizeof(short) == 2, "合成出来的样本按 16 位小端喂给播放器，short 必须是 2 字节");

struct Player {
    const char* program;
    const char* argv[16];   // 最长的是 ffplay 那条，14 个（含结尾的 nullptr）
};

// 参数一律写成「裸 s16le 单声道 44100Hz、从 stdin 读」，几个播放器喂的数据格式就统一了。
// 下面这几条都在本机实测过能跑通（`< raw.pcm` 退出码 0）：
//   paplay --raw ...        PulseAudio / PipeWire 的兼容层，几乎哪台桌面都有，首选
//   aplay -t raw ...        ALSA 自带
//   ffplay -f s16le ...     兜底；必须显式给 -f s16le，裸 PCM 它自己探测不出来
//
// 特意没放 pw-play：它只能吃「文件」，不认 stdin —— 参数里写 "-" 会被当成字面文件名，
// 报 `failed to open audio file "-": Format not recognised`，不给文件名则报
// `filename argument missing`。而本项目坚持不往磁盘写任何东西，所以它用不了。
const Player kPlayers[] = {
    {"paplay", {"paplay", "--raw", "--format=s16le", "--rate=44100", "--channels=1", nullptr}},
    {"aplay", {"aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", "44100", "-c", "1", nullptr}},
    {"ffplay", {"ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet",
                "-f", "s16le", "-ar", "44100", "-ac", "1", "-i", "pipe:0", nullptr}},
};
constexpr int kPlayerCount = static_cast<int>(sizeof(kPlayers) / sizeof(kPlayers[0]));

// 用 G_SPAWN_DO_NOT_REAP_CHILD 就必须自己收尸，否则每敲一记木鱼留一个僵尸。
void onChildExit(GPid pid, gint status, gpointer userData) {
    (void)status;
    (void)userData;
    g_spawn_close_pid(pid);
}

}  // namespace

std::vector<short> buildMokugyoSamples(double volume) {
    const int count = static_cast<int>(kSampleRate * kClickSoundSeconds);

    struct Partial {
        double freq;    // 频率 Hz
        double amp;     // 振幅
        double tau;     // 衰减时间常数（秒）
    };
    const Partial partials[] = {
        {430.0, 0.30, 0.045},    // 木腔的低频“闷”
        {1180.0, 1.00, 0.030},   // 主音
        {1810.0, 0.52, 0.018},   // 非谐波泛音，木头味就靠它
        {2470.0, 0.24, 0.011},
    };

    // 固定种子：每次听起来都一样
    std::mt19937 rng(20261003u);
    std::uniform_real_distribution<double> noiseDist(-1.0, 1.0);
    std::vector<double> noise(static_cast<size_t>(count));
    for (double& value : noise) value = noiseDist(rng);

    std::vector<double> raw(static_cast<size_t>(count));
    double peak = 0.0;
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double bend = 1.0 + 0.055 * std::exp(-t / 0.022);   // 敲下去那一瞬间略高

        double value = 0.0;
        for (const Partial& partial : partials) {
            value += partial.amp * std::exp(-t / partial.tau) *
                     std::sin(2.0 * kPi * partial.freq * bend * t);
        }
        value += 0.55 * noise[static_cast<size_t>(i)] * std::exp(-t / 0.0014);  // 起手的敲击声
        value *= 1.0 - std::exp(-t / 0.0004);                                   // 0.4ms 淡入，防爆音

        raw[static_cast<size_t>(i)] = value;
        peak = std::max(peak, std::fabs(value));
    }

    if (peak <= 0.0) peak = 1.0;
    const double scale = std::min(1.0, std::max(0.0, volume)) * 32767.0 * 0.92 / peak;

    std::vector<short> samples(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        samples[static_cast<size_t>(i)] =
            static_cast<short>(std::lround(raw[static_cast<size_t>(i)] * scale));
    }
    return samples;
}

int mokugyoSampleRate() {
    return kSampleRate;
}

ClickSound::ClickSound(bool enabled, double volume) : enabled_(enabled), volume_(volume) {}

ClickSound::~ClickSound() = default;

void ClickSound::prepare() {
    if (ready_) return;
    ready_ = true;

    if (!enabled_) return;

    for (int i = 0; i < kPlayerCount; ++i) {
        gchar* path = g_find_program_in_path(kPlayers[i].program);
        if (path != nullptr) {
            g_free(path);
            playerIndex_ = i;
            break;
        }
    }
    if (playerIndex_ < 0) return;   // 一个播放器都没有：静默禁用

    samples_ = buildMokugyoSamples(volume_);
}

void ClickSound::play() {
    if (!enabled_) return;
    if (!ready_) prepare();
    if (!enabled_ || playerIndex_ < 0 || samples_.empty()) return;

    const Player& player = kPlayers[playerIndex_];

    gint stdinFd = -1;
    GPid pid = -1;
    GError* error = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_CLOEXEC_PIPES);

    if (!g_spawn_async_with_pipes(nullptr, const_cast<gchar**>(player.argv), nullptr, flags, nullptr,
                                  nullptr, &pid, &stdinFd, nullptr, nullptr, &error)) {
        g_clear_error(&error);
        return;
    }

    // 播放器消费得慢的时候 write 会阻塞，所以丢到后台线程去写；
    // 样本拷一份带走，免得线程还在写、对象已经析构了。
    std::vector<short> data = samples_;
    std::thread([stdinFd, data = std::move(data)]() {
        const char* cursor = reinterpret_cast<const char*>(data.data());
        size_t remaining = data.size() * sizeof(short);
        while (remaining > 0) {
            const ssize_t count = ::write(stdinFd, cursor, remaining);
            if (count > 0) {
                cursor += count;
                remaining -= static_cast<size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            // EPIPE（播放器提前退出）或其他错误：放弃这一次播放。
            // 能走到这里是因为 main 里把 SIGPIPE 忽略了，否则进程已经被干掉。
            break;
        }
        ::close(stdinFd);
    }).detach();

    g_child_watch_add(pid, onChildExit, nullptr);
}
