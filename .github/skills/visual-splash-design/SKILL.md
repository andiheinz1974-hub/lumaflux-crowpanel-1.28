---
name: visual-splash-design
description: "Design splash screens and embedded visual assets. Use for birthday screens, festive greetings, animated titles, button/icon art, baked gradient backgrounds, color direction, and LVGL-friendly visual composition on the CrowPanel."
user-invocable: true
---

# Visual Splash Design

## When To Use
- Birthday or celebration splash screens
- Animated greeting sequences
- Main-menu or settings button/icon asset design
- Baked gradient or illustrated background design
- Requests for more polished color, depth, highlight, or visual hierarchy
- Motion-heavy intro scenes for LVGL
- Requests for better colors, shapes, and composition
- Replacing weak placeholder visuals with intentional art direction

## Principles
- Design for a 240x240 logical stage first, not the physical 480x480 scale.
- Prefer a strong focal composition over many competing elements.
- Use a small number of bold primitives: circles, rings, bars, blocks, lines, stars.
- Keep motion readable: 1-3 main animation ideas are better than many noisy ones.
- Avoid relying on unsupported glyphs or large image assets unless verified.
- For buttons and icons, prioritize silhouette clarity first, finish and decoration second.
- If LVGL runtime rendering is constrained, bake gradients, glow, bevel, and depth into the asset itself.

## Embedded LVGL Guidance
- Labels can clip when nested inside animated containers; test transform parentage carefully.
- Background panels are useful for contrast, but text should stay outside clipping containers when rotating or zooming.
- Favor rhythmic transforms, opacity fades, and staged reveals over dense particle simulation.
- For fireworks, consider stylized bursts made from lines or dots instead of many independent particles.
- When `LV_DRAW_COMPLEX` is disabled, do not depend on runtime radius, shadow, or gradient effects for the final look.
- In constrained embedded builds, prefer pre-rendered PNG/C-array assets for round buttons, domed fills, rims, and complex highlights.

## Recommended Design Workflow
1. Choose the focal element: message, symbol, or motion event.
2. Define the supporting frame: panel, ring, halo, or stage lighting.
3. Choose 2-4 colors with one accent and one contrast anchor.
4. Define animation beats:
   - Entry
   - Emphasis
   - Exit or fade
5. Remove any element that does not support the focal message.

## Buttons And Backgrounds
- Treat each button as a mini poster: clear silhouette, clear center icon, intentional depth.
- Prefer vector-like geometry with crisp edges over fuzzy realism.
- Use radial or directional baked gradients to create dome lighting, not flat fills.
- Keep icons chunky enough to read instantly on a 2.1 inch round display.
- For button families, maintain one shared lighting model across all variants so the menu feels cohesive.
- For backgrounds, use low-frequency gradients or large abstract shapes; avoid noisy texture that fights icons and labels.
- When designing selected states, brighten the fill and add a baked rim or halo rather than relying on LVGL shadows.
- Design assets so they still read well if the display is viewed slightly off-axis or photographed.

## Good Patterns
- Centered message with slow zoom, slight rotation, and hue drift
- Rotating ring behind static or gently breathing text
- Sequential fireworks bursts in the corners behind a stable focal title
- Fade-out of the whole stage near the end instead of abrupt disappearance
- Circular button assets with baked highlights, subtle rim light, and a strong central icon
- Background gradients with one dominant hue family plus one controlled accent zone

## Bad Patterns
- Many simultaneous motions with no focal hierarchy
- Text inside strongly rotating/clipping parents
- Tiny decorative elements that only add noise on a small screen
- Weak contrast between text and animated background
- Flat placeholder buttons that rely on LVGL runtime effects the target build cannot render safely
- High-detail or high-noise backgrounds that reduce legibility of the primary controls