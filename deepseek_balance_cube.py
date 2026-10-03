#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""DeepSeek 余额小方块 —— 极简悬浮窗。

显示内容只有两样：
    1. 余额数字（大字号，尽量占满卡片）
    2. 峰 / 谷 徽标 + 该时段剩余时长（时:分:秒）

外观：
    · 大圆角卡片，四角圆润
    · 卡片里文字占比大，几乎没有多余留白
    · 底图逐行渲染成抗锯齿 PNG，边缘不会出现锯齿
    · 余额数字每刷新一次换一种颜色（金→橘→红→蓝→青→白，可关）

交互：
    左键点击  -> 木鱼「笃」一声 + 轻微 Q 弹一下 + 刷新余额
    拖动      -> 移动窗口
    右键菜单  -> 刷新 / 置顶开关 / 点击音效开关 / 颜色轮换开关 / 退出
    Esc       -> 退出

配置：
    顶部「配置区」里填 API_KEY；刷新节奏由 BALANCE_REFRESH_SECONDS 控制，
    两个都定义在文件开头的配置区。

怎么给 DeepSeek API Key（两种方式，任选一种）：
    方式一：把配置区的 API_KEY = "" 改成 API_KEY = "sk-你的key"。
            最省事，双击快捷方式也能用；缺点是这个文件别发给别人。
    方式二：用环境变量 DEEPSEEK_API_KEY，脚本启动时会自己读。
            这是「临时」的变量，只在当前这个命令行窗口里有效，窗口一关就没了，
            所以设完要在同一个窗口里启动脚本。三种窗口的写法：
                PowerShell :  $env:DEEPSEEK_API_KEY = "sk-你的key"
                cmd        :  set DEEPSEEK_API_KEY=sk-你的key
                Git Bash   :  export DEEPSEEK_API_KEY=sk-你的key
            别写成 `set DEEPSEEK_API_KEY=...` 就跑（那是 cmd 的写法）：
            PowerShell 里的 set 只是建了个 PowerShell 变量，环境变量还是空的。

    优先级：API_KEY 填了就用它，留空才回退到环境变量 DEEPSEEK_API_KEY。
    用快捷方式 / pythonw 启动时没有终端可以设临时变量，这种情况请用方式一。

不想看到黑色命令行窗口？用 pythonw.exe 启动，或者给它建个快捷方式：
    目标(T)    : "…\pythonw.exe" "…\deepseek_balance_cube.py"
    起始位置(S): 脚本所在文件夹
    完整步骤（含开机自启）见同目录的 README.md。

看到余额一直没出来，怎么排查：
    在命令行里运行
        python deepseek_balance_cube.py --check-key
    它会告诉你 Key 是从哪儿读到的、掩码长什么样、接口返回了什么错误。
    最常见的三个坑：
      · 临时变量只在「设它的那个窗口」里有效：换窗口、双击图标、从编辑器
        启动都读不到；脚本已经在跑的话，改完要关掉重开。
      · PowerShell 里的 set 不是 cmd 的 set：`set DEEPSEEK_API_KEY=xxx` 不会
        设置环境变量，要用 `$env:DEEPSEEK_API_KEY = "sk-xxx"`。
      · 值里别带引号：`set DEEPSEEK_API_KEY="sk-xxx"` 会把引号一起存进去，
        请求必然 401（脚本会尽量自动去掉，但最好一开始就别带）。

余额自动刷新：默认是自适应的——平时 5 分钟看一眼，发现余额在掉就自动加密，
    掉得越快刷得越勤，最快 30 秒；不动了再慢慢放回 5 分钟。
    想固定间隔或彻底关掉，改 BALANCE_REFRESH_SECONDS 即可（见配置区）。

依赖：只用 Python 标准库（tkinter）。卡片底图是自己逐行渲染出来的
      抗锯齿 PNG，所以不装 Pillow 也没有锯齿。
"""

from __future__ import annotations

import base64
import json
import math
import os
import queue
import random
import struct
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
import urllib.error
import urllib.request
import zlib
from datetime import datetime, timedelta
from typing import Any

try:  # 点击音效走系统 API 播放内存里的 WAV，Windows 以外自动静音
    import winsound
except ImportError:  # pragma: no cover - 只有非 Windows 才会进来
    winsound = None

# ============================================================================
#  ★ 配置区：把 Key 写在这里，非空则优先使用；留空则回退到环境变量
# ============================================================================
API_KEY = ""
API_KEY_ENV = "DEEPSEEK_API_KEY"

# 余额自动刷新：0 = 自适应（默认，按消耗速度在 30 秒 ~ 5 分钟之间自动调节）
#               正数 = 固定间隔（秒），例如 10 就是固定每 10 秒刷一次
#               负数 = 关掉自动刷新，只能手动点方块刷新
BALANCE_REFRESH_SECONDS = 0

API_URL = "https://api.deepseek.com/user/balance"
TIMEOUT_SECONDS = 20

# ---- 方块尺寸（设计像素，运行时会按系统 DPI 缩放）----
CARD_WIDTH = 156
CARD_HEIGHT = 120
CORNER_RADIUS = 34             # 圆角半径：越大越圆润
OUTER_MARGIN = 8               # 卡片四周的透明留白

# ---- 文字排版（设计像素）----
AMOUNT_FONT_PX = 46            # 余额数字字号
PILL_FONT_PX = 20              # 「峰/谷」徽标与倒计时字号
TEXT_WIDTH_RATIO = 0.84        # 单行文字最多占卡片宽度比例，超出自动缩字号
AMOUNT_CENTER_Y = 0.335        # 余额数字中心位置（卡片高度比例）
PILL_CENTER_Y = 0.745          # 徽标中心位置（卡片高度比例）
PILL_PAD_X = 10                # 徽标左右内边距
PILL_GAP = 7                   # 徽标内「峰/谷」与倒计时的间距
PILL_PAD_Y = 2                 # 徽标上下内边距
PILL_SIDE_MARGIN = 13          # 徽标距离卡片左右边缘的最小留白
BORDER_WIDTH = 1.2             # 卡片描边宽度（像素）

# ---- 动画（“Q 弹”）----
BOUNCE_AMPLITUDE = 0.06        # 缩放幅度：最小缩到 94%，越小越含蓄
BOUNCE_DURATION = 0.55         # 一次 Q 弹的时长（秒）
SPRITE_SCALE_STEP = 0.004      # 底图按缩放档位缓存，避免每帧重渲染

# ---- 点击音效（木鱼）----
CLICK_SOUND = True             # 点击刷新时敲一记木鱼
CLICK_SOUND_VOLUME = 0.75      # 音量 0~1
CLICK_SOUND_SECONDS = 0.15     # 声音长度（秒），木鱼是短促的“笃”
CLICK_SOUND_FILE = "mokugyo_click.wav"   # 缓存文件，放在脚本同目录；已存在就直接用

# ---- 自适应刷新节奏（BALANCE_REFRESH_SECONDS = 0 时生效）----
REFRESH_MIN_SECONDS = 30       # 最快间隔：钱掉得快时最短 30 秒看一眼
REFRESH_MAX_SECONDS = 300      # 最慢间隔：余额不动时 5 分钟看一眼
REFRESH_JITTER = 0.05          # 间隔随机抖动 ±5%，避免每次都卡在同一秒
REFRESH_RATE_TAU = 300.0       # 消耗速率的平滑时间常数（秒），越大越迟钝
REFRESH_SHRINK = 0.5           # 这次余额变了：间隔 ×0.5（更快）
REFRESH_GROW = 1.3             # 这次余额没变：间隔 ×1.3（更慢）
BALANCE_STEP = 0.01            # 余额精度（元）：大概消耗这么多就值得看一眼

# 窗口透明色：底图里等于这个颜色的像素会被系统挖空
TRANSPARENT_COLOR = "#010203"
TRANSPARENT_RGB = (1, 2, 3)

# ---- 配色 ----
COLOR_TOP = (44, 47, 56)       # 卡片渐变起始
COLOR_BOTTOM = (26, 28, 33)    # 卡片渐变结束
COLOR_BORDER = (78, 84, 96)
COLOR_AMOUNT_DIM = "#7d7360"   # 刷新中
COLOR_ERROR = "#ff7070"
COLOR_MUTED = "#8b93a1"
COLOR_PEAK = "#ff8f5e"         # 峰：橙
COLOR_PEAK_BG = "#3a2318"
COLOR_OFF = "#4ede9a"          # 谷：绿
COLOR_OFF_BG = "#16301f"

# 余额数字的颜色：每刷新成功一次就换成下一个（第一个是原来的颜色）
AMOUNT_COLOR_CYCLE = True      # 关掉就固定用列表里的第一个颜色
AMOUNT_COLORS = (
    "#ffd88a",                 # 金（原来的颜色）
    "#ff9f45",                 # 橘
    "#ff5f5f",                 # 红
    "#5aa9ff",                 # 蓝
    "#43d6c8",                 # 青
    "#eef2f7",                 # 白
)

FONT_FAMILY = "Microsoft YaHei UI"

ALWAYS_ON_TOP = True
START_AT = "top-right"         # top-right / top-left / bottom-right / bottom-left
# ============================================================================

# 中国法定节假日（国务院办公厅 国办发明电〔2025〕7 号，2026 年）。
# 高峰时段在这些日期内整日不生效。临近新年时记得补充。
CN_HOLIDAYS_2026: list[tuple[str, str]] = [
    ("2026-01-01", "2026-01-03"),  # 元旦
    ("2026-02-15", "2026-02-23"),  # 春节
    ("2026-04-04", "2026-04-06"),  # 清明
    ("2026-05-01", "2026-05-05"),  # 劳动节
    ("2026-06-19", "2026-06-21"),  # 端午
    ("2026-09-25", "2026-09-27"),  # 中秋
    ("2026-10-01", "2026-10-07"),  # 国庆
]

HOLIDAY_RANGES: list[tuple[datetime, datetime]] = [
    (datetime.strptime(start, "%Y-%m-%d"), datetime.strptime(end, "%Y-%m-%d") + timedelta(days=1))
    for start, end in CN_HOLIDAYS_2026
]

# 高峰时段（北京时间）：周一至周五 09:00-12:00、14:00-18:00
PEAK_WINDOWS: tuple[tuple[int, int], ...] = ((9, 12), (14, 18))

DEMO_PAYLOAD: dict[str, Any] = {
    "is_available": True,
    "balance_infos": [
        {"currency": "CNY", "total_balance": "18.23", "granted_balance": "8.23", "topped_up_balance": "10.00"}
    ],
}


# ---------------------------------------------------------------- 峰谷判定
def is_holiday(moment: datetime) -> bool:
    day = moment.replace(hour=0, minute=0, second=0, microsecond=0)
    return any(start <= day < end for start, end in HOLIDAY_RANGES)


def is_peak(moment: datetime) -> bool:
    """该时刻是否处于高峰计费时段。"""
    if moment.weekday() >= 5:      # 周六周日整日低谷
        return False
    if is_holiday(moment):
        return False
    minutes = moment.hour * 60 + moment.minute
    return any(start * 60 <= minutes < end * 60 for start, end in PEAK_WINDOWS)


_TRANSITION_CACHE: dict[datetime, datetime] = {}


def next_transition(moment: datetime) -> datetime:
    """返回下一次 峰<->谷 切换的时刻（北京时间）。

    结果按分钟缓存：弹性动画一秒内要重绘几十次，而长假里这个前向扫描要跑上万
    次，缓存后同一分钟内只算一次。
    """
    key = moment.replace(second=0, microsecond=0)
    cached = _TRANSITION_CACHE.get(key)
    if cached is not None:
        return cached

    candidate = key + timedelta(minutes=1)
    for _ in range(60 * 24 * 40):  # 最多扫 40 天，足够跨过任意假期
        if is_peak(candidate) != is_peak(moment):
            break
        candidate += timedelta(minutes=1)
    _TRANSITION_CACHE.clear()       # 只留当前这一分钟，别让字典无限长大
    _TRANSITION_CACHE[key] = candidate
    return candidate


def format_remaining(delta: timedelta) -> str:
    """剩余时长 -> 时:分:秒（跨天时小时数继续累加，例如 49:30:05）。"""
    total = max(0, math.ceil(delta.total_seconds()))
    hours, remainder = divmod(total, 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


# ---------------------------------------------------------------- 余额
def clean_key(value: str) -> str:
    """去掉首尾空白，以及误加进去的引号。

    `set DEEPSEEK_API_KEY="sk-xxx"` 这种写法会把引号一起存进环境变量，
    带着引号去请求必然 401，所以这里统一清一遍。
    """
    text = (value or "").strip()
    while len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        text = text[1:-1].strip()
    return text


def resolve_api_key() -> str:
    """脚本里的 API_KEY 优先，其次环境变量。"""
    if API_KEY.strip():
        return clean_key(API_KEY)
    return clean_key(os.environ.get(API_KEY_ENV) or "")


def key_source() -> str:
    """Key 是从哪儿读到的，排查时先看这一行。"""
    if API_KEY.strip():
        return "脚本里的 API_KEY"
    if clean_key(os.environ.get(API_KEY_ENV) or ""):
        return f"系统环境变量 {API_KEY_ENV}"
    return "没找到"


def mask_key(key: str) -> str:
    """只露头尾，用来确认「读到的到底是哪一串」。"""
    if len(key) <= 8:
        return "*" * len(key)
    return f"{key[:4]}{'*' * (len(key) - 6)}{key[-2:]}"


def short_error(error: str) -> str:
    """把错误压成一行短标签，好塞进卡片里显示。"""
    head = error.split(":", 1)[0].strip() or error
    return head[:10]


def fetch_balance(api_key: str) -> dict[str, Any]:
    request = urllib.request.Request(
        API_URL,
        method="GET",
        headers={"Accept": "application/json", "Authorization": f"Bearer {api_key}"},
    )
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        raw = error.read().decode("utf-8", "replace").strip()
        try:
            message = json.loads(raw).get("error", {}).get("message", "")
        except (ValueError, AttributeError):
            message = raw[:160]
        hints = {401: "Key 无效", 402: "余额不足", 403: "无权限", 429: "请求过频"}
        raise RuntimeError(f"{hints.get(error.code, 'HTTP ' + str(error.code))}: {message}".strip(": ")) from None
    except urllib.error.URLError as error:
        raise RuntimeError(f"网络不可达: {error.reason}") from None
    except json.JSONDecodeError:
        raise RuntimeError("返回不是合法 JSON") from None


def first_balance(payload: dict[str, Any] | None) -> str:
    infos = (payload or {}).get("balance_infos") or []
    if not infos:
        return "--"
    return str(infos[0].get("total_balance", "--"))


# ---------------------------------------------------------------- 刷新节奏
def parse_balance(text: str) -> float | None:
    """把接口给的余额字符串转成数字；是 "--" 之类解析不了就返回 None。"""
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


class RefreshPacer:
    """按「烧钱速度」自动决定下一次刷新间隔（30 秒 ~ 5 分钟）。

    思路：
      1. 每次拿到新余额，用 消耗量 / 间隔 估一个即时速率，再做时间加权平滑，
         这样偶尔一次波动不会立刻改变节奏，但持续消耗会攒起来；
      2. 间隔基线 = 一个最小可见变化（BALANCE_STEP 元）÷ 当前速率，
         也就是「大概花掉 0.01 元就去看一眼」——速率越快间隔越短；
      3. 这次余额变了就把间隔再砍一半（刚花过钱，盯紧点），
         没变就放长 1.3 倍（钱没动，少打扰）——两者夹在 30 秒和 5 分钟之间；
      4. 最后按 REFRESH_JITTER 加随机抖动，避免每次都踩在同一秒。
    """

    def __init__(
        self,
        minimum: float = REFRESH_MIN_SECONDS,
        maximum: float = REFRESH_MAX_SECONDS,
    ) -> None:
        self.minimum = float(minimum)
        self.maximum = float(maximum)
        self.interval = float(maximum)      # 还没测出速率，先按最慢的来
        self.rate = 0.0                     # 平滑后的消耗速率（元/秒）
        self._balance: float | None = None
        self._moment: float | None = None

    def observe(self, balance: str | None, moment: float | None = None) -> float:
        """喂入最新余额，返回下一次该等多少秒（已含随机抖动）。"""
        now = time.monotonic() if moment is None else moment
        value = parse_balance(balance or "")
        if value is None:
            return self.delay()             # 拿不到数字就维持当前节奏

        if self._balance is not None and self._moment is not None:
            elapsed = max(1.0, now - self._moment)
            spent = max(0.0, self._balance - value)     # 充值导致的变多不算消耗
            instant = spent / elapsed
            blend = 1.0 - math.exp(-elapsed / REFRESH_RATE_TAU)
            self.rate += (instant - self.rate) * blend

            target = BALANCE_STEP / self.rate if self.rate > 0 else self.maximum
            if spent > 0:
                target = min(target, self.interval * REFRESH_SHRINK)
            elif elapsed >= max(self.minimum, self.interval * 0.5):
                # 只有观察窗口够长，「余额没变」才真的说明消耗慢；
                # 手动连点这种 1 秒的窗口不做判断，免得把间隔越点越长
                target = max(target, self.interval * REFRESH_GROW)
            self.interval = min(self.maximum, max(self.minimum, target))

        self._balance, self._moment = value, now
        return self.delay()

    def delay(self) -> float:
        """当前间隔 + 随机抖动（秒）。"""
        return self.interval * random.uniform(1.0 - REFRESH_JITTER, 1.0 + REFRESH_JITTER)


# ---------------------------------------------------------------- 绘制
def _corner_inset(distance: float, radius: float) -> float:
    """距上/下边缘 distance 处，圆角造成的水平内缩量（圆角矩形的精确解）。"""
    if radius <= 0 or distance <= 0 or distance >= radius:
        return 0.0
    return radius - math.sqrt(max(0.0, radius * radius - (radius - distance) ** 2))


def _row_span(row: int, box: tuple[float, float, float, float], radius: float) -> tuple[float, float] | None:
    """像素行 row 在圆角矩形里的水平覆盖区间；整行都在外面时返回 None。"""
    x0, y0, x1, y1 = box
    center = row + 0.5
    if center <= y0 or center >= y1 or x1 <= x0:
        return None
    inset = max(_corner_inset(center - y0, radius), _corner_inset(y1 - center, radius))
    return x0 + inset, x1 - inset


def _row_coverage(row: int, y0: float, y1: float) -> float:
    """像素行 row 被 [y0, y1] 纵向覆盖的比例，用来给上/下边缘做抗锯齿。"""
    covered = min(row + 1.0, y1) - max(float(row), y0)
    return min(1.0, max(0.0, covered))


def _paint_row(row: bytearray, width: int, lo: float, hi: float, color: tuple[int, int, int], alpha: float = 1.0) -> None:
    """把 [lo, hi) 按覆盖率混进这一行：中间整段直接填，只有边界像素做混色。"""
    if alpha <= 0.001 or hi <= lo:
        return
    lo = max(0.0, lo)
    hi = min(float(width), hi)
    if hi <= lo:
        return

    full_lo = max(0, int(math.ceil(lo - 1e-9)))
    full_hi = min(width - 1, int(math.floor(hi + 1e-9)) - 1)
    if full_hi >= full_lo:
        if alpha >= 0.999:
            row[full_lo * 3:(full_hi + 1) * 3] = bytes(color) * (full_hi - full_lo + 1)
        else:
            for index in range(full_lo, full_hi + 1):
                offset = index * 3
                for channel in range(3):
                    current = row[offset + channel]
                    row[offset + channel] = int(current + (color[channel] - current) * alpha)

    for index in (int(math.floor(lo)), int(math.ceil(hi)) - 1):
        if not 0 <= index < width or full_lo <= index <= full_hi:
            continue
        coverage = min(1.0, (min(hi, index + 1.0) - max(lo, float(index))) * alpha)
        if coverage <= 0.001:
            continue
        offset = index * 3
        for channel in range(3):
            current = row[offset + channel]
            row[offset + channel] = int(current + (color[channel] - current) * coverage)


def _gradient_at(row: int, y0: float, y1: float) -> tuple[int, int, int]:
    span = max(1.0, y1 - y0)
    t = min(1.0, max(0.0, (row + 0.5 - y0) / span))
    return tuple(
        int(COLOR_TOP[i] + (COLOR_BOTTOM[i] - COLOR_TOP[i]) * t) for i in range(3)
    )  # type: ignore[return-value]


def _encode_png(width: int, height: int, rows: list[bytes]) -> bytes:
    """把逐行 RGB 数据打包成 PNG（Tk 8.6+ 自带 PNG 解码）。"""
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)   # 8bit RGB
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def render_card_png(
    width: int,
    height: int,
    card: tuple[float, float, float, float],
    radius: float,
    pill: tuple[float, float, float, float] | None,
    pill_color: tuple[int, int, int],
) -> bytes:
    """逐行渲染卡片底图：圆角 + 垂直渐变 + 描边（全部带抗锯齿）。

    每行先算出圆角矩形在该行的水平区间，区间中间整段填充、只有左右边界
    那一两个像素按覆盖率混色；上下边缘再用行覆盖率补一次，所以四边都不会
    出现楼梯状锯齿。徽标同理画在同一张底图里，边缘自然也是平滑的。
    """
    rows: list[bytes] = []
    for row in range(height):
        line = bytearray(bytes(TRANSPARENT_RGB) * width)

        coverage = _row_coverage(row, card[1], card[3])
        span = _row_span(row, card, radius) if coverage > 0 else None
        if span is not None:
            _paint_row(line, width, span[0], span[1], COLOR_BORDER, coverage)
            _paint_row(
                line, width, span[0] + BORDER_WIDTH, span[1] - BORDER_WIDTH,
                _gradient_at(row, card[1], card[3]), coverage,
            )

        if pill is not None:
            pill_radius = min((pill[2] - pill[0]) / 2, (pill[3] - pill[1]) / 2)
            pill_coverage = _row_coverage(row, pill[1], pill[3])
            pill_span = _row_span(row, pill, pill_radius) if pill_coverage > 0 else None
            if pill_span is not None:
                _paint_row(line, width, pill_span[0], pill_span[1], pill_color, pill_coverage)

        rows.append(bytes(line))
    return _encode_png(width, height, rows)


def canonical_clock(clock: str) -> str:
    """把倒计时里的数字都换成等宽的 8：徽标宽度就不会随秒数跳动。"""
    return "".join("8" if char.isdigit() else char for char in clock)


def round_rect_points(x0: float, y0: float, x1: float, y1: float, r: float, steps: int = 8) -> list[float]:
    """圆角矩形的多边形顶点：每个角用显式圆弧采样。

    不依赖 Tk 的 smooth 样条——样条的控制点语义会让圆角出现接缝。
    """
    r = max(0.0, min(r, (x1 - x0) / 2, (y1 - y0) / 2))
    if r <= 0.5:
        return [x0, y0, x1, y0, x1, y1, x0, y1]
    points: list[float] = []
    centers = (
        (x1 - r, y0 + r, -90.0, 0.0),    # 右上
        (x1 - r, y1 - r, 0.0, 90.0),     # 右下
        (x0 + r, y1 - r, 90.0, 180.0),   # 左下
        (x0 + r, y0 + r, 180.0, 270.0),  # 左上
    )
    for cx, cy, start, end in centers:
        for index in range(steps + 1):
            angle = math.radians(start + (end - start) * index / steps)
            points.extend((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return points


def hex_color(rgb: tuple[int, int, int]) -> str:
    return "#%02x%02x%02x" % rgb


def hex_to_rgb(color: str) -> tuple[int, int, int]:
    color = color.lstrip("#")
    return tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]


# ---------------------------------------------------------------- 音效
def _wav_bytes(samples: list[int], sample_rate: int) -> bytes:
    """把 16bit 单声道样本打包成 WAV（只在内存里，不落盘）。"""
    frames = struct.pack("<%dh" % len(samples), *samples)
    return (
        b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVE"
        + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate, sample_rate * 2, 2, 16)
        + b"data" + struct.pack("<I", len(frames)) + frames
    )


def build_mokugyo_wav(volume: float = CLICK_SOUND_VOLUME) -> bytes:
    """合成一记木鱼「笃」。

    木鱼的声音 = 极短的敲击噪声 + 几个快速衰减的谐振峰（略带非谐波）+ 木腔的
    低频体感，再叠一点音高下滑。全程纯计算，不需要任何音频素材。
    """
    sample_rate = 44100
    count = int(sample_rate * CLICK_SOUND_SECONDS)
    # (频率 Hz, 振幅, 衰减时间常数 秒)
    partials = (
        (430.0, 0.30, 0.045),      # 木腔的低频“闷”
        (1180.0, 1.00, 0.030),     # 主音
        (1810.0, 0.52, 0.018),     # 非谐波泛音，木头味就靠它
        (2470.0, 0.24, 0.011),
    )
    rng = random.Random(20261003)          # 固定种子：每次听起来都一样
    noise = [rng.uniform(-1.0, 1.0) for _ in range(count)]

    raw: list[float] = []
    for index in range(count):
        t = index / sample_rate
        bend = 1.0 + 0.055 * math.exp(-t / 0.022)          # 敲下去那一瞬间略高
        value = 0.0
        for freq, amp, tau in partials:
            value += amp * math.exp(-t / tau) * math.sin(2 * math.pi * freq * bend * t)
        value += 0.55 * noise[index] * math.exp(-t / 0.0014)   # 起手的敲击声
        value *= 1.0 - math.exp(-t / 0.0004)                   # 0.4ms 淡入，防爆音
        raw.append(value)

    peak = max(abs(value) for value in raw) or 1.0
    scale = min(1.0, max(0.0, volume)) * 32767.0 * 0.92 / peak
    return _wav_bytes([int(value * scale) for value in raw], sample_rate)


def script_dir() -> str:
    """脚本所在的目录。

    打包成 exe 之后 __file__ 指向的是临时解包目录（用完就删），所以冻结
    状态下要改用 exe 自己的位置，音效缓存才会落在 exe 旁边。
    """
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


class ClickSound:
    """点击音效：优先用脚本同目录的 WAV 文件，没有就现场合成并写出来。

    为什么要落一个文件：
      · 文件在的话启动时直接跳过合成（省掉那 6~7ms）；
      · 播文件可以用真正的 SND_ASYNC（CPython 不允许「异步 + 内存」），
        点击回调立刻返回，不用为每次点击开一个线程；
      · 想换成自己的声音，直接把这个 wav 替换掉就行。
    目录不可写、没有 winsound 时自动退回「内存合成 + 后台线程播放」。
    """

    def __init__(
        self,
        enabled: bool = CLICK_SOUND,
        volume: float = CLICK_SOUND_VOLUME,
        filename: str = CLICK_SOUND_FILE,
    ) -> None:
        self.enabled = enabled
        self.volume = volume
        self.path = os.path.join(script_dir(), filename) if filename else ""
        self._wav: bytes | None = None      # 兜底：文件不可用时就内存播放
        self._ready = False

    @staticmethod
    def _looks_like_wav(path: str) -> bool:
        """存在、是 RIFF/WAVE、且带数据块，就当作可用（也允许用户自己换一个）。"""
        try:
            if not os.path.isfile(path) or os.path.getsize(path) <= 44:
                return False
            with open(path, "rb") as handle:
                head = handle.read(12)
            return head[:4] == b"RIFF" and head[8:12] == b"WAVE"
        except OSError:
            return False

    def _write_cache(self, data: bytes) -> None:
        """先写临时文件再改名：避免生成到一半被中断留下坏文件。"""
        temp = self.path + ".tmp"
        with open(temp, "wb") as handle:
            handle.write(data)
        os.replace(temp, self.path)

    def prepare(self) -> None:
        """启动时调用：有缓存文件就直接用，没有才合成并写出来。"""
        if self._ready or winsound is None:
            return
        self._ready = True
        try:
            if self.path and not self._looks_like_wav(self.path):
                try:
                    self._write_cache(build_mokugyo_wav(self.volume))
                except OSError:
                    self.path = ""          # 目录只读之类：退回内存播放
            if not self.path:
                self._wav = build_mokugyo_wav(self.volume)
        except Exception:  # noqa: BLE001 - 合成失败就当没有音效
            self.enabled = False

    def play(self) -> None:
        if not self.enabled or winsound is None:
            return
        if not self._ready:
            self.prepare()
        if not self.enabled:
            return
        try:
            if self.path:
                # 文件播放可以真异步：立即返回，不占线程、不卡界面
                winsound.PlaySound(
                    self.path, winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT
                )
            elif self._wav is not None:
                # 内存在 CPython 里只能同步播，丢到后台线程去
                threading.Thread(target=self._play_blocking, daemon=True).start()
        except Exception:  # noqa: BLE001 - 没声卡/播放失败也不该影响刷新
            self.enabled = False

    def _play_blocking(self) -> None:
        try:
            winsound.PlaySound(self._wav, winsound.SND_MEMORY | winsound.SND_NODEFAULT)
        except Exception:  # noqa: BLE001
            self.enabled = False


class BalanceCube:
    def __init__(self, demo: bool = False, self_test: float = 0.0) -> None:
        self.demo = demo
        self.self_test = self_test
        self.root = tk.Tk()
        self.root.title("DeepSeek 余额")
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", ALWAYS_ON_TOP)

        self.ui_scale = max(1.0, float(self.root.tk.call("tk", "scaling")) / 1.3333)
        self.px_w = int(round(CARD_WIDTH * self.ui_scale))
        self.px_h = int(round(CARD_HEIGHT * self.ui_scale))
        self.px_r = int(round(CORNER_RADIUS * self.ui_scale))
        self.px_margin = max(4, int(round(OUTER_MARGIN * self.ui_scale)))

        self.total_w = self.px_w + self.px_margin * 2
        self.total_h = self.px_h + self.px_margin * 2
        self._place_window()

        transparent = TRANSPARENT_COLOR
        self.root.config(bg=transparent)
        self.canvas_bg = transparent
        try:
            self.root.attributes("-transparentcolor", transparent)
        except tk.TclError:
            self.canvas_bg = "#101216"  # 不支持透明时给个深色底

        self.canvas = tk.Canvas(
            self.root, width=self.total_w, height=self.total_h,
            highlightthickness=0, bd=0, bg=self.canvas_bg,
        )
        self.canvas.pack()

        # 状态
        self._state: dict[str, Any] = {
            "payload": None, "error": "", "busy": False, "last_ok": 0.0,
        }
        self._scale = 1.0            # 当前弹性缩放
        self._anim_start = 0.0
        self._anim_job: str | None = None
        self._tick_job: str | None = None
        self._poll_job: str | None = None
        self._refresh_job: str | None = None
        self._pending: queue.Queue = queue.Queue()
        self._fonts: dict[tuple[int, str], tkfont.Font] = {}
        self._sprites: dict[int, tk.PhotoImage] = {}   # 底图按缩放档位缓存
        self._sprite_signature: tuple[Any, ...] | None = None
        self._sprite_ok = True                          # Tk 不支持 PNG 时退回矢量绘制
        self.topmost = bool(ALWAYS_ON_TOP)
        self.sound = ClickSound()
        self.pacer = RefreshPacer()      # 自适应刷新节奏（按消耗速度调节）
        self.color_cycle = AMOUNT_COLOR_CYCLE   # 余额数字是否每次刷新换颜色
        self._color_index = 0
        self._color_started = False             # 第一次拿到的余额仍用原色
        self._press: tuple[int, int, int, int] | None = None   # x_root,y_root,win_x,win_y
        self._dragged = False

        self._bind()
        # 木鱼波形放后台线程先算好，第一次点击不用等
        threading.Thread(target=self.sound.prepare, daemon=True).start()
        # 首帧延后到事件循环里：__init__ 期间 tk scaling 尚未稳定，
        # 而且窗口未映射时 winfo_* 还不可用。
        self.root.after(0, self._redraw)
        self.root.after(80, self.refresh)
        if self.self_test > 0:
            self.root.after(int(self.self_test * 1000), self._quit)

    # -- 定位
    def _place_window(self) -> None:
        screen_w = self.root.winfo_screenwidth()
        screen_h = self.root.winfo_screenheight()
        margin = 12
        positions = {
            "top-right": (screen_w - self.total_w - margin, margin),
            "top-left": (margin, margin),
            "bottom-right": (screen_w - self.total_w - margin, screen_h - self.total_h - 60),
            "bottom-left": (margin, screen_h - self.total_h - 60),
        }
        x, y = positions.get(START_AT, positions["top-right"])
        self.root.geometry(f"{self.total_w}x{self.total_h}+{max(0, x)}+{max(0, y)}")

    # -- 坐标（含四周透明留白）
    def card_left(self, scale: float | None = None) -> float:
        s = self._scale if scale is None else scale
        return self.px_margin + self.px_w * (1 - s) / 2

    def card_top(self, scale: float | None = None) -> float:
        s = self._scale if scale is None else scale
        return self.px_margin + self.px_h * (1 - s) / 2

    def card_width(self, scale: float | None = None) -> float:
        s = self._scale if scale is None else scale
        return self.px_w * s

    def card_height(self, scale: float | None = None) -> float:
        s = self._scale if scale is None else scale
        return self.px_h * s

    # -- 渲染
    def _redraw(self) -> None:
        """重画一帧：底图（带抗锯齿）+ 文字。

        每次都要 delete("all")：徽标和文字原来没有 tag，只删 "card" 会让
        上一帧的旧徽标留在画布上，Q 弹时就会看到一条越来越长的黑色残影。
        """
        self.canvas.delete("all")
        layout = self._layout(self._sprite_scale())
        image = self._sprite_for(layout)
        if image is not None:
            self.canvas.create_image(0, 0, image=image, anchor="nw", tags=("card",))
        else:
            self._draw_card_vector(layout)
        self._draw_content(layout)

    def _sprite_scale(self) -> float:
        """把弹性缩放量化成档位，同一档位的底图可以直接复用。"""
        return max(0.5, round(self._scale / SPRITE_SCALE_STEP) * SPRITE_SCALE_STEP)

    def _sprite_for(self, layout: dict[str, Any]) -> tk.PhotoImage | None:
        """取当前档位的底图；同一档位只在卡片/徽标尺寸变化时重渲染。"""
        if not self._sprite_ok:
            return None
        # 签名只放「和缩放无关」的内容：峰/谷、倒计时位数、徽标底色。
        # 缩放档位本身是缓存键，不能再进签名，否则每帧都会把缓存清空。
        signature = layout["pill_signature"]
        if signature != self._sprite_signature:
            self._sprites.clear()
            self._sprite_signature = signature
        image = self._sprites.get(layout["sprite_key"])
        if image is not None:
            return image
        try:
            png = render_card_png(
                self.total_w, self.total_h, layout["card"], layout["radius"],
                layout["pill"], layout["pill_bg_rgb"],
            )
            image = tk.PhotoImage(master=self.root, data=base64.b64encode(png).decode("ascii"))
        except (tk.TclError, ValueError):
            self._sprite_ok = False     # Tk 太老不认 PNG：退回矢量绘制
            return None
        if len(self._sprites) > 64:
            self._sprites.clear()
        self._sprites[layout["sprite_key"]] = image
        return image

    def _draw_card_vector(self, layout: dict[str, Any]) -> None:
        """没有 PNG 支持时的兜底：用多边形拼出卡片（会有轻微锯齿）。"""
        x0, y0, x1, y1 = layout["card"]
        radius = min(layout["radius"], (x1 - x0) / 2, (y1 - y0) / 2)
        height = y1 - y0
        steps = max(24, int(height / 2))
        for index in range(steps):
            top = y0 + height * index / steps
            bottom = y0 + height * (index + 1) / steps
            inset = max(
                _corner_inset(top - y0, radius),
                _corner_inset(y1 - bottom, radius),
            )
            self.canvas.create_polygon(
                x0 + inset, top, x1 - inset, top,
                x1 - inset, bottom, x0 + inset, bottom,
                fill=hex_color(_gradient_at(int(top), y0, y1)),
                outline="", tags=("card",),
            )
        self.canvas.create_polygon(
            round_rect_points(x0, y0, x1, y1, radius, steps=24),
            fill="", outline=hex_color(COLOR_BORDER), width=1, tags=("card",),
        )
        pill = layout["pill"]
        if pill is not None:
            self.canvas.create_polygon(
                round_rect_points(*pill, min((pill[2] - pill[0]) / 2, (pill[3] - pill[1]) / 2), steps=24),
                fill=layout["pill_bg"], outline="", tags=("card",),
            )

    def _font(self, size_px: float, weight: str = "normal") -> tkfont.Font:
        """按像素取字体（带缓存）。

        负数字号在 Tk 里就是「像素」语义，省掉 DPI 的二次换算；
        传进来的 size_px 已经乘过 ui_scale 与弹性动画的缩放系数。
        """
        size = -max(6, int(round(size_px)))
        key = (size, weight)
        font = self._fonts.get(key)
        if font is None:
            font = tkfont.Font(root=self.root, family=FONT_FAMILY, size=size, weight=weight)
            self._fonts[key] = font
        return font

    def _fit_font(self, text: str, size_px: float, max_width: float, weight: str) -> tkfont.Font:
        """字号自适应：文本太宽就等比缩小——字尽量大，但绝不溢出卡片。"""
        font = self._font(size_px, weight)
        text_width = font.measure(text)
        if max_width > 0 and text_width > max_width:
            font = self._font(size_px * max_width / text_width, weight)
        return font

    def _layout(self, scale: float) -> dict[str, Any]:
        """算好一帧的全部几何：卡片、圆角、徽标矩形、字号、文字落点。

        底图渲染和文字绘制共用这份结果，所以文字一定落在徽标正中间，
        Q 弹时两者也是同一个缩放档位，不会上下不一致。
        """
        x0 = self.px_margin + self.px_w * (1 - scale) / 2
        y0 = self.px_margin + self.px_h * (1 - scale) / 2
        x1, y1 = x0 + self.px_w * scale, y0 + self.px_h * scale
        width, height = x1 - x0, y1 - y0
        cx = x0 + width / 2
        zoom = scale * self.ui_scale             # 弹性动画缩放 + 高 DPI

        # ---- 余额数字：出错时直接把原因写在卡片上，别只给个红杠 ----
        amount = first_balance(self._state["payload"])
        if self._state["error"]:
            amount = short_error(self._state["error"])
        color = AMOUNT_COLORS[self._color_index % len(AMOUNT_COLORS)]
        if self._state["error"]:
            color = COLOR_ERROR
        elif self._state["busy"]:
            color = COLOR_AMOUNT_DIM
        amount_font = self._fit_font(amount, AMOUNT_FONT_PX * zoom, width * TEXT_WIDTH_RATIO, "bold")

        # ---- 峰 / 谷 徽标 + 时:分:秒 倒计时 ----
        now = datetime.now()
        peak = is_peak(now)
        label = "峰" if peak else "谷"
        clock = format_remaining(next_transition(now) - now)
        fg = COLOR_PEAK if peak else COLOR_OFF
        bg = COLOR_PEAK_BG if peak else COLOR_OFF_BG

        pill_px = PILL_FONT_PX * zoom
        pad, gap = PILL_PAD_X * zoom, PILL_GAP * zoom
        font = self._font(pill_px, "bold")
        # 宽度按「把数字都换成 8」的等宽版本算：秒数变化时徽标不会抖
        label_w = font.measure(label)
        clock_w = font.measure(canonical_clock(clock))
        pill_w = label_w + gap + clock_w + pad * 2

        # 长假时倒计时会出现三位数小时，整块徽标等比缩小也要塞进卡片
        avail = width - 2 * PILL_SIDE_MARGIN * zoom
        if pill_w > avail > 0:
            shrink = avail / pill_w
            pad, gap = pad * shrink, gap * shrink
            font = self._font(pill_px * shrink, "bold")
            label_w = font.measure(label)
            clock_w = font.measure(canonical_clock(clock))
            pill_w = label_w + gap + clock_w + pad * 2

        pill_h = font.metrics("linespace") + 2 * PILL_PAD_Y * zoom
        cx_pill = cx - pill_w / 2
        cy_pill = y0 + height * PILL_CENTER_Y
        pill = (cx_pill, cy_pill - pill_h / 2, cx_pill + pill_w, cy_pill + pill_h / 2)

        return {
            "card": (x0, y0, x1, y1),
            "radius": max(2.0, min(self.px_r * scale, width / 2, height / 2)),
            "sprite_key": int(round(scale * 1000)),
            "amount": amount,
            "amount_color": color,
            "amount_font": amount_font,
            "amount_pos": (cx, y0 + height * AMOUNT_CENTER_Y),
            "pill": pill,
            "pill_bg": bg,
            "pill_bg_rgb": hex_to_rgb(bg),
            "pill_fg": fg,
            "pill_font": font,
            "label": label,
            "clock": clock,
            "pill_signature": (label, canonical_clock(clock), bg, round(self.ui_scale, 4)),
            "label_pos": (cx_pill + pad + label_w / 2, cy_pill),
            "clock_pos": (cx_pill + pill_w - pad - font.measure(clock) / 2, cy_pill),
        }

    def _draw_content(self, layout: dict[str, Any]) -> None:
        """文字单独用 Tk 文本画（系统自带抗锯齿），底图里只有形状。"""
        self.canvas.create_text(
            *layout["amount_pos"], text=layout["amount"], fill=layout["amount_color"],
            font=layout["amount_font"], anchor="center", tags=("card",),
        )
        self.canvas.create_text(
            *layout["label_pos"], text=layout["label"], fill=layout["pill_fg"],
            font=layout["pill_font"], anchor="center", tags=("card",),
        )
        self.canvas.create_text(
            *layout["clock_pos"], text=layout["clock"], fill=layout["pill_fg"],
            font=layout["pill_font"], anchor="center", tags=("card",),
        )

    # -- 弹性动画
    def _bounce(self) -> None:
        self._anim_start = time.monotonic()
        if self._anim_job is not None:
            self.root.after_cancel(self._anim_job)
        self._animate()

    def _animate(self) -> None:
        elapsed = time.monotonic() - self._anim_start
        duration = BOUNCE_DURATION
        if elapsed >= duration:
            self._scale = 1.0
            self._redraw()
            self._anim_job = None
            return
        progress = elapsed / duration
        # 欠阻尼弹簧：1-幅度 -> 1.0，回弹两下（幅度小，含蓄一点）
        self._scale = 1.0 - BOUNCE_AMPLITUDE * math_exp_decay(progress)
        self._redraw()
        self._anim_job = self.root.after(16, self._animate)

    # -- 事件
    def _bind(self) -> None:
        self.canvas.bind("<ButtonPress-1>", self._on_press)
        self.canvas.bind("<B1-Motion>", self._on_motion)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<Button-3>", self._on_menu)
        self.root.bind("<Escape>", lambda _event: self._quit())
        self.menu = tk.Menu(self.root, tearoff=0)
        self.menu.add_command(label="刷新", command=self._menu_refresh)
        self._top_var = tk.BooleanVar(master=self.root, value=self.topmost)
        self.menu.add_checkbutton(label="置顶", variable=self._top_var, command=self._toggle_top)
        self._sound_var = tk.BooleanVar(master=self.root, value=self.sound.enabled)
        self.menu.add_checkbutton(label="点击音效", variable=self._sound_var, command=self._toggle_sound)
        self._color_var = tk.BooleanVar(master=self.root, value=self.color_cycle)
        self.menu.add_checkbutton(label="颜色轮换", variable=self._color_var, command=self._toggle_color_cycle)
        self.menu.add_separator()
        self.menu.add_command(label="退出", command=self._quit)

    def _toggle_top(self) -> None:
        """菜单里的「置顶」：勾选 = 置顶，取消 = 不置顶。"""
        self.topmost = bool(self._top_var.get())
        self.root.attributes("-topmost", self.topmost)

    def _menu_refresh(self) -> None:
        """右键菜单里的刷新：也敲一记木鱼。"""
        self.sound.play()
        self.refresh()

    def _toggle_sound(self) -> None:
        self.sound.enabled = bool(self._sound_var.get())
        if self.sound.enabled:
            self.sound.play()        # 重新打开时立刻响一声，方便确认

    def _toggle_color_cycle(self) -> None:
        """菜单里的「颜色轮换」：关掉就回到第一个颜色，不再变。"""
        self.color_cycle = bool(self._color_var.get())
        if not self.color_cycle:
            self._color_index = 0
        self._redraw()

    def _on_press(self, event: tk.Event) -> None:
        self._press = (event.x_root, event.y_root, self.root.winfo_x(), self.root.winfo_y())
        self._dragged = False

    def _on_motion(self, event: tk.Event) -> None:
        if self._press is None:
            return
        x_root, y_root, win_x, win_y = self._press
        dx, dy = event.x_root - x_root, event.y_root - y_root
        if abs(dx) > 3 or abs(dy) > 3:
            self._dragged = True
        if self._dragged:
            self.root.geometry(f"+{win_x + dx}+{win_y + dy}")

    def _on_release(self, _event: tk.Event) -> None:
        if not self._dragged:
            self.sound.play()        # 木鱼「笃」
            self._bounce()   # 先 Q 弹，再刷新
            self.root.after(90, self.refresh)
        self._press = None

    def _on_menu(self, event: tk.Event) -> None:
        try:
            self.menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.menu.grab_release()

    # -- 刷新
    def refresh(self) -> None:
        if self._refresh_job is not None:
            self.root.after_cancel(self._refresh_job)
            self._refresh_job = None
        if self._state["busy"]:
            return

        key = resolve_api_key()
        if not key and not self.demo:
            self._state.update(busy=False, error="缺 Key")
            self._redraw()
            return

        self._state["busy"] = True
        self._redraw()
        threading.Thread(target=self._worker, args=(key,), daemon=True).start()

    def _worker(self, key: str) -> None:
        """后台线程：只做网络请求，绝不碰 Tk 对象。

        Tk 的 after/createcommand 不是线程安全的（`main thread is not in main
        loop`），所以结果放进队列，由主线程轮询取出。
        """
        try:
            payload = DEMO_PAYLOAD if self.demo else fetch_balance(key)
            self._pending.put((payload, ""))
        except RuntimeError as error:
            self._pending.put((None, str(error)))
        except Exception as error:  # noqa: BLE001
            self._pending.put((None, repr(error)))

    def _poll_pending(self) -> None:
        """主线程轮询后台结果。"""
        while True:
            try:
                payload, error = self._pending.get_nowait()
            except queue.Empty:
                break
            self._done(payload, error)
        self._poll_job = self.root.after(50, self._poll_pending)

    def _done(self, payload: dict[str, Any] | None, error: str) -> None:
        self._state["busy"] = False
        if payload is not None:
            self._state["payload"] = payload
            self._state["error"] = ""
            self._state["last_ok"] = time.time()
            if self.color_cycle and self._color_started:
                # 每刷到一次新数据就换下一个颜色（启动那一次仍显示原来的颜色）
                self._color_index = (self._color_index + 1) % len(AMOUNT_COLORS)
            self._color_started = True
        else:
            self._state["error"] = error
        self._redraw()
        if BALANCE_REFRESH_SECONDS < 0 or self._refresh_job is not None:
            return                      # 负数 = 关掉自动刷新，只留手动点
        delay = self._next_delay(fresh=payload is not None)
        self._refresh_job = self.root.after(int(delay * 1000), self.refresh)

    def _next_delay(self, fresh: bool) -> float:
        """下一次自动刷新等多少秒：固定间隔 or 自适应节奏。"""
        if BALANCE_REFRESH_SECONDS > 0:
            return float(BALANCE_REFRESH_SECONDS)
        if not fresh:
            return self.pacer.delay()   # 这次没拿到数据，维持当前节奏再来
        return self.pacer.observe(first_balance(self._state["payload"]))

    def _tick(self) -> None:
        """每秒刷新一次峰谷倒计时（重绘本身很轻，卡片底图有缓存）。"""
        self._redraw()
        self._tick_job = self.root.after(1000, self._tick)

    def _quit(self) -> None:
        for job in (self._anim_job, self._tick_job, self._poll_job, self._refresh_job):
            if job is not None:
                try:
                    self.root.after_cancel(job)
                except tk.TclError:
                    pass
        self.root.destroy()

    def run(self) -> None:
        self._poll_job = self.root.after(50, self._poll_pending)
        self._tick_job = self.root.after(1000, self._tick)
        self.root.mainloop()


def math_exp_decay(progress: float) -> float:
    """欠阻尼衰减：t=0 时为 1，之后回弹两下。"""
    return math.exp(-4.2 * progress) * math.cos(11.0 * progress)


def emit(text: str) -> None:
    """尽量把文字写到控制台。

    打包成窗口程序（pythonw）时没有控制台，stdout 可能为 None，也可能是句柄
    已经失效的对象，直接 print 有可能抛异常。这里统一吞掉，绝不让写日志
    这种小事把程序搞崩。
    """
    if sys.stdout is None:
        return
    try:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()
    except Exception:  # noqa: BLE001
        pass


def show_report(title: str, lines: list[str]) -> None:
    """把一段文字报给用户：有控制台就打出来，没有（打包成窗口程序）就弹对话框。"""
    text = "\n".join(lines)
    if sys.stdout is not None:
        try:
            sys.stdout.write(text + "\n")
            sys.stdout.flush()
            return
        except Exception:  # noqa: BLE001 - 句柄无效，落到下面弹窗
            pass
    try:
        from tkinter import messagebox

        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        messagebox.showinfo(title, text, parent=root)
        root.destroy()
    except Exception:  # noqa: BLE001 - 弹不出来也不能崩
        pass


def check_api_key() -> int:
    """命令行排查：Key 从哪儿来、长什么样、接口到底怎么回。

    对应命令：python deepseek_balance_cube.py --check-key
    0 = 一切正常，1 = 请求失败，2 = 根本没找到 Key。
    打包成窗口程序（pythonw）时没有控制台，结果会弹对话框显示。
    """
    raw = os.environ.get(API_KEY_ENV) or ""
    key = resolve_api_key()
    lines = [f"Key 来源：{key_source()}"]

    if not key:
        lines += [
            "",
            "没有读到 Key。两种给 Key 的方式（二选一）：",
            '  1) 把脚本/配置里的 API_KEY = "" 改成 API_KEY = "sk-你的key"',
            f"  2) 设一个临时的环境变量 {API_KEY_ENV}，然后「在同一个窗口里」启动：",
            f'       PowerShell :  $env:{API_KEY_ENV} = "sk-你的key"',
            f"       cmd        :  set {API_KEY_ENV}=sk-你的key",
            f"       Git Bash   :  export {API_KEY_ENV}=sk-你的key",
            "",
            "两个最容易踩的坑：",
            "  · PowerShell 里的 set 不是 cmd 的 set：`set 变量=值` 不会设置环境变量，",
            "    只是建了个名字里带等号的 PowerShell 变量，$env: 里还是空的。",
            "  · 临时变量只活在那个窗口里：双击图标、换窗口、从编辑器启动都读不到。",
            "    想双击 exe 就能用，请走方式 1（或打包时用 --onefile 出来的 exe 也一样）。",
        ]
        show_report("DeepSeek 余额 · Key 检查", lines)
        return 2

    lines.append(f"Key 掩码：{mask_key(key)}    长度：{len(key)}")
    if raw and clean_key(raw) != raw:
        lines.append("提醒：环境变量的值首尾多了空白或引号，脚本已自动清理；建议重设成干净的值。")
    if not key.startswith("sk-"):
        lines.append("提醒：DeepSeek 的 Key 一般以 sk- 开头，这个看着不太像。")

    lines.append(f"正在请求 {API_URL} …")
    try:
        payload = fetch_balance(key)
    except RuntimeError as error:
        lines += [
            f"结果：失败 —— {error}",
            "",
            "对号入座：",
            "  · Key 无效 / 401        → Key 抄错了、带引号了，或者已经被删掉",
            "  · 余额不足 402          → 账户欠费，去官网充值",
            "  · 网络不可达            → 断网、需要代理/VPN，或防火墙拦了 api.deepseek.com",
            "  · 请求过频 429          → 等一下再试",
        ]
        show_report("DeepSeek 余额 · Key 检查", lines)
        return 1

    currency = ""
    infos = payload.get("balance_infos") or []
    if infos:
        currency = str(infos[0].get("currency", ""))
    lines.append(f"结果：成功，余额 {first_balance(payload)} {currency}")
    show_report("DeepSeek 余额 · Key 检查", lines)
    return 0


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="DeepSeek 余额小方块")
    parser.add_argument("--demo", action="store_true", help="用假数据渲染，不发起网络请求")
    parser.add_argument("--self-test", type=float, default=0.0, metavar="秒", help="渲染指定秒数后退出")
    parser.add_argument("--check-key", action="store_true", help="只检查 Key 和网络，不开窗口")
    args = parser.parse_args()
    if args.check_key:
        sys.exit(check_api_key())
    if not args.demo and not resolve_api_key():
        emit(
            "没找到 DeepSeek API Key：可以填脚本配置区的 API_KEY，"
            f"或设置环境变量 {API_KEY_ENV}；"
            "详细排查请运行: python deepseek_balance_cube.py --check-key"
        )
    try:
        BalanceCube(demo=args.demo, self_test=args.self_test).run()
    except tk.TclError as error:
        print(f"无法创建窗口：{error}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
