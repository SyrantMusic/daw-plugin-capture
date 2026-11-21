# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 DAW Plugin Capture contributors
"""
Capture engine for DAW Plugin Capture.

DAWs such as Ableton Live open plugin editors as tool windows owned by the main window, and OBS's
Window Capture does not list tool windows. For every visible owned window of the DAW the engine clears
WS_EX_TOOLWINDOW (owned windows never get a taskbar button or Alt-Tab entry, so nothing visible
changes) and creates a Window Capture source for it in the chosen OBS scene, positioned where the
window is on screen. Sources follow their windows, are removed when the windows close, and the original
window styles are restored when capturing stops.

Talks to OBS through obs-websocket 5 (built into OBS 28+). Also usable from the command line:

    python capture_core.py --scene "My Scene" [--password ...]
"""
import argparse
import base64
import ctypes
import ctypes.wintypes as wt
import json
import logging
import os
import threading
import time

import obsws_python as obs

__version__ = "1.0.1"

APP_NAME = "DAW Plugin Capture"
PROJECT_URL = "https://github.com/SyrantMusic/daw-plugin-capture"   # shown in the app's legal notice
SOURCE_PREFIX = "DAWCapture"  # every OBS source the app creates starts with this
MUTEX_NAME = "Local\\DAWPluginCapture.Engine"   # held while capturing; shared with the VST3

DATA_DIR = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), APP_NAME)
SETTINGS_FILE = os.path.join(DATA_DIR, "settings.json")

MIN_PROCESS_LEN = 3              # shorter process filters would match unrelated applications

# Windows whose title contains one of these words are never captured: licence, activation, login and
# settings windows often show e-mail addresses and serial numbers.
DEFAULT_IGNORE = ("authoriz, activat, licen, login, log in, sign in, sign-in, register, serial, account, password, "
                  "settings, preferences")

# Standard Windows dialogs (file open/save, message boxes) are never captured: they show folder paths,
# which usually include the Windows user name. Plugin editors use their own window classes.
DIALOG_CLASS = "#32770"

DEFAULTS = {
    "host": "localhost",
    "port": 4455,
    "password": "",
    "scene": "",
    "process": "Ableton Live",   # substring of the DAW's executable name
    "ignore": DEFAULT_IGNORE,
    "interval": 0.25,            # seconds between scans for opened/closed windows
}

# obsws-python logs its connection parameters, including the password, at INFO level.
logging.getLogger("obsws_python").setLevel(logging.WARNING)


def is_local_host(host):
    return host.strip().lower() in ("localhost", "127.0.0.1", "::1", "[::1]")


def ignore_list(text):
    return [s.strip().lower() for s in (text or "").split(",") if s.strip()]


# ---------------------------------------------------------------------------------------------------
# Password protection (Windows DPAPI, bound to the current Windows user)

class _Blob(ctypes.Structure):
    _fields_ = [("cbData", wt.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]


CRYPTPROTECT_UI_FORBIDDEN = 0x1


def _blob(data):
    buf = ctypes.create_string_buffer(data, len(data))
    return _Blob(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))), buf


def protect(text):
    """Encrypt `text` for the current Windows user. Returns base64, or '' for empty input."""
    if not text:
        return ""
    src, _buf = _blob(text.encode("utf-8"))
    out = _Blob()
    if not ctypes.windll.crypt32.CryptProtectData(ctypes.byref(src), APP_NAME, None, None, None,
                                                  CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(out)):
        raise OSError("CryptProtectData failed")
    try:
        return base64.b64encode(ctypes.string_at(out.pbData, out.cbData)).decode("ascii")
    finally:
        ctypes.windll.kernel32.LocalFree(out.pbData)


def unprotect(b64):
    """Decrypt a value from protect(). Returns '' if it can't be decrypted (other user, other PC)."""
    if not b64:
        return ""
    try:
        src, _buf = _blob(base64.b64decode(b64))
        out = _Blob()
        if not ctypes.windll.crypt32.CryptUnprotectData(ctypes.byref(src), None, None, None, None,
                                                        CRYPTPROTECT_UI_FORBIDDEN, ctypes.byref(out)):
            return ""
        try:
            return ctypes.string_at(out.pbData, out.cbData).decode("utf-8")
        finally:
            ctypes.windll.kernel32.LocalFree(out.pbData)
    except (ValueError, OSError):
        return ""


# ---------------------------------------------------------------------------------------------------
# Settings (%APPDATA%\DAW Plugin Capture\settings.json, shared with the VST3)

def load_settings():
    """Saved settings with the password decrypted; {} if there are none."""
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        return {}
    data["password"] = unprotect(data.pop("password_dpapi", ""))
    return data


def save_settings(settings):
    """Write settings atomically. The password is stored DPAPI-encrypted, never in plain text.
    Keys this app doesn't know (written by the VST3) are preserved."""
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            data = {}
    except (OSError, ValueError):
        data = {}
    data.update({k: v for k, v in settings.items() if k != "password"})
    data.pop("password", None)
    try:
        data["password_dpapi"] = protect(settings.get("password", ""))
    except OSError:
        data["password_dpapi"] = ""
    try:
        os.makedirs(DATA_DIR, exist_ok=True)
        tmp = SETTINGS_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)
        os.replace(tmp, SETTINGS_FILE)
    except OSError:
        pass


# ---------------------------------------------------------------------------------------------------
# Win32 (ctypes only, no pywin32)

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
dwmapi = ctypes.WinDLL("dwmapi")
winmm = ctypes.WinDLL("winmm")

GWL_EXSTYLE = -20
GW_OWNER = 4
GA_ROOTOWNER = 3
WS_EX_TOOLWINDOW = 0x80
SWP_FRAME_ONLY = 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020   # NOSIZE|NOMOVE|NOZORDER|NOACTIVATE|FRAMECHANGED
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
DWMWA_EXTENDED_FRAME_BOUNDS = 9
MONITOR_DEFAULTTONEAREST = 2
ERROR_ALREADY_EXISTS = 183

if ctypes.sizeof(ctypes.c_void_p) == 8:
    _get_long, _set_long = user32.GetWindowLongPtrW, user32.SetWindowLongPtrW
    _get_long.restype = _set_long.restype = ctypes.c_ssize_t
    _set_long.argtypes = [wt.HWND, ctypes.c_int, ctypes.c_ssize_t]
else:
    _get_long, _set_long = user32.GetWindowLongW, user32.SetWindowLongW
_get_long.argtypes = [wt.HWND, ctypes.c_int]

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
user32.GetWindow.restype = wt.HWND
user32.GetWindow.argtypes = [wt.HWND, ctypes.c_uint]
user32.GetAncestor.restype = wt.HWND
user32.GetAncestor.argtypes = [wt.HWND, ctypes.c_uint]
user32.MonitorFromWindow.restype = wt.HANDLE
user32.MonitorFromWindow.argtypes = [wt.HWND, wt.DWORD]
user32.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_uint]
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.CreateMutexW.restype = wt.HANDLE
kernel32.CreateMutexW.argtypes = [ctypes.c_void_p, wt.BOOL, wt.LPCWSTR]
kernel32.CloseHandle.argtypes = [wt.HANDLE]


class MONITORINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("rcMonitor", wt.RECT), ("rcWork", wt.RECT), ("dwFlags", wt.DWORD)]


def set_dpi_aware():
    """Per-monitor DPI awareness, so window rectangles are in physical pixels."""
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except (AttributeError, OSError):
        user32.SetProcessDPIAware()


def window_text(hwnd):
    buf = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buf, 512)
    return buf.value


def class_name(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


_exe_cache = {}     # pid -> exe name; cleared every scan, since Windows reuses process ids


def exe_of(hwnd):
    """File name of the executable that owns `hwnd`."""
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value in _exe_cache:
        return _exe_cache[pid.value]
    name = ""
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
    if handle:
        buf = ctypes.create_unicode_buffer(1024)
        size = wt.DWORD(1024)
        if kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
            name = os.path.basename(buf.value)
        kernel32.CloseHandle(handle)
    _exe_cache[pid.value] = name
    return name


def frame_rect(hwnd):
    """Visible window bounds, excluding the invisible resize border and shadow."""
    r = wt.RECT()
    if dwmapi.DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, ctypes.byref(r), ctypes.sizeof(r)) != 0:
        user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def client_rect(hwnd):
    """Client area in screen coordinates."""
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    origin = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return origin.x, origin.y, origin.x + r.right, origin.y + r.bottom


def monitor_rect(hwnd):
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    user32.GetMonitorInfoW(user32.MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), ctypes.byref(mi))
    r = mi.rcMonitor
    return r.left, r.top, r.right, r.bottom


def get_exstyle(hwnd):
    return _get_long(hwnd, GWL_EXSTYLE)


def set_exstyle(hwnd, exstyle):
    _set_long(hwnd, GWL_EXSTYLE, exstyle)
    user32.SetWindowPos(hwnd, None, 0, 0, 0, 0, SWP_FRAME_ONLY)


def find_plugin_windows(process, ignore=()):
    """{hwnd: (title, class, exe)} for visible, titled windows of the DAW that are owned by another
    window, front-most first. The DAW's main window has no owner, so it is skipped; plugin editors and
    dialogs are owned. Standard Windows dialogs and titles containing any (lower-case) word in `ignore`
    are skipped."""
    found = {}
    needle = process.strip().lower()
    if len(needle) < MIN_PROCESS_LEN:
        return found
    _exe_cache.clear()

    def visit(hwnd, _):
        if not user32.IsWindowVisible(hwnd) or user32.IsIconic(hwnd) or not user32.GetWindow(hwnd, GW_OWNER):
            return True
        title = window_text(hwnd)
        if not title or any(word in title.lower() for word in ignore):
            return True
        exe = exe_of(hwnd)
        if needle not in exe.lower():
            return True
        cls = class_name(hwnd)
        if cls == DIALOG_CLASS:
            return True
        left, top, right, bottom = frame_rect(hwnd)
        if right - left >= 50 and bottom - top >= 50:
            found[hwnd] = (title, cls, exe)
        return True

    user32.EnumWindows(WNDENUMPROC(visit), 0)
    return found


def obs_window_string(title, cls, exe):
    """OBS's `window` setting: "title:class:exe" with '#' and ':' escaped."""
    def esc(s):
        return s.replace("#", "#22").replace(":", "#3A")
    return f"{esc(title)}:{esc(cls)}:{exe}"


def parse_obs_window_string(value):
    """(title, class, exe) from OBS's `window` setting, or None."""
    parts = (value or "").split(":")
    if len(parts) != 3:
        return None
    return tuple(p.replace("#3A", ":").replace("#22", "#") for p in parts)


# obs_alignment flags
ALIGN_LEFT, ALIGN_RIGHT, ALIGN_TOP, ALIGN_BOTTOM = 1, 2, 4, 8


def _aligned(space, flags, low, high):
    """How far something sits into `space` when aligned to its low edge, high edge or centre."""
    return 0.0 if flags & low else space if flags & high else space / 2


def source_layout(t):
    """Where a scene item draws its source: (x, y, scale_x, scale_y) such that source pixel (u, v) lands
    on canvas point (x + u * scale_x, y + v * scale_y). None if the item is rotated, flipped or empty."""
    crop_l, crop_r = t.get("cropLeft", 0), t.get("cropRight", 0)
    crop_t, crop_b = t.get("cropTop", 0), t.get("cropBottom", 0)
    w = t.get("sourceWidth", 0) - crop_l - crop_r
    h = t.get("sourceHeight", 0) - crop_t - crop_b
    if w <= 0 or h <= 0 or abs(t.get("rotation", 0.0)) > 0.01:
        return None

    bounds = t.get("boundsType", "OBS_BOUNDS_NONE")
    if bounds == "OBS_BOUNDS_NONE":
        sx, sy = t.get("scaleX", 1.0), t.get("scaleY", 1.0)
        box_w, box_h, off_x, off_y = w * sx, h * sy, 0.0, 0.0
    else:
        box_w, box_h = t.get("boundsWidth", 0.0), t.get("boundsHeight", 0.0)
        fit_w, fit_h = box_w / w, box_h / h
        if bounds == "OBS_BOUNDS_STRETCH":
            sx, sy = fit_w, fit_h
        else:
            s = {"OBS_BOUNDS_SCALE_OUTER": max(fit_w, fit_h),
                 "OBS_BOUNDS_SCALE_TO_WIDTH": fit_w,
                 "OBS_BOUNDS_SCALE_TO_HEIGHT": fit_h,
                 "OBS_BOUNDS_MAX_ONLY": min(1.0, fit_w, fit_h)}.get(bounds, min(fit_w, fit_h))
            sx = sy = s
        inner = t.get("boundsAlignment", 0)          # where the source sits inside its bounding box
        off_x = _aligned(box_w - w * sx, inner, ALIGN_LEFT, ALIGN_RIGHT)
        off_y = _aligned(box_h - h * sy, inner, ALIGN_TOP, ALIGN_BOTTOM)
    if sx <= 0 or sy <= 0:
        return None

    align = t.get("alignment", ALIGN_LEFT | ALIGN_TOP)    # which point of the box `position` refers to
    x = t.get("positionX", 0.0) - _aligned(box_w, align, ALIGN_LEFT, ALIGN_RIGHT) + off_x - crop_l * sx
    y = t.get("positionY", 0.0) - _aligned(box_h, align, ALIGN_TOP, ALIGN_BOTTOM) + off_y - crop_t * sy
    return x, y, sx, sy


# ---------------------------------------------------------------------------------------------------
# OBS

class Cancelled(Exception):
    """An error that retrying won't fix; the engine stops."""


def connect(cfg, timeout=3):
    return obs.ReqClient(host=cfg["host"], port=int(cfg["port"]), password=cfg["password"] or "",
                         timeout=timeout)


def describe_error(e):
    """(user-facing message, fatal) for an exception raised while talking to OBS."""
    s = str(e).lower()
    if isinstance(e, ConnectionRefusedError) or "10061" in s or "refused" in s:
        return "OBS isn't reachable - is OBS open with Tools > WebSocket Server Settings > Enable ticked?", False
    if "no password provided" in s:
        return ("OBS has authentication on - enter the WebSocket password "
                "(OBS: WebSocket Server Settings > Show Connect Info)."), True
    if "identify" in s or "auth" in s:
        return ("OBS rejected the login - check the WebSocket password "
                "(OBS: WebSocket Server Settings > Show Connect Info)."), True
    if "timed out" in s:
        return "Timed out talking to OBS - check host and port.", False
    return str(e) or type(e).__name__, False


def friendly_error(e):
    return describe_error(e)[0]


def list_scenes(cfg):
    """(scene names in OBS dock order, current program scene)."""
    cl = connect(cfg)
    try:
        response = cl.get_scene_list()
        return [s["sceneName"] for s in reversed(response.scenes)], response.current_program_scene_name
    finally:
        cl.disconnect()


class _Tracked:
    __slots__ = ("name", "item", "title", "orig_exstyle", "last")

    def __init__(self, name, item, title, orig_exstyle):
        self.name, self.item, self.title, self.orig_exstyle = name, item, title, orig_exstyle
        self.last = None


class Engine:
    """Capture loop; call run() on a worker thread and stop() from any thread.

    Callbacks: log(message, tag), on_windows(list of titles), on_status("connecting" | "connected" |
    "waiting" | "stopped")."""

    def __init__(self, cfg, log=print, on_windows=None, on_status=None):
        self.cfg = {**DEFAULTS, **cfg}
        self.log = log
        self.on_windows = on_windows or (lambda titles: None)
        self.on_status = on_status or (lambda state: None)
        self.stop_event = threading.Event()
        self.ignore = ignore_list(self.cfg["ignore"])
        self.cl = None
        self.tracked = {}
        self.counter = 0
        self.canvas_w = 1920
        self.track_dt = 1 / 60
        self.layout = None           # (DAW window, client area?, x, y, scale x, scale y) of the DAW's capture
        self.layout_note = None      # what was last logged about it
        self.input_settings = {}     # source name -> (time fetched, settings)

    def stop(self):
        self.stop_event.set()

    def _settings_of(self, name):
        """A source's settings, fetched at most every 2 s."""
        now = time.monotonic()
        cached = self.input_settings.get(name)
        if cached is None or now - cached[0] > 2.0:
            cached = (now, self.cl.get_input_settings(name).input_settings)
            self.input_settings[name] = cached
        return cached[1]

    def _update_layout(self, items):
        """Find the scene's Window Capture of the DAW, so plugin sources line up with it wherever it is
        on the canvas. Without one, the canvas is assumed to show the whole monitor."""
        if not self.tracked:
            return
        daw = user32.GetAncestor(next(iter(self.tracked)), GA_ROOTOWNER)
        exe, cls = exe_of(daw).lower(), class_name(daw)
        best = None
        for item in items:
            name = item.get("sourceName", "")
            if item.get("inputKind") != "window_capture" or name.startswith(SOURCE_PREFIX) \
                    or not item.get("sceneItemEnabled", True):
                continue
            settings = self._settings_of(name)
            target = parse_obs_window_string(settings.get("window"))
            if target and target[2].lower() == exe and (best is None or (target[1] == cls) > best[0]):
                best = (target[1] == cls, item, settings)

        self.layout = None
        if best is None:
            note = ("No Window Capture of your DAW in this scene, so plugin sources are placed as if the "
                    "canvas shows the whole monitor.", "yellow")
        else:
            _, item, settings = best
            geometry = source_layout(item.get("sceneItemTransform") or {})
            if geometry is None:
                note = (f"'{item['sourceName']}' is rotated, flipped or not showing, so plugin sources are "
                        "placed as if the canvas shows the whole monitor.", "yellow")
            else:
                self.layout = (daw, settings.get("client_area", True), *geometry)
                note = (f"Lining plugin sources up with '{item['sourceName']}'.", "dim")
        if note != self.layout_note:
            self.layout_note = note
            self.log(*note)

    def _restack(self, items, front_to_back):
        """Order the plugin sources in OBS like their windows on screen, keeping their place in the scene."""
        ours = {w.item for w in self.tracked.values()}
        index = {i["sceneItemId"]: i["sceneItemIndex"] for i in items if i.get("sceneItemId") in ours}
        wanted = [self.tracked[h].item for h in reversed(front_to_back)
                  if h in self.tracked and self.tracked[h].item in index]
        if len(wanted) < 2 or sorted(index, key=index.get) == wanted:
            return
        top = max(index.values())
        for item in wanted:             # each one moves to the top of the block, so the front window ends last
            self.cl.set_scene_item_index(self.cfg["scene"], item, top)

    def _connect(self):
        self.cl = connect(self.cfg, timeout=5)
        video = self.cl.get_video_settings()
        self.canvas_w = video.base_width
        # Follow window moves once per OBS frame: faster can't show on stream, slower shows as lag.
        fps = video.fps_numerator / video.fps_denominator if video.fps_denominator else 60.0
        self.track_dt = 1.0 / min(max(fps, 30.0), 240.0)
        self.layout, self.layout_note, self.input_settings = None, None, {}

        scenes = [s["sceneName"] for s in self.cl.get_scene_list().scenes]
        if self.cfg["scene"] not in scenes:
            raise Cancelled(f"Scene '{self.cfg['scene']}' doesn't exist in OBS. Pick one from the list.")
        leftovers = [i["inputName"] for i in self.cl.get_input_list().inputs
                     if i["inputName"].startswith(SOURCE_PREFIX)]
        for name in leftovers:
            self.cl.remove_input(name)
        if leftovers:
            self.log(f"Removed {len(leftovers)} leftover source(s) from an earlier run.", "dim")
        self.on_status("connected")
        self.log(f"Connected to OBS at {self.cfg['host']}:{self.cfg['port']}  ->  scene '{self.cfg['scene']}'"
                 f"  (canvas {self.canvas_w}px wide)", "green")

    def _transform(self, hwnd):
        left, top, _, _ = frame_rect(hwnd)
        if self.layout:
            # Same place relative to the DAW's capture as the window is relative to the DAW on screen.
            daw, client_area, x, y, sx, sy = self.layout
            daw_left, daw_top, _, _ = client_rect(daw) if client_area else frame_rect(daw)
            x, y = x + (left - daw_left) * sx, y + (top - daw_top) * sy
        else:
            mon_left, mon_top, mon_right, _ = monitor_rect(hwnd)
            sx = sy = self.canvas_w / float(mon_right - mon_left)
            x, y = (left - mon_left) * sx, (top - mon_top) * sy
        return {"positionX": x, "positionY": y, "scaleX": sx, "scaleY": sy, "rotation": 0.0, "alignment": 5,
                "boundsType": "OBS_BOUNDS_NONE"}

    def _restore_style(self, hwnd, win):
        """Put back WS_EX_TOOLWINDOW unless the window was changed since (or the handle was reused)."""
        if user32.IsWindow(hwnd) and get_exstyle(hwnd) == win.orig_exstyle & ~WS_EX_TOOLWINDOW:
            set_exstyle(hwnd, win.orig_exstyle)

    def _step(self):
        """Scan for opened, renamed and closed windows."""
        scene = self.cfg["scene"]
        current = find_plugin_windows(self.cfg["process"], self.ignore)
        changed = False

        for hwnd, (title, cls, exe) in current.items():
            win = self.tracked.get(hwnd)
            if win is None:
                orig = get_exstyle(hwnd)
                if orig & WS_EX_TOOLWINDOW:
                    set_exstyle(hwnd, orig & ~WS_EX_TOOLWINDOW)
                self.counter += 1
                name = f"{SOURCE_PREFIX} {self.counter}: {title}"[:200]
                response = self.cl.create_input(scene, name, "window_capture", {
                    "window": obs_window_string(title, cls, exe),
                    "method": 2,         # Windows 10 (1903 and up) capture
                    "priority": 1,       # match by window title
                    "cursor": True,
                }, True)
                self.tracked[hwnd] = _Tracked(name, response.scene_item_id, title, orig)
                self.log(f"+ {title}", "cyan")
                changed = True
            elif win.title != title:     # e.g. the track was renamed: retarget the capture
                self.cl.set_input_settings(win.name, {"window": obs_window_string(title, cls, exe)}, True)
                self.log(f"~ {win.title}  ->  {title}", "dim")
                win.title = title
                changed = True

        for hwnd in [h for h in self.tracked if h not in current]:
            win = self.tracked.pop(hwnd)
            try:
                self.cl.remove_input(win.name)
            except Exception:
                pass                     # already gone in OBS
            # DAWs often hide a closed plugin window rather than destroy it.
            if exe_of(hwnd):
                self._restore_style(hwnd, win)
            self.log(f"- {win.title}", "magenta")
            changed = True

        if self.tracked:
            items = self.cl.get_scene_item_list(scene).scene_items
            self._update_layout(items)
            self._restack(items, list(current))
        self._follow()

        if changed:
            self.on_windows([w.title for w in self.tracked.values()])

    def _follow(self):
        """Move each source to its window's current position (no window enumeration)."""
        for hwnd, win in self.tracked.items():
            if not user32.IsWindow(hwnd):
                continue                 # removed by the next scan
            transform = self._transform(hwnd)
            key = tuple(round(v, 1) if isinstance(v, float) else v for v in transform.values())
            if key != win.last:
                self.cl.set_scene_item_transform(self.cfg["scene"], win.item, transform)
                win.last = key

    def _cleanup(self, obs_ok=True):
        for hwnd, win in self.tracked.items():
            if obs_ok and self.cl:
                try:
                    self.cl.remove_input(win.name)
                except Exception:
                    pass
            self._restore_style(hwnd, win)
        self.tracked.clear()
        self.on_windows([])
        if self.cl:
            try:
                self.cl.disconnect()
            except Exception:
                pass
            self.cl = None

    def _capture_until_next_scan(self):
        """Between scans, follow windows at OBS's frame rate on fixed deadlines so overshoot doesn't add up."""
        now = time.monotonic()
        next_scan, tick = now + float(self.cfg["interval"]), now
        while True:
            tick += self.track_dt
            if self.stop_event.wait(max(0.0, tick - time.monotonic())) or tick >= next_scan:
                return
            self._follow()

    def run(self):
        set_dpi_aware()
        if len(self.cfg["process"].strip()) < MIN_PROCESS_LEN:
            self.log(f"The DAW process name must be at least {MIN_PROCESS_LEN} characters "
                     "(a shorter one could match unrelated apps).", "red")
            self.on_status("stopped")
            return
        mutex = kernel32.CreateMutexW(None, False, MUTEX_NAME)
        if mutex and ctypes.get_last_error() == ERROR_ALREADY_EXISTS:
            kernel32.CloseHandle(mutex)
            self.log("DAW Plugin Capture is already capturing (in your DAW's VST3 or another copy of the app). "
                     "Stop it there first.", "red")
            self.on_status("stopped")
            return
        if not is_local_host(self.cfg["host"]):
            self.log("Note: OBS WebSocket traffic is unencrypted. Only connect to another PC over a network you trust.",
                     "yellow")

        winmm.timeBeginPeriod(1)         # 1 ms timer resolution; the default rounds 16.7 ms waits up to ~31 ms
        try:
            while not self.stop_event.is_set():
                try:
                    if self.cl is None:
                        self.on_status("connecting")
                        self._connect()
                    self._step()
                    self._capture_until_next_scan()
                except Cancelled:
                    raise
                except Exception as e:
                    message, fatal = describe_error(e)
                    if fatal:
                        raise Cancelled(message) from e
                    self.log(message, "red")
                    self._cleanup(obs_ok=False)
                    self.on_status("waiting")
                    self.log("Retrying in 3 s... (press Stop to give up)", "dim")
                    if self.stop_event.wait(3):
                        break
        except Cancelled as e:
            self.log(str(e), "red")
        finally:
            winmm.timeEndPeriod(1)
            self._cleanup()
            if mutex:
                kernel32.CloseHandle(mutex)
            self.on_status("stopped")
            self.log("Stopped. Plugin sources removed and window styles restored.", "yellow")


def main():
    parser = argparse.ArgumentParser(description="Mirror DAW plugin windows into OBS as Window Captures.")
    parser.add_argument("--scene", required=True, help="OBS scene to add plugin windows to")
    parser.add_argument("--host", default=DEFAULTS["host"])
    parser.add_argument("--port", type=int, default=DEFAULTS["port"])
    parser.add_argument("--password", default=DEFAULTS["password"])
    parser.add_argument("--process", default=DEFAULTS["process"], help="substring of the DAW's .exe name")
    parser.add_argument("--ignore", default=DEFAULTS["ignore"],
                        help="comma-separated title words; matching windows are never captured")
    parser.add_argument("--interval", type=float, default=DEFAULTS["interval"],
                        help="seconds between scans for opened/closed windows")
    parser.add_argument("--version", action="version", version=f"{APP_NAME} {__version__}")
    args = parser.parse_args()

    engine = Engine(vars(args), log=lambda message, tag=None: print(message, flush=True))
    worker = threading.Thread(target=engine.run, daemon=True)
    worker.start()
    try:
        while worker.is_alive():
            worker.join(0.5)
    except KeyboardInterrupt:
        engine.stop()
        worker.join(5)


if __name__ == "__main__":
    main()
