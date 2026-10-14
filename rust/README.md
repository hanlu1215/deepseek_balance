# DeepSeek 余额小方块 — Rust / Win32 版

Windows C++ 版（仓库根目录的 `src/`）的 Rust 移植。外观、交互、命令行参数都对齐，
配置项在 `src/config.rs`，字段含义与 C++ 版 `config.h` 一一对应。

**这个目录是独立的**：自带 `Cargo.toml` 和全部源码，整份拷到别处照样能编
（图标引用上一级的 `../ks.ico`，缺失时只警告、exe 用默认图标，不影响构建）。

## 技术栈

| 关注点 | 方案 |
| --- | --- |
| 窗口 / 绘制 | `windows` crate：分层窗口 + `UpdateLayeredWindow` + GDI+（flat API），逐像素半透明 |
| 网络 | `windows` crate 的 WinHTTP（跟随系统代理） |
| JSON | `serde_json` |
| 随机 | `rand`（刷新抖动；木鱼噪声用固定种子） |
| 图标 / 清单 | 编译期 `embed-resource` 嵌入（`build.rs` 现场生成 rc 脚本） |

## 编译

```powershell
cd rust
cargo build --release
```

产物在 **`target/release/deepseek_balance_cube.exe`**（约 380 KB，无控制台窗口，
运行期不创建或修改任何文件）。

## 运行

```powershell
.\target\release\deepseek_balance_cube.exe              # 正常启动
.\target\release\deepseek_balance_cube.exe --demo       # 用假数据渲染，不联网
.\target\release\deepseek_balance_cube.exe --self-test 5  # 渲染 5 秒后自动退出
.\target\release\deepseek_balance_cube.exe --check-key  # 只查 Key 和网络，不开窗口
.\target\release\deepseek_balance_cube.exe -h           # 帮助
```

退出码语义与 C++ 版一致：`--check-key` 返回 `0` 正常、`1` 请求失败、`2` 没找到 Key。
在 cmd / PowerShell 里跑 `--check-key` 打控制台；双击运行没有控制台则弹对话框。

## 配置 API Key

改 `src/config.rs` 里的这一行，然后**重新编译**（Key 是编进 exe 的）：

```rust
pub const API_KEY_LITERAL: &str = "sk-你的key";
```

留空则回退到环境变量 `DEEPSEEK_API_KEY`（变量名可在 `API_KEY_ENV` 改）。
改外观 / 改行为同样全在 `src/config.rs`，改完重新编译。

想换成自己的点击音效：把一个 `mokugyo_click.wav` 放到 exe 同目录即可，程序只读不写；
没有这个文件就用内置合成的那记「笃」。

## 模块对应

| `rust/src/` | 作用 | 对应 C++ 版（根 `src/`） |
| --- | --- | --- |
| `config.rs` | 配置区 | `config.h` |
| `main.rs` | 入口、命令行、`--check-key` | `main.cpp` |
| `cube.rs` | 窗口、交互、状态机、动画、菜单 | `cube.{h,cpp}` |
| `render.rs` | GDI+ 渲染（分层窗口 + 逐像素 alpha） | `render.{h,cpp}` |
| `balance.rs` | WinHTTP 请求、Key 解析、错误归类 | `balance.{h,cpp}` |
| `peak.rs` | 峰 / 谷判定、法定节假日、倒计时 | `peak.{h,cpp}` |
| `pacer.rs` | 自适应刷新节奏 | `pacer.{h,cpp}` |
| `sound.rs` | 木鱼波形合成 + 播放 | `sound.{h,cpp}` |
| `util.rs` | 宽字符串、截断、错误文案等杂项 | `util.{h,cpp}` |

JSON 解析改用 `serde_json`，没有对应 C++ 版的 `json.{h,cpp}`。

## 和 C++ 版的实现差异

- GDI+ 用的是 flat API（`Gdip*` 函数族）而不是 C++ 类，每个 GDI+ 对象都有 RAII 守卫
  （`PathGuard` / `BrushGuard` / `FontGuard` …），不存在泄漏和析构顺序问题。
- 窗口过程函数里实例指针走 `GWLP_USERDATA`，与 C++ 版同一模式；取数结果用
  `Box::into_raw` 经 `WM_APP+1` 交回 UI 线程。
- 两个 GDI+ 排版坑的注释也一并保留（`render.rs` 文件头）：测量和绘制必须用同一个
  GenericTypographic StringFormat；垂直居中用 cell ascent+descent 而不是行盒。
- 法定节假日表同样在 `src/peak.rs`（2026 年），跨年记得补充。
