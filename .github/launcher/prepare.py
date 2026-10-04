"""Collect the app-only image and original license notices for Launcher publication."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path.cwd()
image = root / sys.argv[1]
if not image.is_file() or image.stat().st_size == 0 or image.read_bytes()[:1] != b"\xe9":
    raise SystemExit("Missing raw ESP application image")
destination = root / ".git/launcher-output"
destination.mkdir(parents=True, exist_ok=True)
shutil.copyfile(image, destination / "Cardenza.bin")
paths = {root / p for p in subprocess.check_output(["git", "ls-files", "-z"]).decode().split("\0") if p}
for directory in (".pio/libdeps/cardenza", "extra_components", "managed_components", "components", "MicroPython"):
    parent = root / directory
    if parent.is_dir():
        for current, dirs, files in os.walk(parent):
            dirs[:] = [d for d in dirs if d not in (".git", "build", "build-CARDENZA", "__pycache__")]
            paths.update(Path(current) / name for name in files)
licenses = sorted(p for p in paths if p.is_file() and
    p.suffix.lower() in ("", ".md", ".txt", ".rst") and
    any(word in p.name.casefold() for word in ("license", "licence", "copying", "notice")))
pieces = ["Cardenza application build\nSource: " + os.environ.get("GITHUB_REPOSITORY", "local checkout") +
    "\nCommit: " + subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() +
    "\n\nOriginal license texts follow. The HAL license does not relicense the upstream app.\n"]
for path in licenses:
    pieces.append("\n--- " + path.relative_to(root).as_posix() + " ---\n" + path.read_text(encoding="utf-8", errors="replace"))
framework = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio"))) / "packages/framework-arduinoespressif32"
sdk = Path(os.environ.get("IDF_PATH", "esp-idf"))
for parent in (framework, sdk):
    if parent.is_dir():
        for name in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING", "COPYING.LESSER", "NOTICE", "NOTICE.txt"):
            path = parent / name
            if path.is_file():
                pieces.append("\n--- SDK/framework " + name + " ---\n" + path.read_text(encoding="utf-8", errors="replace"))
arduino_header = framework / "cores/esp32/Arduino.h"
if arduino_header.is_file():
    pieces.append("\n--- Arduino core original licensing header ---\n" +
        "\n".join(arduino_header.read_text(encoding="utf-8", errors="replace").splitlines()[:40]))
for header in sys.argv[2:]:
    path = root / header
    pieces.append("\n--- " + header + " (original header) ---\n" +
        "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[:100]))
if not licenses:
    raise SystemExit("No original license notices found")
notice = "\n".join(pieces).encode()
if len(notice) > 1024 * 1024:
    raise SystemExit("License notices exceed publisher limit; review included dependency texts")
(destination / "NOTICES.txt").write_bytes(notice)
print("Prepared raw App BIN and", len(licenses), "license files;", len(notice), "notice bytes")
