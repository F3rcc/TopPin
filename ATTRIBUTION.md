# 署名与许可 / Attribution

本仓库是**二次创作（derivative work）**，不是原创项目。

This repository is a **derivative work**, not an original project.

---

## 1. 原始项目 / Upstream project

**TopPin** — <https://github.com/chiimyen/TopPin>

托盘常驻、单实例运行、全局热键、置顶/取消置顶、管理员提权、开机自启等**原有功能全部来自上游**。

```
MIT License
Copyright (c) 2026 chiimyen
```

---

## 2. 移植来源 / Ported from

**Microsoft PowerToys — AlwaysOnTop 模块**
<https://github.com/microsoft/PowerToys/tree/main/src/modules/alwaysontop/AlwaysOnTop>

本分支的**边框外观、不透明度调节、游戏模式判定、拖动跟随策略**是照着该模块的源码实现的（C 代码为本分支自行编写，未复制其 C++ 源文件；几何规则、默认参数与交互语义与之一致）。

| PowerToys 源文件 | 移植到 TopPin 的内容 |
| --- | --- |
| `WindowBorder.cpp` `GetFrameRect()` | 边框矩形 = `DWMWA_EXTENDED_FRAME_BOUNDS` 向外扩 thickness |
| `WindowBorder.cpp` `UpdateBorderPosition()` | 拖动跟随只做 `SetWindowPos`，不重绘 |
| `WindowBorder.cpp` `REFRESH_BORDER_INTERVAL` | 100 ms 刷新间隔 |
| `FrameDrawer.cpp` `ConvertRect()` / `CreateBorderGeometry()` | 描边中心线内缩 `thickness/2+1`，圆环由内外两个矩形（圆角矩形）做差 |
| `Settings.h` | `frameThickness=4`、`frameOpacity=100`、`frameAccentColor=true`、`roundCornersEnabled=true`、`blockInGameMode=true`、透明度 `20%–100%`、步长 `10%` |
| `WindowCornersUtil.cpp` | `DWMWCP_ROUND=8` / `DWMWCP_ROUNDSMALL=4` / 其它 `0` |
| `common/utils/game_mode.h` | `SHQueryUserNotificationState() == QUNS_RUNNING_D3D_FULL_SCREEN` |
| `AlwaysOnTop.cpp` `StepWindowTransparency()` / `ApplyWindowAlpha()` / `RestoreWindowAlpha()` | 不透明度步进、原始 layered 状态缓存与还原 |
| `AlwaysOnTop.cpp` `SubscribeToEvents()` | `EVENT_OBJECT_LOCATIONCHANGE` 事件驱动的位置同步 |

```
MIT License
Copyright (c) Microsoft Corporation
```

---

## 3. 免责声明 / Disclaimer

- 本仓库与 **Microsoft**、**PowerToys** 官方**没有任何关系**，不是 PowerToys 的发行渠道，也未获得其背书。
- 本仓库与上游 **chiimyen/TopPin** 的作者无隶属关系；本分支的改动与任何问题不应归因于原作者。
- 本项目不包含 PowerToys 的任何二进制文件或资源；不包含上游 TopPin 之外的其他第三方组件。

---

## 4. 完整许可文本 / Full license texts

### PowerToys (MIT)

```
MIT License

Copyright (c) Microsoft Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### TopPin (MIT)

完整文本见仓库根目录 [`LICENSE`](LICENSE)（Copyright (c) 2026 chiimyen）。
