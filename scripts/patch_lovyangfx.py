#!/usr/bin/env python3
# PlatformIO pre-build hook (crowpanel_s3_5hmi only): neuter LovyanGFX's esp32s3
# Bus_RGB scan. On this board esp_lcd owns the RGB scan-out (HW double-buffer +
# bounce buffer — see hal/Display.cpp), and two drivers can't share LCD_CAM. So
# Bus_RGB::init() here only allocates the framebuffer Panel_RGB draws into and skips
# the i80/GDMA/LCD_CAM setup entirely. Idempotent + anchor-based (LovyanGFX pinned).
import os
Import("env")  # noqa: F821

base = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],   # noqa: F821
                    "LovyanGFX", "src", "lgfx", "v1", "platforms", "esp32s3")
CPP = os.path.join(base, "Bus_RGB.cpp")

MARKER = "OVERHEAD: esp_lcd owns the scan"
ANCHOR = "// ここでは ESP-IDF"   # first comment line inside Bus_RGB::init()
INSERT = ("    // OVERHEAD: esp_lcd owns the scan-out (hal/Display). The app draws into this\n"
          "    // separate work framebuffer; flushFramebuffer() copies only the CHANGED rows to\n"
          "    // esp_lcd's scanned FB (like LVGL's dirty rects). The LCD_CAM setup below is skipped.\n"
          "    _frame_buffer = (uint8_t*)heap_alloc_psram((size_t)_cfg.panel->width() * _cfg.panel->height() * 2);\n"
          "    return _frame_buffer != nullptr;\n")

if not os.path.exists(CPP):
    print("[patch_lovyangfx] not found (skipping):", CPP)
else:
    with open(CPP, "r", encoding="utf-8") as f:
        src = f.read()
    if MARKER in src:
        print("[patch_lovyangfx] already patched: Bus_RGB.cpp")
    elif ANCHOR not in src:
        print("[patch_lovyangfx] ANCHOR MISSING (pin LovyanGFX version?) in Bus_RGB.cpp")
    else:
        src = src.replace(ANCHOR, INSERT + ANCHOR, 1)
        with open(CPP, "w", encoding="utf-8") as f:
            f.write(src)
        print("[patch_lovyangfx] neutered Bus_RGB scan -> Bus_RGB.cpp")
