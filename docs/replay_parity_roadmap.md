# Replay Feature Parity Roadmap

This document tracks replay/timeline capabilities against modern dashboard workflows and defines an incremental implementation path.

Teaching intent:

- Give students a clear model for how forensic telemetry workflows are built in layers.
- Keep each iteration testable and useful on its own.

## Parity baseline (must-have)

These are required for practical match-debug usability and are complete in the current baseline.

- Shared global replay cursor across all widgets.
- Timeline scrub/zoom/pan.
- Play/pause and rewind controls.
- Deterministic replay at a given cursor.
- Basic recording and replay load path.

Status: complete in local `main`.

## Competitive parity (should-have)

These are the next capabilities typically found in mature analysis dashboards.

- Timeline readability
  - tick marks and time labels
  - current cursor timestamp display
  - visible window-span indicator (for zoom context)
- Event markers
  - render markers for connect/disconnect/stale gaps
  - marker jump navigation (next/previous)
  - marker list with click-to-seek
- Seek and navigation ergonomics
  - keyboard stepping and fixed-step scrub actions
  - resilient fast seek behavior on larger files

Status: implemented in local `main` for the current scope.

## Advanced parity (nice-to-have)

These are high-value but not required for initial parity.

- User bookmarks are implemented; richer freeform annotations remain future work.
- Auto anomaly markers are implemented (event flags + brownout-style heuristics).
- Selection-window statistics are partially implemented via visible-window marker summary; min/max/avg over arbitrary selected ranges remain future work.
- Export/share helpers for incident review snapshots.
- Dockable replay workspace panels (controls/timeline/markers with persistent panel composition) are implemented.

Status: partially implemented; remaining work is centered on richer annotations/stats and export/share helpers.

## Iteration plan

## Iteration A - Timeline readability and affordances

Status: implemented in local `main`.

Scope:

- Add tick marks/time labels on timeline.
- Show current cursor time and window span in UI.
- Keep existing replay controls unchanged.

Manual test focus:

- Zoom in/out and verify labels remain readable and consistent.
- Scrub to specific visible label points and confirm synchronized widget values.

Acceptance:

- Timeline displays clear temporal context at both broad and narrow zoom levels.

## Iteration B - System markers and jump workflow

Status: implemented in local `main`.

Scope:

- Emit and store system markers from replay stream (`connect`, `disconnect`, `stale`).
- Render marker glyphs on timeline.
- Add next/previous marker controls.

Manual test focus:

- Replay a session with known transitions and verify marker placement.
- Jump between markers and verify cursor movement + synchronized widgets.

Acceptance:

- Marker-based navigation reliably lands on expected event times.

## Iteration C - Marker list and keyboard navigation

Status: implemented in local `main`.

Scope:

- Add marker list panel (time/type summary).
- Add keyboard step controls for precise replay stepping.

Manual test focus:

- Use list click-to-seek and keyboard stepping during zoomed-in inspection.

Acceptance:

- Users can move quickly between coarse incidents and fine-grained moments.

## Iteration D - Analysis helpers

Status: partially implemented in local `main`.

Scope:

- Add bookmarks/annotations.
- Add optional brownout/anomaly auto markers.
- Add selection-range summary stats.

Manual test focus:

- Investigate a known incident and produce a concise timestamped narrative from markers/bookmarks.

Acceptance:

- Replay supports practical post-match forensic review workflow end-to-end.

## Notes for comparing with newer dashboards

When evaluating parity, compare workflows, not exact visuals:

- time to locate an incident
- confidence in synchronized state at a timestamp
- number of interactions required to move from full-match view to sub-second detail

If those are competitive, UI appearance can evolve without blocking functionality parity.

## Dockable workspace note (2026-03-14)

Dockable replay workspace implementation is complete and merged to local `main`:

- replay controls and replay timeline are now panelized as dock widgets
- replay markers continue to participate in the same dockable workspace flow
- panel visibility can be toggled from `View`
- panel context menus support `Float`, `Dock Left`, `Dock Right`, and `Dock Bottom`
- choosing `Dock Bottom` restores default side-by-side bottom replay layout
- `Reset Replay Layout` restores the default replay workspace arrangement
- replay visibility preferences persist between sessions

Follow-up polish that also landed:

- timeline/header readout pressure was reduced by moving `t=` / `window=` readouts to status-bar labels
- repeated dock/float workflows now use the same deterministic persistence guard pattern used by replay markers
