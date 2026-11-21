# Third-party notices

DAW Plugin Capture is licensed under the [GNU Affero General Public License v3.0 or later](LICENSE).
The released binaries include the third-party components below. All of their licences are compatible
with distributing the combined work under the AGPLv3.

## Desktop app (`DAW Plugin Capture.exe`)

Built with [PyInstaller](https://pyinstaller.org), which bundles a Python runtime and the packages below.

| Component | Licence |
|---|---|
| [Python](https://www.python.org) runtime and standard library | [PSF License 2.0](https://docs.python.org/3/license.html) |
| [Tcl/Tk](https://www.tcl-lang.org) (via tkinter) | [Tcl/Tk License](https://www.tcl-lang.org/software/tcltk/license.html) (BSD-style) |
| [obsws-python](https://github.com/aatikturk/obsws-python) | [GPL-3.0-only](https://github.com/aatikturk/obsws-python/blob/main/LICENSE). Section 13 of the GPLv3 allows combining it with AGPLv3 code. |
| [websocket-client](https://github.com/websocket-client/websocket-client) | [Apache-2.0](https://github.com/websocket-client/websocket-client/blob/master/LICENSE) |
| [PyInstaller](https://github.com/pyinstaller/pyinstaller) bootloader | [GPL-2.0-or-later with the bootloader exception](https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt) |

## VST3 plugin (`DAW Plugin Capture.vst3`)

| Component | Licence |
|---|---|
| [JUCE](https://juce.com) 9 (`juce_core`, `juce_events`, `juce_data_structures`, `juce_graphics`, `juce_gui_basics`, `juce_gui_extra`, `juce_audio_basics`, `juce_audio_processors`, `juce_audio_processors_headless`, `juce_audio_plugin_client`, `juce_cryptography`) | [AGPL-3.0-only](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) (or the commercial JUCE licence) |
| [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (vendored in JUCE) | MIT |
| Third-party code vendored in the JUCE modules above, such as zlib (Zlib), HarfBuzz (MIT), SheenBidi (Apache-2.0), libpng (libpng-2.0), libjpeg (IJG), LunaSVG and PlutoVG (MIT) | Listed with versions in JUCE's [`JUCE.spdx.json`](https://github.com/juce-framework/JUCE/blob/master/JUCE.spdx.json) |

VST is a registered trademark of Steinberg Media Technologies GmbH.

OBS Studio is not distributed with this project. It is a separate program, and this project talks to it
through its WebSocket API. "OBS" and "OBS Studio" are trademarks of their respective owners. This project
is not affiliated with or endorsed by the OBS Project.
