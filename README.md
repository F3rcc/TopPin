# TopPin

[简体中文](README.md) | [English](README_EN.md)

<p align="center">
  <img src="Assets/TopPin-icon-preview.png" width="128" alt="TopPin 图标">
</p>

<p align="center"><strong>一个只有窗口置顶功能的 50 KB Windows 小工具。</strong></p>

TopPin 使用原生 Win32 C 编写。它没有主窗口、安装程序、后台服务或联网功能：运行后进入系统托盘，按一次快捷键置顶当前窗口，再按一次取消。

## 本分支的定制

这是 [F3rcc/TopPin](https://github.com/F3rcc/TopPin)，在上游 `chiimyen/TopPin` 基础上加了这些功能：

- **热键可配置**：不再写死 `Ctrl+Alt+T`，改 `TopPin.ini` 即可，托盘菜单可直接打开/重载配置。
- **PowerToys 同款置顶边框**：几何、默认外观、圆角规则全部照搬 PowerToys `AlwaysOnTop` 模块（见下方“边框是怎么画的”）。
- **窗口不透明度调节**：默认 `Ctrl+Win+=` 提高、`Ctrl+Win+-` 降低，每次 10%，范围 20%–100%，只对已置顶窗口生效，取消置顶时自动还原。
- **游戏时不激活**：独占全屏（D3D fullscreen）运行中不响应置顶，默认开启。

## 下载与使用

系统要求：Windows 10/11 x64。

1. 从 [Releases](https://github.com/F3rcc/TopPin/releases) 下载 `TopPin.exe`，或从 [Actions](https://github.com/F3rcc/TopPin/actions) 取最新构建产物。
2. 运行后，TopPin 会进入右下角系统托盘。
3. 点击需要置顶的窗口。
4. 按快捷键（默认 `Ctrl + Alt + T`）置顶；再次按下即可取消。

也可以双击托盘图标切换最近使用的窗口，或者右键托盘图标管理已经置顶的窗口。

> TopPin 是本地编译的开源程序，目前没有商业代码签名证书。Windows 可能显示“未知发布者”，你可以从本仓库检查源码并自行编译。

## 功能

- 单个便携 EXE，不需要安装。
- 全局快捷键默认 `Ctrl + Alt + T`，**可在 `TopPin.ini` 里改成任意组合**。
- 置顶窗口周围显示一圈 PowerToys 风格的边框：默认跟随系统主题色、4px、随窗口圆角、DPI 自适应。
- 可调已置顶窗口的不透明度（20%–100%，步长 10%），取消置顶自动还原。
- 独占全屏游戏运行时不激活，默认开启。
- 托盘菜单查看和取消已置顶窗口。
- 可选开机自动启动，默认关闭。
- 遇到管理员窗口时按需请求 Windows UAC 权限。
- 单实例运行；目标窗口关闭后自动清理。
- 退出 TopPin 时恢复由它置顶的窗口。
- 不读写用户文档、不联网、不收集数据。

## 配置文件 `TopPin.ini`

首次运行时自动在 `TopPin.exe` 同目录生成（UTF-16 LE，带 BOM，记事本可直接编辑）：

```ini
[TopPin]
Hotkey=Ctrl+Alt+T
OpacityUpHotkey=Ctrl+Win+=
OpacityDownHotkey=Ctrl+Win+-
Border=1
BorderColor=auto
BorderOpacity=100
BorderThickness=4
BorderCorner=1
BlockInGameMode=1
```

| 键 | 说明 | PowerToys 默认 |
| --- | --- | --- |
| `Hotkey` | 置顶/取消热键。修饰键 `Ctrl`/`Alt`/`Shift`/`Win` 至少一个，加一个主键 | `Win+Ctrl+T`（本分支沿用 `Ctrl+Alt+T`） |
| `OpacityUpHotkey` | 提高不透明度 | `Ctrl+Win+=` |
| `OpacityDownHotkey` | 降低不透明度 | `Ctrl+Win+-` |
| `Border` | `1` 显示边框，`0` 关闭 | `1` |
| `BorderColor` | `auto` 跟随系统主题色；也可写 `#RRGGBB` | 主题色（拿不到时 `#00ADEF`） |
| `BorderOpacity` | 边框不透明度，1–100 | `100` |
| `BorderThickness` | 边框粗细（像素，会乘 DPI 缩放），1–32 | `4` |
| `BorderCorner` | `1` 跟随窗口圆角，`0` 直角 | `1` |
| `BlockInGameMode` | `1` 独占全屏游戏时不激活 | `1` |

热键主键支持 `A-Z`、`0-9`、`F1-F24`、`Space`、`Tab`、`Enter`、`Esc`、`Insert`、`Delete`、`Home`、`End`、`PageUp`、`PageDown`、`Up`、`Down`、`Left`、`Right`、`Num0-Num9`、`PrintScreen`、`Pause`，以及 `-` `=` `,` `.` `;` `/` `[` `\` `]` `'` 和反引号。大小写不敏感，空格会被忽略。

改完在托盘菜单点 **重新加载配置** 即可生效，不用重启；写错了会自动退回默认值并在托盘气泡里提示。

> 关掉 `Border` 后连边框定时器都不会启动，回归“零额外开销”的上游行为。

## 边框是怎么画的

全部照搬 PowerToys `src/modules/alwaysontop/AlwaysOnTop`：

1. 取 `DWMWA_EXTENDED_FRAME_BOUNDS`（窗口可见边框），向外各扩 `BorderThickness`，这就是边框窗口的矩形——所以圆环画在**窗口外面**，只压住 1px，不遮内容。
2. 描边中心线 = 客户区向内 `thickness / 2 + 1` 的圆角矩形，描边宽度 = `thickness`；圆角半径取窗口的 `DWMWA_WINDOW_CORNER_PREFERENCE`（ROUND=8 / ROUNDSMALL=4 / 其它=0）再乘 DPI 缩放。
3. 逐像素用圆角矩形有符号距离场算覆盖率做抗锯齿，写进 32 位预乘 ARGB 位图，再用 `UpdateLayeredWindow` 贴上去——和 PowerToys 用 D2D 画出来的结果一致。
4. 只遍历靠近四边的“边框带”，中间像素直接跳过，所以即使 4K 窗口渲染开销也可忽略。
5. 边框窗口是 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW`，点击完全穿透、不进任务栏和 Alt-Tab。

## 不透明度与游戏模式

- 调节只作用在**已置顶**窗口上，内部用的是 `WS_EX_LAYERED` + `SetLayeredWindowAttributes`，并会缓存窗口原本的 layered/alpha/colorkey 状态，取消置顶或退出程序时原样还原。
- 游戏判定与 PowerToys 相同：`SHQueryUserNotificationState() == QUNS_RUNNING_D3D_FULL_SCREEN`（独占全屏）。命中时置顶热键直接不响应，透明度和退出的热键不受影响。

## 托盘菜单

- **切换当前窗口置顶**：与快捷键相同，右括号里显示当前配置的组合键。
- **已置顶窗口**：查看并单独取消当前置顶窗口。
- **取消全部置顶**：一次恢复所有窗口。
- **重新加载配置**：重新读取 `TopPin.ini` 并重注册快捷键。
- **打开配置文件…**：用默认编辑器打开 `TopPin.ini`。
- **开机自动启动**：写入或删除当前用户的 Windows `Run` 启动项。
- **以管理员身份重新启动**：通过标准 UAC 流程控制管理员窗口。
- **退出 TopPin**：恢复窗口后退出。

## 管理员窗口与开机启动

TopPin 默认以普通权限运行，因此不会每次开机都弹出 UAC。目标程序以管理员身份运行时，TopPin 会询问是否继续；确认后，普通实例先恢复当前窗口状态，再由管理员实例接管并重试置顶。

“开机自动启动”默认关闭，不会创建服务或计划任务。启用后请不要随意移动 `TopPin.exe`；如果移动了文件，在新位置重新勾选一次即可更新启动路径。

## 已知限制

- 管理员提权必须经过 Windows UAC 确认，不能静默绕过。
- Windows 只保证置顶窗口位于普通窗口之上；多个置顶窗口之间没有唯一的最高层。
- 独占全屏游戏不属于普通桌面窗口，无法保证覆盖。

## 从源码编译

### 云端（推荐，什么都不用装）

推送到 `main` 或手动触发 `.github/workflows/build.yml`，GitHub Actions 会在 `windows-latest` 上用 MSYS2 UCRT64 的 MinGW-w64 编译，产物作为 `TopPin-exe` artifact 上传；打 `v*` tag 会自动发布到 Releases。

### 本地

安装 MinGW-w64 GCC，并确保 `gcc.exe` 与 `windres.exe` 位于 `PATH`，然后运行：

```bat
build.cmd
```

成品位于 `dist\TopPin.exe`。

图标的矢量母版是 `Assets/TopPin.svg`。只有重新生成 ICO 时才需要 Python 和 Pillow：

```bat
python tools\build_icon.py
```

## 测试

```bat
tests\build-smoke.cmd
build\TopPinSmoke.exe
```

烟雾测试会短暂创建一个窗口，验证：

- 单实例运行
- 开机启动开启与撤销
- 真实快捷键置顶与取消
- 退出后恢复窗口

注意：烟雾测试固定发送默认的 `Ctrl+Alt+T`，所以它要求 `TopPin.ini` 里保持默认 `Hotkey`（或者直接删掉 ini 文件）。

测试开始前如果发现 TopPin 正在运行，会通过正常退出消息关闭它。

## 源码结构

- `TopPin.c`：全部程序逻辑。
- `TopPin.rc`、`app.manifest`：图标、版本信息和 Windows 权限清单。
- `Assets/TopPin.svg`、`Assets/TopPin.ico`：图标源文件与多尺寸资源。
- `tools/build_icon.py`：图标生成脚本。
- `tests/TopPinSmoke.c`：原生烟雾测试。

## 许可证

[MIT License](LICENSE)
