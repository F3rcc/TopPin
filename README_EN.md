# TopPin

[简体中文](README.md) | [English](README_EN.md)

<p align="center">
  <img src="Assets/TopPin-icon-preview.png" width="128" alt="TopPin icon">
</p>

<p align="center"><strong>A tiny (~65 KB) Windows utility that does one thing: keep a window always on top.</strong></p>

TopPin is written in native Win32 C. It has no main window, installer, background service, or network access. Launch it into the system tray, press one shortcut to pin the active window, and press it again to unpin.

> **This is a derivative work.** It is based on the original [chiimyen/TopPin](https://github.com/chiimyen/TopPin) and ports the frame
> appearance, opacity control, game-mode check and move-tracking strategy from the
> [Microsoft PowerToys](https://github.com/microsoft/PowerToys) `AlwaysOnTop` module.
> See **[ATTRIBUTION.md](ATTRIBUTION.md)** for the file-by-file mapping and licenses.
> Not affiliated with Microsoft or the PowerToys project.

> Note: this fork is documented in Chinese — see **[README.md](README.md)** for the full feature list
> (configurable hotkeys, PowerToys-style frame, opacity hotkeys, game-mode block, notification toggle).

## Download and use

System requirements: Windows 10/11 x64.

1. Download `TopPin.exe` from GitHub Releases.
2. Run it; TopPin appears in the system tray.
3. Focus the window you want to pin.
4. Press `Ctrl + Alt + T` to pin it. Press the shortcut again to unpin it.

You can also double-click the tray icon to toggle the most recently used window, or right-click it to manage pinned windows.

> TopPin is a locally built open-source application and is not currently signed with a commercial code-signing certificate. Windows may show an “Unknown publisher” warning. You can inspect the source in this repository and build it yourself.

## Features

- One portable EXE; no installation required.
- Global `Ctrl + Alt + T` shortcut.
- Tray menu for viewing and unpinning pinned windows.
- Optional startup with Windows, disabled by default.
- On-demand Windows UAC elevation for administrator windows.
- Single-instance behavior and automatic cleanup of closed target windows.
- Restores windows pinned by TopPin when the utility exits.
- No user-document access, networking, telemetry, or data collection.

The current v0.1.0 x64 executable is **51,712 bytes (about 50.5 KiB)**. In one idle measurement it used about 1.54 MiB of private memory and accumulated 0 ms of CPU time over a 1.2-second sample. Actual values vary by system.

## Tray menu

- **Toggle current window**: same action as `Ctrl + Alt + T`.
- **Pinned windows**: view and individually unpin windows.
- **Unpin all**: restore every window pinned by TopPin.
- **Start with Windows**: add or remove the current-user Windows `Run` entry.
- **Restart as administrator**: use the standard UAC flow for elevated windows.
- **Exit TopPin**: restore pinned windows and exit.

## Administrator windows and startup

TopPin runs with normal permissions by default, so it does not show a UAC prompt on every login. If the target application is elevated, TopPin asks for confirmation. The normal instance restores its current window state, then an elevated instance takes over and retries the operation.

“Start with Windows” is disabled by default and does not create a service or scheduled task. If you enable it, keep `TopPin.exe` at a stable path. If you move the file, enable the option again from the new location to update the startup path.

## Known limitations

- Administrator elevation always requires Windows UAC confirmation; it cannot be silent.
- Windows guarantees that topmost windows stay above ordinary windows, but it does not define one absolute highest window among multiple topmost windows.
- Exclusive fullscreen games are not ordinary desktop windows and cannot be guaranteed to stay below TopPin targets.

## Build from source

Install MinGW-w64 GCC and make sure `gcc.exe` and `windres.exe` are available in `PATH`, then run:

```bat
build.cmd
```

The executable is written to `dist\TopPin.exe`.

The vector icon source is `Assets/TopPin.svg`. Python and Pillow are required only when regenerating the ICO:

```bat
python tools\build_icon.py
```

## Test

```bat
tests\build-smoke.cmd
build\TopPinSmoke.exe
```

The smoke test briefly creates a window and verifies:

- single-instance behavior
- enabling and disabling startup with Windows
- pinning and unpinning through the real global shortcut
- restoring the target window when TopPin exits

If TopPin is already running, the test asks it to exit cleanly before starting.

## Source layout

- `TopPin.c`: all application logic.
- `TopPin.rc` and `app.manifest`: icon, version metadata, and Windows privilege manifest.
- `Assets/TopPin.svg` and `Assets/TopPin.ico`: icon source and multi-size resource.
- `tools/build_icon.py`: deterministic icon generation script.
- `tests/TopPinSmoke.c`: native smoke test.

## License

[MIT License](LICENSE)
