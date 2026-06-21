#!/usr/bin/env python3
# PlatformIO pre-build hook (crowpanel_s3_5hmi only): patch LovyanGFX's esp32s3
# Bus_RGB for vblank double-buffering, so hal/Display can present a fully-drawn back
# buffer and the panel swaps to it at vblank -> no tearing on the V1.2 RGB panel.
#
# LovyanGFX upstream has no double-buffer for RGB panels; this adds a tiny swap hook:
#   - Bus_RGB::setScanBuffer(fb)  -> request a scan-buffer switch (applied in the ISR)
#   - VSYNC ISR repoints the DMA descriptors to `fb` at vblank
# Idempotent + anchor-based: skips if already applied, no-ops if anchors move (pin the
# LovyanGFX version so the anchors stay put).
import os
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

base = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],   # noqa: F821
                    "LovyanGFX", "src", "lgfx", "v1", "platforms", "esp32s3")
HPP = os.path.join(base, "Bus_RGB.hpp")
CPP = os.path.join(base, "Bus_RGB.cpp")

# (marker-already-present, anchor-to-find, text-to-insert-after-anchor)
EDITS = {
    HPP: [
        ("setScanBuffer(uint8_t* fb)",
         "void readPixels(void* dst, pixelcopy_t* param, uint32_t length) override {}",
         "\n\n    // Overhead: switch the scan to `fb` at the next vblank (double-buffering)."
         "\n    void setScanBuffer(uint8_t* fb) { _pendingScanBuffer = fb; }"),
        ("volatile uint8_t* _pendingScanBuffer",
         "uint8_t *_frame_buffer = nullptr;",
         "\n    volatile uint8_t* _pendingScanBuffer = nullptr;"
         "\n    size_t _fb_desc_count = 0;"
         "\n    int    _fb_skip_bytes = 0;"),
    ],
    CPP: [
        ("me->_pendingScanBuffer)",
         "if (intr_status & LCD_LL_EVENT_VSYNC_END) {",
         "\n      if (me->_pendingScanBuffer) {              // double-buffer swap at vblank"
         "\n        uint8_t* fb = (uint8_t*)me->_pendingScanBuffer;"
         "\n        me->_pendingScanBuffer = nullptr;"
         "\n        constexpr size_t MAX_DMA_LEN = (4096 - 64);"
         "\n        for (size_t i = 0; i < me->_fb_desc_count; ++i)"
         "\n          me->_dmadesc[i].buffer = fb + i * MAX_DMA_LEN;"
         "\n        me->_dmadesc_restart.buffer = fb + me->_fb_skip_bytes;"
         "\n        me->_frame_buffer = fb;"
         "\n      }"),
        ("_fb_desc_count =",
         "_dmadesc_restart.dw0.size -= skip_bytes;",
         "\n    _fb_desc_count = dmadesc_size;   // for the VSYNC-ISR double-buffer swap"
         "\n    _fb_skip_bytes = skip_bytes;"),
    ],
}

for path, edits in EDITS.items():
    if not os.path.exists(path):
        print("[patch_lovyangfx] not found (skipping):", path)
        continue
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    changed = False
    for marker, anchor, insert in edits:
        if marker in src:
            continue
        if anchor not in src:
            print("[patch_lovyangfx] ANCHOR MISSING in %s (pin LovyanGFX version?): %r"
                  % (os.path.basename(path), anchor[:40]))
            continue
        src = src.replace(anchor, anchor + insert, 1)
        changed = True
    if changed:
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        print("[patch_lovyangfx] applied double-buffer patch ->", os.path.basename(path))
    else:
        print("[patch_lovyangfx] already patched:", os.path.basename(path))
