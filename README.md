# DeepSeek 余额小方块

一个永远在最前面的桌面小方块，只显示两样东西：**余额** 和 **峰/谷时段的倒计时**。
点一下敲声木鱼顺便刷新，拖一下换位置，右键菜单里能置顶和关音效。

- 大圆角卡片，边缘带抗锯齿，文字占满卡片（不装 Pillow 也没有锯齿）
- 峰 / 谷 徽标 + `时:分:秒` 倒计时，自动跳过周末和法定节假日
- 点击敲木鱼（音效是代码现场合成的，不需要素材文件）
- 余额数字每刷新一次换一种颜色：金 → 橘 → 红 → 蓝 → 青 → 白（可关）
- 刷新节奏自适应：平时 5 分钟看一眼，余额掉得快就自动加密，最快 30 秒
- 纯 Python 标准库（tkinter），**不需要 pip 安装任何东西**

---

## 1. 运行环境

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows 10 / 11（用到 tkinter、winsound、窗口透明色） |
| Python | 3.10 或更高（开发环境 3.14） |
| 依赖 | 无。官方安装包自带的 tkinter 就够 |

装 Python 时记得勾上 **Add python.exe to PATH**，以及安装选项里的 **tcl/tk and IDLE**（默认就是勾上的）。

---

## 2. 快速开始

1. 把 `deepseek_balance_cube.py` 放到一个固定目录，例如 `D:\Tools\deepseek_balance\`
2. 给脚本一个 API Key（见第 3 节，二选一）
3. 双击启动（见第 4、5 节），或者命令行 `python deepseek_balance_cube.py`

退出：按 `Esc`，或右键菜单 → 退出。

---

## 3. 怎么给 API Key

### 方式一：直接写进脚本（推荐，尤其配合快捷方式）

用记事本/VS Code 打开脚本，找到文件开头的配置区：

```python
API_KEY = ""          # ← 把 Key 填进引号里
API_KEY_ENV = "DEEPSEEK_API_KEY"
```

改成：

```python
API_KEY = "sk-你的key"
```

保存即可。缺点是这个文件不能发给别人（里面有你的 Key）。

### 方式二：临时环境变量（只在当前命令行窗口有效）

打开一个命令行窗口，先设变量，**然后在这个同一个窗口里启动脚本**：

| 窗口类型 | 示例命令 |
| --- | --- |
| PowerShell | `$env:DEEPSEEK_API_KEY = "sk-你的key"` |
| cmd | `set DEEPSEEK_API_KEY=sk-你的key` |
| Git Bash | `export DEEPSEEK_API_KEY=sk-你的key` |

设完可以验证一下（PowerShell）：

```powershell
echo $env:DEEPSEEK_API_KEY
```

接着在同一个窗口里运行：

```powershell
python deepseek_balance_cube.py
```

> ⚠️ 两个最容易踩的坑
>
> - **PowerShell 里的 `set` 不是 cmd 的 `set`**：`set DEEPSEEK_API_KEY=sk-xxx` 不会设置环境变量，只是建了一个名字里带等号的 PowerShell 变量，`$env:` 里依然是空的。PowerShell 要用 `$env:DEEPSEEK_API_KEY = "sk-xxx"`。
> - **临时变量只活在设它的那个窗口里**：换一个窗口、双击图标、从编辑器里启动，都读不到；窗口一关变量就没了。所以想双击就能用，请用方式一。

优先级：`API_KEY` 填了就用它，留空才回退到环境变量 `DEEPSEEK_API_KEY`。

---

## 4. 用 pythonw.exe 启动（没有黑色命令行窗口）

`python.exe` 会带一个控制台窗口；`pythonw.exe` 完全没有窗口，适合日常挂着。

**第一步，找到 pythonw.exe 的完整路径**（PowerShell 里执行）：

```powershell
(Get-Command pythonw.exe).Source
```

或者：

```powershell
python -c "import sys, os; print(os.path.join(os.path.dirname(sys.executable), 'pythonw.exe'))"
```

一般长这样：

```
C:\Users\hanlu\AppData\Local\Programs\Python\Python314\pythonw.exe
```

**第二步，用它启动脚本**（两个路径各自用英文双引号包起来，中间一个空格）：

```powershell
& "C:\Users\hanlu\AppData\Local\Programs\Python\Python314\pythonw.exe" `
  "D:\Tools\deepseek_balance\deepseek_balance_cube.py"
```

cmd 里这样写：

```cmd
start "" "C:\Users\hanlu\AppData\Local\Programs\Python\Python314\pythonw.exe" "D:\Tools\deepseek_balance\deepseek_balance_cube.py"
```

因为 pythonw 没有控制台，出错信息也看不到。要排查就用 `python.exe` 运行，或者用 `--check-key`（见第 7 节）。

> 另一个偷懒的办法：把脚本复制一份改名为 `deepseek_balance_cube.pyw`，双击就用 pythonw 打开（前提是 `.pyw` 已经关联到 Python）。不过 `.pyw` 和 `.py` 是两个文件，改代码时别忘了改哪个。

---

## 5. 创建快捷方式（含参数怎么填）

### 做法 A：手动新建

1. 在桌面（或任意文件夹）空白处右键 → **新建** → **快捷方式**
2. 「请键入对象的位置」里填——**注意先写 pythonw.exe，再写脚本，两段都用英文双引号**：

```
"C:\Users\hanlu\AppData\Local\Programs\Python\Python314\pythonw.exe" "D:\Tools\deepseek_balance\deepseek_balance_cube.py"
```

3. 下一步 → 名称填 `DeepSeek 余额` → 完成
4. 右键这个快捷方式 → **属性**，在「快捷方式」标签页里核对：

| 字段 | 填什么 |
| --- | --- |
| **目标(T)** | `"…\pythonw.exe" "…\deepseek_balance_cube.py"`（就是上面那串） |
| **起始位置(S)** | `D:\Tools\deepseek_balance`（脚本所在文件夹） |
| **运行方式(R)** | 常规 |
| **快捷键(K)** | 可选，比如 `Ctrl+Alt+B` |
| **更改图标(C)** | 可选，选 `pythonw.exe`，或你自己的 `.ico` |

5. 确定，然后双击测试。

### 做法 B：从脚本直接生成（更快）

1. 右键 `deepseek_balance_cube.py` → **显示更多选项** → **发送到** → **桌面快捷方式**
2. 右键生成的快捷方式 → 属性 → 把「目标」改成：

```
"C:\Users\hanlu\AppData\Local\Programs\Python\Python314\pythonw.exe" "D:\Tools\deepseek_balance\deepseek_balance_cube.py"
```

3. 「起始位置」填 `D:\Tools\deepseek_balance`，确定。

> 不改「目标」的话，双击会用 `python.exe` 启动，会多一个黑色命令行窗口。

### 参数说明与注意事项

- **目标里的第一段必须是 `pythonw.exe`**，不是 `python.exe`；写错就会有黑窗口。
- **路径里有空格必须加双引号**，例如 `C:\Program Files\...`。
- **起始位置建议填脚本所在文件夹**：脚本找自己的目录用的是绝对路径，填不填都能跑，但填上更稳妥（也方便以后加需要相对路径的功能）。
- **快捷方式启动没有命令行窗口**，所以没法在启动前设临时环境变量；这种场景请用第 3 节的方式一（把 Key 写进脚本）。
- **想开机自动启动**：按 `Win + R` 输入 `shell:startup` 回车，把快捷方式拖进打开的那个文件夹即可。
- 想改脚本里的配置，改完重新双击快捷方式就行（脚本不驻留后台，是普通窗口程序）。

---

## 6. 交互与菜单

| 操作 | 效果 |
| --- | --- |
| 左键单击 | 木鱼「笃」一声 + 轻微 Q 弹 + 立刻刷新余额 |
| 按住左键拖动 | 移动窗口位置 |
| 右键单击 | 菜单：刷新 / 置顶（勾选）/ 点击音效（勾选）/ 颜色轮换（勾选）/ 退出 |
| `Esc` | 退出 |

菜单里的三项是**勾选开关**：勾上=置顶、取消=不置顶；勾上=有音效、取消=静音（重新勾上时会响一声确认）；勾上=余额数字每次刷新换色，取消=固定用原来的金色。

余额数字的配色顺序是 **金 → 橘 → 红 → 蓝 → 青 → 白**，循环播放。启动时显示原来的金色，之后每成功刷新一次换下一个颜色（手动点击和自动刷新都算）。刷新失败时会显示红色的错误文字，不参与轮换。

---

## 7. 命令行参数

| 参数 | 作用 |
| --- | --- |
| 无参数 | 正常启动悬浮窗 |
| `--check-key` | 只检查 Key 和网络，不开窗口；排查问题的第一选择 |
| `--demo` | 用假数据渲染，不访问网络（看外观时用） |
| `--self-test 5` | 渲染 5 秒后自动退出（配合 `--demo` 截图用） |

排查 Key 的例子：

```powershell
python deepseek_balance_cube.py --check-key
```

输出会告诉你 Key 从哪儿读到、掩码长什么样、接口返回什么错误：

```
Key 来源：环境变量 DEEPSEEK_API_KEY
Key 掩码：sk-a**********90    长度：35
正在请求 https://api.deepseek.com/user/balance …
结果：成功，余额 18.23 CNY
```

失败时会对着错误给出原因（Key 无效 / 余额不足 / 网络不可达 / 请求过频）。如果显示「没找到」，就按第 3 节重新给 Key。

---

## 8. 刷新节奏（自适应）

默认不按固定间隔刷，而是**跟着花钱速度走**：

- 平时 5 分钟看一眼；
- 发现余额在掉就把间隔砍半，掉得越快刷得越勤，**最快 30 秒**；
- 余额不动就慢慢放长，**回到 5 分钟**；
- 每次还会加 ±5% 的随机抖动，不会固定踩在同一秒。

想改行为，配置区里这些参数可调：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `BALANCE_REFRESH_SECONDS` | `0` | `0`=自适应，正数=固定秒数（如 `10`），负数=关掉自动刷新 |
| `REFRESH_MIN_SECONDS` | `30` | 最快间隔 |
| `REFRESH_MAX_SECONDS` | `300` | 最慢间隔 |
| `REFRESH_JITTER` | `0.05` | 随机抖动比例（±5%） |
| `REFRESH_RATE_TAU` | `300` | 速率平滑时间常数，越大反应越慢 |
| `BALANCE_STEP` | `0.01` | 大概消耗这么多钱就去看一眼（调大→整体刷得更慢） |

---

## 9. 外观与音效相关配置

都在文件开头的配置区，改完重启生效：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `CARD_WIDTH` / `CARD_HEIGHT` | `156` / `120` | 卡片尺寸（设计像素，会按系统 DPI 缩放） |
| `CORNER_RADIUS` | `34` | 圆角半径，越大越圆润 |
| `AMOUNT_FONT_PX` / `PILL_FONT_PX` | `46` / `20` | 余额数字、峰谷徽标字号 |
| `COLOR_TOP` / `COLOR_BOTTOM` | `(44,47,56)` / `(26,28,33)` | 卡片渐变起止色 |
| `AMOUNT_COLORS` | 金 → 橘 → 红 → 蓝 → 青 → 白 | 余额数字的配色表，每刷新一次换下一个 |
| `AMOUNT_COLOR_CYCLE` | `True` | 是否轮换颜色；`False` 就固定用列表里的第一个颜色 |
| `ALWAYS_ON_TOP` | `True` | 启动时是否置顶 |
| `START_AT` | `top-right` | 初始位置：`top-right` / `top-left` / `bottom-right` / `bottom-left` |
| `CLICK_SOUND` | `True` | 是否播放点击音效 |
| `CLICK_SOUND_VOLUME` | `0.75` | 音量 0~1 |
| `CLICK_SOUND_SECONDS` | `0.15` | 音效长度 |
| `CLICK_SOUND_FILE` | `mokugyo_click.wav` | 音效缓存文件；填 `""` 则完全在内存里合成 |

想换音色：直接改脚本里 `build_mokugyo_wav()` 的 `partials`（频率、振幅、衰减时间），然后**删掉 `mokugyo_click.wav`**，下次启动会重新生成。

---

## 10. 常见问题

**方块里显示红字「缺 Key」**
没读到 Key。跑 `python deepseek_balance_cube.py --check-key`，或按第 3 节配置。

**方块里显示红字「Key 无效」**
Key 抄错了、过期了，或者值里混进了引号/空格。用 `--check-key` 看掩码和长度，和你在官网复制的那串对一下，多了少了都会显示出来。

**方块里显示红字「网络不可达」**
断网、公司网络需要代理，或者防火墙拦了 `api.deepseek.com`。

**只在当前窗口设了环境变量，双击快捷方式却读不到**
正常现象：临时变量不跨窗口。用第 3 节的方式一把 Key 写进脚本。

**点击没有声音**
右键菜单看「点击音效」是否勾上；远程桌面/无声卡的机器会静音；确认 `CLICK_SOUND = True`。

**余额数字的颜色一直在变，能固定住吗？**
这是故意的（每刷新一次换一种颜色，用来提示"刚刚刷到新数据"）。右键菜单取消勾选「颜色轮换」就固定成原来的金色；想彻底关掉，把配置区的 `AMOUNT_COLOR_CYCLE` 改成 `False`。想换配色就改 `AMOUNT_COLORS` 这个列表，第一项是固定模式下用的颜色。

**`mokugyo_click.wav` 是什么？可以删吗？**
启动时自动生成的音效缓存（存在就跳过生成）。可以删，下次启动会重新生成；也可以直接替换成你自己的 wav 文件。不喜欢在目录里看到它，就把 `CLICK_SOUND_FILE` 改成 `""`。

**峰/谷 时段怎么算的？**
周一至周五 `09:00-12:00`、`14:00-18:00` 是高峰（峰，橙色），其余时间和周末、法定假日是低谷（谷，绿色）。节假日表在脚本的 `CN_HOLIDAYS_2026`，跨年时记得补充下一年的。

**卡片看起来有点大/小**
改 `CARD_WIDTH`、`CARD_HEIGHT`、`AMOUNT_FONT_PX`，或者改 `CORNER_RADIUS` 调圆润程度。

---

## 11. 文件说明

| 文件 | 说明 |
| --- | --- |
| `deepseek_balance_cube.py` | 主脚本（唯一必需的文件） |
| `mokugyo_click.wav` | 首次启动自动生成的音效缓存，可删 |
| `README.md` | 本说明 |
| `__pycache__/` | Python 运行时生成的缓存，可删 |
