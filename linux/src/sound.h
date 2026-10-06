#pragma once

#include <vector>

// 点击音效：波形由代码现场合成，全程在内存里，**不落任何文件**。
// 所以这个程序运行期间不会在磁盘上留东西，也不需要任何音频素材。
//
// Linux 上没有 WinMM，播放交给系统里现成的命令行播放器：按
// pw-play -> paplay -> aplay -> ffplay 的顺序探测，第一个存在的就用，
// 把裸 PCM 通过管道塞进它的 stdin。四个都没有就静默禁用音效（菜单项会置灰）。
class ClickSound {
public:
    ClickSound(bool enabled, double volume);
    ~ClickSound();
    ClickSound(const ClickSound&) = delete;
    ClickSound& operator=(const ClickSound&) = delete;

    // 启动时调用一次：探测播放器 + 合成波形（几毫秒），第一次点击就不用等。
    void prepare();

    void play();

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // 系统里一个能用的播放器都没有时为 false。
    bool available() const { return playerIndex_ >= 0; }

private:
    bool enabled_;
    double volume_;
    std::vector<short> samples_;   // 裸 s16le 单声道样本
    int playerIndex_ = -1;         // 探测到的播放器下标；-1 = 没有
    bool ready_ = false;
};

// 合成一记木鱼「笃」。
//
// 木鱼的声音 = 极短的敲击噪声 + 几个快速衰减的谐振峰（略带非谐波）+ 木腔的
// 低频体感，再叠一点音高下滑。全程纯计算，参数和 Windows 版逐字一致
// （连随机种子都一样），所以两边的音色相同。
std::vector<short> buildMokugyoSamples(double volume);

// 单声道采样率，喂给播放器的参数要和这个对上。
int mokugyoSampleRate();
