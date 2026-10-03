# DeepSeek 余额小方块

一个常驻桌面角落的小悬浮窗，只显示两样东西：**账户余额** 和 **峰 / 谷计费时段的倒计时**。
左键点一下敲声木鱼顺便刷新，拖一下换位置，右键菜单里能置顶、关音效、关颜色轮换。

- **大圆角卡片**：底图逐行渲染成抗锯齿 PNG，边缘不毛糙，文字占满卡片几乎没有留白
- **峰 / 谷 徽标 + `时:分:秒` 倒计时**：自动跳过周末和法定节假日
- **点击木鱼音效**：波形由代码现场合成，仓库里不需要任何音频素材文件
- **余额数字每次刷新换一种颜色**：金 → 橘 → 红 → 蓝 → 青 → 白（可关）
- **自适应刷新节奏**：平时 5 分钟看一眼，余额掉得快就自动加密（最快 30 秒），余额不动再慢慢放回 5 分钟，并带随机抖动
- **零第三方依赖**：只用 Python 标准库（tkinter + winsound），连 `pip install` 都不需要
- **可以打包成单文件 exe**：别人拿到双击就能跑，不用装 Python（见「打包成 exe」）

---

## 1. 系统要求

| 项目 | 要求 |
| --- | --- |
| 系统 | **Windows 10 / 11**（用到 tkinter、winsound 和窗口透明色，其他系统跑不了） |
| Python | 3.10 或更高（本项目在 3.14 上开发验证） |
| 依赖 | 无。官方安装包自带 tkinter 就够 |

装 Python 时记得勾上 **Add python.exe to PATH**，安装选项里的 **tcl/tk and IDLE** 保持默认勾选。

---

## 2. 快速开始

```powershell
# 1. 拿到代码（clone 或直接下载 zip 解压）
git clone <仓库地址>
cd deepseek_balance

# 2. 给一个 API Key（见第 3 节，二选一）

# 3. 运行
python deepseek_balance_cube.py
```

方块出现在屏幕右上角。退出：按 `Esc`，或右键菜单 → 退出。

---

## 3. 配置 API Key

### 方式一：写进脚本（最省事，适合自己用）

打开 `deepseek_balance_cube.py`，找到文件开头的配置区：

```python
API_KEY = ""          # ← 把 Key 填进引号里
API_KEY_ENV = "DEEPSEEK_API_KEY"
```

改成 `API_KEY = "sk-你的key"` 保存即可。

### 方式二：用临时环境变量（不改文件，推荐给开源仓库/多人共用）

在命令行窗口里先设变量，**然后在同一个窗口里启动脚本**：

| 窗口类型 | 示例命令 |
| --- | --- |
| PowerShell | `$env:DEEPSEEK_API_KEY = "sk-你的key"` |
| cmd | `set DEEPSEEK_API_KEY=sk-你的key` |
| Git Bash | `export DEEPSEEK_API_KEY=sk-你的key` |

验证一下（PowerShell）：

```powershell
echo $env:DEEPSEEK_API_KEY      # 能打印出你的 Key 就对了
python deepseek_balance_cube.py
```

> ⚠️ 两个最容易踩的坑
>
> - **PowerShell 里的 `set` 不是 cmd 的 `set`**：`set DEEPSEEK_API_KEY=sk-xxx` 不会设置环境变量，只是建了一个名字里带等号的 PowerShell 变量，`$env:` 里依然是空的。PowerShell 要用 `$env:DEEPSEEK_API_KEY = "sk-xxx"`。
> - **临时变量只活在设它的那个窗口**：换窗口、双击图标、从编辑器启动都读不到；窗口关掉就没了。

优先级：`API_KEY` 填了就用它，留空才回退到环境变量 `DEEPSEEK_API_KEY`。

> 🔒 **如果你要开源 / 分享这个项目**：把 `API_KEY` 留空，并且别让带 Key 的文件进版本库。提交前用 `git status` / `git diff` 扫一眼；万一已经把 Key 提交或发出去过，去 DeepSeek 官网把那个 Key 删掉重新生成一把。

---

## 4. 启动方式

### 4.1 带命令行窗口（调试用）

```powershell
python deepseek_balance_cube.py
```

好处是出错信息、排查输出都能看到。

### 4.2 用 pythonw.exe 启动（没有黑色窗口）

`python.exe` 会带一个控制台窗口，`pythonw.exe` 完全没有窗口，适合日常挂着。

先找到它的完整路径：

```powershell
(Get-Command pythonw.exe).Source
# 或者
python -c "import sys, os; print(os.path.join(os.path.dirname(sys.executable), 'pythonw.exe'))"
```

一般长这样：`C:\Python314\pythonw.exe`。然后启动（**两个路径各自用英文双引号包起来，中间一个空格**）：

```powershell
& "C:\Python314\pythonw.exe" "D:\你的目录\deepseek_balance\deepseek_balance_cube.py"
```

cmd 里：

```cmd
start "" "C:\Python314\pythonw.exe" "D:\你的目录\deepseek_balance\deepseek_balance_cube.py"
```

> 另一个偷懒办法：把脚本复制一份改名为 `deepseek_balance_cube.pyw`，双击就会用 pythonw 打开（前提是 `.pyw` 已关联到 Python）。
> 注意 pythonw 没有控制台，报错信息也看不到；要排查就用 `python.exe` 运行，或在 exe 版里用 `--check-key`（会弹对话框）。

---

## 5. 创建快捷方式（含参数怎么填）

### 做法 A：手动新建

1. 桌面空白处右键 → **新建** → **快捷方式**
2. 「请键入对象的位置」填（第一段是 pythonw.exe，第二段是脚本，都加英文双引号）：

```
"C:\Python314\pythonw.exe" "D:\你的目录\deepseek_balance\deepseek_balance_cube.py"
```

3. 下一步 → 名称填 `DeepSeek 余额` → 完成
4. 右键快捷方式 → **属性** → 「快捷方式」标签页核对：

| 字段 | 填什么 |
| --- | --- |
| **目标(T)** | `"…\pythonw.exe" "…\deepseek_balance_cube.py"` |
| **起始位置(S)** | 脚本所在文件夹（例如 `D:\你的目录\deepseek_balance`） |
| **运行方式(R)** | 常规 |
| **快捷键(K)** | 可选，比如 `Ctrl+Alt+B` |
| **更改图标(C)** | 可选，选 `pythonw.exe`，或你自己的 `.ico` |

### 做法 B：从脚本直接生成（更快）

1. 右键脚本 → **显示更多选项** → **发送到** → **桌面快捷方式**
2. 右键生成的快捷方式 → 属性 → 把「目标」改成上面那种 pythonw 开头的写法
3. 「起始位置」填脚本所在文件夹，确定

> 不改「目标」的话，双击会用 `python.exe` 启动，会多一个黑色命令行窗口。

**几个注意点**

- 目标里的第一段必须是 `pythonw.exe`，不是 `python.exe`。
- 路径里有空格必须加双引号。
- 快捷方式启动没有终端，所以没法在启动前设临时环境变量——这种场景请用第 3 节的方式一，或者改用打包好的 exe。
- **开机自启**：`Win + R` 输入 `shell:startup` 回车，把快捷方式拖进打开的那个文件夹。

---

## 6. 打包成独立 exe（可选）

想要**一个能拷给别人的单文件程序**（对方不用装 Python），用 PyInstaller 打包。下面这套流程跟具体的 Python 安装位置无关，照着做就行。

### 6.1 前置条件

- 脚本本身能正常跑起来：`python deepseek_balance_cube.py`
- 安装 PyInstaller（只是打包工具，不是运行依赖）：

```powershell
python -m pip install --upgrade pyinstaller
```

> 建议在虚拟环境里打包，免得把系统里其他无关库一起塞进去，产物也更小：
>
> ```powershell
> python -m venv .venv
> .venv\Scripts\activate              # PowerShell 用 .venv\Scripts\Activate.ps1
> python -m pip install --upgrade pyinstaller
> ```
>
> 如果 pip 说要**现场编译 bootloader**（某些非官方 Python 发行版会这样，比如 MSYS2 的 Python），那本机需要有 C 编译器；用 [python.org](https://www.python.org/downloads/windows/) 的官方安装包可以直接装现成的 wheel，省事。

### 6.2 基础打包命令

在项目目录下执行：

```powershell
python -m PyInstaller --noconfirm --clean --onefile --noconsole --name DeepSeekBalance deepseek_balance_cube.py
```

产物在 `dist\DeepSeekBalance.exe`，拖到任意位置双击都能跑。

| 参数 | 作用 |
| --- | --- |
| `--onefile` | 打成单个 exe（运行时自解压到 `%TEMP%`，启动慢 1~2 秒） |
| `--onedir` | 打成文件夹（启动快，但要整个文件夹一起拷） |
| `--noconsole` | **不显示命令行窗口**，等价于 `--windowed`；要"无黑窗"就靠它 |
| `--name` | 产物名字，随便改 |
| `--icon app.ico` | 设置图标（可选，见 6.3） |
| `--clean` | 先清掉上次的构建缓存，避免改了代码还打旧包 |
| `--noconfirm` | 覆盖已有产物时不再询问 |

### 6.3 加图标（可选）

```powershell
python -m PyInstaller --noconfirm --clean --onefile --noconsole --name DeepSeekBalance --icon app.ico deepseek_balance_cube.py
```

`--icon` 只接受 `.ico` 文件。手上是 png/jpg 的话先转换，并且**最好做成包含多个尺寸**的 ico（16 / 24 / 32 / 48 / 256），否则任务栏和大图标会糊。用 Pillow 一行就能转：

```python
from PIL import Image
Image.open("icon.png").save("app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (256, 256)])
```

打包完成后，在资源管理器里把查看方式切成「大图标」就能看到 exe 的实际图标；如果还是旧图标，那是 Windows 的图标缓存（重命名一下或用工具刷新缓存即可）。

### 6.4 验证打包结果

1. 双击 `dist\DeepSeekBalance.exe`：窗口应该正常出现，**不弹黑色命令行窗口**；
2. 首次运行后，exe 旁边会**自动生成 `mokugyo_click.wav`**（木鱼音效的缓存，脚本版则生成在脚本旁边）；如果 exe 放在不能写的目录（比如 `C:\Program Files`），会自动退回内存合成播放，不影响使用；
3. exe 里的代码是**打包那一刻的副本**，改了脚本必须重新打包；
4. 想排查 Key / 网络问题时，exe 没有控制台，`--check-key` 会**弹对话框**显示结果（脚本版则直接打印在命令行里）。

### 6.5 打包常见问题

| 现象 | 原因 / 处理方法 |
| --- | --- |
| 双击后有黑色命令行窗口 | 漏了 `--noconsole`（或写成了 `--console`） |
| 启动要等 1~2 秒 | `--onefile` 每次运行都要先自解压；介意就用 `--onedir` |
| 杀毒软件 / SmartScreen 报警 | PyInstaller 打的包没有代码签名，首次运行点「更多信息 → 仍要运行」即可 |
| 报 `ModuleNotFoundError` | 把缺的模块用 `--hidden-import 模块名` 显式带上 |
| 改了代码但 exe 没变 | 忘了 `--clean`，或者打包的不是同一份文件 |
| 打包体积偏大 | 在干净的 venv 里打包；或用 `--exclude-module` 排掉用不到的库 |
| 中文路径 / 中文 exe 名 | 一般没问题，出问题就先把产物换成英文名试试 |

### 6.6 开源仓库里要注意的两件事

1. **别把 API Key 打进发行版**：Key 会随脚本一起进 exe。它不是明文躺在文件里（打包后是压缩过的字节码），但用现成的解包工具能提取出来，所以发布前确认 `API_KEY` 是空的，让使用者自己填或用环境变量。同理，别把带 Key 的 exe 传到网上。
2. **别把构建产物提交进版本库**：`.gitignore` 建议加上

```gitignore
__pycache__/
*.pyc
.venv/
build/
dist/
*.spec
*.exe
mokugyo_click.wav
```

> 说明：`--noconsole` 之后 `print()` 是看不到的，所以脚本里所有输出都走了安全通道（写不出来也不会让程序崩），窗口模式下会改用对话框提示。

---

## 7. 交互与菜单

| 操作 | 效果 |
| --- | --- |
| 左键单击 | 木鱼「笃」一声 + 轻微 Q 弹 + 立刻刷新余额 |
| 按住左键拖动 | 移动窗口位置 |
| 右键单击 | 菜单：刷新 / 置顶（勾选）/ 点击音效（勾选）/ 颜色轮换（勾选）/ 退出 |
| `Esc` | 退出 |

菜单里的三项是**勾选开关**：

- **置顶**：勾上 = 窗口永远在最前面，取消 = 普通窗口；
- **点击音效**：勾上 = 点击有木鱼声，取消 = 静音（重新勾上时会响一声确认）；
- **颜色轮换**：勾上 = 余额数字每次刷新换一种颜色，取消 = 固定用原来的金色。

余额数字的配色顺序是 **金 → 橘 → 红 → 蓝 → 青 → 白**，循环播放。启动时是原来的金色，之后每成功刷新一次换下一个颜色；刷新失败时显示红色错误文字，不参与轮换。

如果 Key 没配好，方块中间会直接显示 `缺 Key`、`Key 无效`、`网络不可达` 这样的红字提示，而不是只给一个红杠。

---

## 8. 命令行参数

| 参数 | 作用 |
| --- | --- |
| 无参数 | 正常启动悬浮窗 |
| `--check-key` | 只检查 Key 和网络，不开窗口；**排查问题的第一选择** |
| `--demo` | 用假数据渲染，不访问网络（看外观时用） |
| `--self-test 5` | 渲染 5 秒后自动退出（配合 `--demo` 截图用） |

```powershell
python deepseek_balance_cube.py --check-key
```

输出会告诉你 Key 从哪儿读到、掩码什么样、接口返回什么错误：

```
Key 来源：系统环境变量 DEEPSEEK_API_KEY
Key 掩码：sk-a**********90    长度：35
正在请求 https://api.deepseek.com/user/balance …
结果：成功，余额 18.23 CNY
```

失败时会按错误给出原因（Key 无效 / 余额不足 / 网络不可达 / 请求过频）。

---

## 9. 刷新节奏（自适应）

默认不按固定间隔刷，而是**跟着花钱速度走**：

- 平时 5 分钟看一眼；
- 发现余额在掉就把间隔砍半，掉得越快刷得越勤，**最快 30 秒**；
- 余额不动就慢慢放长，**回到 5 分钟**；
- 每次还会加 ±5% 的随机抖动，不会固定踩在同一秒。

配置项：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `BALANCE_REFRESH_SECONDS` | `0` | `0` = 自适应，正数 = 固定秒数（如 `10`），负数 = 关掉自动刷新 |
| `REFRESH_MIN_SECONDS` | `30` | 最快间隔 |
| `REFRESH_MAX_SECONDS` | `300` | 最慢间隔 |
| `REFRESH_JITTER` | `0.05` | 随机抖动比例（±5%） |
| `REFRESH_RATE_TAU` | `300` | 消耗速率的平滑时间常数（秒），越大反应越迟钝 |
| `REFRESH_SHRINK` / `REFRESH_GROW` | `0.5` / `1.3` | 余额变了砍半 / 没变放长 30% |
| `BALANCE_STEP` | `0.01` | 大概消耗这么多钱就去看一眼；调大 → 整体刷得更慢 |

---

## 10. 外观与音效配置

都在脚本开头的配置区，改完重启生效：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `CARD_WIDTH` / `CARD_HEIGHT` | `156` / `120` | 卡片尺寸（设计像素，会按系统 DPI 缩放） |
| `CORNER_RADIUS` | `34` | 圆角半径，越大越圆润 |
| `AMOUNT_FONT_PX` / `PILL_FONT_PX` | `46` / `20` | 余额数字、峰谷徽标的字号 |
| `COLOR_TOP` / `COLOR_BOTTOM` | `(44,47,56)` / `(26,28,33)` | 卡片渐变起止色 |
| `AMOUNT_COLORS` | 六色 | 余额数字配色表（每刷新一次换下一个） |
| `AMOUNT_COLOR_CYCLE` | `True` | 是否轮换颜色；`False` 固定用第一个颜色 |
| `COLOR_PEAK` / `COLOR_OFF` | 橙 / 绿 | 峰、谷徽标的主色 |
| `ALWAYS_ON_TOP` | `True` | 启动时是否置顶 |
| `START_AT` | `top-right` | 初始位置：`top-right` / `top-left` / `bottom-right` / `bottom-left` |
| `BOUNCE_AMPLITUDE` / `BOUNCE_DURATION` | `0.06` / `0.55` | 点击 Q 弹的幅度与时长 |
| `CLICK_SOUND` | `True` | 是否播放点击音效 |
| `CLICK_SOUND_VOLUME` / `CLICK_SOUND_SECONDS` | `0.75` / `0.15` | 音量、音效长度 |
| `CLICK_SOUND_FILE` | `mokugyo_click.wav` | 音效缓存文件名；填 `""` 则完全在内存里合成 |
| `CN_HOLIDAYS_2026` | 2026 年节假日 | 高峰时段在这些日期内不生效，跨年记得补下一年 |

**换音色**：改 `build_mokugyo_wav()` 里的 `partials`（频率、振幅、衰减时间），然后删掉 `mokugyo_click.wav`，下次启动会重新生成。

---

## 11. 常见问题

**方块里显示红字「缺 Key」**
没读到 Key。跑 `python deepseek_balance_cube.py --check-key`，或按第 3 节配置。

**显示「Key 无效」**
Key 抄错了、过期了，或者值里混进了引号/空格。用 `--check-key` 看掩码和长度，和你在官网复制的那串对一下。

**显示「网络不可达」**
断网、公司网络需要代理，或者防火墙拦了 `api.deepseek.com`。

**只在当前窗口设了环境变量，双击快捷方式却读不到**
正常现象：临时变量不跨窗口。用第 3 节的方式一把 Key 写进脚本，或者改用打包好的 exe（exe 也没法提前设临时变量）。

**点击没有声音**
右键菜单看「点击音效」是否勾上；远程桌面 / 没有声卡的机器会静音；确认 `CLICK_SOUND = True`。

**余额数字的颜色一直在变，能固定住吗？**
这是故意的（每刷新一次换一种颜色，用来提示"刚刷到新数据"）。右键菜单取消勾选「颜色轮换」即可；想彻底关掉就把 `AMOUNT_COLOR_CYCLE` 改成 `False`。

**`mokugyo_click.wav` 是什么？可以删吗？**
启动时自动生成的音效缓存（存在就跳过生成）。可以删，下次启动会重新生成；也可以直接替换成你自己的 wav。不想在目录里看到它，就把 `CLICK_SOUND_FILE` 改成 `""`。

**峰 / 谷 时段怎么算的？**
周一至周五 `09:00-12:00`、`14:00-18:00` 是高峰（峰，橙色），其余时间、周末和法定假日是低谷（谷，绿色）。节假日表在脚本的 `CN_HOLIDAYS_2026`，跨年时记得补充。

**卡片太大 / 太小 / 不够圆**
改 `CARD_WIDTH`、`CARD_HEIGHT`、`CORNER_RADIUS`、`AMOUNT_FONT_PX`。

---

## 12. 文件说明

| 文件 | 说明 |
| --- | --- |
| `deepseek_balance_cube.py` | 主脚本，唯一必需的文件 |
| `README.md` | 本说明 |
| `mokugyo_click.wav` | 首次运行自动生成的音效缓存，可删、可替换 |
| `*.ico` | 可选，打包 exe 时用 `--icon` 指定的图标 |
| `__pycache__/`、`build/`、`dist/`、`*.spec` | 运行/打包产生的缓存与产物，都可以删，建议写进 `.gitignore` |

---

## 13. 反馈与贡献

欢迎提 Issue 和 PR。改代码前建议先自查两件事：

```powershell
python deepseek_balance_cube.py --demo --self-test 5   # 看外观有没有画歪
python deepseek_balance_cube.py --check-key            # 看接口链路通不通
```

---

## 14. 许可证

见仓库根目录的 `LICENSE` 文件。
