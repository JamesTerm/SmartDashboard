# Replay Feature Parity Roadmap

This document tracks replay/timeline capabilities against modern dashboard workflows and defines an incremental implementation path.

Teaching intent:

- Give students a clear model for how forensic telemetry workflows are built in layers.
- Keep each iteration testable and useful on its own.

## Parity baseline (must-have)

These are required for practical match-debug usability and are mostly complete.

- Shared global replay cursor across all widgets.
- Timeline scrub/zoom/pan.
- Play/pause and rewind controls.
- Deterministic replay at a given cursor.
- Basic recording and replay load path.

Status: mostly complete in current branch.

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

Status: planned next.

## Advanced parity (nice-to-have)

These are high-value but not required for initial parity.

- User bookmarks/annotations.
- Auto anomaly markers (brownout threshold, comm drop heuristics).
- Selection window statistics (min/max/avg over selected range).
- Export/share helpers for incident review snapshots.

Status: future iteration set.

## Iteration plan

## Iteration A - Timeline readability and affordances

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

Scope:

- Add marker list panel (time/type summary).
- Add keyboard step controls for precise replay stepping.

Manual test focus:

- Use list click-to-seek and keyboard stepping during zoomed-in inspection.

Acceptance:

- Users can move quickly between coarse incidents and fine-grained moments.

## Iteration D - Analysis helpers

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
