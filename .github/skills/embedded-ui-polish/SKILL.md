---
name: embedded-ui-polish
description: "Refine embedded LVGL UI for touch ergonomics, navigation clarity, hidden/admin flows, settings structure, and round-display layout. Use for improving CrowPanel interaction design and UI reliability."
user-invocable: true
---

# Embedded UI Polish

## When To Use
- Settings and info screens need cleanup
- Hidden menus or service flows need more reliable activation
- Touch targets feel fragile or crowded
- A screen needs clearer hierarchy on the round 240x240 UI
- Navigation and back behavior need simplification

## Principles
- Small-screen reliability beats novelty.
- Hidden actions should use explicit, repeatable triggers.
- Important actions should have large touch targets and visible feedback.
- Keep the user oriented: screen title, primary action, safe exit.

## Recommended Hidden/Admin Patterns
- Multi-tap on build number or version label
- Long-press on a specific stable label
- Button + encoder sequences for hardware-assisted access
- Avoid freehand gesture recognition unless absolutely necessary

## Layout Rules
- Keep primary actions in the lower half where thumbs naturally land.
- Reserve top-left for Back when used consistently.
- Limit the number of simultaneously important objects.
- Make status text secondary in color and size.

## LVGL Interaction Guidance
- Use generous padding around tappable labels.
- Prefer stable containers and static hit areas.
- Separate visual decoration from touch targets.
- Avoid state machines that depend on high-frequency touch path sampling.

## Review Checklist
1. Can the user understand the screen in under 2 seconds?
2. Is the primary action visually obvious?
3. Can the hidden/admin flow trigger accidentally?
4. Are back/cancel paths obvious and safe?
5. Is the interaction robust with imperfect touches?