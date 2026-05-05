---
description: "Use when editing embedded firmware, board init, display/touch/input logic, rotary/pushbutton handling, power sequencing, sleep/wake, or networking flow for Elecrow CrowPanel 2.1."
applyTo: "**/*.{ino,c,cc,cpp,cxx,h,hpp}"
---

# Firmware Instructions (Elecrow CrowPanel 2.1)

- Keep boot deterministic with staged bring-up: board I2C/power, panel power/reset, display smoke test, LVGL, then services.

- Keep touch handling robust for CST8XX: guard absent touch, clamp coordinates, and avoid assumptions about event timing.
- Keep rotary encoder and pushbutton handling edge-based and debounced to avoid false wake/input bursts.
- Isolate display/input initialization from WiFi/WLED startup so black-screen triage can run without network dependencies.
- Prefer runtime-safe fallback paths (safe/minimal mode) over permanent compile-time toggles that can hide GUI regressions.
- Avoid hidden cross-module ownership of hardware globals; prefer explicit APIs and clear module boundaries.
- Validate edge cases after edits: black-screen recovery path, wake-from-sleep via touch/rotary/button, no-WiFi boot, repeated reset behavior.
- After meaningful firmware changes, always build, upload, and verify serial boot + first visible frame on the physical CrowPanel 2.1.

## Rotary encoder decoding (VERIFIED on hardware — must not regress)

The CrowPanel 2.1 rotary encoder has been debugged and re-solved multiple
times. The rules below are the outcome of measurement on the real device
and are mandatory for any code path that produces an encoder tick. Do
not "simplify" them without re-measuring with a logic probe.

- Pins: A (CLK) = GPIO 42, B (DT) = GPIO 4, both inputs with pull-up.
  Button is routed through the PCF8574 at bit 5 (active low).
- One mechanical detent equals HALF a quadrature cycle on A (A toggles
  between high and low with every click). Therefore decode on BOTH
  rising AND falling edges of A. Decoding on only one edge makes every
  second detent do nothing.
- Sign convention: clockwise rotation must produce `diff = +1` and must
  advance the main-menu selection clockwise (Power → Sun → FX → Play →
  Settings). Counter-clockwise must produce `diff = -1`.
- Correct formula in `board_hal_read_encoder_diff()`:
    ```cpp
    if (a_now != s_encoder_a_last) {
        diff = (a_now == b_now) ? +1 : -1;  // CW = +1 on this hardware
    }
    s_encoder_a_last = a_now;
    ```
- Keep `kEncoderSignFlip` in `main/ui_wled_menu.cpp` at `+1`. Fixing
  direction belongs in the HAL, not in the UI, so every consumer
  (brightness slider, effects list, presets list, settings) stays in
  sync.
- Known wrong implementations that have regressed this project and
  must NOT be re-introduced:
  1. Rising-edge-only decoding — only every second detent registers.
  2. Decoding both edges but flipping the sign test between rising and
     falling — each detent generates +1/-1 that cancel.
  3. Flipping the sign in the UI layer instead of the HAL — desynchronizes
     the main menu from sliders and lists.
- Validation checklist after any change in this area:
  - [ ] One physical click advances the main-menu selection by exactly
        one button, in both directions.
  - [ ] CW goes Power → Sun → FX → Play → Settings → Power.
  - [ ] Brightness slider and Effects/Presets lists respond one-for-one
        to clicks and in the same direction.
