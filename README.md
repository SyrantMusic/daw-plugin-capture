# DAW Plugin Capture

Captures plugin windows from DAWs (Serum, Pro-Q, Kontakt, etc) in **OBS Studio**, without the need for `Display Capture`.

OBS's `Window Capture` can capture a DAW's main window, but plugin editors/windows open as separate *tool windows* that OBS doesn't list.

**DAW Plugin Capture** can detect these windows and for each one, adds a `Window Capture` source for it directly to your OBS scene, exactly where the window is on screen. 
- The `Window Capture` source mirrors the position of the plugin window.
- If the plugin window is then closed, the `Window Capture` source will be removed. 

Only DAW windows are captured, so notifications, web browsers and other miscellaneous windows dragged over the plugins will not be visible.

**Two versions are available:**

| | |
|---|---|
| **VST3 plugin** | Add to your master track. Captures the DAW it is loaded into. Can be started with your project. |
| **Desktop app** | Run DAW Plugin Capture.exe. Works with any DAW as long as the plugin windows are owned by the main window. (e.g. Ableton Live) |

> **Note:** This project was built as a solution specifically for `Ableton`. It *should* work for any DAW whose plugin windows are owned by its main window. Currently untested.

## Requirements
- Windows 10 (version 1903 or newer) or Windows 11
- OBS Studio 28 or newer (obs-websocket is included)

## Installation

Download the latest version from Releases.

- **VST3 plugin:** Copy the `DAW Plugin Capture.vst3` folder to `C:\Program Files\Common Files\VST3` (or another location searched by your DAW). Then rescan your plugins.
- **Desktop app:** run `DAW Plugin Capture.exe`. Nothing to install. Since the binaries are not code signed, you may be asked to confirm to allow it to run for the first time.

## Setup

1. **OBS:** Go to `Tools → WebSocket Server Settings` and tick `Enable WebSocket server`. Click `Show Connect Info` to get the port and password. You may set a static secure password using any strong password generator online.
2. **OBS:** Add a `Window Capture` of your DAW to a scene. Ensure that `Capture Method` is set to `Windows 10 (1903 and up)`. 
3. Run `DAW Plugin Capture` (either the plugin or the desktop app).
4. **Connection:** Enter the:
   - `host` - localhost is fine for most use cases.
   - `port` - set to 4455 by default.
   - `password` - Pre-generated password from OBS or user-defined password.
   - `process` - name of your DAW (e.g. "Ableton Live 12 Suite"). Desktop app only, the plugin always uses the DAW it's loaded in.

   To verify your setup, click `Test connection`.
5. **Capture:** Select a scene (click `↻ Scenes` to refresh). Then click `▶ Start`.

Finally, open a plugin in your DAW. It should be visible in OBS.

When you're done, click `■ Stop`, or close the application or the plugin. All remaining sources created by `DAW Plugin Capture` will be removed.

To automatically start, check `--start-on-load` (plugin) or `--start-on-launch` (app).

> **Note:** `--start-on-load` and `--start-on-launch` will only work if you have configured your `websocket password` prior.

## How it works

When opening plugin windows in Ableton (or other similar DAW software with this behavior) DAWs create a window *owned* by the main window with the style `WS_EX_TOOLWINDOW` meaning they are not visible in OBS and window capture will not work. 

While capturing, **DAW Plugin Capture**:
1. Periodically (every 0.25 seconds) checks for visible, owned windows in the DAW.
2. Removes the `WS_EX_TOOLWINDOW` flag, allowing them to be captured by OBS. As these windows are *owned*, they will never have an entry in the taskbar or Alt-Tab, so no other changes occur.
3. Adds a `Window Capture source` (named `DAWCapture N: <title>` ) in OBS using `obs-websocket`. The position of this source will always match the window (at the speed of the OBS frame rate). Scaling is also supported.
4. If the window is closed or capturing is stopped, the source is removed, and the original `WS_EX_TOOLWINDOW` flag is restored.

## Privacy and Security

**Only windows from DAWs are captured.** 
- In the plugin, only windows from the plugin’s process are captured. 
- In the desktop app, only windows from the process specified in --process (must be at least 3 characters) are captured.

**Privacy filtering:** If any word from `--ignore` is present in the title of a window, it will not be captured. By default, licence, activation, login and settings windows are ignored. These may contain sensitive e-mail addresses and serial numbers.

**No system dialogs:** Standard system dialogs (Open, Save As, Export, messages) will never be captured. These contain file paths, which generally contain your Windows username.

**Password storage:** Settings are stored in `%APPDATA%\DAW Plugin Capture\settings.json`. The password is encrypted with Windows DPAPI, and can only be read by your Windows account on your PC. The password is never stored in DAW project files. If `--save-password` is unchecked, you will be asked to enter it every session.

**Remote OBS:** Unencrypted data is sent over the WebSocket. Connecting to OBS on a different PC is fine only on a trusted network. However, do not expose the OBS WebSocket port to the internet.

The plugin and desktop app will not run simultaneously. If both are running, the second to start will show that the other is already running.

## Limitations

- Sources will be positioned assuming that the OBS canvas covers the entire monitor that the plugin window is on (e.g. 1920x1080 canvas if on 1080p screen). If not, scaling will be applied to the width.

- **DAW Plugin Capture** is currently confirmed to remain performant and stable with up to 10 plugin windows open at once. Reliability is not guaranteed with large projects where `Auto-Open Plug-In Windows` is enabled in Ableton's settings and preferences.

- Currently only works on Windows.

## Building

### Desktop app

Ensure Python 3.9+ is installed.

```
cd app

pip install -r requirements.txt

pythonw daw_plugin_capture.pyw
```

Or double-click `Start DAW Plugin Capture.bat`.

To build into `dist/` as a single executable:
```
pip install pyinstaller
python build_exe.py
```
The engine can also be run without the GUI:

```
python capture_core.py --scene "My Scene" [--password ...] [--host localhost] [--port 4455] [--process "Ableton Live"]
```

### VST3 plugin

Visual Studio 2022 or newer must be installed with the C++ workload. CMake 3.22 or newer must be installed. JUCE will be automatically downloaded, or alternatively if JUCE is locally checked out, set `JUCE_DIR`.

```
cmake -S vst3 -B vst3/build -A x64 [-DJUCE_DIR=C:/path/to/JUCE]

cmake --build vst3/build --config Release
```

The plugin will be generated at `vst3/build/DAWPluginCapture_artefacts/Release/VST3/`. 

The C++ runtime is statically linked, so no Visual C++ redistributable is required.

## Project Structure

```
app/                            desktop app (Python)
  capture_core.py               capture engine, settings, command-line interface
  daw_plugin_capture.pyw        terminal-style GUI (Tkinter)
  build_exe.py                  PyInstaller build script
vst3/                           VST3 plugin (C++ / JUCE)
  Source/CaptureEngine.*        capture engine (port of capture_core.py)
  Source/ObsClient.*            minimal obs-websocket 5 client
  Source/PluginEditor.*         plugin UI
  Source/TerminalLookAndFeel.*  shared terminal theme
```

## Licensing

DAW Plugin Capture is free software, licensed under [GNU Affero General Public License v3.0 or
later](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for bundled component licenses. 

The VST3 plugin is built using JUCE, which is licensed under AGPLv3.

If distributing binaries, include `LICENSE` and `THIRD_PARTY_NOTICES.md`. Corresponding source code must also be made available, as required by the AGPL.

**DAW Plugin Capture is not associated with the OBS Project, Ableton or Steinberg.**