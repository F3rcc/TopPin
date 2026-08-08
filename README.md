# TopPin

[简体中文](README.md) | [English](README_EN.md)

<p align="center">
  <img src="Assets/TopPin-icon-preview.png" width="128" alt="TopPin 图标">
</p>

<p align="center"><strong>一个只有窗口置顶功能的 50 KB Windows 小工具。</strong></p>

TopPin 使用原生 Win32 C 编写。它没有主窗口、安装程序、后台服务或联网功能：运行后进入系统托盘，按一次快捷键置顶当前窗口，再按一次取消。

## 下载与使用

系统要求：Windows 10/11 x64。

1. 从 GitHub Releases 下载 `TopPin.exe`。
2. 运行后，TopPin 会进入右下角系统托盘。
3. 点击需要置顶的窗口。
4. 按 `Ctrl + Alt + T` 置顶；再次按下即可取消。

也可以双击托盘图标切换最近使用的窗口，或者右键托盘图标管理已经置顶的窗口。

> TopPin 是本地编译的开源程序，目前没有商业代码签名证书。Windows 可能显示“未知发布者”，你可以从本仓库检查源码并自行编译。

## 功能

- 单个便携 EXE，不需要安装。
- 全局快捷键 `Ctrl + Alt + T`。
- 托盘菜单查看和取消已置顶窗口。
- 可选开机自动启动，默认关闭。
- 遇到管理员窗口时按需请求 Windows UAC 权限。
- 单实例运行；目标窗口关闭后自动清理。
- 退出 TopPin 时恢复由它置顶的窗口。
- 不读写用户文档、不联网、不收集数据。

当前 v0.1.0 x64 成品为 **51,712 字节（约 50.5 KiB）**。空闲实测约 1.54 MiB 私有内存，持续 1.2 秒采样的 CPU 时间增量为 0 ms；实际数值会随系统环境变化。

## 托盘菜单

- **切换当前窗口置顶**：与 `Ctrl + Alt + T` 相同。
- **已置顶窗口**：查看并单独取消当前置顶窗口。
- **取消全部置顶**：一次恢复所有窗口。
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

测试开始前如果发现 TopPin 正在运行，会通过正常退出消息关闭它。

## 源码结构

- `TopPin.c`：全部程序逻辑。
- `TopPin.rc`、`app.manifest`：图标、版本信息和 Windows 权限清单。
- `Assets/TopPin.svg`、`Assets/TopPin.ico`：图标源文件与多尺寸资源。
- `tools/build_icon.py`：图标生成脚本。
- `tests/TopPinSmoke.c`：原生烟雾测试。

## 许可证

[MIT License](LICENSE)
