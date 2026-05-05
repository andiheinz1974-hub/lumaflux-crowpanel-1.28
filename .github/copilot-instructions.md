# CrowPanel 1.28 Project Guidelines

## Objective
Build stable, deterministic firmware for Elecrow CrowPanel 1.28.

## Engineering Defaults
- Prefer small, reversible changes over broad rewrites.
- Preserve known-good behavior unless a change is explicitly required.
- Keep initialization order explicit and deterministic.
- Avoid hidden side effects in global constructors.
- Separate hardware bring-up from network/services startup.

## Boot and Display Reliability
- Keep the boot path staged: board init, display power, display smoke test, LVGL, then services.
- If black-screen or bootloop occurs, prioritize restoring minimal boot first.
- Treat GC9A01 color inversion (`invert_color=true`) and SPI timing as hardware-critical; avoid casual changes.
- Keep a fallback path that can run without WiFi/WLED services.

## Code Quality
- Keep functions focused and shallow.
- Add short comments only where behavior is non-obvious.
- Guard against invalid states and null pointers in hardware-facing code.
- Prefer explicit state flags over implicit coupling across modules.

## Validation
- Build and upload after meaningful firmware changes.
- Verify serial boot output and first visible frame behavior after upload.
- For display changes, verify: backlight, color test, LVGL frame, touch, encoder/button.
- If automated tests are unavailable, provide a short manual verification checklist.

## Collaboration
- For larger changes, structure work into architecture, implementation, and validation.
- Surface assumptions and risks clearly before concluding.
