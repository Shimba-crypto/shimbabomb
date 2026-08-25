# DESIGN.md — ShimbaBomb UI System

Design tokens + rules that every ShimbaBomb-generated interface follows.
Consumed by `std/ui.sb` components; humans and AI agents building SB UIs read this first.

## Principles

Dark-first, calm, data-forward surfaces. Content sits on quiet layered panels;
one accent color carries interaction. Motion explains hierarchy, never decorates.

## Color tokens (semantic)

| Token | Hex | Role |
|---|---|---|
| `--bg` | #11111b | page background |
| `--surface` | #1e1e2e | cards / panels |
| `--overlay` | #181825 | elevated popovers |
| `--border` | #45475a | hairlines, dividers |
| `--text` | #cdd6f4 | primary text (12.9:1 on surface) |
| `--muted` | #a6adc8 | secondary text (8.6:1) |
| `--accent` | #89b4fa | interactive: links, buttons, focus |
| `--accent-strong` | #74a0f0 | hover state |
| `--success` | #a6e3a1 | positive states |
| `--warning` | #f9e2af | caution |
| `--danger` | #f38ba8 | destructive / errors |

Dark mode only (the runtime is dark-native). Never gray-on-gray: body text is
never below `--muted`.

## Typography

System stack (offline-safe): `-apple-system, "Segoe UI", Roboto, Ubuntu, sans-serif`
Mono for numbers/code: `ui-monospace, "JetBrains Mono", Consolas, monospace`

Scale: 36 display · 28 h1 · 20 h2 · 16 body (base, line-height 1.5) · 14 secondary · 12 caption-min.

## Spacing · Radius · Shadow

4px grid: 4 · 8 · 12 · 16 · 24 · 32 · 48.
Radius: 6 inputs · 10 cards/buttons · 16 modals.
Shadow (rest): `0 1px 2px rgba(0,0,0,.35)`; (raised): `0 8px 24px rgba(0,0,0,.45)`.

## Components (see std/ui.sb)

page shell · topbar · card · stat tile · button · pill/badge · list rows ·
progress bar · key-value table · footer. Every interactive element: ≥44×44px
hit area, visible `:focus-visible` ring (`--accent`, 2px, offset 2px).

## Motion

Durations: 120ms (hover/state) · 200ms (enter/exit) · ease-out cubic.
Bars/counters animate once on load. All motion collapses under
`@media (prefers-reduced-motion: reduce)`.

## Accessibility minimums

Contrast ≥4.5:1 for text · focus never removed · icons paired with text or
aria-label · charts never encode meaning by color alone (label + value shown).

## Anti-patterns (never)

Raw hex inside component code — tokens only · emoji as icons (SVG only) ·
placeholder-only form labels · animating width/height · disabled zoom ·
hover-only affordances · pure-black (#000) backgrounds.
