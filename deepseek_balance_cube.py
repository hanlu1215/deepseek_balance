#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DeepSeek 余额小方块 —— 极简悬浮窗。

显示内容只有两样：
    1. 余额数字（大字号，尽量占满卡片）
    2. 峰 / 谷 徽标 + 该时段剩余时长（时:分:秒）

外观：
    · 大圆角卡片，四角圆润
    · 卡片里文字占比大，几乎没有多余留白

交互：
    左键点击  -> Q 弹一下并刷新余额
    拖动      -> 移动窗口
    右键菜单  -> 刷新 / 置顶开关 / 退出
    Esc       -> 退出

配置：
    顶部「配置区」里填 API_KEY；余额自动刷新间隔 BALANCE_REFRESH_SECONDS
    就定义在它旁边，默认 10 秒刷新一次。

依赖：只用 Python 标准库（tkinter）。没有 Pillow 也能跑；
      检测到 Pillow 会自动升级成抗锯齿 + 渐变版本。
"""

from __future__ import annotations

import json
import math
import os
import queue
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
import urllib.error
import urllib.request
from datetime import datetime, timedelta
from typing import Any

try:  # 可选增强
    from PIL import Image, ImageDraw, ImageTk

    HAS_PIL = True
except Exception:  # noqa: BLE001 - 没装 Pillow 就走纯 Canvas 路径
    HAS_PIL = False

# ============================================================================
#  ★ 配置区：把 Key 写在这里，非空则优先使用；留空则回退到环境变量
# ============================================================================
API_KEY = ""
API_KEY_ENV = "DEEPSEEK_API_KEY"

# 余额自动刷新间隔（秒）：紧挨着 Key 定义，默认 10 秒刷新一次；0 = 不自动刷新
BALANCE_REFRESH_SECONDS = 10

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

# ---- 配色 ----
COLOR_TOP = (44, 47, 56)       # 卡片渐变起始
COLOR_BOTTOM = (26, 28, 33)    # 卡片渐变结束
COLOR_BORDER = (78, 84, 96)
COLOR_AMOUNT = "#ffd88a"       # 余额数字
COLOR_AMOUNT_DIM = "#7d7360"   # 刷新中
COLOR_ERROR = "#ff7070"
COLOR_MUTED = "#8b93a1"
COLOR_PEAK = "#ff8f5e"         # 峰：橙
COLOR_PEAK_BG = "#3a2318"
COLOR_OFF = "#4ede9a"          # 谷：绿
COLOR_OFF_BG = "#16301f"

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
def resolve_api_key() -> str:
    """脚本里的 API_KEY 优先，其次环境变量。"""
    if API_KEY.strip():
        return API_KEY.strip()
    return (os.environ.get(API_KEY_ENV) or "").strip()


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


# ---------------------------------------------------------------- 绘制
def _rounded_rect(draw: ImageDraw.ImageDraw, box, radius: int, **kwargs) -> None:
    draw.rounded_rectangle(box, radius=radius, **kwargs)


def build_card_pil(width: int, height: int, radius: int) -> Image.Image:
    """抗锯齿版本：垂直渐变 + 描边的圆角卡片。"""
    scale = 3
    big_w, big_h = width * scale, height * scale

    body = Image.new("RGBA", (big_w, big_h), (0, 0, 0, 0))
    card_box = (0, 0, big_w - 1, big_h - 1)
    mask = Image.new("L", (big_w, big_h), 0)
    _rounded_rect(ImageDraw.Draw(mask), card_box, radius * scale, fill=255)

    gradient = Image.new("RGB", (big_w, big_h), COLOR_BOTTOM)
    painter = ImageDraw.Draw(gradient)
    for y in range(big_h):
        t = y / max(1, big_h - 1)
        painter.line(
            [(0, y), (big_w, y)],
            fill=tuple(int(COLOR_TOP[i] + (COLOR_BOTTOM[i] - COLOR_TOP[i]) * t) for i in range(3)),
        )
    body.paste(gradient, (0, 0), mask)
    _rounded_rect(
        ImageDraw.Draw(body), card_box, radius * scale,
        outline=COLOR_BORDER + (255,), width=scale,
    )
    return body.resize((width, height), Image.LANCZOS)


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

        transparent = "#010203"
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

        self._card_image = None          # PIL 底图（没有 Pillow 时为 None）
        self._build_card_sprite()

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
        self._press: tuple[int, int, int, int] | None = None   # x_root,y_root,win_x,win_y
        self._dragged = False

        self._bind()
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

    # -- 卡片底图
    def _build_card_sprite(self) -> None:
        if HAS_PIL:
            image = build_card_pil(self.px_w, self.px_h, self.px_r)
            self._card_image = ImageTk.PhotoImage(image)

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
        """重画卡片本体与内容。"""
        self.canvas.delete("card")
        x0 = self.card_left()
        y0 = self.card_top()
        x1 = x0 + self.card_width()
        y1 = y0 + self.card_height()
        radius = max(4.0, self.px_r * self._scale)

        if self._card_image is not None and abs(self._scale - 1.0) < 1e-6:
            self.canvas.create_image(0, 0, image=self._card_image, anchor="nw", tags=("card",))
        else:
            self._draw_card_body(x0, y0, x1, y1, radius)
        self._draw_content(x0, y0, x1, y1)

    def _draw_card_body(self, x0: float, y0: float, x1: float, y1: float, radius: float) -> None:
        """纯 Canvas 卡片：切片式渐变填充 + 描边（无需 Pillow）。

        注意不能用"等高色带"堆叠：每带只有几像素高却套用几十像素的圆角，
        会退化成胶囊形状，堆起来就是锯齿边。改成从当前行填到底的切片，
        每片的左右内缩量由圆形方程算出，拼出真正的圆角。
        """
        height = y1 - y0
        radius = max(0.0, min(radius, (x1 - x0) / 2, height / 2))
        steps = max(24, int(height / 3))
        for index in range(steps):
            top = y0 + height * index / steps
            bottom = y0 + height * (index + 1) / steps
            inset = max(
                self._corner_inset(top - y0, radius),
                self._corner_inset(y1 - bottom, radius),
            )
            self.canvas.create_polygon(
                x0 + inset, top, x1 - inset, top,
                x1 - inset, bottom, x0 + inset, bottom,
                fill=hex_color(self._gradient_color((index + 0.5) / steps)),
                outline="", tags=("card",),
            )
        self.canvas.create_polygon(
            round_rect_points(x0, y0, x1, y1, radius),
            fill="", outline=hex_color(COLOR_BORDER), width=1, tags=("card",),
        )

    @staticmethod
    def _corner_inset(distance_from_edge: float, radius: float) -> float:
        """距上/下边缘 distance_from_edge 处，圆角造成的水平内缩量。"""
        if radius <= 0 or distance_from_edge >= radius:
            return 0.0
        return radius - math.sqrt(max(0.0, radius * radius - (radius - distance_from_edge) ** 2))

    @staticmethod
    def _gradient_color(t: float) -> tuple[int, int, int]:
        return tuple(
            int(COLOR_TOP[i] + (COLOR_BOTTOM[i] - COLOR_TOP[i]) * t) for i in range(3)
        )  # type: ignore[return-value]

    @staticmethod
    def _blend(base: str, other: str, alpha: float) -> str:
        def parts(color: str) -> tuple[int, int, int]:
            color = color.lstrip("#")
            return tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]

        if not base.startswith("#") or len(base) != 7:
            base = "#101216"
        b, o = parts(base), parts(other)
        return "#%02x%02x%02x" % tuple(int(b[i] + (o[i] - b[i]) * alpha) for i in range(3))

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

    def _draw_content(self, x0: float, y0: float, x1: float, y1: float) -> None:
        width = x1 - x0
        height = y1 - y0
        cx = x0 + width / 2
        zoom = self._scale * self.ui_scale        # 弹性动画缩放 + 高 DPI
        max_text_w = width * TEXT_WIDTH_RATIO

        # ---- 余额数字：字号先顶满卡片，再按文本长度收一点 ----
        amount = first_balance(self._state["payload"])
        color = COLOR_AMOUNT
        if self._state["error"]:
            color = COLOR_ERROR
        elif self._state["busy"]:
            color = COLOR_AMOUNT_DIM

        amount_font = self._fit_font(amount, AMOUNT_FONT_PX * zoom, max_text_w, "bold")
        self.canvas.create_text(
            cx, y0 + height * AMOUNT_CENTER_Y, text=amount, fill=color,
            font=amount_font, anchor="center",
        )

        # ---- 峰 / 谷 徽标 + 时:分:秒 倒计时 ----
        now = datetime.now()
        peak = is_peak(now)
        label = "峰" if peak else "谷"
        clock = format_remaining(next_transition(now) - now)
        fg = COLOR_PEAK if peak else COLOR_OFF
        bg = COLOR_PEAK_BG if peak else COLOR_OFF_BG

        pill_px = PILL_FONT_PX * zoom
        pad = PILL_PAD_X * zoom
        gap = PILL_GAP * zoom
        font = self._font(pill_px, "bold")
        label_w = font.measure(label)
        clock_w = font.measure(clock)
        pill_w = label_w + gap + clock_w + pad * 2

        # 长假时倒计时会出现三位数小时，整块徽标等比缩小也要塞进卡片
        avail = width - 2 * PILL_SIDE_MARGIN * zoom
        if pill_w > avail:
            shrink = avail / pill_w
            font = self._font(pill_px * shrink, "bold")
            pad *= shrink
            gap *= shrink
            label_w = font.measure(label)
            clock_w = font.measure(clock)
            pill_w = label_w + gap + clock_w + pad * 2

        pill_h = font.metrics("linespace") + 2 * PILL_PAD_Y * zoom
        px0 = cx - pill_w / 2
        py0 = y0 + height * PILL_CENTER_Y - pill_h / 2
        self.canvas.create_polygon(
            round_rect_points(px0, py0, px0 + pill_w, py0 + pill_h, pill_h / 2),
            fill=bg, outline="", smooth=True,
        )
        self.canvas.create_text(
            px0 + pad + label_w / 2, py0 + pill_h / 2,
            text=label, fill=fg, font=font,
        )
        self.canvas.create_text(
            px0 + pill_w - pad - clock_w / 2, py0 + pill_h / 2,
            text=clock, fill=fg, font=font,
        )

    # -- 弹性动画
    def _bounce(self) -> None:
        self._anim_start = time.monotonic()
        if self._anim_job is not None:
            self.root.after_cancel(self._anim_job)
        self._animate()

    def _animate(self) -> None:
        elapsed = time.monotonic() - self._anim_start
        duration = 0.62
        if elapsed >= duration:
            self._scale = 1.0
            self._redraw()
            self._anim_job = None
            return
        progress = elapsed / duration
        # 欠阻尼弹簧：0.78 -> 1.0，回弹两下
        self._scale = 1.0 - 0.22 * math_exp_decay(progress)
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
        self.menu.add_command(label="刷新", command=self.refresh)
        self.menu.add_command(label="置顶", command=self._toggle_top)
        self.menu.add_separator()
        self.menu.add_command(label="退出", command=self._quit)

    def _toggle_top(self) -> None:
        self.topmost = not getattr(self, "topmost", True)
        self.root.attributes("-topmost", self.topmost)

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
        else:
            self._state["error"] = error
        self._redraw()
        if BALANCE_REFRESH_SECONDS > 0 and self._refresh_job is None:
            self._refresh_job = self.root.after(BALANCE_REFRESH_SECONDS * 1000, self.refresh)

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


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="DeepSeek 余额小方块")
    parser.add_argument("--demo", action="store_true", help="用假数据渲染，不发起网络请求")
    parser.add_argument("--self-test", type=float, default=0.0, metavar="秒", help="渲染指定秒数后退出")
    args = parser.parse_args()
    try:
        BalanceCube(demo=args.demo, self_test=args.self_test).run()
    except tk.TclError as error:
        print(f"无法创建窗口：{error}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
