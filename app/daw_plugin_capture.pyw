# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 DAW Plugin Capture contributors
"""
DAW Plugin Capture - desktop app (Tkinter).

Run with `pythonw daw_plugin_capture.pyw`; capture_core.py must be next to this file.
"""
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import font as tkfont
from tkinter import messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capture_core as core  # noqa: E402

# ---------------------------------------------------------------------------------------------------
# Theme

BG, PANEL, FG, DIM = "#0c0c0c", "#161616", "#cccccc", "#6a6a6a"
CYAN, GREEN, YELLOW, RED, MAGENTA, TRACK = "#3ad7e6", "#4ec95b", "#e5c07b", "#f05d5d", "#f92672", "#3a3a3a"
FONT = ("Consolas", 10)
FONT_B = ("Consolas", 10, "bold")

STATUS = {
    "stopped": ("○ idle", DIM),
    "connecting": ("◌ connecting…", YELLOW),
    "connected": ("● capturing", GREEN),
    "waiting": ("◌ waiting for OBS", YELLOW),
}

LEGAL_NOTICE = (f"{core.APP_NAME} {core.__version__}  ·  Copyright (C) 2026 DAW Plugin Capture contributors\n"
                "Free software under the GNU AGPL v3 or later, provided WITHOUT ANY WARRANTY."
                + (f"\nSource code: {core.PROJECT_URL}" if core.PROJECT_URL else ""))


def pick_font(root):
    global FONT, FONT_B
    available = set(tkfont.families(root))
    for family in ("Cascadia Mono", "Cascadia Code", "Consolas", "Lucida Console", "Courier New"):
        if family in available:
            FONT, FONT_B = (family, 10), (family, 10, "bold")
            return


def button(parent, text, command, color, side="left"):
    b = tk.Button(parent, text=text, command=command, fg=color, bg=PANEL, activebackground=TRACK,
                  activeforeground=color, font=FONT_B, relief="flat", bd=0, padx=12, pady=3,
                  disabledforeground=TRACK, cursor="hand2")
    b.pack(side=side, padx=(0, 6) if side == "left" else (6, 0))
    return b


def flag(parent, text, var, command=None):
    w = tk.Checkbutton(parent, text=text, variable=var, fg=DIM, bg=BG, selectcolor=PANEL, activebackground=BG,
                       activeforeground=FG, font=FONT, highlightthickness=0, bd=0, command=command)
    w.pack(side="left", padx=(0, 14))
    return w


def entry(parent, var, width=None, show=None):
    e = tk.Entry(parent, textvariable=var, bg=PANEL, fg=YELLOW, insertbackground=YELLOW, font=FONT,
                 disabledbackground=PANEL, disabledforeground=DIM, relief="flat", highlightthickness=1,
                 highlightbackground=TRACK, highlightcolor=CYAN, show=show or "")
    if width:
        e.config(width=width)
    return e


def log_box(parent):
    frame = tk.Frame(parent, bg=BG)
    text = tk.Text(frame, bg=BG, fg=FG, font=FONT, relief="flat", wrap="word", highlightthickness=0,
                   state="disabled", height=10)
    bar = ThinScrollbar(frame, command=text.yview)
    text.config(yscrollcommand=bar.set)
    bar.pack(side="right", fill="y")
    text.pack(side="left", fill="both", expand=True)
    for name, color in (("cyan", CYAN), ("green", GREEN), ("yellow", YELLOW), ("red", RED), ("dim", DIM),
                        ("magenta", MAGENTA)):
        text.tag_config(name, foreground=color)
    return frame, text


class ThinScrollbar(tk.Canvas):
    """Slim themed scrollbar (the native Windows one ignores colours). Same API as tk.Scrollbar."""

    def __init__(self, master, command, width=10):
        super().__init__(master, width=width, bg=BG, highlightthickness=0, bd=0)
        self.command = command
        self.lo, self.hi = 0.0, 1.0
        self.hover = False
        self.drag = None
        self.bind("<Configure>", lambda e: self.redraw())
        self.bind("<Enter>", lambda e: self._set_hover(True))
        self.bind("<Leave>", lambda e: self._set_hover(False))
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<B1-Motion>", self._motion)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<MouseWheel>", lambda e: self.command("scroll", -int(e.delta / 40) or (-1 if e.delta > 0 else 1),
                                                         "units"))

    def set(self, lo, hi):
        self.lo, self.hi = float(lo), float(hi)
        self.redraw()

    def _set_hover(self, on):
        self.hover = on
        self.redraw()

    def _thumb(self):
        h = max(self.winfo_height(), 1)
        y0, y1 = self.lo * h, self.hi * h
        if y1 - y0 < 24:
            mid = (y0 + y1) / 2
            y0 = max(0, min(mid - 12, h - 24))
            y1 = y0 + 24
        return y0, y1

    def redraw(self):
        self.delete("all")
        if self.hi - self.lo >= 0.999:
            return
        w = self.winfo_width()
        y0, y1 = self._thumb()
        color = CYAN if self.drag else (DIM if self.hover else TRACK)
        r = max(w - 6, 2)
        self.create_line(w / 2, y0 + r / 2 + 2, w / 2, y1 - r / 2 - 2, fill=color, width=r, capstyle="round")

    def _press(self, e):
        y0, y1 = self._thumb()
        if y0 <= e.y <= y1:
            self.drag = (e.y, self.lo)
            self.redraw()
        else:
            self.command("scroll", 1 if e.y > y1 else -1, "pages")

    def _motion(self, e):
        if self.drag:
            y, lo = self.drag
            self.command("moveto", lo + (e.y - y) / max(self.winfo_height(), 1))

    def _release(self, _e):
        self.drag = None
        self.redraw()


# ---------------------------------------------------------------------------------------------------
# Application

class App:
    def __init__(self, root, pages, status_label):
        self.root = root
        self.events = queue.Queue()      # worker threads -> UI thread
        self.engine = None
        self.worker = None
        self.status_label = status_label
        saved = core.load_settings()

        self.host = tk.StringVar(value=saved.get("host", core.DEFAULTS["host"]))
        self.port = tk.StringVar(value=str(saved.get("port", core.DEFAULTS["port"])))
        self.password = tk.StringVar(value=saved.get("password", ""))
        self.scene = tk.StringVar(value=saved.get("scene", ""))
        self.process = tk.StringVar(value=saved.get("process", core.DEFAULTS["process"]))
        self.ignore = tk.StringVar(value=saved.get("ignore", core.DEFAULTS["ignore"]))
        self.save_pw = tk.BooleanVar(value=saved.get("save_password", True))
        self.autostart = tk.BooleanVar(value=saved.get("autostart", False))
        self.show_pw = tk.BooleanVar(value=False)

        self._build_capture(pages[0])
        self._build_connection(pages[1])
        self._set_status("stopped")

        self.log(f"{core.APP_NAME} - shows your DAW's plugin windows on stream, without capturing the whole "
                 "screen.", "dim")
        if not self.scene.get():
            self.log("First run: set up the OBS connection on the CONNECTION tab (Ctrl+2), then pick a scene here "
                     "and press ▶ Start.", "yellow")
        else:
            self.log("Pick the OBS scene that holds your DAW capture and press ▶ Start. Open a plugin and it appears "
                     "in OBS; close it and it's removed.", "dim")
        self._poll()
        if self.scene.get():
            self.refresh_scenes(quiet=True)
        if self.autostart.get() and self.scene.get():
            self.root.after(400, self.start)

    # ---- tab 1: capture
    def _build_capture(self, page):
        top = tk.Frame(page, bg=BG)
        top.pack(fill="x", padx=12, pady=(12, 4))
        tk.Label(top, text="PS>", fg=GREEN, bg=BG, font=FONT_B).pack(side="left")
        tk.Label(top, text=" OBS scene:", fg=FG, bg=BG, font=FONT).pack(side="left")
        self.scene_box = ttk.Combobox(top, textvariable=self.scene, font=FONT, style="Term.TCombobox", width=32)
        self.scene_box.pack(side="left", padx=8, ipady=2)
        self.scene_box.bind("<Return>", lambda e: self.start())
        self.refresh_btn = button(top, "↻ Scenes", self.refresh_scenes, CYAN)
        self.run_btn = button(top, "▶ Start", self.start, GREEN)
        self.stop_btn = button(top, "■ Stop", self.stop, RED)
        self.stop_btn.config(state="disabled")

        options = tk.Frame(page, bg=BG)
        options.pack(fill="x", padx=12, pady=(0, 6))
        flag(options, "--start-on-launch", self.autostart, self.save)
        self.target = tk.Label(options, text="", fg=DIM, bg=BG, font=FONT, anchor="w")
        self.target.pack(side="left")
        for var in (self.host, self.port, self.process):
            var.trace_add("write", lambda *_: self._update_target())
        self._update_target()

        body = tk.Frame(page, bg=BG)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))
        side = tk.Frame(body, bg=PANEL, padx=10, pady=8, width=280)
        side.pack(side="right", fill="y", padx=(10, 0))
        side.pack_propagate(False)
        tk.Label(side, text="CAPTURED WINDOWS", fg=DIM, bg=PANEL, font=FONT_B, anchor="w").pack(fill="x", pady=(0, 6))
        self.win_list = tk.Label(side, text="·", fg=TRACK, bg=PANEL, font=FONT, anchor="nw", justify="left",
                                 wraplength=260)
        self.win_list.pack(fill="both", expand=True)
        frame, self.log_text = log_box(body)
        frame.pack(side="left", fill="both", expand=True)

    # ---- tab 2: connection
    def _build_connection(self, page):
        wrap = tk.Frame(page, bg=BG)
        wrap.pack(fill="both", expand=True, padx=12, pady=12)

        def row(label, make_widget, hint=""):
            r = tk.Frame(wrap, bg=BG)
            r.pack(fill="x", pady=3)
            tk.Label(r, text=label, fg=FG, bg=BG, font=FONT, width=14, anchor="w").pack(side="left")
            make_widget(r)
            if hint:
                tk.Label(r, text=hint, fg=DIM, bg=BG, font=FONT, anchor="w").pack(side="left", padx=10)

        def header(text, pady):
            tk.Label(wrap, text=text, fg=CYAN, bg=BG, font=FONT_B, anchor="w").pack(fill="x", pady=pady)

        def indent(r):
            tk.Label(r, text="", bg=BG, width=14).pack(side="left")

        def password_row(r):
            self.pw_entry = entry(r, self.password, 30, show="•")
            self.pw_entry.pack(side="left", ipady=3)
            tk.Checkbutton(r, text="show", variable=self.show_pw, command=self._toggle_pw, fg=DIM, bg=BG,
                           selectcolor=PANEL, activebackground=BG, activeforeground=FG, font=FONT,
                           highlightthickness=0, bd=0).pack(side="left", padx=(10, 0))

        header("OBS WEBSOCKET", (0, 6))
        row("--host", lambda r: entry(r, self.host, 22).pack(side="left", ipady=3), "localhost if OBS runs on this PC")
        row("--port", lambda r: entry(r, self.port, 8).pack(side="left", ipady=3), "OBS default: 4455")
        row("--password", password_row, "blank if authentication is off")
        r = tk.Frame(wrap, bg=BG)
        r.pack(fill="x", pady=(2, 10))
        indent(r)
        flag(r, "--save-password", self.save_pw, self.save)
        tk.Label(r, text="encrypted for your Windows account only", fg=DIM, bg=BG, font=FONT).pack(side="left")

        header("DAW", (6, 6))
        row("--process", lambda r: entry(r, self.process, 30).pack(side="left", ipady=3),
            "part of the DAW's .exe name, e.g. Ableton Live, FL64, bitwig")

        header("PRIVACY", (10, 6))
        row("--ignore", lambda r: entry(r, self.ignore, 60).pack(side="left", ipady=3),
            "windows with these title words are never shown")

        r = tk.Frame(wrap, bg=BG)
        r.pack(fill="x", pady=(14, 6))
        indent(r)
        self.test_btn = button(r, "Test connection", self.test_connection, CYAN)
        button(r, "Save", lambda: (self.save(), self._conn_msg("Saved.", GREEN)), GREEN)
        self.conn_status = tk.Label(r, text="", fg=DIM, bg=BG, font=FONT, anchor="w")
        self.conn_status.pack(side="left", padx=10)

        tk.Frame(wrap, bg=TRACK, height=1).pack(fill="x", pady=12)
        help_text = (
            "In OBS:  Tools > WebSocket Server Settings > tick 'Enable WebSocket server'.\n"
            "         'Show Connect Info' shows the port and password to copy here.\n\n"
            "Your OBS canvas is assumed to show one full monitor (e.g. a 1920x1080 canvas for a 1080p screen)\n"
            "so plugin windows land where they are on screen - put your DAW's Window Capture full-canvas.\n\n"
            "Changes apply the next time you press ▶ Start."
        )
        tk.Label(wrap, text=help_text, fg=DIM, bg=BG, font=FONT, justify="left", anchor="w").pack(fill="x")
        tk.Label(wrap, text=LEGAL_NOTICE, fg=TRACK, bg=BG, font=FONT, justify="left",
                 anchor="sw").pack(side="bottom", fill="x")

    # ---- helpers
    def config(self):
        try:
            port = int(self.port.get().strip())
        except ValueError:
            port = core.DEFAULTS["port"]
        return {"host": self.host.get().strip() or "localhost", "port": port, "password": self.password.get(),
                "scene": self.scene.get().strip(), "process": self.process.get().strip(),
                "ignore": self.ignore.get(), "interval": core.DEFAULTS["interval"]}

    def save(self):
        c = self.config()
        core.save_settings({"host": c["host"], "port": c["port"], "scene": c["scene"], "process": c["process"],
                            "ignore": c["ignore"], "password": c["password"] if self.save_pw.get() else "",
                            "save_password": self.save_pw.get(), "autostart": self.autostart.get()})

    def _update_target(self):
        self.target.config(text=f"│  {self.host.get() or 'localhost'}:{self.port.get()}  ·  "
                                f"watching '{self.process.get()}'  (change on the CONNECTION tab)")

    def _toggle_pw(self):
        self.pw_entry.config(show="" if self.show_pw.get() else "•")

    def _conn_msg(self, text, color):
        self.conn_status.config(text=text, fg=color)

    def log(self, message, tag=None):
        """Thread-safe: queued for the UI thread."""
        self.events.put(("log", message, tag))

    def _write(self, message, tag):
        self.log_text.config(state="normal")
        self.log_text.insert("end", message + "\n", tag)
        self.log_text.see("end")
        self.log_text.config(state="disabled")

    def _set_status(self, state):
        text, color = STATUS.get(state, STATUS["stopped"])
        self.status_label.config(text=text, fg=color)

    def _busy(self):
        return self.worker is not None and self.worker.is_alive()

    def _set_busy(self, on):
        self.run_btn.config(state="disabled" if on else "normal")
        self.stop_btn.config(state="normal" if on else "disabled")
        self.refresh_btn.config(state="disabled" if on else "normal")
        self.scene_box.config(state="disabled" if on else "normal")

    # ---- actions
    def refresh_scenes(self, quiet=False):
        cfg = self.config()

        def job():
            try:
                scenes, current = core.list_scenes(cfg)
                self.events.put(("scenes", scenes, current, quiet))
            except Exception as e:
                if not quiet:
                    self.log(core.friendly_error(e), "red")
        threading.Thread(target=job, daemon=True).start()

    def test_connection(self):
        cfg = self.config()
        self._conn_msg("connecting…", YELLOW)
        self.test_btn.config(state="disabled")

        def job():
            try:
                scenes, current = core.list_scenes(cfg)
                self.events.put(("test", True, f"OK - {len(scenes)} scene(s), live scene '{current}'."))
                self.events.put(("scenes", scenes, current, True))
            except Exception as e:
                self.events.put(("test", False, core.friendly_error(e)))
        threading.Thread(target=job, daemon=True).start()

    def start(self):
        if self._busy():
            return
        cfg = self.config()
        if not cfg["scene"]:
            self._write("Pick an OBS scene first (↻ Scenes loads the list from OBS).", "red")
            return
        if len(cfg["process"]) < core.MIN_PROCESS_LEN:
            self._write(f"Set the DAW process name on the CONNECTION tab first (at least {core.MIN_PROCESS_LEN} "
                        "characters, so it can't match unrelated apps).", "red")
            return
        self.save()
        self._write("", None)
        self._write(f"PS> capture --scene \"{cfg['scene']}\" --process \"{cfg['process']}\"", "green")
        self.engine = core.Engine(cfg, log=self.log, on_windows=lambda t: self.events.put(("windows", t)),
                                  on_status=lambda s: self.events.put(("status", s)))
        self.worker = threading.Thread(target=self.engine.run, daemon=True)
        self.worker.start()
        self._set_busy(True)

    def stop(self):
        if self.engine:
            self.engine.stop()
            self.stop_btn.config(state="disabled")

    def on_close(self):
        self.save()
        if self._busy():
            self.engine.stop()
            self.worker.join(5)          # let it remove its OBS sources and restore window styles
        self.root.destroy()

    def _poll(self):
        """Apply queued events from worker threads (every 100 ms)."""
        try:
            while True:
                kind, *args = self.events.get_nowait()
                if kind == "log":
                    self._write(*args)
                elif kind == "status":
                    self._set_status(args[0])
                elif kind == "windows":
                    titles = args[0]
                    self.win_list.config(text="\n".join(f"▸ {t}" for t in titles) if titles else "·",
                                         fg=CYAN if titles else TRACK)
                elif kind == "scenes":
                    scenes, current, quiet = args
                    self.scene_box["values"] = scenes
                    if not self.scene.get() or (self.scene.get() not in scenes and not self._busy()):
                        if self.scene.get() and not quiet:
                            self.log(f"Scene '{self.scene.get()}' no longer exists in OBS.", "yellow")
                        self.scene.set(current)
                    if not quiet:
                        self.log(f"{len(scenes)} scene(s) in OBS: " + ", ".join(scenes), "dim")
                elif kind == "test":
                    ok, message = args
                    self.test_btn.config(state="normal")
                    self._conn_msg(message, GREEN if ok else RED)
                    if ok:
                        self.save()
        except queue.Empty:
            pass
        if self.worker is not None and not self.worker.is_alive():
            self.worker = None
            self._set_busy(False)
        self.root.after(100, self._poll)


# ---------------------------------------------------------------------------------------------------
# Borderless window

FRAME = "#1c1c1c"
GRIP = 5
CORNER = 22
MIN_W, MIN_H = 860, 460


class Chrome:
    """Replaces the Windows title bar: the tab bar drags the window (double-click maximises), the
    edges resize it, and minimise / maximise / close buttons sit top-right. Keeps the taskbar button."""

    CURSOR = {"n": "sb_v_double_arrow", "s": "sb_v_double_arrow", "w": "sb_h_double_arrow",
              "e": "sb_h_double_arrow", "nw": "size_nw_se", "se": "size_nw_se", "ne": "size_ne_sw",
              "sw": "size_ne_sw"}
    HIT = {"w": 10, "e": 11, "n": 12, "nw": 13, "ne": 14, "s": 15, "sw": 16, "se": 17}   # WM_NCHITTEST codes

    def __init__(self, root, bar, on_close, shell):
        from ctypes import windll
        self.user32 = windll.user32
        self.root, self.on_close, self.shell = root, on_close, shell
        self.maxed, self.normal_geo, self.drag, self.hidden = False, None, None, False

        available = set(tkfont.families(root))
        icon_family = next((f for f in ("Segoe Fluent Icons", "Segoe MDL2 Assets") if f in available), None)
        self.glyph = ({"min": "", "max": "", "restore": "", "close": ""} if icon_family else
                      {"min": "—", "max": "□", "restore": "❐", "close": "✕"})
        icon_font = (icon_family, 8) if icon_family else (FONT[0], 11)

        def control(key, command, hover_bg, hover_fg):
            b = tk.Label(bar, text=self.glyph[key], font=icon_font, bg=PANEL, fg=FG, width=6, cursor="hand2")
            b.pack(side="right", fill="y")
            b.bind("<Enter>", lambda e: b.config(bg=hover_bg, fg=hover_fg))
            b.bind("<Leave>", lambda e: b.config(bg=PANEL, fg=FG))
            b.bind("<ButtonRelease-1>", lambda e: command() if b.winfo_containing(e.x_root, e.y_root) is b else None)
            return b
        control("close", on_close, RED, "#ffffff")
        self.max_btn = control("max", self.toggle_max, TRACK, CYAN)
        control("min", self.minimize, TRACK, CYAN)

        root.overrideredirect(True)
        root.configure(bg=FRAME)
        shell.pack_configure(padx=GRIP, pady=GRIP)
        for side, place in (("n", dict(x=0, y=0, relwidth=1, height=GRIP)),
                            ("s", dict(x=0, rely=1, y=-GRIP, relwidth=1, height=GRIP)),
                            ("w", dict(x=0, y=0, width=GRIP, relheight=1)),
                            ("e", dict(relx=1, x=-GRIP, y=0, width=GRIP, relheight=1))):
            edge = tk.Frame(root, bg=FRAME)
            edge.place(**place)
            edge.side = side
            edge.bind("<Motion>", lambda e, f=edge: f.config(cursor=self.CURSOR[self._edge(f, e)]))
            edge.bind("<ButtonPress-1>", lambda e, f=edge: self._resize(self._edge(f, e)))
        root.bind("<Alt-F4>", lambda e: on_close())
        root.bind("<Unmap>", self._on_unmap, add="+")
        root.bind("<Map>", self._on_map, add="+")
        root.after(20, self._to_taskbar)

    def hwnd(self):
        return self.user32.GetParent(self.root.winfo_id())

    # A borderless window is shown before Tk repaints it, so it is cloaked while minimised.
    def _cloak(self, on):
        from ctypes import byref, c_int, windll
        windll.dwmapi.DwmSetWindowAttribute(self.hwnd(), 13, byref(c_int(1 if on else 0)), 4)   # DWMWA_CLOAK

    def _on_unmap(self, e):
        if e.widget is self.root:
            self._cloak(True)
            self.hidden = True

    def _on_map(self, e):
        if e.widget is self.root and self.hidden:
            self.hidden = False
            self.user32.RedrawWindow(self.hwnd(), None, None, 0x1 | 0x4 | 0x80 | 0x100)
            self.root.update()
            self._cloak(False)

    @staticmethod
    def _edge(frame, e):
        side = frame.side
        if side in ("n", "s"):
            if e.x < CORNER:
                return side + "w"
            if e.x > frame.winfo_width() - CORNER:
                return side + "e"
        else:
            if e.y < CORNER:
                return "n" + side
            if e.y > frame.winfo_height() - CORNER:
                return "s" + side
        return side

    def grip(self, widget):
        widget.bind("<ButtonPress-1>", self._drag_start, add="+")
        widget.bind("<B1-Motion>", self._drag_move, add="+")
        widget.bind("<ButtonRelease-1>", self._drag_end, add="+")
        widget.bind("<Double-Button-1>", lambda e: self.toggle_max(), add="+")

    def lift_grips(self):
        for w in self.root.place_slaves():
            if hasattr(w, "side"):
                w.lift()

    def _to_taskbar(self):
        """overrideredirect hides the taskbar button; make the window an app window again."""
        h = self.hwnd()
        ex = self.user32.GetWindowLongW(h, -20)
        self.user32.SetWindowLongW(h, -20, (ex & ~0x80) | 0x40000)          # -TOOLWINDOW, +APPWINDOW
        self.user32.SetWindowLongW(h, -16, self.user32.GetWindowLongW(h, -16) | 0x20000)   # +MINIMIZEBOX
        self.root.withdraw()
        self.root.after(10, self.root.deiconify)
        self.root.after(30, self.root.focus_force)
        self.lift_grips()

    def work_area(self):
        mi = core.MONITORINFO()
        mi.cbSize = core.ctypes.sizeof(mi)
        monitor = core.user32.MonitorFromWindow(self.hwnd(), core.MONITOR_DEFAULTTONEAREST)
        core.user32.GetMonitorInfoW(monitor, core.ctypes.byref(mi))
        r = mi.rcWork
        return r.left, r.top, r.right - r.left, r.bottom - r.top

    def minimize(self):
        # Posted rather than called: a direct ShowWindow re-enters Tk mid-call.
        self.user32.PostMessageW(self.hwnd(), 0x0112, 0xF020, 0)            # WM_SYSCOMMAND, SC_MINIMIZE

    def _set_maxed(self, on):
        self.maxed = on
        self.max_btn.config(text=self.glyph["restore" if on else "max"])
        self.shell.pack_configure(padx=0 if on else GRIP, pady=0 if on else GRIP)
        for w in self.root.place_slaves():
            if hasattr(w, "side"):
                w.lower(self.shell) if on else w.lift()

    def toggle_max(self):
        if self.maxed:
            self._set_maxed(False)
            self.root.geometry(self.normal_geo)
        else:
            self.normal_geo = self.root.geometry()
            x, y, w, h = self.work_area()
            self._set_maxed(True)
            self.root.geometry(f"{w}x{h}+{x}+{y}")

    def _drag_start(self, e):
        self.drag = (e.x_root - self.root.winfo_x(), e.y_root - self.root.winfo_y())

    def _drag_move(self, e):
        if not self.drag:
            return
        dx, dy = self.drag
        if self.maxed:                   # dragging a maximised window restores it under the cursor
            fraction = dx / max(self.root.winfo_width(), 1)
            self.toggle_max()
            dx = int(int(self.normal_geo.split("x")[0]) * fraction)
            self.drag = (dx, dy)
        self.root.geometry(f"+{e.x_root - dx}+{e.y_root - dy}")

    def _drag_end(self, e):
        if self.drag and not self.maxed:
            _, top, _, _ = self.work_area()
            if e.y_root <= top + 2:      # dropped on the top edge: maximise
                self.root.geometry(f"+{e.x_root - self.drag[0]}+{top + 40}")
                self.toggle_max()
        self.drag = None

    def _resize(self, side):
        if not self.maxed:
            self.user32.ReleaseCapture()
            self.user32.PostMessageW(self.hwnd(), 0x00A1, self.HIT[side], 0)   # WM_NCLBUTTONDOWN: native resize


# ---------------------------------------------------------------------------------------------------
# Window

def app_icon(size=32):
    """A large window with a smaller plugin window over it. Same shapes as build_exe.py's .ico."""
    img = tk.PhotoImage(width=size, height=size)
    img.put(PANEL, to=(0, 0, size, size))
    s = size / 32
    for (x0, y0, x1, y1), color in (((3, 5, 25, 25), CYAN), ((5, 9, 23, 23), BG), ((13, 13, 29, 28), MAGENTA),
                                    ((15, 17, 27, 26), BG), ((17, 19, 25, 21), GREEN)):
        img.put(color, to=(int(x0 * s), int(y0 * s), max(int(x1 * s), int(x0 * s) + 1),
                           max(int(y1 * s), int(y0 * s) + 1)))
    return img


def build(root):
    root.title(core.APP_NAME)
    root.minsize(MIN_W, MIN_H)
    root._icons = (app_icon(32), app_icon(16))
    root.iconphoto(True, *root._icons)
    shell = tk.Frame(root, bg=BG)
    shell.pack(fill="both", expand=True, padx=1, pady=1)

    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("Term.TCombobox", fieldbackground=PANEL, background=PANEL, foreground=YELLOW,
                    arrowcolor=CYAN, bordercolor=TRACK, lightcolor=PANEL, darkcolor=PANEL,
                    selectbackground=PANEL, selectforeground=YELLOW, insertcolor=YELLOW)
    style.map("Term.TCombobox", fieldbackground=[("readonly", PANEL), ("disabled", PANEL)],
              foreground=[("readonly", YELLOW), ("disabled", DIM)])
    root.option_add("*TCombobox*Listbox.background", PANEL)
    root.option_add("*TCombobox*Listbox.foreground", CYAN)
    root.option_add("*TCombobox*Listbox.selectBackground", TRACK)
    root.option_add("*TCombobox*Listbox.font", FONT)

    bar = tk.Frame(shell, bg=PANEL)
    bar.pack(fill="x")
    pages = [tk.Frame(shell, bg=BG), tk.Frame(shell, bg=BG)]
    tabs = []
    logo = tk.Label(bar, image=root._icons[1], bg=PANEL, padx=10)
    logo.pack(side="left")

    def show(index):
        for i, (page, tab) in enumerate(zip(pages, tabs)):
            if i == index:
                page.pack(fill="both", expand=True)
                tab.config(fg=BG, bg=GREEN if i == 0 else CYAN)
            else:
                page.pack_forget()
                tab.config(fg=DIM, bg=PANEL)

    for i, text in enumerate(("  1  CAPTURE  ", "  2  CONNECTION  ")):
        tab = tk.Label(bar, text=text, font=FONT_B, bg=PANEL, fg=DIM, padx=6, pady=5, cursor="hand2")
        tab.pack(side="left")
        tab.bind("<Button-1>", lambda e, i=i: show(i))
        tabs.append(tab)
    status = tk.Label(bar, text="", font=FONT_B, bg=PANEL, fg=DIM, padx=14)
    status.pack(side="left")

    app = App(root, pages, status)
    chrome = Chrome(root, bar, app.on_close, shell)
    hint = tk.Label(bar, text="Ctrl+1 / Ctrl+2", font=FONT, bg=PANEL, fg=TRACK)
    hint.pack(side="right", padx=14)
    spacer = tk.Label(bar, text="", bg=PANEL)
    spacer.pack(side="left", fill="both", expand=True)
    for widget in (bar, logo, hint, spacer, status):
        chrome.grip(widget)
    root.bind("<Control-Key-1>", lambda e: show(0))
    root.bind("<Control-Key-2>", lambda e: show(1))
    show(0 if app.scene.get() else 1)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    chrome.lift_grips()
    root.chrome = chrome
    return app


def main():
    core.set_dpi_aware()
    # Own taskbar identity, so Windows doesn't group the window under python(w).exe and its icon.
    core.ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("DAWPluginCapture.App")
    try:
        root = tk.Tk()
        pick_font(root)
        build(root)
        x, y, w, h = root.chrome.work_area()
        width, height = min(1100, w), min(600, h)
        root.geometry(f"{width}x{height}+{x + (w - width) // 2}+{y + (h - height) // 3}")
        root.lift()
        root.attributes("-topmost", True)
        root.after(300, lambda: root.attributes("-topmost", False))
        root.focus_force()
        root.mainloop()
    except Exception:
        import traceback
        error = traceback.format_exc()
        try:
            os.makedirs(core.DATA_DIR, exist_ok=True)
            with open(os.path.join(core.DATA_DIR, "error.log"), "w", encoding="utf-8") as f:
                f.write(error)
        except OSError:
            pass
        r = tk.Tk()
        r.withdraw()
        messagebox.showerror(f"{core.APP_NAME} crashed", error[-1500:])


if __name__ == "__main__":
    main()
