# Changelog

## 1.0.1

- Plugin sources now line up with your DAW's Window Capture in the scene, wherever it is placed, scaled or
  cropped on the canvas. Without one, the canvas is still assumed to show the whole monitor.
- Plugin sources are stacked in OBS in the same order as their windows, so the plugin you click to the front
  is also in front on stream.
- The mouse cursor is now captured in plugin sources.
- VST3: the plugin's own window is no longer briefly captured when it is opened while capturing.
- Releases are now built by GitHub Actions, with SHA-256 checksums.

## 1.0.0

First public release.

- Mirrors a DAW's plugin windows into OBS as Window Capture sources, placed where they are on screen
  with no Display Capture needed.
- Sources follow their windows at OBS's frame rate and are removed when the windows close.
- Comes as a desktop app (`DAW Plugin Capture.exe`) and a VST3 plugin for the master track. Both share
  the same settings.
- The OBS WebSocket password is stored encrypted with Windows DPAPI.
- A privacy filter keeps licence, activation and login windows off stream.
