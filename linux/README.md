# DeepSeek 余额小方块 — C++ / Linux (GTK3) 版

Windows 版（仓库根目录的 `src/`）的 Linux 移植。外观、交互、命令行参数都对齐，配置项字段名一一对应。

**这个目录是独立的**：自带 `CMakeLists.txt` 和全部源码，从 `linux/` 里直接构建，
不读也不写上一层的任何东西。仓库根的 Windows 版同样没被动过一个字节，两边各编各的。

---

## 依赖

全是系统库，不需要额外装什么（桌面发行版基本自带）：

| 依赖 | 用途 | Ubuntu 包名 |
| --- | --- | --- |
| GTK3 ≥ 3.22 | 窗口、菜单、事件 | `libgtk-3-dev` |
| cairo | 卡片绘制（逐像素半透明） | 随 GTK3 |
| Pango | 文字排版与度量 | 随 GTK3 |
| GIO + glib-networking | HTTPS 请求（TLS 由 `libgiognutls.so` 提供） | `libglib2.0-dev`、`glib-networking` |

编译期只需要前四项的开发文件；**运行期**还需要一个命令行播放器放木鱼音效
（`paplay` / `aplay` / `ffplay` 之一，见下文「音效」）。

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
```

## 编译

**在 `linux/` 目录里构建**。这个目录是独立的：整份拷到别的地方、甚至拷到另一台机器上，
照样能编，不依赖仓库里的任何其它东西。

```bash
cd linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

产物在 **`build/bin/deepseek_balance_cube`**（从仓库根看就是 `linux/build/bin/deepseek_balance_cube`）。

> 别在仓库根目录跑 `cmake -S .`：根 `CMakeLists.txt` 是 Windows 专用的，在 Linux 上会直接
> `FATAL_ERROR`（这是它本来的行为，没动过）。两边互不依赖，各编各的。

## 运行

下面都以「当前在 `linux/` 目录里」为前提：

```bash
./build/bin/deepseek_balance_cube              # 正常启动（自动转后台，不占终端）
./build/bin/deepseek_balance_cube --demo       # 用假数据渲染，不联网
./build/bin/deepseek_balance_cube --foreground # 留在前台跑（调试用，报错能直接看到）
./build/bin/deepseek_balance_cube --check-key  # 只查 Key 和网络，不开窗口
./build/bin/deepseek_balance_cube -h           # 帮助
```

**启动后会自动转入后台**，终端马上就还给你：

```
$ ./build/bin/deepseek_balance_cube
DeepSeek 余额 已在后台运行（想留在前台加 --foreground）。
$ █                                              ← 提示符立刻回来，挂件在后台常驻
```

原理是标准的 double-fork + `setsid`，标准流接到 `/dev/null`。代价是启动失败时的报错也看不见，
所以加了 `--foreground`（或 `-f`）留在前台。`--check-key` / `--render-png` / `--self-test` 都要往
终端出结果，它们**不会**转后台。

要停掉它，随便挑一种：

- 右键菜单 → **退出**（最省事）
- 点一下方块让它拿到焦点，按 **Esc**（菜单开着的话先关菜单，再按一次才退出）
- 命令：

```bash
pkill -x deepseek_balanc    # 注意：Linux 的进程名被截断到 15 个字符，写全名反而匹配不上
```

调试排版用的额外选项（Windows 版没有）：

```bash
# 把卡片离屏渲染成 PNG，不开窗口、不需要 DISPLAY，SSH 里也能跑
./build/bin/deepseek_balance_cube --demo --render-png /tmp/card.png
# 试试长错误短句会不会溢出（会自动缩字号）
./build/bin/deepseek_balance_cube --render-png /tmp/e.png --render-png-text "网络不可达: 域名解析失败"
# 按指定时刻算峰谷（比如国庆假期里的三位数小时倒计时）
./build/bin/deepseek_balance_cube --demo --render-png /tmp/h.png --render-png-at 1791200000
# 2 倍分辨率渲染，检查 HiDPI 下几何和文字是否等比例
./build/bin/deepseek_balance_cube --demo --render-png /tmp/c2.png --render-png-scale 2
```

## 配置 API Key

改 `src/config.h` 里的这一行，然后**重新编译**：

```cpp
inline constexpr const char* kApiKeyLiteral = "sk-你的key";
```

留空则回退到环境变量 `DEEPSEEK_API_KEY`（变量名可在 `kApiKeyEnv` 改）：

```bash
export DEEPSEEK_API_KEY=sk-你的key
./build/bin/deepseek_balance_cube
```

想让变量一直有效就写进 `~/.bashrc` 或 `~/.profile` —— 但**别把 Key 提交进仓库**。

`--check-key` 会打印 Key 来源、掩码、长度，并发一次真实请求，对排查很有用：

```
Key 来源：环境变量 DEEPSEEK_API_KEY
Key 掩码：sk-a*****************************45    长度：35
正在请求 https://api.deepseek.com/user/balance …
结果：失败 —— Key 无效: Authentication Fails, Your api key: ****2345 is invalid (request_id: ...)
```

退出码：`0` 正常，`1` 请求失败，`2` 根本没找到 Key。

---

## 和 Windows 版的差异

功能、交互、布局、命令行参数都一致，差异只有下面这几处，都是平台决定的：

### 强制走 X11 后端（XWayland）

启动时若 `DISPLAY` 存在且没设 `GDK_BACKEND`，程序会自己设成 `x11`。

原因：**原生 Wayland 下合成器不允许客户端自己定位窗口，也没有置顶协议**，
「贴右上角 / 拖动移动 / 置顶」这三个功能会全部失效（窗口位置由合成器随便安排、拖不动、也不置顶）。
走 XWayland 之后这三个功能才真实可用。没有 X 时会自动回退，程序照常运行，只是这三项不生效
（启动时会打一行提示）。

想强制用原生 Wayland：`GDK_BACKEND=wayland ./build/bin/deepseek_balance_cube`。

### 时区

峰谷时段是按**北京时间**硬编码的（周一至周五 09:00-12:00、14:00-18:00，周末和法定节假日整日低谷）。
Windows 版隐含「本机就是北京时间」；Linux 上机器时区可能是别的，所以启动时会把**进程**的
`TZ` 设成 `Asia/Shanghai`（只影响本进程，不动系统设置）。

### 音效

Windows 版走 WinMM；Linux 上没有，改成：代码现场合成裸 s16le 单声道 44100Hz PCM，
通过管道喂给探测到的第一个系统播放器。**全程不落盘**，磁盘上不留任何文件。

探测顺序：

| 播放器 | 参数 | 说明 |
| --- | --- | --- |
| `paplay` | `--raw --format=s16le --rate=44100 --channels=1` | PulseAudio / PipeWire 的兼容层，几乎哪台桌面都有，首选 |
| `aplay` | `-q -t raw -f S16_LE -r 44100 -c 1` | ALSA 自带 |
| `ffplay` | `-nodisp -autoexit -loglevel quiet -f s16le -ar 44100 -ac 1 -i pipe:0` | 兜底；裸 PCM 它探测不出来，`-f s16le` 不能省 |

都没装就**静默禁用音效**（右键菜单里那一项会置灰），其余功能不受影响。

> 特意没放 `pw-play`：它只能吃「文件」，不认 stdin —— 参数里写 `-` 会被当成字面文件名，
> 报 `failed to open audio file "-": Format not recognised`；不给文件名则报
> `filename argument missing`。而本项目坚持不往磁盘写东西，所以它用不了。

音效参数（开关、音量、时长）在 `src/config.h`，和 Windows 版同名同义。

### 代理

`http_proxy` / `https_proxy` / `all_proxy` 环境变量会被自动识别（curl、wget 那套写法）：

```bash
export https_proxy=http://127.0.0.1:7890
./build/bin/deepseek_balance_cube
```

不设这些变量时，走 GIO 的默认代理解析器，也就是**跟随系统代理设置**（GNOME 的「网络代理」），
对应 Windows 版 `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY` 的行为。

### 右键菜单是自定义的，没有用 GtkMenu

不是因为 GtkMenu 不好，是因为它在这台机器上**真的用不了**：

GtkMenu 弹出时会做一次 `gdk_seat_grab(..., owner_events = FALSE)`（即 `XGrabPointer` 且
owner_events 为假）。在本机的 Wayland + XWayland 组合下，这种抓取一旦生效，菜单项就再也收不到
能触发激活的点击 —— 菜单能弹出、能高亮、点一下也会关闭，但 `activate` 永远不发。
一个教科书式的最小 GTK 程序在这里也一样坏，不是本项目代码的问题。

所以改成自定义弹出窗口（`GtkWindow` + `GTK_WINDOW_POPUP` + 一排 `GtkButton`），指针抓取改成
`owner_events = TRUE`：

| `owner_events` | 菜单项能点 | 点菜单外能收到事件 |
| --- | --- | --- |
| `FALSE`（GtkMenu 用的） | ❌ | — |
| `TRUE`（本程序用的） | ✅ | ✅ 应用内、应用外都能 |

顺带的好处：菜单配色可以用 CSS 跟着卡片的深色走，不像系统菜单那样一片惨白。

### 顺带修掉的两个问题

- **错误短句按码点截断**：`shortError` 要把错误压到 10 个字符，Windows 版按 `wchar_t` 数没问题，
  但 Linux 上字符串是 UTF-8，按字节截会把中文劈成半个（Pango 会显示乱码/报警告），所以改成按码点。
- **写管道前忽略 SIGPIPE**：播放器提前退出时写它的 stdin 会收到 SIGPIPE，默认动作是直接杀掉进程。
  程序启动就把 SIGPIPE 忽略掉，`write` 返回 `EPIPE` 时安静放弃这一次播放。

### 没做的

- **自包含单文件**：Windows 版能静态链接成孤零零一个 exe；GTK3 依赖一堆系统库和运行期加载的
  gio 模块（`libgiognutls.so` 等），做不到也不该做。
- **托盘图标**：Linux 上各桌面（GNOME 没有原生托盘）差异太大，没做。窗口本身也不进任务栏和 Alt-Tab。

---

## 文件结构

```
linux/
├── CMakeLists.txt      ← 独立构建入口（在 linux/ 里 cmake -S . -B build）
├── README.md
├── ks.ico              ← 可选：放这里就有窗口图标，没有也不影响运行
└── src/                ← 全部源码都在这里
```

| `linux/src/` 下的文件 | 作用 | 对应 Windows 版（根 `src/`） |
| --- | --- | --- |
| `config.h` | **配置区**：Key、刷新节奏、配色、尺寸、音效、字体候选 | `config.h` |
| `main.cpp` | 入口、命令行、X11 后端 / 时区 / SIGPIPE、`--check-key`、`--render-png` | `main.cpp` |
| `cube.{h,cpp}` | 窗口、信号、交互、状态机、动画、菜单、定时器、网络编排 | `cube.{h,cpp}` |
| `render.{h,cpp}` | cairo + Pango 绘制：圆角路径、文字度量、居中立字、字号自适应 | `render.{h,cpp}` |
| `balance.{h,cpp}` | GIO HTTPS 请求、Key 解析、错误归类 | `balance.{h,cpp}` |
| `peak.{h,cpp}` | 峰 / 谷判定、法定节假日、倒计时 | `peak.{h,cpp}` |
| `pacer.{h,cpp}` | 自适应刷新节奏 | `pacer.{h,cpp}`（原样复用） |
| `sound.{h,cpp}` | 木鱼波形合成 + 播放器探测 + 管道播放 | `sound.{h,cpp}` |
| `json.{h,cpp}` | 极简 JSON 解析（零依赖） | `json.{h,cpp}`（原样复用） |
| `util.{h,cpp}` | UTF-8 按码点截断、可执行文件目录 | `util.{h,cpp}` |

上面这份源码**一份都没有跨目录 include**：`#include "config.h"` 这类都靠编译器按「当前文件
所在目录」解析，所以整个 `linux/` 目录可以单独拷走。CMake 只是把 `linux/src` 加进了头文件搜索路径。

`json` / `pacer` 是纯标准 C++，直接复用；`peak` 只把 `std::wstring` 换成了 `std::string`。

---

## 排查

**窗口是黑底的、圆角外是黑的**

当前显示环境没有合成器，拿不到 RGBA visual（启动时 stderr 会有一行 `G_CRITICAL` 警告）。
GNOME / KDE 默认都有合成器；如果用的是极简 WM，需要开一个（如 `picom`）。

**点卡片旁边的透明区域，下层窗口没反应**

这是预期的：透明留白区域被 input shape 裁掉了，不吞点击。如果想让整块矩形都可点，
去掉 `src/cube.cpp` 里 `applyInputShape()` 的调用即可。

**启动后哪都点不动 / 窗口不在右上角**

多半是走了原生 Wayland 后端（看启动时有没有那行提示）。确认 `DISPLAY` 环境变量存在，
或显式 `GDK_BACKEND=x11`。

**没有木鱼音效**

先看右键菜单里「点击音效」是否置灰（置灰 = 三个播放器一个都没找到）。
手动确认一下播放器本身能用：

```bash
head -c 13230 /dev/urandom | paplay --raw --format=s16le --rate=44100 --channels=1; echo $?
```

**菜单用的是浅色主题，和深色卡片不搭**

右键菜单走的是系统 GTK 主题（没做自定义皮肤，这样在 KDE / 各种主题下都协调）。
想改就换一套全局 GTK 主题，或者在 `src/cube.cpp` 的 `showMenu()` 里给菜单挂一个自定义 CSS。
