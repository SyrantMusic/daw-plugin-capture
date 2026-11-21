# Changelog

## Unreleased

- VST3: the plugin's own window is no longer briefly captured when it is opened while capturing.

## 1.0.0

First public release.

- Mirrors a DAW's plugin windows into OBS as Window Capture sources, placed where they are on screen
  with no Display Capture needed.
- Sources follow their windows at OBS's frame rate and are removed when the windows close.
- Comes as a desktop app (`DAW Plugin Capture.exe`) and a VST3 plugin for the master track. Both share
  the same settings.
- The OBS WebSocket password is stored encrypted with Windows DPAPI.
- A privacy filter keeps licence, activation and login windows off stream.
