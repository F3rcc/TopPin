# TopPin

[简体中文](README.md) | [English](README_EN.md)

<p align="center">
  <img src="Assets/TopPin-icon-preview.png" width="128" alt="TopPin 图标">
</p>

<p align="center"><strong>一个只有窗口置顶功能的 50 KB Windows 小工具。</strong></p>

TopPin 使用原生 Win32 C 编写。它没有主窗口、安装程序、后台服务或联网功能：运行后进入系统托盘，按一次快捷键置顶当前窗口，再按一次取消。

## 本分支的定制

这是 [F3rcc/TopPin](https://github.com/F3rcc/TopPin)，在上游 `chiimyen/TopPin` 基础上加了两个功能：

- **热键可配置**：不再写死 `Ctrl+Alt+T`，改 `TopPin.ini` 即可，托盘菜单可直接打开/重载配置。
- **置顶高亮边框**：被钉住的窗口四周会有一圈可配颜色/宽度的边框，关掉后不会多出任何定时器。

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
- 置顶窗口周围显示可配置的高亮边框（颜色、宽度、开关）。
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
Border=1
BorderColor=#FFB020
BorderWidth=3
```

| 键 | 说明 |
| --- | --- |
| `Hotkey` | 修饰键 `Ctrl`/`Alt`/`Shift`/`Win` 至少一个，加一个主键。主键支持 `A-Z`、`0-9`、`F1-F24`、`Space`、`Tab`、`Enter`、`Esc`、`Insert`、`Delete`、`Home`、`End`、`PageUp`、`PageDown`、`Up`、`Down`、`Left`、`Right`、`Num0-Num9`、`PrintScreen`、`Pause`。大小写不敏感，空格会被忽略。 |
| `Border` | `1` 显示置顶高亮边框，`0` 关闭。 |
| `BorderColor` | `#RRGGBB` 十六进制颜色，默认 `#FFB020`。 |
| `BorderWidth` | 边框宽度，1-16 像素。 |

改完在托盘菜单点 **重新加载配置** 即可生效，不用重启；写错了会自动退回默认值并在托盘气泡里提示。

> 关掉 `Border` 后连边框定时器都不会启动，回归“零额外开销”的上游行为。

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
