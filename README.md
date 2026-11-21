# DAW Plugin Capture

Show your DAW's plugin windows (Serum, Pro-Q, Kontakt, ...) on stream in **OBS Studio**, without Display Capture.

OBS's Window Capture can capture a DAW's main window, but plugin editors open as separate *tool windows*
that OBS doesn't list. DAW Plugin Capture watches for them, makes each one capturable, and adds a Window
Capture source for it to your OBS scene, exactly where the window is on screen. Move the plugin and the
source follows; close it and the source is removed. Only your DAW's windows are captured, so
notifications, browsers and anything dragged on top of a plugin stay off stream.

Available two ways, sharing the same settings:

| | |
|---|---|
| **VST3 plugin** | Put *DAW Plugin Capture* on your master track. It captures the DAW it's loaded in and can start with your project. |
| **Desktop app** | `DAW Plugin Capture.exe` runs alongside any DAW. |

Built for Ableton Live. It works with any DAW whose plugin windows are owned by its main window.

## Requirements

- Windows 10 (version 1903 or later) or Windows 11
- OBS Studio 28 or later (obs-websocket is built in)

## Installation

Download the latest release from the Releases page.

- **VST3:** copy the `DAW Plugin Capture.vst3` folder to `C:\Program Files\Common Files\VST3` (or another
  folder your DAW scans) and rescan plugins.
- **Desktop app:** run `DAW Plugin Capture.exe`. Nothing needs installing. The binaries aren't code-signed,
  so Windows SmartScreen may ask for confirmation on first launch.

## Setup

1. **OBS:** *Tools > WebSocket Server Settings*, tick **Enable WebSocket server**. *Show Connect Info* shows
   the port and password.
2. **OBS:** add a **Window Capture** of your DAW to a scene (Capture Method: *Windows 10 (1903 and up)*),
   stretched to fill the canvas.
3. Open DAW Plugin Capture, either the plugin window on your master track or the desktop app.
4. **CONNECTION tab:** enter the host, port and password, then click **Test connection**. In the desktop
   app, also set the DAW's process name (for example `Ableton Live`).
5. **CAPTURE tab:** choose your scene (**↻ Scenes** loads the list) and press **▶ Start**.

Open a plugin and it appears in OBS. Press **■ Stop**, close the app or remove the plugin, and every source
it created is removed. Tick **--start-on-load** (plugin) or **--start-on-launch** (app) to start
automatically.

## How it works

DAWs open plugin editors as windows *owned* by their main window, with the `WS_EX_TOOLWINDOW` style that
hides them from OBS's window list. While capturing, DAW Plugin Capture:

1. scans the DAW's owned, visible windows four times a second;
2. clears `WS_EX_TOOLWINDOW` on each one, which lets OBS capture it. Owned windows never get a taskbar button
   or Alt-Tab entry, so nothing else changes;
3. creates a Window Capture source (named `DAWCapture N: <title>`) through obs-websocket and keeps it
   aligned with the window at OBS's frame rate;
4. removes the source and restores the original window style when the window closes or capturing stops.

## Privacy and security

- **Only DAW windows are captured.** The plugin captures windows of its own process. The app captures only
  windows of the process named in `--process`, which must be at least three characters long.
- **Privacy filter.** Windows whose title contains any word in `--ignore` are never captured. The defaults
  cover licence, activation, login and settings windows, which often show e-mail addresses and serial numbers.
- **No system dialogs.** Standard Windows dialogs (Open, Save As, Export, message boxes) are never captured,
  because they show folder paths that usually include your Windows user name.
- **Password storage.** Settings are kept in `%APPDATA%\DAW Plugin Capture\settings.json`. The password is
  encrypted with Windows DPAPI, so only your Windows account on that PC can read it, and it is never stored
  in DAW project files. Untick `--save-password` to enter it each session.
- **Remote OBS.** WebSocket traffic is unencrypted. Connecting to OBS on another PC is fine on a network you
  trust, but don't expose the OBS WebSocket port to the internet.
- The plugin and the app never capture at the same time. Whichever starts second reports that the other is
  already capturing.

## Limitations

- Sources are positioned assuming the OBS canvas shows the whole monitor the plugin window is on (for example
  a 1920×1080 canvas for a 1080p screen). Other canvas sizes are scaled by width.
- Windows only.

## Building from source

### Desktop app

Requires Python 3.9 or later.

```
cd app
python -m pip install -r requirements.txt
pythonw daw_plugin_capture.pyw          # or double-click "Start DAW Plugin Capture.bat"
```

To build the single-file executable into `dist/`:

```
python -m pip install pyinstaller
python build_exe.py
```

The engine also runs without the GUI:

```
python capture_core.py --scene "My Scene" [--password ...] [--host localhost] [--port 4455] [--process "Ableton Live"]
```

### VST3 plugin

Requires Visual Studio 2022 or later with the C++ workload, and CMake 3.22 or later. JUCE is downloaded
automatically, or you can point `JUCE_DIR` at a local checkout.

```
cmake -S vst3 -B vst3/build -A x64 [-DJUCE_DIR=C:/path/to/JUCE]
cmake --build vst3/build --config Release
```

The plugin is written to `vst3/build/DAWPluginCapture_artefacts/Release/VST3/`. It links the C++ runtime
statically, so users don't need the Visual C++ redistributable.

## Project layout

```
app/                          desktop app (Python)
  capture_core.py               capture engine, settings, command-line interface
  daw_plugin_capture.pyw        terminal-style GUI (Tkinter)
  build_exe.py                  PyInstaller build script
vst3/                         VST3 plugin (C++ / JUCE)
  Source/CaptureEngine.*        capture engine (port of capture_core.py)
  Source/ObsClient.*            minimal obs-websocket 5 client
  Source/PluginEditor.*         plugin UI
  Source/TerminalLookAndFeel.*  shared terminal theme
```

## License

DAW Plugin Capture is free software, released under the [GNU Affero General Public License v3.0 or
later](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the licences of bundled
components. The VST3 plugin is built with JUCE under the AGPLv3.

If you distribute binaries, include `LICENSE` and `THIRD_PARTY_NOTICES.md` and make the corresponding
source code available, as the AGPL requires.

DAW Plugin Capture is not affiliated with the OBS Project, Ableton or Steinberg.
