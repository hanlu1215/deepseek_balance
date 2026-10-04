#include "sound.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <thread>

#include "config.h"
#include "util.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 44100;

// 16bit 单声道样本 -> WAV 字节流。
std::vector<char> wavBytes(const std::vector<short>& samples, int sampleRate) {
    const unsigned int dataBytes = static_cast<unsigned int>(samples.size() * sizeof(short));
    std::vector<char> out;
    out.reserve(44 + dataBytes);

    auto put32 = [&out](unsigned int value) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    };
    auto put16 = [&out](unsigned short value) {
        for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    };
    auto putText = [&out](const char* text) {
        while (*text != '\0') out.push_back(*text++);
    };

    putText("RIFF");
    put32(36 + dataBytes);
    putText("WAVE");

    putText("fmt ");
    put32(16);                                                            // 子块大小
    put16(1);                                                             // PCM
    put16(1);                                                             // 单声道
    put32(static_cast<unsigned int>(sampleRate));
    put32(static_cast<unsigned int>(sampleRate * 2));                     // 字节率
    put16(2);                                                             // 块对齐
    put16(16);                                                            // 位深

    putText("data");
    put32(dataBytes);
    for (const short sample : samples) put16(static_cast<unsigned short>(sample));
    return out;
}

}  // namespace

std::vector<char> buildMokugyoWav(double volume) {
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
    return wavBytes(samples, kSampleRate);
}

ClickSound::ClickSound(bool enabled, double volume) : enabled_(enabled), volume_(volume) {}

ClickSound::~ClickSound() = default;

bool ClickSound::looksLikeWav(const std::wstring& path) {
    // 存在、是 RIFF/WAVE、且带数据块，就当作可用（也允许用户自己换一个）。
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;

    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(info.nFileSizeHigh);
    size.LowPart = info.nFileSizeLow;
    if (size.QuadPart <= 44) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char head[12] = {};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, head, sizeof(head), &read, nullptr);
    CloseHandle(file);
    if (!ok || read < sizeof(head)) return false;

    return std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WAVE", 4) == 0;
}

void ClickSound::prepare() {
    if (ready_) return;
    ready_ = true;

    if (!enabled_) return;

    // 用户自备的 wav 优先（只读，不会创建或修改任何文件），否则用内置合成的。
    // 合成只要几毫秒，所以完全可以启动时现算，不需要缓存到磁盘。
    const std::wstring candidate = executableDir() + kClickSoundFile;
    if (looksLikeWav(candidate)) {
        path_ = candidate;
        return;
    }
    memory_ = buildMokugyoWav(volume_);
}

void ClickSound::play() {
    if (!enabled_) return;
    if (!ready_) prepare();
    if (!enabled_) return;

    if (!path_.empty()) {
        // 文件播放可以真异步：立即返回，不占线程、不卡界面
        ::PlaySoundW(path_.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    } else if (!memory_.empty()) {
        // 内存播放只能同步，丢到后台线程去；数据拷一份带走，
        // 免得线程还在放、对象已经没了。
        std::vector<char> data = memory_;
        std::thread([data = std::move(data)]() {
            ::PlaySoundW(reinterpret_cast<LPCWSTR>(data.data()), nullptr,
                         SND_MEMORY | SND_NODEFAULT);
        }).detach();
    }
}
