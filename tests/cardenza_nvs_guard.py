"""Compile the actual Cardenza erase wrapper and check shared-NVS safety."""
import os
import pathlib
import shutil
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
if not compiler:
    raise SystemExit("Set CXX to a host C++ compiler")
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    (work / "esp_partition.h").write_text("""
#pragma once
#include <stddef.h>
using esp_err_t = int;
constexpr int ESP_PARTITION_TYPE_DATA = 1;
constexpr int ESP_PARTITION_SUBTYPE_DATA_NVS = 2;
constexpr int ESP_ERR_NOT_SUPPORTED = 0x106;
struct esp_partition_t { int type; int subtype; size_t size; };
""")
    (work / "check.cpp").write_text("""
#include <cassert>
#include <esp_partition.h>
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
static unsigned forwarded = 0;
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t) {
    ++forwarded;
    return 42;
}
int main() {
    const esp_partition_t nvs{1, 2, 0x5000}, app{0, 0, 0x300000}, fs{1, 0x82, 0x80000};
    assert(__wrap_esp_partition_erase_range(&nvs, 0, nvs.size) == ESP_ERR_NOT_SUPPORTED);
    assert(forwarded == 0);
    assert(__wrap_esp_partition_erase_range(&nvs, 0, 0x1000) == 42);
    assert(__wrap_esp_partition_erase_range(&nvs, 0x1000, 0x1000) == 42);
    assert(__wrap_esp_partition_erase_range(&app, 0, app.size) == 42);
    assert(__wrap_esp_partition_erase_range(&fs, 0, fs.size) == 42);
    assert(__wrap_esp_partition_erase_range(nullptr, 0, 0x1000) == 42);
    assert(forwarded == 5);
}
""")
    executable = work / ("check.exe" if os.name == "nt" else "check")
    subprocess.run([compiler, "-std=c++11", "-I" + str(work),
                    str(repo / "src/cardenza/cardenza_m5_power.cpp"), str(work / "check.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Shared NVS reset denied; normal page GC, app and filesystem erases forwarded.")
