# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 DAW Plugin Capture contributors
"""
Build "DAW Plugin Capture.exe" (single file, no console window) into <repo>/dist.

    python -m pip install pyinstaller -r requirements.txt
    python build_exe.py
"""
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIST = HERE.parent / "dist"
sys.path.insert(0, str(HERE))
from capture_core import APP_NAME, __version__  # noqa: E402

VERSION = tuple(int(p) for p in __version__.split(".")) + (0,)
COPYRIGHT = "Copyright (C) 2026 DAW Plugin Capture contributors. Licensed under the GNU AGPL v3 or later."

# Same palette and shapes as app_icon() in daw_plugin_capture.pyw (32x32 design grid).
PANEL, BG = (0x16, 0x16, 0x16), (0x0c, 0x0c, 0x0c)
CYAN, MAGENTA, GREEN = (0x3a, 0xd7, 0xe6), (0xf9, 0x26, 0x72), (0x4e, 0xc9, 0x5b)
ICON_RECTS = [((3, 5, 25, 25), CYAN), ((5, 9, 23, 23), BG), ((13, 13, 29, 28), MAGENTA),
              ((15, 17, 27, 26), BG), ((17, 19, 25, 21), GREEN)]


def icon_image(size):
    """One 32-bit BMP icon entry (BGRA rows, bottom-up, followed by an empty AND mask)."""
    s = size / 32
    pixels = [[(*PANEL, 255)] * size for _ in range(size)]
    for (x0, y0, x1, y1), color in ICON_RECTS:
        left, top = int(x0 * s), int(y0 * s)
        right, bottom = max(int(x1 * s), left + 1), max(int(y1 * s), top + 1)
        for y in range(top, min(bottom, size)):
            for x in range(left, min(right, size)):
                pixels[y][x] = (*color, 255)
    rows = b"".join(bytes(v for (r, g, b, a) in pixels[y] for v in (b, g, r, a)) for y in reversed(range(size)))
    mask = b"\0" * (((size + 31) // 32) * 4 * size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(rows) + len(mask), 0, 0, 0, 0)
    return header + rows + mask


def write_icon(path, sizes=(16, 24, 32, 48, 64, 128, 256)):
    images = [icon_image(s) for s in sizes]
    data = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for size, image in zip(sizes, images):
        data += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    Path(path).write_bytes(data + b"".join(images))


def write_version_info(path):
    numbers = ", ".join(map(str, VERSION))
    dotted = ".".join(map(str, VERSION))
    Path(path).write_text(f"""VSVersionInfo(
  ffi=FixedFileInfo(filevers=({numbers}), prodvers=({numbers}), mask=0x3f, flags=0x0, OS=0x40004,
                    fileType=0x1, subtype=0x0, date=(0, 0)),
  kids=[
    StringFileInfo([StringTable('040904B0', [
      StringStruct('CompanyName', 'DAW Plugin Capture contributors'),
      StringStruct('FileDescription', '{APP_NAME} - show DAW plugin windows in OBS'),
      StringStruct('FileVersion', '{dotted}'),
      StringStruct('InternalName', '{APP_NAME}'),
      StringStruct('LegalCopyright', '{COPYRIGHT}'),
      StringStruct('OriginalFilename', '{APP_NAME}.exe'),
      StringStruct('ProductName', '{APP_NAME}'),
      StringStruct('ProductVersion', '{dotted}')])]),
    VarFileInfo([VarStruct('Translation', [1033, 1200])])
  ]
)
""", encoding="utf-8")


def main():
    work = Path(tempfile.mkdtemp(prefix="daw_plugin_capture_build_"))
    try:
        icon, version_file = work / "daw_plugin_capture.ico", work / "version.txt"
        write_icon(icon)
        write_version_info(version_file)
        subprocess.run([
            sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--windowed",
            "--name", APP_NAME, "--icon", str(icon), "--version-file", str(version_file),
            "--distpath", str(DIST), "--workpath", str(work / "build"), "--specpath", str(work),
            "--paths", str(HERE), "--hidden-import", "capture_core",
            str(HERE / "daw_plugin_capture.pyw"),
        ], check=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"\nBuilt {DIST / (APP_NAME + '.exe')}")


if __name__ == "__main__":
    main()
