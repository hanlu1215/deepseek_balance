#pragma once

#include <string>
#include <vector>

// 点击音效：波形由代码现场合成，全程在内存里，**不落任何文件**。
// 所以 exe 是自包含的 —— 单独一个文件就能跑，运行期间也不会在磁盘上留东西。
//
// 想换成自己的声音：把一个 mokugyo_click.wav 放到 exe 同目录即可，程序会优先用它
// （只读不写）。没有这个文件就用内置合成的那记「笃」。
class ClickSound {
public:
    ClickSound(bool enabled, double volume);
    ~ClickSound();
    ClickSound(const ClickSound&) = delete;
    ClickSound& operator=(const ClickSound&) = delete;

    // 启动时调用一次：有现成的 wav 就用它，否则合成一份放内存里。
    void prepare();

    void play();

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

private:
    static bool looksLikeWav(const std::wstring& path);

    bool enabled_;
    double volume_;
    std::wstring path_;          // 用户自备的 wav；为空表示用内存里合成的那份
    std::vector<char> memory_;
    bool ready_ = false;
};

// 合成一记木鱼「笃」。
//
// 木鱼的声音 = 极短的敲击噪声 + 几个快速衰减的谐振峰（略带非谐波）+ 木腔的
// 低频体感，再叠一点音高下滑。全程纯计算，不需要任何音频素材。
std::vector<char> buildMokugyoWav(double volume);
